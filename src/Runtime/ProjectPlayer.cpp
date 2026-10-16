#include <algorithm>
#include <chrono>
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
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.GamePackage;
import Dot.Script.Compiler;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.Platform.Events;
import Dot.Platform.Input;
import Dot.World.Scene;

#include "Player.hpp"

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
                 const Project::GamePackage* game, const PlayerOptions& options)
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
      applicationConfig.pauseOnFocusLoss = options.pauseOnFocusLoss;
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
      sessionConfig.audio.output.volume = options.volume;
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
        const bool quitting = input.key(Platform::Key::Escape).pressed;

        if (quitting) {
          application->stop();
        }

        auto requested = session->pointerCaptureRequested();
        auto focused = application->window().focused();
        auto relative = application->window().relativeMouse();

        if (!requested || !focused || !relative) {
          return std::unexpected(!requested ? requested.error()
                                 : !focused ? focused.error()
                                            : relative.error());
        }

        const bool capture = *requested && *focused && !quitting;
        auto text = application->window().setTextInput(!capture);

        if (!text) {
          return text;
        }

        if (capture != *relative) {
          auto changed = application->window().setRelativeMouse(capture);

          if (!changed) {
            return changed;
          }

          auto clean = input;
          clean.clearPointerDelta();
          return session->publish(clean, capture).transform([](auto) {});
        }

        return session->publish(input, capture).transform([](auto) {});
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

        try {
          return options.driver ? options.driver(*application, *session, *sceneRenderer, callbacks,
                                                 presentations)
                                : application->run(callbacks, options.frames);
        } catch (const std::exception& error) {
          return Core::fail(Core::ErrorCode::External, "Player loop: " + std::string(error.what()));
        }
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

  auto runProject(const Project::Project& project, const Script::TypeScriptCompiler& compiler,
                  const PlayerOptions& options) -> Core::Result<void> {
    return execute(&project, &compiler, nullptr, options);
  }

  auto runGame(const Project::GamePackage& game, const PlayerOptions& options)
      -> Core::Result<void> {
    return execute(nullptr, nullptr, &game, options);
  }

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

    auto played = runProject(*project, *compiler, {.frames = frameBudget});

    if (!played) {
      std::println(stderr, "{}", played.error().message);
      return 1;
    }

    return 0;
  }

  auto playGame(const std::filesystem::path& root, std::size_t frames) -> int {
    auto files = Core::FileSystem::open(root);
    auto game = files ? Project::GamePackage::open(*files)
                      : Core::Result<Project::GamePackage>{std::unexpected(files.error())};

    if (!game) {
      std::println(stderr, "{}", game.error().message);
      return 1;
    }

    auto played = runGame(*game, {.frames = frames});

    if (!played) {
      std::println(stderr, "{}", played.error().message);
      return 1;
    }

    return 0;
  }

}  // namespace Dot::Runtime
