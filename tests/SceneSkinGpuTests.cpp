#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <utility>
#include <vector>

import Dot.Animation.SceneSkin;
import Dot.Animation.SkinFiles;
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Graphics.Geometry;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.Graphics.Skinning;
import Dot.Math;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot;
  using namespace Dot::Graphics;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Core::FileSystem files{storage.files().value()};
    World::Scene scene{World::Scene::create().value()};
    World::EntityId joint{scene.createEntity("Joint").value()};
    World::EntityId owner{scene.createEntity("Mesh").value()};
    MeshData geometry{quad().value()};
    Animation::Skin skin
        = Animation::Skin::create({std::vector<Math::Mat4>(1),
                                   std::vector<Animation::SkinInfluence>(geometry.vertices.size())})
              .value();
    World::SkinBinding properties{"assets/rig.dotskin", {joint.value}};

    Fixture() {
      files.createDirectories("assets").value();
      Animation::saveSkin(files, properties.asset, skin).value();
      Assets::saveMesh(files, "assets/mesh.dotmesh", geometry).value();
      scene
          .setMeshRenderer(owner,
                           World::MeshRenderer{World::MeshKind::Asset, "assets/mesh.dotmesh"})
          .value();
      scene.setTransform(owner, {{7, 0, 0}}).value();
      scene.setSkin(owner, properties).value();
      const auto camera = scene.createEntity("Camera").value();
      World::Camera cameraProperties;
      cameraProperties.primary = true;
      cameraProperties.projection = World::Projection::Orthographic;
      cameraProperties.orthographicHeight = 4;
      scene.setCamera(camera, cameraProperties).value();
      scene.setTransform(camera, {{0, 0, 5}}).value();
    }
  };
}  // namespace

TEST_CASE("skin.scene_gpu_matches_saved_cpu_reference_bounds_picking_and_persistent_target") {
  Fixture f;
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  const auto initial = service.render(f.scene, 64, 64).value();
  const auto residency = renderer.resourceStats().value();
  REQUIRE(f.scene.setTransform(f.joint, {{1, 0, 0}}));
  auto actual = service.render(f.scene, 64, 64).value();
  CHECK(actual.pixels != initial.pixels);
  CHECK(renderer.resourceStats()->vertexBytes == residency.vertexBytes);
  CHECK(renderer.resourceStats()->skinBytes == residency.skinBytes);
  const auto bounds = service.bounds(f.scene, f.owner).value();
  CHECK(bounds.minimum.x == doctest::Approx(.5));
  CHECK(bounds.maximum.x == doctest::Approx(1.5));
  CHECK(service.pick(f.scene, 64, 64, {.75F, .5F}).value() == f.owner);
  CHECK_FALSE(service.pick(f.scene, 64, 64, {.5F, .5F}).value());
  auto clone = World::decodeScene(World::encodeScene(f.scene).value()).value();
  auto binding = Animation::SceneSkin::bind(f.scene, f.owner, f.skin).value();
  const auto reference = deformMesh(f.geometry, f.skin, binding.palette(f.scene).value()).value();
  REQUIRE(Assets::saveMesh(f.files, "assets/reference.dotmesh", reference));
  const World::EntityId copied{clone.id(), f.owner.value};
  REQUIRE(clone.setSkin(copied, std::nullopt));
  REQUIRE(clone.setMeshRenderer(
      copied, World::MeshRenderer{World::MeshKind::Asset, "assets/reference.dotmesh"}));
  auto referenceRenderer = SceneRenderer::create(renderer, f.files).value();
  CHECK(referenceRenderer.render(clone, 64, 64)->pixels == actual.pixels);
  auto target = renderer.createTarget(64, 64).value();
  REQUIRE(service.renderInto(f.scene, target));
  CHECK(renderer.readTarget(target)->pixels == actual.pixels);
  CHECK(service.stats()->skins == 1);
  CHECK(service.stats()->meshes == 1);
}

