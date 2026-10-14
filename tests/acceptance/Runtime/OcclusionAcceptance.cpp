#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Graphics.Renderer;
import Dot.Math;

namespace Dot::Runtime {
  /** Render actual geometric SSAO with IBL and cascaded shadows; save enabled,
   * disabled and diagnostic visibility images and verify HDR attenuation/release.
   * 真实绘制几何 SSAO、IBL 与级联阴影；保存启用、关闭及可见性诊断图，验证 HDR 衰减与释放。 */
  auto testOcclusion(const std::filesystem::path& output) -> int {
    auto files = Core::FileSystem::open(output);
    auto renderer = Graphics::Renderer::create();

    if (!files || !renderer) {
      std::println(stderr, "{}", files ? renderer.error().message : files.error().message);
      return 1;
    }

    auto floorData = Graphics::quad({10, 10}).value();
    auto cubeData = Graphics::cube().value();
    auto sphereData = Graphics::sphere(0.7F, 48, 32).value();

    for (auto* data : {&floorData, &cubeData, &sphereData}) {
      for (auto& vertex : data->vertices) {
        vertex.color = {1, 1, 1, 1};
      }
    }

    auto floor = renderer->upload(floorData);
    auto cube = renderer->upload(cubeData);
    auto sphere = renderer->upload(sphereData);
    auto target = renderer->createTarget(960, 540);

    if (!floor || !cube || !sphere || !target) {
      std::println(stderr, "Occlusion gallery resource creation failed");
      return 1;
    }

    const Math::Vec3 position{6, 5, 9};
    const auto camera = Math::multiply(Math::perspective(42, 16.0F / 9.0F, 0.1F, 60).value(),
                                       Math::lookAt(position, {}).value())
                            .value();
    Graphics::Lighting lighting;
    lighting.viewPosition = position;
    Graphics::ShadowSettings shadow;
    shadow.resolution = 512;
    shadow.distance = 40;
    Graphics::OcclusionSettings settings;
    settings.radius = 0.8F;
    settings.directions = 16;
    settings.steps = 8;
    lighting.lights = {{.direction = {0.6F, -1, 0.35F}, .intensity = 2, .shadows = shadow}};
    Graphics::Material surface;
    surface.model = Graphics::SurfaceModel::MetallicRoughness;
    surface.baseColor = {0.6F, 0.6F, 0.6F, 1};
    surface.roughness = 1;
    std::array<Graphics::MeshDraw, 3> draws{};
    draws[0] = {&*floor,
                Math::compose({}, Math::rotationEulerDegrees({-90, 0, 0}).value()).value(),
                {1, 1, 1, 1},
                surface};
    draws[1] = {&*cube,
                Math::compose({-1.3F, 0.75F, 0}, {}, {1.5F, 1.5F, 1.5F}).value(),
                {1, 1, 1, 1},
                surface};
    draws[1].material.baseColor = {0.65F, 0.12F, 0.04F, 1};
    draws[1].material.roughness = 0.3F;
    draws[2] = {&*sphere, Math::compose({1.2F, 0.7F, 0}).value(), {1, 1, 1, 1}, surface};
    draws[2].material.baseColor = {0.08F, 0.24F, 0.6F, 1};
    draws[2].material.roughness = 0.2F;
    auto environment = renderer->bakeEnvironment(
        Graphics::HdrImage{4, 2, std::vector<float>(32, 1)}, {16, 4, 16, 16, 32});

    if (!environment
        || !renderer->setEnvironment({Graphics::EnvironmentLighting{*environment, 1, 0, true}})) {
      std::println(stderr, "Occlusion gallery environment creation failed");
      return 1;
    }

    std::array<Graphics::HdrImage, 2> linear;
    const std::array names{"occlusion-enabled.ppm", "occlusion-disabled.ppm"};

    for (std::size_t mode = 0; mode < names.size(); ++mode) {
      auto configured = renderer->setOcclusion(mode == 0 ? std::optional(settings) : std::nullopt);

      if (!configured) {
        std::println(stderr, "{}", configured.error().message);
        return 1;
      }

      auto updated = renderer->setLighting(lighting);

      if (!updated) {
        std::println(stderr, "{}", updated.error().message);
        return 1;
      }

      auto rendered = renderer->renderMeshesInto(*target, draws, camera);

      if (!rendered) {
        std::println(stderr, "{}", rendered.error().message);
        return 1;
      }

      auto image = renderer->readTarget(*target);
      auto hdr = renderer->readHdrTarget(*target);

      if (!image || !hdr) {
        std::println(stderr, "{}", image ? hdr.error().message : image.error().message);
        return 1;
      }

      linear[mode] = std::move(*hdr);
      const auto save = [&](const char* name, const Graphics::Image& pixels) -> Core::Result<void> {
        std::string ppm = std::format("P6\n{} {}\n255\n", pixels.width, pixels.height);

        for (std::size_t index = 0; index < pixels.pixels.size(); index += 4) {
          ppm.append(reinterpret_cast<const char*>(pixels.pixels.data() + index), 3);
        }

        return files->writeTextAtomic(name, ppm);
      };
      auto saved = save(names[mode], *image);

      if (!saved) {
        std::println(stderr, "{}", saved.error().message);
        return 1;
      }

      if (mode == 0) {
        auto visibility = renderer->readOcclusion();

        if (!visibility || !save("occlusion-visibility.ppm", *visibility)) {
          std::println(stderr, "Occlusion diagnostic readback/save failed");
          return 1;
        }
      }
    }

    std::size_t occluded{};

    for (std::size_t index = 0; index < linear[0].pixels.size(); index += 4) {
      occluded += linear[1].pixels[index] - linear[0].pixels[index] > 0.01F;

      if (linear[0].pixels[index] > linear[1].pixels[index] + 0.001F) {
        std::println(stderr, "Occlusion increased radiance");
        return 1;
      }
    }

    if (occluded < 100 || renderer->resourceStats()->occlusionBytes != 0
        || renderer->readOcclusion()) {
      std::println(stderr, "Occlusion gallery coverage/release failed: {}", occluded);
      return 1;
    }

    std::println("Occlusion GPU acceptance passed: {} attenuated pixels", occluded);
    return 0;
  }
}  // namespace Dot::Runtime
