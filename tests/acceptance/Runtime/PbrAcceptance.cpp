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
    Graphics::Lighting lights;
    lights.viewPosition = {0, 1.2F, 9};
    lights.lights = {{.direction = {-0.6F, -0.8F, -1}, .color = {1, 0.95F, 0.9F}, .intensity = 5},
                     {.direction = {1, -0.3F, -0.5F}, .color = {0.6F, 0.75F, 1}, .intensity = 2}};
    renderer.setLighting(lights).value();
    std::size_t presented{};

    {
      auto mesh = renderer.upload(Graphics::sphere(0.5F, 64, 32).value()).value();
      Graphics::Image normal{8, 8, {}};
      normal.pixels.resize(8 * 8 * 4);

      for (std::size_t index = 0; index < 64; ++index) {
        normal.pixels[index * 4] = (index / 8 + index % 8) % 2 == 0 ? 218 : 38;
        normal.pixels[index * 4 + 1] = 128;
        normal.pixels[index * 4 + 2] = 218;
        normal.pixels[index * 4 + 3] = 255;
      }

      auto mapped
          = renderer.uploadTexture(normal, {.colorSpace = Graphics::ColorSpace::Linear}).value();
      std::array<Graphics::MeshDraw, 15> draws{};
      const std::array<float, 5> roughness{0.12F, 0.28F, 0.45F, 0.65F, 0.9F};
      const std::array<Math::Vec4, 3> colors{
          {{0.6F, 0.6F, 0.6F, 1}, {1, 0.766F, 0.336F, 1}, {0.955F, 0.638F, 0.538F, 1}}};

      for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 5; ++column) {
          auto& draw = draws[row * 5 + column];
          draw.mesh = &mesh;
          draw.transform = Math::compose({static_cast<float>(column) * 1.2F - 2.4F,
                                          1.2F - static_cast<float>(row) * 1.2F, 0})
                               .value();
          draw.material.model = Graphics::SurfaceModel::MetallicRoughness;
          draw.material.baseColor = colors[row];
          draw.material.metallic = row == 0 ? 0 : 1;
          draw.material.roughness = roughness[column];
        }
      }

      draws.back().material.normalTexture = mapped;
      const auto camera = Math::multiply(Math::perspective(42, 16.0F / 9.0F, 0.1F, 100).value(),
                                         Math::lookAt(lights.viewPosition, {}).value())
                              .value();
      auto target = renderer.createTarget(960, 540).value();
      renderer.renderMeshesInto(target, draws, camera).value();
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

        auto result = renderer.presentMeshes(draws, camera);

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
        "HDR");
    return 0;
  }
}  // namespace Dot::Runtime