TEST_CASE("skin.scene_gpu_ignored_mesh_reflection_preserves_physical_faces_picking_and_normals") {
  Fixture f;
  Assets::MaterialDescription material;
  material.baseColor = {.2F, .4F, .8F, 1};
  REQUIRE(Assets::saveMaterial(f.files, "assets/physical.dotmaterial", material));
  REQUIRE(f.scene.setMaterial(f.owner, World::MaterialBinding{"assets/physical.dotmaterial"}));
  const auto light = f.scene.createEntity("Light").value();
  REQUIRE(f.scene.setLight(light, World::Light{.intensity = 4}));
  REQUIRE(f.scene.setOcclusion(OcclusionSettings{}));
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  const auto initial = service.render(f.scene, 64, 64).value();
  REQUIRE(service.pick(f.scene, 64, 64, {.5F, .5F}).value() == f.owner);
  REQUIRE(f.scene.setTransform(f.owner, {{7, 3, 1}, {}, {-2, 3, .5F}}));
  const auto mirrored = service.render(f.scene, 64, 64).value();
  CHECK((mirrored.pixels == initial.pixels));
  CHECK(service.pick(f.scene, 64, 64, {.5F, .5F}).value() == f.owner);
  REQUIRE(f.scene.setTransform(f.joint, {{}, {}, {-1, 1, 1}}));
  const auto mirroredJoint = service.render(f.scene, 64, 64).value();
  CHECK((mirroredJoint.pixels == initial.pixels));
  CHECK(service.pick(f.scene, 64, 64, {.5F, .5F}).value() == f.owner);
}

TEST_CASE("skin.scene_gpu_shares_inputs_prunes_hidden_disabled_and_foreign_scene_scopes") {
  Fixture f;
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  auto second = f.scene.createEntity("Second").value();
  REQUIRE(f.scene.setMeshRenderer(second, *f.scene.entity(f.owner)->mesh));
  REQUIRE(f.scene.setSkin(second, f.properties));
  REQUIRE(service.render(f.scene, 32, 32));
  CHECK(service.stats()->meshes == 1);
  CHECK(service.stats()->skins == 2);
  CHECK(renderer.resourceStats()->skins == 2);
  auto hidden = *f.scene.entity(second)->mesh;
  hidden.visible = false;
  REQUIRE(f.scene.setMeshRenderer(second, hidden));
  REQUIRE(service.render(f.scene, 32, 32));
  CHECK(service.stats()->skins == 1);
  f.properties.enabled = false;
  REQUIRE(f.scene.setSkin(f.owner, f.properties));
  REQUIRE(service.render(f.scene, 32, 32));
  CHECK(service.stats()->skins == 0);
  CHECK(renderer.resourceStats()->skins == 0);
  CHECK(renderer.resourceStats()->skinBytes == 0);
  auto other = World::decodeScene(World::encodeScene(f.scene).value()).value();
  f.properties.enabled = true;
  const World::EntityId owner{other.id(), f.owner.value};
  REQUIRE(other.setSkin(owner, f.properties));
  REQUIRE(service.render(other, 32, 32));
  CHECK(service.stats()->skins == 1);
  CHECK(service.bounds(other, owner));
  REQUIRE(service.clearCache());
  CHECK(service.stats()->bytes == 0);
  CHECK(renderer.resourceStats()->meshes == 0);
  CHECK(renderer.resourceStats()->skins == 0);
}

