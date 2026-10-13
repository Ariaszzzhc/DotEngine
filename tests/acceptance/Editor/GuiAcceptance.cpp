#include <SDL3/SDL.h>
#include <imgui.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Application;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.Document;
import Dot.Project;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;

namespace Dot::Editor {
  auto testGui(const std::filesystem::path& parentRoot) -> int {
    auto parent = Core::FileSystem::open(parentRoot);
    if (!parent) {
      std::println(stderr, "GUI acceptance setup failed");
      return 1;
    }

    auto storage = Core::StagedDirectory::create(*parent);

    if (!storage) {
      std::println(stderr, "{}", storage.error().message);
      return 1;
    }

    const auto contexts = Graphics::guiContexts();

    {
      auto project = Project::Project::create(storage->files().value(), "gui", {});

      if (!project) {
        std::println(stderr, "{}", project.error().message);
        return 1;
      }

      auto document = Document::open(*project);
      Core::ApplicationConfig config;
      config.window = {"DotEngine GUI Acceptance", 480, 360, false, true, true};
      config.pauseOnFocusLoss = false;
      auto application = Core::Application::create(config);

      if (!document || !application) {
        std::println(stderr, "GUI document/application failed");
        return 1;
      }

      auto renderer = Graphics::Renderer::create(&application->window());

      if (!renderer) {
        std::println(stderr, "{}", renderer.error().message);
        return 1;
      }

      auto enabled = renderer->enableGui();
      auto drawing = Graphics::SceneRenderer::create(*renderer, project->files().value());
      auto target = renderer->createTarget(192, 192);

      if (!enabled || !drawing || !target) {
        std::println(stderr, "GUI renderer/target initialization failed");
        return 1;
      }

      auto initial = drawing->renderInto(*document->scene().value(), *target);

      if (!initial) {
        std::println(stderr, "{}", initial.error().message);
        return 1;
      }

      auto before = renderer->readTarget(*target);

      if (!before) {
        std::println(stderr, "{}", before.error().message);
        return 1;
      }

      std::optional<Core::Error> inputError;
      auto connection = application->events().subscribe([&](const auto& event) {
        auto handled = renderer->guiEvent(event);

        if (!handled) {
          inputError = handled.error();
          application->stop();
        }
      });
      int clicks = 0;
      unsigned presented = 0;
      unsigned uiFrames = 0;
      Core::LoopCallbacks callbacks;
      callbacks.render = [&](const auto&) -> Core::Result<void> {
        auto scene = drawing->renderInto(*document->scene().value(), *target);

        if (!scene) {
          return scene;
        }

        auto result = renderer->presentGui([&]() -> Core::Result<void> {
          ImGui::SetNextWindowPos({8, 8});
          ImGui::SetNextWindowSize({450, 330});
          if (uiFrames++ == 0) {
            ImGui::SetNextWindowFocus();
          }
          ImGui::Begin("Scene editor", nullptr, ImGuiWindowFlags_NoDecoration);
          if (uiFrames == 1) {
            ImGui::SetKeyboardFocusHere();
          }
          const bool clicked = ImGui::Button("Move cube", {120, 28});

          ImGui::SetItemDefaultFocus();
          ImGui::TextUnformatted("GPU scene preview");
          auto image = renderer->guiImage(*target, 192, 192);
          ImGui::End();

          if (!image) {
            return image;
          }

          if (clicked) {
            ++clicks;
            auto snapshot = nlohmann::json::parse(document->snapshot().value());
            auto transform = snapshot["scene"]["entities"][1]["transform"];
            transform["position"] = {1.5, 0, 0};
            auto commands = nlohmann::json::array(
                {{{"op", "transform"}, {"entity", "2"}, {"value", transform}}});
            auto changed = document->apply(commands.dump(), document->stats()->revision);

            if (!changed) {
              return std::unexpected(changed.error());
            }
          }

          return {};
        });

        if (!result) {
          return std::unexpected(result.error());
        }

        presented += *result == Graphics::PresentResult::Presented ? 1U : 0U;
        return {};
      };

      for (int frame = 0; frame < 12; ++frame) {
        if (frame >= 2 && frame <= 5) {
          SDL_Event focus{};
          focus.window.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
          focus.window.windowID = application->window().id();
          SDL_Event key{};
          const bool down = frame == 2 || frame == 4;
          key.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
          key.key.windowID = application->window().id();
          key.key.scancode = frame <= 3 ? SDL_SCANCODE_TAB : SDL_SCANCODE_SPACE;
          key.key.key = frame <= 3 ? SDLK_TAB : SDLK_SPACE;
          key.key.down = down;

          if (!SDL_PushEvent(&focus) || !SDL_PushEvent(&key)) {
            std::println(stderr, "Queue GUI acceptance input failed: {}", SDL_GetError());
            return 1;
          }
        }

        auto advanced = application->step(std::chrono::milliseconds{16}, callbacks);

        if (!advanced || inputError) {
          std::println(stderr, "GUI frame failed: {}",
                       inputError ? inputError->message : advanced.error().message);
          return 1;
        }
      }

      auto after = renderer->readTarget(*target);

      if (!after || clicks != 1 || presented == 0 || !document->stats()->dirty
          || before->pixels == after->pixels || renderer->resourceStats()->targets != 1) {
        std::println(stderr, "GUI acceptance failed: clicks={}, presented={}", clicks, presented);
        return 1;
      }

      auto closed = renderer->closeGui();

      if (!closed) {
        std::println(stderr, "{}", closed.error().message);
        return 1;
      }
    }

    if (Graphics::guiContexts() != contexts) {
      std::println(stderr, "GUI acceptance retained context ownership");
      return 1;
    }

    std::println(
        "GUI acceptance passed: real window/WebGPU presentation, SDL keyboard-activated "
        "button, document edit, direct GPU preview pixel changes and context cleanup");
    return 0;
  }
}  // namespace Dot::Editor
