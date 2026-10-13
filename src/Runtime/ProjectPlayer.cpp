#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Application;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.GamePackage;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Audio.Mixer;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.Platform.Events;
import Dot.Platform.Input;
import Dot.World.Scene;

namespace Dot::Runtime {
  namespace {
    auto lifecycle(const Script::ScriptReport& report) -> Core::Result<void> {
      for (const auto& failure : report.failures) {
        std::println(stderr, "Script entity {} phase {}: {}", failure.entity.value,
                     static_cast<int>(failure.phase), failure.error.message);
      }

      if (report.realmFaulted) {
        return Core::fail(Core::ErrorCode::InvalidState, "Project script realm faulted");
      }

      return {};
    }

    auto execute(const Project::Project* project, const Script::TypeScriptCompiler* compiler,
                 const Project::GamePackage* game, std::size_t frameBudget, bool acceptance)
        -> Core::Result<void> {
      auto description = game ? game->description() : project->description();
      auto files = game ? game->files() : project->files();

      if (!description) {
        return std::unexpected(description.error());
      }

      if (!files) {
        return std::unexpected(files.error());
      }

      Core::ApplicationConfig applicationConfig;
      applicationConfig.window.title = description->name;
      applicationConfig.window.width = static_cast<int>(description->width);
      applicationConfig.window.height = static_cast<int>(description->height);
      applicationConfig.window.gpuSurface = true;
      applicationConfig.fixedStep = std::chrono::nanoseconds{1'000'000'000 / description->fixedHz};
      applicationConfig.pauseOnFocusLoss = !acceptance;
      auto application = Core::Application::create(applicationConfig);

      if (!application) {
        return std::unexpected(application.error());
      }

      auto text = application->window().setTextInput(true);

      if (!text) {
        return text;
      }

      /** Stable locals preserve borrowed window/renderer addresses and teardown order.
       * 固定局部对象保持借用窗口/渲染器地址及销毁顺序。 */
      auto renderer = Graphics::Renderer::create(&application->window());

      if (!renderer) {
        return std::unexpected(renderer.error());
      }

      auto sceneRenderer = Graphics::SceneRenderer::create(*renderer, *files);

      if (!sceneRenderer) {
        return std::unexpected(sceneRenderer.error());
      }

      Project::GameSessionConfig sessionConfig;
      sessionConfig.audioMode = Project::SessionAudio::Playback;
      sessionConfig.audio.output.volume = acceptance ? 0 : 1;
      auto session
          = game ? Project::GameSession::createCompiled(*files, *description, game->scene().value(),
                                                        game->program().value(), sessionConfig)
                 : Project::GameSession::create(*project, *compiler, sessionConfig);

      if (!session) {
        return std::unexpected(session.error());
      }

      auto scene = session->scene();

      if (!scene) {
        return std::unexpected(scene.error());
      }

      std::optional<std::string> lastReloadError;
      std::size_t presentations = 0;
      auto lastTitle = description->name;
      bool delayed = false;
      const auto dispatch
          = [&](Core::Result<Script::LiveScriptReport> report) -> Core::Result<void> {
        if (!report) {
          return std::unexpected(report.error());
        }

        if (report->source.error) {
          if (!lastReloadError || *lastReloadError != report->source.error->message) {
            std::println(stderr, "Script reload: {}", report->source.error->message);
          }

          lastReloadError = report->source.error->message;
        } else {
          lastReloadError.reset();
        }

        if (report->source.reload) {
          auto retired = lifecycle(report->source.reload->retired);

          if (!retired) {
            return retired;
          }
        }

        return lifecycle(report->frame);
      };

      Core::LoopCallbacks callbacks;
      callbacks.prepareFrame = [&](const Platform::InputState& input) -> Core::Result<void> {
        if (input.key(Platform::Key::Escape).pressed) {
          application->stop();
        }

        if (acceptance) {
          Platform::InputState driven;
          static_cast<void>(
              driven.consume({0, 0, Platform::KeyChanged{Platform::Key::D, true, false}}));
          return session->publish(driven).transform([](auto) {});
        }

        return session->publish(input).transform([](auto) {});
      };

      callbacks.fixedUpdate = [&](double) { return dispatch(session->fixedUpdate()); };

      callbacks.update = [&](double seconds) {
        if (seconds > 1) {
          if (!delayed) {
            std::println(stderr, "Project frame exceeded one second; variable delta capped at 1");
          }

          delayed = true;
        } else {
          delayed = false;
        }

        return dispatch(session->update(std::min(seconds, 1.0)));
      };

      callbacks.render = [&](const Core::FrameState& frame) -> Core::Result<void> {
        if (frame.dropped.count() > 0) {
          std::println(stderr, "Project fixed updates dropped {} ns", frame.dropped.count());
        }

        auto name = (*scene)->name();

        if (!name) {
          return std::unexpected(name.error());
        }

        if (*name != lastTitle) {
          auto changed = application->window().setTitle(*name);

          if (!changed) {
            return changed;
          }

          lastTitle = std::move(*name);
        }

        return sceneRenderer->present(**scene).transform([&](auto result) {
          if (result == Graphics::PresentResult::Presented) {
            ++presentations;
          }
        });
      };

      /** Close on every dispatch/render failure; destructor never substitutes hooks.
       * 分发/渲染失败也显式关闭，销毁函数不替代停止钩子。 */
      auto run = [&]() -> Core::Result<void> {
        auto started = session->start();

        if (!started) {
          return std::unexpected(started.error());
        }

        auto checked = lifecycle(*started);

        if (!checked) {
          return checked;
        }

        if (!acceptance) {
          return application->run(callbacks, frameBudget);
        }

        auto before = sceneRenderer->render(**scene, 96, 96);

        if (!before) {
          return std::unexpected(before.error());
        }

        for (std::size_t frame = 0; frame < frameBudget; ++frame) {
          auto advanced = application->step(std::chrono::milliseconds{16}, callbacks);

          if (!advanced) {
            return std::unexpected(advanced.error());
          }
        }

        auto after = sceneRenderer->render(**scene, 96, 96);
        auto entities = (*scene)->entities();

        if (!after) {
          return std::unexpected(after.error());
        }

        if (!entities) {
          return std::unexpected(entities.error());
        }

        auto cube = std::ranges::find(*entities, "Cube", &World::EntityInfo::name);

        if (cube == entities->end() || std::abs(cube->transform.position.x - 0.72F) > 0.001F
            || before->pixels == after->pixels || presentations == 0) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Project player acceptance did not move/present the actual template");
        }

        return {};
      }();
      auto closed = session->close();

      if (!closed) {
        std::println(stderr, "Project shutdown: {}", closed.error().message);
      } else {
        auto cleanup = lifecycle(*closed);

        if (!cleanup && run) {
          return cleanup;
        }
      }

      if (!run) {
        return run;
      }

      return closed ? Core::Result<void>{} : std::unexpected(closed.error());
    }
  }  // namespace