TEST_CASE("skin.scene_gpu_exact_cache_budget_missing_assets_and_count_errors_are_explicit") {
  Fixture f;
  auto renderer = Renderer::create().value();
  const auto source = f.geometry.vertices.size() * sizeof(Vertex)
                      + f.geometry.indices.size() * sizeof(std::uint32_t);
  const auto skinBytes
      = f.geometry.vertices.size() * (2 * sizeof(Vertex) + sizeof(Animation::SkinInfluence))
        + sizeof(Math::Mat4) + 48;
  auto exact = SceneRenderer::create(renderer, f.files, source + skinBytes).value();
  REQUIRE(exact.render(f.scene, 16, 16));
  CHECK(exact.stats()->bytes == source + skinBytes);
  CHECK(exact.stats()->skinBytes == skinBytes);
  REQUIRE(exact.clearCache());
  auto shortBudget = SceneRenderer::create(renderer, f.files, source + skinBytes - 1).value();
  CHECK_FALSE(shortBudget.render(f.scene, 16, 16));
  CHECK(shortBudget.stats()->skins == 0);
  CHECK(renderer.resourceStats()->skins == 0);
  auto unrooted = SceneRenderer::create(renderer).value();
  CHECK_FALSE(unrooted.render(f.scene, 16, 16));
  auto valid = SceneRenderer::create(renderer, f.files).value();
  auto wrong = Animation::Skin::create(
                   {std::vector<Math::Mat4>(1), std::vector<Animation::SkinInfluence>(5)})
                   .value();
  REQUIRE(Animation::saveSkin(f.files, f.properties.asset, wrong));
  CHECK_FALSE(valid.render(f.scene, 16, 16));
  CHECK(valid.stats()->skins == 0);
  wrong = Animation::Skin::create(
              {std::vector<Math::Mat4>(2), std::vector<Animation::SkinInfluence>(4)})
              .value();
  REQUIRE(Animation::saveSkin(f.files, f.properties.asset, wrong));
  CHECK_FALSE(valid.render(f.scene, 16, 16));
  CHECK_FALSE(valid.bounds(f.scene, f.owner));
}

TEST_CASE("skin.scene_gpu_retains_immutable_assets_until_explicit_reload_and_recovers_disabled") {
  Fixture f;
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  const auto before = service.render(f.scene, 16, 16).value();
  REQUIRE(f.files.writeTextAtomic(f.properties.asset, "corrupt skin"));
  CHECK(service.render(f.scene, 16, 16)->pixels == before.pixels);
  REQUIRE(service.clearCache());
  CHECK_FALSE(service.render(f.scene, 16, 16));
  CHECK(service.stats()->skins == 0);
  f.properties.enabled = false;
  REQUIRE(f.scene.setSkin(f.owner, f.properties));
  REQUIRE(service.render(f.scene, 16, 16));
  CHECK(service.stats()->skins == 0);
}

TEST_CASE("skin.scene_gpu_invalid_joint_blend_preserves_target_and_all_residency") {
  Fixture f;
  const auto tip = f.scene.createEntity("Tip").value();
  auto description = *f.skin.description().value();
  description.inverseBindMatrices.resize(2);

  for (auto& influence : description.vertices) {
    influence.joints[1] = 1;
    influence.weights = {.5F, .5F};
  }

  REQUIRE(Animation::saveSkin(f.files, f.properties.asset,
                              Animation::Skin::create(std::move(description)).value()));
  f.properties.joints.push_back(tip.value);
  REQUIRE(f.scene.setSkin(f.owner, f.properties));
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  auto target = renderer.createTarget(32, 32).value();
  REQUIRE(service.renderInto(f.scene, target));
  const auto before = renderer.readTarget(target).value();
  const auto bytes = service.stats()->bytes;
  REQUIRE(f.scene.setTransform(tip, {{}, {}, {-1, 1, 1}}));
  CHECK_FALSE(service.renderInto(f.scene, target));
  CHECK_FALSE(service.bounds(f.scene, f.owner));
  CHECK(renderer.readTarget(target)->pixels == before.pixels);
  CHECK(service.stats()->bytes == bytes);
  CHECK(service.stats()->skins == 1);
  REQUIRE(f.scene.setTransform(tip, {{1, 0, 0}}));
  REQUIRE(service.renderInto(f.scene, target));
  CHECK(renderer.readTarget(target)->pixels != before.pixels);
  CHECK(service.stats()->bytes == bytes);
}
