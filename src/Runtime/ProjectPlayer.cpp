#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
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
import Dot.Platform.UserData;
import Dot.World.Scene;

#include "Player.hpp"

namespace Dot::Runtime {
  namespace {
    auto lifecycle(const Script::ScriptReport& report) -> Core::Result<void> {
      for (const auto& failure : report.failures) {
        std::println(stderr, "Script scene {} entity {} phase {}: {}", failure.entity.scene,
                     failure.entity.value, static_cast<int>(failure.phase), failure.error.message);
      }

      if (report.realmFaulted) {
        return Core::fail(Core::ErrorCode::InvalidState, "Project script realm faulted");
      }

      return {};
    }

    auto execute(const Project::Project* project, const Script::TypeScriptCompiler* compiler,
                 const Project::GamePackage* game, const PlayerOptions& options)
        -> Core::Result<void> {
      std::optional<Core::FileSystem> profileFiles;
      std::filesystem::path profileName;

      if (options.profile) {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(*options.profile, error);

        if (error || absolute.filename().empty()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid application profile path");
        }

        auto files = Core::FileSystem::open(absolute.parent_path());
        auto resolved = files ? files->resolve(absolute.filename())
                              : Core::Result<std::filesystem::path>{std::unexpected(files.error())};

        if (!resolved) {
          return Core::fail(resolved.error().code,
                            "Prepare application profile: " + resolved.error().message);
        }

        const auto status = std::filesystem::symlink_status(*resolved, error);

        if ((error && error != std::errc::no_such_file_or_directory)
            || (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Application profile output must be a regular file");
        }

        profileFiles = std::move(*files);
        profileName = absolute.filename();
      }

      auto description = game ? game->description() : project->description();
      auto files = game ? game->files() : project->files();

      if (!description) {
        return std::unexpected(description.error());
      }

      if (!files) {
        return std::unexpected(files.error());
      }

      auto userData = Platform::userDataDirectory(description->applicationId);

      if (!userData) {
        return std::unexpected(userData.error());
      }

      auto directory = userData->createDirectories("saves");

      if (!directory) {
        return std::unexpected(directory.error());
      }

      auto saves = Core::FileSystem::open(userData->root() / "saves");

      if (!saves) {
        return std::unexpected(saves.error());
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

      if (profileFiles) {
        auto enabled = application->setProfiling(true);

        if (!enabled) {
          return enabled;
        }
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
      sessionConfig.progress.saves = std::move(*saves);
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
      std::uint64_t lastSceneRequest{};
      auto lastTitle = description->name;
      bool delayed = false;
      bool started = false;
      std::optional<Graphics::ScenePipelineStage> loadingStage;
      std::size_t gameFrames{};
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
          return {};
        }

        if (!started) {
          auto prepared = sceneRenderer->preparePipelines(**scene);

          if (!prepared) {
            return std::unexpected(prepared.error());
          }

          if (*prepared != Graphics::ScenePipelineStage::Ready) {
            if (loadingStage != *prepared) {
              const auto phase = *prepared == Graphics::ScenePipelineStage::Environment
                                     ? "environment lighting"
                                     : "shadow shaders";
              std::println(stderr, "Preparing {}", phase);
              auto changed
                  = application->window().setTitle(description->name + " - Preparing " + phase);

              if (!changed) {
                return changed;
              }

              loadingStage = *prepared;
            }

            return {};
          }

          auto report = session->start();

          if (!report) {
            return std::unexpected(report.error());
          }

          auto checked = lifecycle(*report);

          if (!checked) {
            return checked;
          }

          started = true;

          if (loadingStage) {
            std::println(stderr, "Rendering pipelines ready; starting game");
            lastTitle.clear();
          }
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

      callbacks.fixedUpdate = [&](double) {
        return started ? dispatch(session->fixedUpdate()) : Core::Result<void>{};
      };

      callbacks.update = [&](double seconds) -> Core::Result<void> {
        if (!started) {
          return {};
        }

        if (seconds > 1) {
          if (!delayed) {
            std::println(stderr, "Project frame exceeded one second; variable delta capped at 1");
          }

          delayed = true;
        } else {
          delayed = false;
        }

        auto updated = dispatch(session->update(std::min(seconds, 1.0)));

        if (!updated) {
          return updated;
        }

        auto stats = session->stats();

        if (!stats) {
          return Core::Result<void>{std::unexpected(stats.error())};
        }

        if (stats->transition && stats->transition->error
            && stats->transition->id != lastSceneRequest) {
          lastSceneRequest = stats->transition->id;
          std::println(stderr, "Scene request {} '{}': {}", stats->transition->id,
                       stats->transition->path, stats->transition->error->message);
        }

        return Core::Result<void>{};
      };

      callbacks.render = [&](const Core::FrameState& frame) -> Core::Result<void> {
        if (frame.dropped.count() > 0) {
          std::println(stderr, "Project fixed updates dropped {} ns", frame.dropped.count());
        }

        if (!started) {
          return renderer->present({}, {}, {0.055F, 0.06F, 0.075F, 1}).transform([](auto) {});
        }

        scene = session->scene();

        if (!scene) {
          return std::unexpected(scene.error());
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

        auto shown = sceneRenderer->present(**scene).transform([](auto) {});

        if (shown && options.frames && ++gameFrames == options.frames) {
          application->stop();
        }

        return shown;
      };

      /** Close on every dispatch/render failure; destructor never substitutes hooks.
       * 分发/渲染失败也显式关闭，销毁函数不替代停止钩子。 */
      auto run = [&]() -> Core::Result<void> {
        try {
          return application->run(callbacks);
        } catch (const std::exception& error) {
          return Core::fail(Core::ErrorCode::External, "Player loop: " + std::string(error.what()));
        }
      }();
      auto closed = session->close();

      if (profileFiles) {
        auto profile = application->profiling();
        auto encoded = profile ? Core::encodeApplicationProfiling(*profile)
                               : Core::Result<std::string>{std::unexpected(profile.error())};
        auto saved = encoded ? profileFiles->writeTextAtomic(profileName, *encoded)
                             : Core::Result<void>{std::unexpected(encoded.error())};

        if (!saved) {
          std::println(stderr, "Save application profile: {}", saved.error().message);

          if (run && closed) {
            return std::unexpected(saved.error());
          }
        }
      }

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
                   std::size_t frameBudget, std::optional<std::filesystem::path> profile) -> int {
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

    auto played
        = runProject(*project, *compiler, {.frames = frameBudget, .profile = std::move(profile)});

    if (!played) {
      std::println(stderr, "{}", played.error().message);
      return 1;
    }

    return 0;
  }

  auto playGame(const std::filesystem::path& root, std::size_t frames,
                std::optional<std::filesystem::path> profile) -> int {
    auto files = Core::FileSystem::open(root);
    auto game = files ? Project::GamePackage::open(*files)
                      : Core::Result<Project::GamePackage>{std::unexpected(files.error())};

    if (!game) {
      std::println(stderr, "{}", game.error().message);
      return 1;
    }

    auto played = runGame(*game, {.frames = frames, .profile = std::move(profile)});

    if (!played) {
      std::println(stderr, "{}", played.error().message);
      return 1;
    }

    return 0;
  }

}  // namespace Dot::Runtime