  auto playProject(const std::filesystem::path& root, const std::filesystem::path& executable,
                   std::size_t frameBudget) -> int {
    auto project = Project::Project::open(root);

    if (!project) {
      std::println(stderr, "{}", project.error().message);
      return 1;
    }

    auto compiler = Script::TypeScriptCompiler::create(executable);

    if (!compiler) {
      std::println(stderr, "{}", compiler.error().message);
      return 1;
    }

    auto played = execute(&*project, &*compiler, nullptr, frameBudget, false);

    if (!played) {
      std::println(stderr, "{}", played.error().message);
      return 1;
    }

    return 0;
  }

  auto playGame(const std::filesystem::path& root, std::size_t frames, bool acceptance) -> int {
    auto files = Core::FileSystem::open(root);
    auto game = files ? Project::GamePackage::open(*files)
                      : Core::Result<Project::GamePackage>{std::unexpected(files.error())};

    if (!game) {
      std::println(stderr, "{}", game.error().message);
      return 1;
    }

    auto played = execute(nullptr, nullptr, &*game, frames, acceptance);

    if (!played) {
      std::println(stderr, "{}", played.error().message);
      return 1;
    }

    return 0;
  }

  auto testGamePlayer(const std::filesystem::path& executable, const std::filesystem::path& sdkRoot,
                      const std::filesystem::path& parentRoot) -> int {
    auto parent = Core::FileSystem::open(parentRoot).value();
    auto storage = Core::StagedDirectory::create(parent).value();
    auto files = storage.files().value();
    auto sdk = Core::FileSystem::open(sdkRoot).value();
    auto project = Project::Project::create(files, "authoring", {}, sdk).value();
    auto compiler = Script::TypeScriptCompiler::create(executable).value();
    auto scene = project.loadStartup().value();
    auto published = Project::GamePackage::publish(project, scene, compiler, files, "game");

    if (!published) {
      std::println(stderr, "{}", published.error().message);
      return 1;
    }

    std::filesystem::rename(published->root(), files.root() / "relocated");
    std::filesystem::remove_all(project.files()->root());
    const auto generations = Script::compilerGenerations();
    const auto vm = Script::Runtime::backendMemory();
    const auto audio = Audio::Mixer::backendMemory();
    auto run = [&]() -> Core::Result<void> {
      auto source = Core::FileSystem::open(files.root() / "relocated").value();
      auto game = Project::GamePackage::open(source);

      if (!game) {
        return std::unexpected(game.error());
      }

      return execute(nullptr, nullptr, &*game, 30, true);
    }();

    if (!run) {
      std::println(stderr, "{}", run.error().message);
      return 1;
    }

    if (Script::compilerGenerations() != generations
        || Script::Runtime::backendMemory().bytes != vm.bytes
        || Audio::Mixer::backendMemory().bytes != audio.bytes) {
      std::println(stderr, "Exported game retained program/VM/audio ownership");
      return 1;
    }

    std::println(
        "Exported game acceptance passed: relocated package, no authoring/compiler at playback, "
        "native input/GPU/audio and shutdown");
    return 0;
  }

