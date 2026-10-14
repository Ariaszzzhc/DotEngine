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

import Dot.Core.Files;
import Dot.Graphics.Renderer;
import Dot.Math;

namespace Dot::Runtime {
  /** Render actual offscreen opaque 3D geometry with hard/filtered/no shadows.
   * Save post-ACES images and verify raw HDR differences and working-map release.
   * 离屏绘制真实不透明 3D 几何，对比硬边/过滤/无阴影；保存 GPU ACES 图像，验证
   * 原始 HDR 差异及工作深度图释放。 */
  auto testShadows(const std::filesystem::path& output) -> int {
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
      std::println(stderr, "Shadow gallery resource creation failed");
      return 1;
    }

    const Math::Vec3 position{6, 5, 9};
    const auto camera = Math::multiply(Math::perspective(42, 16.0F / 9.0F, 0.1F, 60).value(),
                                       Math::lookAt(position, {}).value())
                            .value();
    Graphics::Lighting lighting;
    lighting.viewPosition = position;
    Graphics::ShadowSettings settings;
    settings.resolution = 512;
    settings.cascades = 4;
    settings.distance = 40;
    settings.filterRadius = 0;
    lighting.lights = {{.direction = {0.6F, -1, 0.35F}, .intensity = 5, .shadows = settings}};
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
    std::array<Graphics::HdrImage, 3> linear;
    const std::array names{"shadow-hard.ppm", "shadow-soft.ppm", "shadow-none.ppm"};

    for (std::size_t mode = 0; mode < names.size(); ++mode) {
      if (mode == 1) {
        lighting.lights[0].shadows->filterRadius = 6;
      } else if (mode == 2) {
        lighting.lights[0].shadows.reset();
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
      std::string ppm = "P6\n960 540\n255\n";

      for (std::size_t index = 0; index < image->pixels.size(); index += 4) {
        ppm.append(reinterpret_cast<const char*>(image->pixels.data() + index), 3);
      }

      auto saved = files->writeTextAtomic(names[mode], ppm);

      if (!saved) {
        std::println(stderr, "{}", saved.error().message);
        return 1;
      }
    }

    std::size_t occluded{};
    std::size_t filtered{};

    for (std::size_t index = 0; index < linear[0].pixels.size(); index += 4) {
      occluded += linear[2].pixels[index] - linear[0].pixels[index] > 0.1F;
      filtered += std::abs(linear[1].pixels[index] - linear[0].pixels[index]) > 0.01F;
    }

    if (occluded < 100 || filtered < 100 || renderer->resourceStats()->shadowBytes != 0) {
      std::println(stderr, "Shadow gallery coverage/filtering/release failed: {} / {}", occluded,
                   filtered);
      return 1;
    }

    std::println("Shadow GPU acceptance passed: {} occluded and {} filtered pixels", occluded,
                 filtered);
    return 0;
  }
}  // namespace Dot::Runtime
