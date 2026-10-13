#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <vector>

import Dot.Core.Application;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Assets.Material;
import Dot.Assets.Mesh;
import Dot.Graphics.SceneRenderer;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Core.Result;
import Dot.Graphics.Renderer;
import Dot.Math;

namespace Dot::Runtime {
  /** Render a dielectric/gold/copper sphere grid across roughness, plus a normal
   * mapped surface, through direct lights/HDR and actual native presentation.
   * 按粗糙度渲染介质/金/铜球网格及法线贴图表面，经直接光/HDR 和真实原生窗口呈现。 */
  auto testPbr(const std::filesystem::path& root) -> int {
    auto files = Core::FileSystem::open(root).value();
    Core::ApplicationConfig config;
    config.window.title = "DotEngine PBR - Direct Lighting";
    config.window.width = 960;
    config.window.height = 540;
    config.window.gpuSurface = true;
    config.pauseOnFocusLoss = false;
    config.frameDelay = std::chrono::milliseconds{5};
    auto application = Core::Application::create(config).value();
    auto renderer = Graphics::Renderer::create(&application.window()).value();
    std::size_t presented{};

    {
      auto storage = Core::StagedDirectory::create(files).value();
      auto assets = storage.files().value();
      assets.createDirectories("assets").value();
      Assets::saveMesh(assets, "assets/sphere.dotmesh", Graphics::sphere(0.5F, 64, 32).value())
          .value();
      auto fixtures = Core::FileSystem::open(DOT_IMAGE_FIXTURE_DIRECTORY).value();
      assets.writeAtomic("assets/normal.png", fixtures.readBytes("normal.png").value()).value();
      auto scene = World::Scene::create("Persisted physical gallery").value();
      const auto pose = [](Math::Vec3 position, Math::Vec3 target) -> World::Transform {
        const auto world = Math::inverse(Math::lookAt(position, target).value()).value();
        const auto parts = Math::decomposeTrs(world).value();
        return {parts.position, parts.rotation, parts.scale};
      };
      auto camera = scene.createEntity("Camera").value();
      scene
          .setCamera(camera,
                     World::Camera{.verticalFovDegrees = 42, .farPlane = 100, .primary = true})
          .value();
      scene.setTransform(camera, pose({0, 1.2F, 9}, {})).value();
      const std::array<Math::Vec3, 2> directions{{{-0.6F, -0.8F, -1}, {1, -0.3F, -0.5F}}};
      const std::array<Math::Vec3, 2> lightColors{{{1, 0.95F, 0.9F}, {0.6F, 0.75F, 1}}};

      for (std::size_t index = 0; index < 2; ++index) {
        auto light = scene.createEntity("Direct light").value();
        scene
            .setLight(light, World::Light{.color = lightColors[index],
                                          .intensity = index == 0 ? 5.0F : 2.0F})
            .value();
        scene.setTransform(light, pose({}, directions[index])).value();
      }

      const std::array<float, 5> roughness{0.12F, 0.28F, 0.45F, 0.65F, 0.9F};
      const std::array<Math::Vec4, 3> colors{
          {{0.6F, 0.6F, 0.6F, 1}, {1, 0.766F, 0.336F, 1}, {0.955F, 0.638F, 0.538F, 1}}};

      for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 5; ++column) {
          const auto path = std::format("assets/material-{}-{}.dotmaterial", row, column);
          Assets::MaterialDescription material;
          material.baseColor = colors[row];
          material.metallic = row == 0 ? 0 : 1;
          material.roughness = roughness[column];

          if (row == 2 && column == 4) {
            Graphics::TextureOptions linear;
            linear.colorSpace = Graphics::ColorSpace::Linear;
            material.normalTexture = Assets::TextureSource{"assets/normal.png", linear};
          }

          Assets::saveMaterial(assets, path, material).value();
          auto entity = scene.createEntity("Physical sphere").value();
          scene
              .setMeshRenderer(entity, World::MeshRenderer{.kind = World::MeshKind::Asset,
                                                           .asset = "assets/sphere.dotmesh"})
              .value();
          scene.setMaterial(entity, World::MaterialBinding{path}).value();
          scene
              .setTransform(entity, {{static_cast<float>(column) * 1.2F - 2.4F,
                                      1.2F - static_cast<float>(row) * 1.2F, 0}})
              .value();
        }
      }

      World::saveScene(assets, "gallery.dotscene", scene).value();
      auto loaded = World::loadScene(assets, "gallery.dotscene").value();
      auto drawing = Graphics::SceneRenderer::create(renderer, assets).value();
      auto target = renderer.createTarget(960, 540).value();
      drawing.renderInto(loaded, target).value();
      const auto cache = drawing.stats().value();

      if (cache.meshes != 1 || cache.materials != 15 || cache.textures != 1) {
        std::println(stderr, "Persisted scene did not retain shared material resources");
        return 1;
      }

      auto raw = renderer.readHdrTarget(target).value();
      auto image = renderer.readTarget(target).value();
      const auto maximum = *std::max_element(raw.pixels.begin(), raw.pixels.end());

      if (maximum <= 1) {
        std::println(stderr, "PBR direct lighting produced no HDR highlights");
        return 1;
      }

      std::string header = std::format("P6\n{} {}\n255\n", image.width, image.height);
      std::vector<std::byte> ppm;
      ppm.reserve(header.size() + static_cast<std::size_t>(image.width) * image.height * 3);

      for (const auto value : header) {
        ppm.push_back(static_cast<std::byte>(value));
      }

      for (std::size_t index = 0; index < image.pixels.size(); index += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
          ppm.push_back(static_cast<std::byte>(image.pixels[index + channel]));
        }
      }

      files.writeAtomic("pbr-direct.ppm", ppm).value();
      Core::LoopCallbacks callbacks;
      callbacks.render = [&](const Core::FrameState& frame) -> Core::Result<void> {
        if (frame.index == 3) {
          auto resized = application.window().resize(800, 450);

          if (!resized) {
            return resized;
          }
        }

        auto result = drawing.present(loaded);

        if (!result) {
          return std::unexpected(result.error());
        }

        presented += *result == Graphics::PresentResult::Presented;
        return {};
      };
      application.run(callbacks, 8).value();
    }

    const auto stats = renderer.resourceStats().value();

    if (presented < 4 || stats.meshes != 0 || stats.targets != 0 || stats.textures != 0) {
      std::println(stderr, "PBR native presentation/shared resource release failed");
      return 1;
    }

    std::println(
        "PBR acceptance passed: dielectric/metal roughness grid, normal mapping, direct light and "
        "HDR from saved material/light scene assets");
    return 0;
  }
}  // namespace Dot::Runtime