  auto testProjectPlayer(const std::filesystem::path& executable,
                         const std::filesystem::path& sdkRoot,
                         const std::filesystem::path& parentRoot) -> int {
    auto parent = Core::FileSystem::open(parentRoot);
    auto sdk = Core::FileSystem::open(sdkRoot);
    auto compiler = Script::TypeScriptCompiler::create(executable);

    if (!parent || !sdk || !compiler) {
      std::println(stderr, "Project player acceptance setup failed");
      return 1;
    }

    auto staging = Core::StagedDirectory::create(*parent);

    if (!staging) {
      std::println(stderr, "{}", staging.error().message);
      return 1;
    }

    auto project = Project::Project::create(
        staging->files().value(), "game",
        Project::Description{"DotEngine Project Test", "scenes/main.dotscene", "scripts", 320, 240},
        *sdk);

    if (!project) {
      std::println(stderr, "{}", project.error().message);
      return 1;
    }

    const auto generations = Script::compilerGenerations();
    const auto vm = Script::Runtime::backendMemory();
    const auto audio = Audio::Mixer::backendMemory();
    auto played = execute(&*project, &*compiler, nullptr, 30, true);

    if (!played) {
      std::println(stderr, "{}", played.error().message);
      return 1;
    }

    if (Script::compilerGenerations() != generations
        || Script::Runtime::backendMemory().bytes != vm.bytes
        || Audio::Mixer::backendMemory().bytes != audio.bytes) {
      std::println(stderr, "Project player retained compiler/VM/audio resources");
      return 1;
    }

    std::println(
        "Project player acceptance passed: published input, attached TypeScript, native "
        "window/GPU presentation and pixel changes, real audio endpoint, explicit shutdown");
    return 0;
  }
}  // namespace Dot::Runtime
