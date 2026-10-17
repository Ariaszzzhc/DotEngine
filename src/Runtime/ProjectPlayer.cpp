#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Application;
import Dot.Core.Control;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.PlayerControl;
import Dot.Project.GamePackage;
import Dot.Script.Compiler;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.Graphics.TextRenderer;
import Dot.Graphics.Text;
import Dot.Script.Hud;
import Dot.Platform.Events;
import Dot.Platform.Input;
import Dot.Platform.UserData;
import Dot.Platform.Window;
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

      std::optional<Core::Control> control;

      if (options.control) {
        auto parent = Core::FileSystem::open(*options.control);
        auto opened
            = parent ? Core::Control::create(
                           *parent, nlohmann::json{{"kind", "player"},
                                                   {"applicationId", description->applicationId},
                                                   {"continuesUnfocused", true}}
                                        .dump())
                     : Core::Result<Core::Control>{std::unexpected(parent.error())};

        if (!opened) {
          return Core::fail(opened.error().code,
                            "Prepare player control: " + opened.error().message);
        }

        control.emplace(std::move(*opened));
      }

      auto userData = options.dataDirectory
                          ? Core::FileSystem::open(*options.dataDirectory)
                          : Platform::userDataDirectory(description->applicationId);

      if (!userData) {
        return Core::fail(userData.error().code,
                          "Prepare player data directory: " + userData.error().message);
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
      applicationConfig.pauseOnFocusLoss = options.pauseOnFocusLoss && !options.control;
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

      const auto* base = SDL_GetBasePath();

      if (!base) {
        return Core::fail(Core::ErrorCode::Io,
                          "Resolve player font directory: " + std::string(SDL_GetError()));
      }

      const auto fontRoot
          = std::filesystem::path{std::u8string(base, base + std::char_traits<char>::length(base))};
      auto textRenderer = Graphics::TextRenderer::create(
          *renderer, fontRoot / (game ? "EngineSans.ttf" : "resources/EngineSans.ttf"));

      if (!textRenderer) {
        return std::unexpected(textRenderer.error());
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

      std::optional<Project::PlayerControl> agent;

      if (control) {
        auto created = Project::PlayerControl::create(*session);

        if (!created) {
          return std::unexpected(created.error());
        }

        agent.emplace(std::move(*created));
        const auto utf = control->files()->root().generic_u8string();
        std::println("{}", nlohmann::json{{"ok", true},
                                          {"kind", "player"},
                                          {"control", std::string(utf.begin(), utf.end())}}
                               .dump());
        std::fflush(stdout);
      }

      const auto observe = [&](const Script::ScriptReport& report) -> Core::Result<void> {
        auto recorded = agent ? agent->record(report) : Core::Result<void>{};
        return recorded ? lifecycle(report) : recorded;
      };

      auto scene = session->scene();

      if (!scene) {
        return std::unexpected(scene.error());
      }

      std::optional<std::string> lastReloadError;
      std::uint64_t lastSceneRequest{};
      std::uint64_t presentedHudScope{};
      std::uint64_t presentedHudRevision{};
      std::uint64_t presentedButtonRevision{};
      Platform::Extent presentedSize{};

      struct RenderMeta {
        std::uint64_t tag;
        std::uint64_t scope;
        std::uint64_t hud;
        std::uint64_t buttons;
      };

      std::optional<RenderMeta> pendingRender;
      auto lastTitle = description->name;
      bool delayed = false;
      bool started = false;
      std::optional<Graphics::ScenePreparationStage> loadingStage;
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
          auto retired = observe(report->source.reload->retired);

          if (!retired) {
            return retired;
          }
        }

        return observe(report->frame);
      };

      Core::LoopCallbacks callbacks;
      callbacks.prepareFrame = [&](const Platform::InputState& input) -> Core::Result<void> {
        if (control) {
          auto processed = control->process(
              [&](std::string_view request) -> Core::Result<std::string> {
                auto result = agent->perform(request);
                auto closing = agent->closing();

                if (!closing) {
                  return std::unexpected(closing.error());
                }

                if (*closing) {
                  application->stop();
                }

                if (result) {
                  auto response = nlohmann::json::parse(*result);

                  if (response.contains("player")) {
                    auto focused = application->window().focused();
                    auto captured = application->window().relativeMouse();

                    if (!focused || !captured) {
                      const auto& error = !focused ? focused.error() : captured.error();
                      return Core::fail(
                          error.code,
                          "Player request may have committed; native input status failed: "
                              + error.message);
                    }

                    auto textActive = application->window().textInputActive();
                    auto logical = application->window().size();

                    if (!textActive || !logical) {
                      return std::unexpected(!textActive ? textActive.error() : logical.error());
                    }

                    response["native"] = {{"focused", *focused},
                                          {"pointerCaptured", *captured},
                                          {"textInputActive", *textActive},
                                          {"inputArea", {logical->width, logical->height}}};
                    auto text = textRenderer->stats();

                    if (!text) {
                      return Core::fail(text.error().code,
                                        "Inspect native text: " + text.error().message);
                    }

                    auto frames = sceneRenderer->asyncStats();

                    if (!frames) {
                      return std::unexpected(frames.error());
                    }

                    const auto& f = *frames;
                    response["rendering"]
                        = {{"submitted", std::to_string(f.submitted)},
                           {"completed", std::to_string(f.completed)},
                           {"deferred", std::to_string(f.deferred)},
                           {"cancelled", std::to_string(f.cancelled)},
                           {"pending", f.pending ? nlohmann::json(std::to_string(*f.pending))
                                                 : nlohmann::json(nullptr)},
                           {"latest",
                            f.latest
                                ? nlohmann::json{{"serial", std::to_string(f.latest->serial)},
                                                 {"tag", std::to_string(f.latest->tag)},
                                                 {"applicationFrame",
                                                  std::to_string(f.latest->tag - 1)},
                                                 {"scene", std::to_string(f.latest->scene)},
                                                 {"size", {f.latest->width, f.latest->height}},
                                                 {"viewProjection", f.latest->viewProjection.data}}
                                : nlohmann::json(nullptr)}};
                    response["hudText"]
                        = {{"textureBytes", std::to_string(text->textureBytes)},
                           {"rasterizations", std::to_string(text->rasterizations)},
                           {"overflow", text->overflow},
                           {"presentedScope", std::to_string(presentedHudScope)},
                           {"presentedRevision", std::to_string(presentedHudRevision)},
                           {"presentedButtonRevision", std::to_string(presentedButtonRevision)}};
                    return response.dump();
                  }
                }

                return result;
              },
              [&] { return application->running(); });

          if (!processed) {
            return std::unexpected(processed.error());
          }

          if (!application->running()) {
            return {};
          }
        }

        const bool quitting = input.key(Platform::Key::Escape).pressed;

        if (quitting) {
          application->stop();
          return {};
        }

        if (!started) {
          auto prepared = sceneRenderer->prepare(**scene);

          if (!prepared) {
            return std::unexpected(prepared.error());
          }

          if (*prepared != Graphics::ScenePreparationStage::Ready) {
            if (loadingStage != *prepared) {
              const auto phase
                  = *prepared == Graphics::ScenePreparationStage::Environment
                        ? "environment lighting"
                    : *prepared == Graphics::ScenePreparationStage::Shadows ? "shadow shaders"
                    : *prepared == Graphics::ScenePreparationStage::Occlusion
                        ? "ambient occlusion shaders"
                    : *prepared == Graphics::ScenePreparationStage::Skins     ? "skinning shaders"
                    : *prepared == Graphics::ScenePreparationStage::Geometry  ? "geometry"
                    : *prepared == Graphics::ScenePreparationStage::SkinPoses ? "initial skin poses"
                    : *prepared == Graphics::ScenePreparationStage::EnvironmentMap
                        ? "HDR environment"
                        : "material textures";
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

          auto checked = observe(*report);

          if (!checked) {
            return checked;
          }

          started = true;

          if (loadingStage) {
            std::println(stderr, "Scene resources ready; starting game");
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
        auto textRequest = session->textInputRequested();

        if (!textRequest) {
          return std::unexpected(textRequest.error());
        }

        auto text = application->window().setTextInput(*textRequest && !capture);

        if (!text) {
          return text;
        }

        auto published = input;

        if (capture != *relative) {
          auto changed = application->window().setRelativeMouse(capture);

          if (!changed) {
            return changed;
          }

          published.clearPointerDelta();
        }

        auto controlled = agent ? agent->next(published)
                                : Core::Result<Platform::InputState>{std::move(published)};
        if (!controlled) {
          return std::unexpected(controlled.error());
        }

        auto logical = application->window().size();
        auto textActive = application->window().textInputActive();
        auto scripted = agent ? agent->usingAgentInput() : Core::Result<bool>{false};

        if (!logical || !textActive || !scripted) {
          return std::unexpected(!logical      ? logical.error()
                                 : !textActive ? textActive.error()
                                               : scripted.error());
        }

        auto location = session->stats();
        auto pixels = renderer->surfaceSize();

        if (!location || !pixels) {
          return std::unexpected(!location ? location.error() : pixels.error());
        }

        auto ui = session->hud();
        auto buttons
            = ui ? ui->buttonRevision() : Core::Result<std::uint64_t>{std::unexpected(ui.error())};

        if (!buttons) {
          return std::unexpected(buttons.error());
        }

        const Project::GameInputArea area{
            {static_cast<float>(logical->width), static_cast<float>(logical->height)},
            (*focused || *scripted) && !quitting && logical->width > 0 && logical->height > 0
                && pixels->width == presentedSize.width && pixels->height == presentedSize.height
                && presentedHudScope == location->location.scope
                && presentedButtonRevision == *buttons};
        return session->publish(*controlled, capture, area, *textActive).transform([](auto) {});
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

        auto hud = session->hud();
        auto state = hud ? hud->snapshot()
                         : Core::Result<Script::HudSnapshot>{std::unexpected(hud.error())};

        if (!state) {
          return std::unexpected(state.error());
        }

        auto size = renderer->surfaceSize();

        if (!size) {
          return std::unexpected(size.error());
        }

        if (size->width <= 0 || size->height <= 0) {
          pendingRender.reset();
          return sceneRenderer->cancelAsync();
        }

        auto layers = hud->draws();

        if (!layers) {
          return std::unexpected(layers.error());
        }

        auto screen = textRenderer->prepare(*layers, static_cast<std::uint32_t>(size->width),
                                            static_cast<std::uint32_t>(size->height));

        if (!screen) {
          return std::unexpected(screen.error());
        }

        const RenderMeta current{frame.index + 1, state->scope, state->revision,
                                 state->buttonRevision};
        auto presented = sceneRenderer->presentAsync(**scene, current.tag, current.scope,
                                                     {0, 0, 0, 1}, {}, *screen);

        if (!presented) {
          return std::unexpected(presented.error());
        }

        if (presented->completed) {
          const auto tag = presented->completed->tag;
          const auto* metadata = tag == current.tag                           ? &current
                                 : pendingRender && pendingRender->tag == tag ? &*pendingRender
                                                                              : nullptr;

          if (!metadata) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Presented frame has no owned HUD provenance");
          }

          presentedHudScope = metadata->scope;
          presentedHudRevision = metadata->hud;
          presentedButtonRevision = metadata->buttons;
          presentedSize = {static_cast<int>(presented->completed->width),
                           static_cast<int>(presented->completed->height)};
        }

        if (presented->accepted) {
          pendingRender = current;
        }

        auto status = sceneRenderer->asyncStats();

        if (!status) {
          return std::unexpected(status.error());
        }

        if (!status->pending) {
          pendingRender.reset();
        }

        if (presented->error) {
          return std::unexpected(*presented->error);
        }

        if (presented->completed && options.frames && ++gameFrames == options.frames) {
          application->stop();
        }

        return {};
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
      auto retired = control ? control->close() : Core::Result<void>{};

      if (!retired) {
        std::println(stderr, "Retire player control: {}", retired.error().message);
      }

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

      if (!closed) {
        return std::unexpected(closed.error());
      }

      return retired;
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
                   const PlayerOptions& options) -> int {
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

    auto played = runProject(*project, *compiler, options);

    if (!played) {
      std::println(stderr, "{}", played.error().message);
      return 1;
    }

    return 0;
  }

  auto playGame(const std::filesystem::path& root, const PlayerOptions& options) -> int {
    auto files = Core::FileSystem::open(root);
    auto game = files ? Project::GamePackage::open(*files)
                      : Core::Result<Project::GamePackage>{std::unexpected(files.error())};

    if (!game) {
      std::println(stderr, "{}", game.error().message);
      return 1;
    }

    auto played = runGame(*game, options);

    if (!played) {
      std::println(stderr, "{}", played.error().message);
      return 1;
    }

    return 0;
  }

}  // namespace Dot::Runtime
