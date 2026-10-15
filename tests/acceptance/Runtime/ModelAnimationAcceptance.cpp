#include <exception>
#include <filesystem>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

import Dot.Animation.SceneAnimator;
import Dot.Assets.Image;
import Dot.Assets.ModelBundle;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.Resources;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.Project;
import Dot.World.Scene;

namespace Dot::Runtime {
  /** Import real GLB animation, reload native assets and render actual frame PNGs.
   * 导入实际 GLB 动画，重新加载原生资产并输出真实帧 PNG。 */
  auto testModelAnimation(const std::filesystem::path& source, const std::filesystem::path& output)
      -> int {
    try {
      auto files = Core::FileSystem::open(output).value();
      auto storage = Core::StagedDirectory::create(files).value();
      auto project = Project::Project::create(storage.files().value(), "game").value();
      auto resources = Editor::ResourceStore::open(project).value();
      const auto imported = resources.importModel(source, "assets/model").value();

      if (imported.animations != 1) {
        throw std::runtime_error("Model animation import must retain its selected-scene clip");
      }

      auto model
          = Assets::loadModelBundle(project.files().value(), "assets/model/model.dotmodel").value();
      auto& scene = model.scene;
      const auto camera = scene.createEntity("Camera").value();
      scene
          .setCamera(camera, World::Camera{.projection = World::Projection::Orthographic,
                                           .orthographicHeight = 4,
                                           .primary = true})
          .value();
      scene.setTransform(camera, {{.5F, 0, 3}}).value();
      scene.setLight(scene.createEntity("Key light").value(), World::Light{.intensity = 3}).value();
      auto animator
          = Animation::SceneAnimator::bind(scene, model.animations[0], model.bindings).value();
      auto renderer = Graphics::Renderer::create().value();
      const auto baseline = renderer.resourceStats().value();

      {
        auto drawing = Graphics::SceneRenderer::create(renderer, project.files().value()).value();
        const auto initial = drawing.render(scene, 256, 256).value();
        bool changed{};

        for (int frame = 0; frame <= 8; ++frame) {
          animator.seek(scene, double(frame) * .25).value();
          const auto image = drawing.render(scene, 256, 256).value();
          changed = changed || image.pixels != initial.pixels;
          files
              .writeAtomic("model-animation-" + std::to_string(frame) + ".png",
                           Assets::encodeImage(image).value())
              .value();
        }

        if (!changed) {
          throw std::runtime_error("Imported animation did not change real model pixels");
        }

        animator.stop(scene).value();

        if (drawing.render(scene, 256, 256)->pixels != initial.pixels) {
          throw std::runtime_error("Imported animation stop did not restore bind-pose pixels");
        }
      }

      const auto after = renderer.resourceStats().value();

      if (after.meshes != baseline.meshes || after.vertexBytes != baseline.vertexBytes
          || after.indexBytes != baseline.indexBytes || after.textures != baseline.textures
          || after.targets != baseline.targets) {
        throw std::runtime_error("Imported animation retained drawing resources");
      }

      std::println(
          "Model animation acceptance passed: GLB import, native bundle reload, "
          "hierarchical TRS, nine GPU frames, rest restore and resource release");
      return 0;
    } catch (const std::exception& error) {
      std::println(stderr, "Model animation acceptance: {}", error.what());
      return 1;
    }
  }
}  // namespace Dot::Runtime
