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

#include "../../EnvironmentFixture.hpp"

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Math;

namespace Dot::Runtime {
  /** Save and reopen a scene with real geometric SSAO, IBL and cascaded shadows; save enabled,
   * disabled and diagnostic visibility images and verify HDR attenuation/release.
   * 保存并重开场景，真实绘制几何 SSAO、IBL 与级联阴影；保存启用、关闭及可见性诊断图，验证 HDR
   * 衰减与释放。 */
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

    const auto checked = [&](Core::Result<void> result) -> bool {
      if (!result) {
        std::println(stderr, "{}", result.error().message);
      }

      return result.has_value();
    };

    if (!checked(files->createDirectories("assets"))
        || !checked(Assets::saveMesh(*files, "assets/floor.dotmesh", floorData))
        || !checked(Assets::saveMesh(*files, "assets/cube.dotmesh", cubeData))
        || !checked(Assets::saveMesh(*files, "assets/sphere.dotmesh", sphereData))
        || !checked(DotTest::Environment::write(*files))) {
      return 1;
    }

    const std::array<Math::Vec4, 3> colors{
        {{0.6F, 0.6F, 0.6F, 1}, {0.65F, 0.12F, 0.04F, 1}, {0.08F, 0.24F, 0.6F, 1}}};
    const std::array<const char*, 3> materials{
        "assets/floor.dotmaterial", "assets/cube.dotmaterial", "assets/sphere.dotmaterial"};
    const std::array<const char*, 3> meshes{"assets/floor.dotmesh", "assets/cube.dotmesh",
                                            "assets/sphere.dotmesh"};
    auto scene = World::Scene::create("Saved occlusion gallery").value();
    auto camera = scene.createEntity("Camera").value();
    auto light = scene.createEntity("Sun").value();
    Graphics::ShadowSettings shadow;
    shadow.resolution = 512;
    shadow.distance = 40;

    if (!checked(scene.setCamera(
            camera,
            World::Camera{
                .verticalFovDegrees = 42, .nearPlane = 0.1F, .farPlane = 60, .primary = true}))
        || !checked(scene.setTransform(
            camera, {{6, 5, 9}, Math::rotationEulerDegrees({-24.8F, 33.69F, 0}).value()}))
        || !checked(scene.setLight(light, World::Light{.intensity = 2, .shadows = shadow}))
        || !checked(
            scene.setTransform(light, {{}, Math::rotationEulerDegrees({-55, -30, 0}).value()}))
        || !checked(scene.setEnvironment(
            World::Environment{"assets/studio.hdr", 1, 0, true, {16, 4, 16, 16, 32}}))) {
      return 1;
    }

    const std::array<World::Transform, 3> transforms{
        {{{}, Math::rotationEulerDegrees({-90, 0, 0}).value()},
         {{-1.3F, 0.75F, 0}, {}, {1.5F, 1.5F, 1.5F}},
         {{1.2F, 0.7F, 0}}}};

    for (std::size_t index = 0; index < colors.size(); ++index) {
      Assets::MaterialDescription material;
      material.baseColor = colors[index];
      material.roughness = index == 0 ? 1.0F : 0.3F;
      auto entity = scene.createEntity(meshes[index]).value();

      if (!checked(Assets::saveMaterial(*files, materials[index], material))
          || !checked(scene.setMeshRenderer(
              entity, World::MeshRenderer{.kind = World::MeshKind::Asset, .asset = meshes[index]}))
          || !checked(scene.setMaterial(entity, World::MaterialBinding{materials[index]}))
          || !checked(scene.setTransform(entity, transforms[index]))) {
        return 1;
      }
    }

    auto target = renderer->createTarget(960, 540);
    auto drawing = Graphics::SceneRenderer::create(*renderer, *files);

    if (!target || !drawing) {
      std::println(stderr, "Saved occlusion gallery target/renderer creation failed");
      return 1;
    }

    Graphics::OcclusionSettings settings;
    settings.radius = 0.8F;
    settings.directions = 16;
    settings.steps = 8;
    std::array<Graphics::HdrImage, 2> linear;
    const std::array names{"occlusion-enabled.ppm", "occlusion-disabled.ppm"};

    for (std::size_t mode = 0; mode < names.size(); ++mode) {
      if (!checked(scene.setOcclusion(mode == 0 ? std::optional(settings) : std::nullopt))
          || !checked(World::saveScene(*files, "gallery.dotscene", scene))) {
        return 1;
      }

      auto loaded = World::loadScene(*files, "gallery.dotscene");

      if (!loaded) {
        std::println(stderr, "{}", loaded.error().message);
        return 1;
      }

      if (!checked(drawing->renderInto(*loaded, *target))) {
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
