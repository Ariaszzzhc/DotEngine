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
  /** Exercise emissive 3D rendering, pre/post-map evidence, native presentation,
   * resize and shared resource release. Save the GPU image as an RGB PPM artifact.
   * 验证自发光三维绘制、映射前后数据、原生呈现、尺寸变化和共享资源释放，
   * 将 GPU 图像保存为 RGB PPM 产物。 */
  auto testHdr(const std::filesystem::path& root) -> int {
    auto files = Core::FileSystem::open(root).value();
    Core::ApplicationConfig config;
    config.window.title = "DotEngine HDR";
    config.window.width = 640;
    config.window.height = 360;
    config.window.gpuSurface = true;
    config.pauseOnFocusLoss = false;
    config.frameDelay = std::chrono::milliseconds{5};
    auto application = Core::Application::create(config).value();
    auto renderer = Graphics::Renderer::create(&application.window()).value();
    auto geometry = Graphics::withFlatNormals(Graphics::cube().value()).value();

    for (auto& vertex : geometry.vertices) {
      vertex.color
          = {(vertex.normal.x + 2) / 3, (vertex.normal.y + 2) / 3, (vertex.normal.z + 2) / 3, 1};
    }

    std::size_t presented{};

    {
      auto mesh = renderer.upload(geometry).value();
      const auto camera = Math::multiply(Math::perspective(50, 16.0F / 9.0F, 0.1F, 100).value(),
                                         Math::lookAt({0, 2, 7}, {}).value())
                              .value();
      std::array<Graphics::MeshDraw, 3> draws{};
      const std::array<float, 3> intensities{0, 2, 8};

      for (std::size_t index = 0; index < draws.size(); ++index) {
        draws[index].mesh = &mesh;
        draws[index].transform
            = Math::compose({static_cast<float>(index) * 1.6F - 1.6F, 0, 0}, {}, {1, 1, 1}).value();
        draws[index].material.emissive
            = {intensities[index], intensities[index], intensities[index]};
      }

      auto target = renderer.createTarget(640, 360).value();
      renderer.renderMeshesInto(target, draws, camera).value();
      auto hdr = renderer.readHdrTarget(target).value();
      auto image = renderer.readTarget(target).value();
      float maximum{};

      for (std::size_t index = 0; index < hdr.pixels.size(); index += 4) {
        maximum = std::max(maximum, hdr.pixels[index]);
      }

      if (maximum < 8 || maximum > 9) {
        std::println(stderr, "HDR highlight radiance was clipped or corrupted: {}", maximum);
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

      files.writeAtomic("hdr-tonemap.ppm", ppm).value();
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

    if (presented < 4 || stats.meshes != 0 || stats.targets != 0) {
      std::println(stderr, "HDR native presentation/release failed");
      return 1;
    }

    std::println(
        "HDR acceptance passed: floating radiance, ACES/sRGB output, native resize and release");
    return 0;
  }
}  // namespace Dot::Runtime
