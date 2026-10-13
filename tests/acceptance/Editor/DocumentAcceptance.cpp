#include <cmath>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.Editor.Document;
import Dot.World.Scene;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;

namespace Dot::Editor {
  namespace {
    auto exercise(Document& document, const Project::Project& project, bool render)
        -> Core::Result<void> {
      auto before = document.snapshot();

      if (!before) {
        return std::unexpected(before.error());
      }

      auto snapshot = nlohmann::json::parse(*before);
      auto transform = snapshot["scene"]["entities"][1]["transform"];
      transform["position"] = {1.5, 0, 0};
      auto selected
          = document.dispatch("{\"command\":\"select\",\"revision\":\"1\",\"entity\":\"2\"}");

      if (!selected) {
        return std::unexpected(selected.error());
      }

      std::optional<Graphics::Renderer> renderer;
      std::optional<Graphics::SceneRenderer> sceneRenderer;
      std::optional<Graphics::Image> initial;

      if (render) {
        auto created = Graphics::Renderer::create();

        if (!created) {
          return std::unexpected(created.error());
        }

        renderer.emplace(std::move(*created));
        auto drawing = Graphics::SceneRenderer::create(*renderer, project.files().value());

        if (!drawing) {
          return std::unexpected(drawing.error());
        }

        sceneRenderer.emplace(std::move(*drawing));
        auto image = sceneRenderer->render(*document.scene().value(), 96, 96);

        if (!image) {
          return std::unexpected(image.error());
        }

        initial = std::move(*image);
      }

      auto old = document.stats()->selected.value();
      auto operations
          = nlohmann::json::array({{{"op", "transform"}, {"entity", "2"}, {"value", transform}}});
      auto changed = document.apply(operations.dump(), 2);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      if (document.scene().value()->entity(old) || !document.stats()->dirty || !changed->changed) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Editor edit retained stale handle or lost dirty state");
      }

      std::optional<Graphics::Image> edited;

      if (render) {
        auto image = sceneRenderer->render(*document.scene().value(), 96, 96);

        if (!image) {
          return std::unexpected(image.error());
        }

        if (image->pixels == initial->pixels) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Editor transform did not change actual GPU pixels");
        }

        edited = std::move(*image);
      }

      auto undone = document.undo(document.stats()->revision);

      if (!undone) {
        return undone;
      }

      if (document.stats()->dirty) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Editor undo did not restore saved baseline");
      }

      if (render) {
        auto image = sceneRenderer->render(*document.scene().value(), 96, 96);

        if (!image) {
          return std::unexpected(image.error());
        }

        if (image->pixels != initial->pixels) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Editor undo did not restore initial GPU pixels");
        }
      }

      auto redone = document.redo(document.stats()->revision);

      if (!redone) {
        return redone;
      }

      if (render) {
        auto image = sceneRenderer->render(*document.scene().value(), 96, 96);

        if (!image) {
          return std::unexpected(image.error());
        }

        if (image->pixels != edited->pixels) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Editor redo did not restore edited GPU pixels");
        }
      }

      auto saved = document.save(document.stats()->revision);

      if (!saved) {
        return saved;
      }

      auto reopened = Document::open(project);

      if (!reopened) {
        return std::unexpected(reopened.error());
      }

      auto cube = reopened->scene().value()->findEntity(2).value();

      if (reopened->stats()->dirty
          || std::abs(reopened->scene().value()->entity(cube)->transform.position.x - 1.5F)
                 > 0.001F) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Editor save/reopen did not preserve native scene");
      }

      return {};
    }
  }  // namespace

  auto testDocument(const std::filesystem::path& parentRoot, bool render) -> int {
    auto parent = Core::FileSystem::open(parentRoot);
    if (!parent) {
      std::println(stderr, "Editor document acceptance setup failed");
      return 1;
    }

    auto owned = Core::StagedDirectory::create(*parent);

    if (!owned) {
      std::println(stderr, "{}", owned.error().message);
      return 1;
    }

    auto project = Project::Project::create(owned->files().value(), "game", {});

    if (!project) {
      std::println(stderr, "{}", project.error().message);
      return 1;
    }

    auto document = Document::open(*project);

    if (!document) {
      std::println(stderr, "{}", document.error().message);
      return 1;
    }

    auto verified = exercise(*document, *project, render);

    if (!verified) {
      std::println(stderr, "{}", verified.error().message);
      return 1;
    }

    std::println(
        "Editor document acceptance passed: selection, transactional edit, undo/redo, "
        "native save/reopen{}",
        render ? ", actual GPU pixel restoration" : "");
    return 0;
  }
}  // namespace Dot::Editor
