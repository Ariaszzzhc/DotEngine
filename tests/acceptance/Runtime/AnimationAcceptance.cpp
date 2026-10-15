#include <array>
#include <exception>
#include <filesystem>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Animation.SceneAnimator;
import Dot.Assets.Image;
import Dot.Core.Files;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.World.Scene;

namespace Dot::Runtime {
  /** Real offscreen hierarchy playback, saved native clip and PNG frame evidence.
   * 实际离屏层级动画播放、原生片段保存与 PNG 帧证据。 */
  auto testAnimation(const std::filesystem::path& root) -> int {
    using namespace Animation;

    try {
      auto files = Core::FileSystem::open(root).value();
      auto scene = World::Scene::create("Mechanical animation acceptance").value();
      auto camera = scene.createEntity("Camera").value();
      scene
          .setCamera(camera, World::Camera{.projection = World::Projection::Orthographic,
                                           .orthographicHeight = 4,
                                           .primary = true})
          .value();
      scene.setTransform(camera, {{0, 0, 5}}).value();
      auto parent = scene.createEntity("Sliding body").value();
      auto child = scene.createEntity("Rotating panel").value();
      scene.setMeshRenderer(parent, World::MeshRenderer{.asset = {}, .tint = {.2F, .6F, 1, 1}})
          .value();
      scene.setMeshRenderer(child, World::MeshRenderer{.asset = {}, .tint = {1, .4F, .1F, 1}})
          .value();
      scene.setTransform(parent, {{-1, 0, 0}}).value();
      scene.setTransform(child, {{0, 1, 0}, {}, {.5F, .25F, .25F}}).value();
      scene.setParent(child, parent).value();
      auto clip = Clip::create({"Mechanical motion",
                                {{0,
                                  Path::Translation,
                                  Interpolation::Linear,
                                  {{0, {-1, 0, 0, 0}}, {2, {1, 0, 0, 0}}}},
                                 {1,
                                  Path::Rotation,
                                  Interpolation::Linear,
                                  {{0, {0, 0, 0, 1}}, {2, {0, 0, .70710678F, .70710678F}}}},
                                 {1,
                                  Path::Scale,
                                  Interpolation::Linear,
                                  {{0, {.5F, .25F, .25F, 0}}, {2, {1, .5F, .25F, 0}}}}}})
                      .value();
      saveClip(files, "mechanical.dotanimation", clip).value();
      auto loaded = loadClip(files, "mechanical.dotanimation").value();
      const std::array bindings{NodeBinding{0, parent}, NodeBinding{1, child}};
      auto animator = SceneAnimator::bind(scene, loaded, bindings).value();
      auto renderer = Graphics::Renderer::create().value();
      const auto baseline = renderer.resourceStats().value();

      {
        auto drawing = Graphics::SceneRenderer::create(renderer, files).value();
        auto initial = drawing.render(scene, 256, 256).value();
        bool changed{};

        for (int frame = 0; frame <= 8; ++frame) {
          animator.seek(scene, double(frame) * .25).value();
          auto image = drawing.render(scene, 256, 256).value();
          changed = changed || image.pixels != initial.pixels;
          files
              .writeAtomic("animation-" + std::to_string(frame) + ".png",
                           Assets::encodeImage(image).value())
              .value();
        }

        if (!changed || scene.entity(child)->transform.position.y != 1) {
          throw std::runtime_error(
              "Animation failed to change real pixels or changed an unauthored channel");
        }

        animator.stop(scene).value();

        if (drawing.render(scene, 256, 256)->pixels != initial.pixels) {
          throw std::runtime_error("Animation stop did not restore real bind-pose pixels");
        }
      }

      if (renderer.resourceStats()->meshes != baseline.meshes
          || renderer.resourceStats()->targets != baseline.targets) {
        throw std::runtime_error("Animation acceptance retained drawing resources");
      }

      std::println(
          "Animation acceptance passed: strict saved clip, hierarchical TRS playback, nine GPU "
          "frames, bind-pose restore and resource release");
      return 0;
    } catch (const std::exception& error) {
      std::println(stderr, "Animation acceptance: {}", error.what());
      return 1;
    }
  }
}  // namespace Dot::Runtime
