#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

import Dot.Assets.Material;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Math;

namespace {
  struct Fixture {
    Dot::Core::StagedDirectory storage;
    Dot::Core::FileSystem files;
    Dot::World::Scene scene;
    Dot::World::EntityId camera;
    Dot::World::EntityId mesh;
    Dot::World::EntityId light;

    Fixture()
        : storage(Dot::Core::StagedDirectory::create(
                      Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()),
          files(storage.files().value()),
          scene(Dot::World::Scene::create().value()),
          camera(scene.createEntity("Camera").value()),
          mesh(scene.createEntity("Quad").value()),
          light(scene.createEntity("Light").value()) {
      REQUIRE(scene.setCamera(camera,
                              Dot::World::Camera{.projection = Dot::World::Projection::Orthographic,
                                                 .orthographicHeight = 2,
                                                 .primary = true}));
      REQUIRE(scene.setTransform(camera, {{0, 0, 2}}));
      REQUIRE(scene.setMeshRenderer(
          mesh, Dot::World::MeshRenderer{.kind = Dot::World::MeshKind::Quad, .asset = {}}));
      REQUIRE(scene.setMaterial(mesh, Dot::World::MaterialBinding{"surface.dotmaterial"}));
      REQUIRE(scene.setLight(light, Dot::World::Light{.intensity = std::numbers::pi_v<float>}));
      Dot::Assets::MaterialDescription material;
      material.baseColor = {0.5F, 0.5F, 0.5F, 1};
      material.roughness = 1;
      REQUIRE(Dot::Assets::saveMaterial(files, "surface.dotmaterial", material));
    }

    auto texture(std::string_view name, std::string_view destination) -> void {
      const auto fixtures = Dot::Core::FileSystem::open(DOT_IMAGE_FIXTURE_DIRECTORY).value();
      auto bytes = fixtures.readBytes(std::string(name)).value();
      REQUIRE(files.writeAtomic(std::string(destination), bytes));
    }
  };

  auto renderer() -> Dot::Graphics::Renderer {
    auto result = Dot::Graphics::Renderer::create(
        nullptr, {.postProcess
                  = {0, Dot::Graphics::ToneMap::LinearClamp, Dot::Graphics::ColorSpace::Linear}});
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }

  auto red(const Dot::Graphics::HdrImage& image, std::uint32_t x = 1, std::uint32_t y = 1)
      -> float {
    return image.pixels.at((static_cast<std::size_t>(y) * image.width + x) * 4);
  }
}  // namespace

TEST_CASE("scene_material.saved_physical_factors_and_hierarchical_light_pose_drive_gpu_pixels") {
  Fixture fixture;
  auto device = renderer();
  auto drawing = Dot::Graphics::SceneRenderer::create(device, fixture.files).value();
  auto target = device.createTarget(3, 3).value();
  auto loaded = Dot::World::decodeScene(Dot::World::encodeScene(fixture.scene).value()).value();
  REQUIRE(drawing.renderInto(loaded, target));
  CHECK(red(device.readHdrTarget(target).value()) == doctest::Approx(0.49).epsilon(0.002));
  CHECK(device.lighting()->viewDirection == Dot::Math::Vec3{0, 0, -1});
  REQUIRE(fixture.scene.setLight(fixture.light,
                                 Dot::World::Light{.kind = Dot::World::LightKind::Point,
                                                   .intensity = 4 * std::numbers::pi_v<float>,
                                                   .range = 100}));
  REQUIRE(fixture.scene.setTransform(fixture.light, {{0, 0, 2}}));
  REQUIRE(drawing.renderInto(fixture.scene, target));
  CHECK(red(device.readHdrTarget(target).value()) == doctest::Approx(0.49).epsilon(0.002));
  auto parent = fixture.scene.createEntity("Light parent").value();
  REQUIRE(fixture.scene.setParent(fixture.light, parent));
  REQUIRE(fixture.scene.setTransform(parent, {{0, 0, 2}}));
  REQUIRE(drawing.renderInto(fixture.scene, target));
  CHECK(red(device.readHdrTarget(target).value()) == doctest::Approx(0.49 / 4).epsilon(0.003));
  REQUIRE(fixture.scene.setLight(fixture.light, Dot::World::Light{.enabled = false}));
  REQUIRE(drawing.renderInto(fixture.scene, target));
  CHECK(red(device.readHdrTarget(target).value()) == 0);
  CHECK(drawing.stats()->materials == 1);
}

TEST_CASE("scene_material.shared_textures_materials_reload_and_prune_with_owned_byte_counters") {
  Fixture fixture;
  fixture.texture("normal.png", "normal.png");
  fixture.texture("surface.png", "color.png");
  auto material = Dot::Assets::loadMaterial(fixture.files, "surface.dotmaterial").value();
  material.baseColorTexture = Dot::Assets::TextureSource{"color.png"};
  Dot::Graphics::TextureOptions linear;
  linear.colorSpace = Dot::Graphics::ColorSpace::Linear;
  material.normalTexture = Dot::Assets::TextureSource{"normal.png", linear};
  REQUIRE(Dot::Assets::saveMaterial(fixture.files, "surface.dotmaterial", material));
  REQUIRE(Dot::Assets::saveMaterial(fixture.files, "second.dotmaterial", material));
  auto second = fixture.scene.createEntity("Second").value();
  REQUIRE(fixture.scene.setTransform(second, {{0.5F, 0, -0.1F}}));
  REQUIRE(fixture.scene.setMeshRenderer(
      second, Dot::World::MeshRenderer{.kind = Dot::World::MeshKind::Quad, .asset = {}}));
  REQUIRE(fixture.scene.setMaterial(second, Dot::World::MaterialBinding{"second.dotmaterial"}));
  auto device = renderer();
  auto drawing = Dot::Graphics::SceneRenderer::create(device, fixture.files).value();
  auto target = device.createTarget(3, 3).value();
  REQUIRE(drawing.renderInto(fixture.scene, target));
  CHECK(drawing.stats()->meshes == 1);
  CHECK(drawing.stats()->materials == 2);
  CHECK(drawing.stats()->textures == 2);
  CHECK(device.resourceStats()->textures == 2);
  CHECK(drawing.stats()->bytes
        == device.resourceStats()->vertexBytes + device.resourceStats()->indexBytes
               + drawing.stats()->textureBytes);
  material.emissive = {8, 0, 0};
  REQUIRE(Dot::Assets::saveMaterial(fixture.files, "surface.dotmaterial", material));
  const auto before = device.readHdrTarget(target)->pixels;
  REQUIRE(drawing.renderInto(fixture.scene, target));
  CHECK(device.readHdrTarget(target)->pixels == before);
  REQUIRE(drawing.clearCache());
  CHECK(device.resourceStats()->textures == 0);
  REQUIRE(drawing.renderInto(fixture.scene, target));
  CHECK(red(device.readHdrTarget(target).value()) > 7);
  REQUIRE(fixture.scene.setMeshRenderer(fixture.mesh, std::nullopt));
  REQUIRE(fixture.scene.setMeshRenderer(second, std::nullopt));
  REQUIRE(drawing.renderInto(fixture.scene, target));
  CHECK(drawing.stats()->bytes == 0);
  CHECK(drawing.stats()->materials == 0);
  CHECK(drawing.stats()->textures == 0);
  CHECK(device.resourceStats()->textures == 0);
}

TEST_CASE("scene_material.invalid_or_partial_resources_and_budgets_fail_without_a_new_frame") {
  Fixture fixture;
  auto device = renderer();
  auto drawing = Dot::Graphics::SceneRenderer::create(device, fixture.files).value();
  auto target = device.createTarget(3, 3).value();
  REQUIRE(drawing.renderInto(fixture.scene, target));
  const auto before = device.readHdrTarget(target)->pixels;
  REQUIRE(
      fixture.scene.setMaterial(fixture.mesh, Dot::World::MaterialBinding{"missing.dotmaterial"}));
  CHECK_FALSE(drawing.renderInto(fixture.scene, target));
  CHECK(target.info()->frames == 1);
  CHECK(device.readHdrTarget(target)->pixels == before);
  fixture.texture("surface.png", "color.png");
  auto material = Dot::Assets::loadMaterial(fixture.files, "surface.dotmaterial").value();
  material.baseColorTexture = Dot::Assets::TextureSource{"color.png"};
  material.emissiveTexture = Dot::Assets::TextureSource{"missing.png"};
  REQUIRE(Dot::Assets::saveMaterial(fixture.files, "surface.dotmaterial", material));
  REQUIRE(
      fixture.scene.setMaterial(fixture.mesh, Dot::World::MaterialBinding{"surface.dotmaterial"}));
  CHECK_FALSE(drawing.renderInto(fixture.scene, target));
  CHECK(device.resourceStats()->textures == 0);
  CHECK(drawing.stats()->textures == 0);
  CHECK(target.info()->frames == 1);
  material.emissiveTexture.reset();
  Dot::Graphics::TextureOptions linear;
  linear.colorSpace = Dot::Graphics::ColorSpace::Linear;
  material.normalTexture = Dot::Assets::TextureSource{"color.png", linear};
  REQUIRE(Dot::Assets::saveMaterial(fixture.files, "surface.dotmaterial", material));
  CHECK_FALSE(drawing.renderInto(fixture.scene, target));
  CHECK(device.resourceStats()->textures == 0);
  CHECK(target.info()->frames == 1);
  material.normalTexture.reset();
  REQUIRE(Dot::Assets::saveMaterial(fixture.files, "surface.dotmaterial", material));
  REQUIRE(drawing.clearCache());
  auto limited = Dot::Graphics::SceneRenderer::create(device, fixture.files,
                                                      4 * sizeof(Dot::Graphics::Vertex) + 6 * 4)
                     .value();
  CHECK_FALSE(limited.renderInto(fixture.scene, target));
  CHECK(limited.stats()->textures == 0);
  CHECK(target.info()->frames == 1);
  auto rootless = Dot::Graphics::SceneRenderer::create(device).value();
  CHECK_FALSE(rootless.renderInto(fixture.scene, target));
  REQUIRE(drawing.renderInto(fixture.scene, target));
}

TEST_CASE("scene_material_light_limit_and_camera_override_are_validated_before_submission") {
  Fixture fixture;
  auto device = renderer();
  auto drawing = Dot::Graphics::SceneRenderer::create(device, fixture.files).value();
  auto target = device.createTarget(3, 3).value();
  REQUIRE(drawing.renderInto(fixture.scene, target));
  for (int index = 0; index < 64; ++index) {
    auto light = fixture.scene.createEntity("Extra light").value();
    REQUIRE(fixture.scene.setLight(light, Dot::World::Light{}));
  }
  CHECK_FALSE(drawing.renderInto(fixture.scene, target));
  CHECK(target.info()->frames == 1);
  auto extra = fixture.scene.entities()->back().id;
  REQUIRE(fixture.scene.setLight(extra, Dot::World::Light{.enabled = false}));
  REQUIRE(drawing.renderInto(fixture.scene, target));
  CHECK(device.lighting()->lights.size() == 64);
  auto alternate = fixture.scene.createEntity("Perspective camera").value();
  REQUIRE(fixture.scene.setCamera(alternate, Dot::World::Camera{}));
  REQUIRE(fixture.scene.setTransform(alternate, {{1, 2, 3}}));
  REQUIRE(drawing.renderInto(fixture.scene, target, {}, alternate));
  CHECK(device.lighting()->viewPosition == Dot::Math::Vec3{1, 2, 3});
  CHECK_FALSE(device.lighting()->viewDirection);
}
