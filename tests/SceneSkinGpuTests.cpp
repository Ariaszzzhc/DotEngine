#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <thread>
#include <utility>
#include <vector>

import Dot.Animation.SceneSkin;
import Dot.Animation.SkinFiles;
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Core.Files;
import Dot.Core.Result;
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

TEST_CASE("skin.scene_gpu_preparation_follows_visibility_and_rejects_missing_initial_assets") {
  Fixture f;
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer).value();
  auto mesh = f.scene.entity(f.owner)->mesh.value();
  mesh.visible = false;
  REQUIRE(f.scene.setMeshRenderer(f.owner, mesh));
  CHECK(service.readiness(f.scene).value() == ScenePreparationStage::Ready);
  CHECK(service.prepare(f.scene).value() == ScenePreparationStage::Ready);
  CHECK_FALSE(renderer.skinPipelinesReady().value());
  mesh.visible = true;
  REQUIRE(f.scene.setMeshRenderer(f.owner, mesh));
  f.properties.enabled = false;
  REQUIRE(f.scene.setSkin(f.owner, f.properties));
  CHECK(service.prepare(f.scene).value() == ScenePreparationStage::Geometry);
  CHECK_FALSE(renderer.skinPipelinesReady().value());
  f.properties.enabled = true;
  REQUIRE(f.scene.setSkin(f.owner, f.properties));
  CHECK(service.readiness(f.scene).value() == ScenePreparationStage::Skins);
  auto ready = service.prepare(f.scene);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (ready && *ready != ScenePreparationStage::Ready
         && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ready = service.prepare(f.scene);
  }

  REQUIRE_FALSE(ready);
  CHECK(renderer.skinPipelinesReady().value());
  CHECK(service.stats()->bytes == 0);
  CHECK(renderer.resourceStats()->skins == 0);
  CHECK(renderer.resourceStats()->meshes == 0);
  auto target = renderer.createTarget(32, 32).value();
  CHECK_FALSE(service.renderInto(f.scene, target));
  CHECK(target.info()->frames == 0);
  auto loaded = SceneRenderer::create(renderer, f.files).value();
  CHECK(loaded.prepare(f.scene).value() == ScenePreparationStage::Geometry);
  REQUIRE(f.scene.setTransform(f.joint, {{1, 0, 0}}));
  REQUIRE(loaded.renderInto(f.scene, target));
  CHECK(target.info()->frames == 1);
  CHECK(loaded.bounds(f.scene, f.owner)->minimum.x == doctest::Approx(.5));
  CHECK(loaded.pick(f.scene, 32, 32, {.75F, .5F}).value() == f.owner);
  CHECK(renderer.resourceStats()->skins == 1);
}

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

TEST_CASE("skin.scene_gpu_unchanged_pose_reuses_shadows_across_bounds_and_picking") {
  Fixture f;
  const auto light = f.scene.createEntity("Light").value();
  REQUIRE(f.scene.setLight(
      light,
      World::Light{.shadows = ShadowSettings{.resolution = 128, .cascades = 1, .distance = 12}}));
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  const auto first = service.render(f.scene, 64, 64).value();
  CHECK(renderer.visibilityStats()->mesh->shadowPagesUpdated == 1);
  CHECK(service.render(f.scene, 64, 64)->pixels == first.pixels);
  CHECK(renderer.visibilityStats()->mesh->shadowPagesReused == 1);
  CHECK(renderer.visibilityStats()->mesh->shadowDraws == 0);
  REQUIRE(service.bounds(f.scene, f.owner));
  REQUIRE(service.pick(f.scene, 64, 64, {.5F, .5F}));
  CHECK(service.render(f.scene, 64, 64)->pixels == first.pixels);
  CHECK(renderer.visibilityStats()->mesh->shadowPagesReused == 1);
  REQUIRE(f.scene.setTransform(f.joint, {{.5F, 0, 0}}));
  REQUIRE(service.bounds(f.scene, f.owner));
  CHECK(service.render(f.scene, 64, 64)->pixels != first.pixels);
  CHECK(renderer.visibilityStats()->mesh->shadowPagesUpdated == 1);
  REQUIRE(f.scene.setTransform(f.joint, {}));
  CHECK(service.render(f.scene, 64, 64)->pixels == first.pixels);
  CHECK(renderer.visibilityStats()->mesh->shadowPagesUpdated == 1);
  CHECK(service.render(f.scene, 64, 64)->pixels == first.pixels);
  CHECK(renderer.visibilityStats()->mesh->shadowPagesReused == 1);
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

namespace {
  auto prepareInitial(SceneRenderer& service, const World::Scene& scene) -> ScenePreparationStage {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto ready = service.prepare(scene);

    while (ready && *ready != ScenePreparationStage::Ready
           && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      ready = service.prepare(scene);
    }

    REQUIRE_MESSAGE(ready, (ready ? "" : ready.error().message));
    return *ready;
  }

  auto preparePosePending(SceneRenderer& service, const World::Scene& scene) -> void {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto stage = service.prepare(scene);

    while (stage && *stage != ScenePreparationStage::SkinPoses
           && *stage != ScenePreparationStage::Ready
           && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      stage = service.prepare(scene);
    }

    REQUIRE_MESSAGE(stage, (stage ? "" : stage.error().message));
    REQUIRE(*stage == ScenePreparationStage::SkinPoses);
  }
}  // namespace

TEST_CASE("skin.scene_initial_pose_preparation_cancels_stale_hidden_and_cleared_batches") {
  Fixture f;
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  REQUIRE(prepareInitial(service, f.scene) == ScenePreparationStage::Ready);
  auto target = renderer.createTarget(32, 32).value();
  REQUIRE(service.renderInto(f.scene, target));
  const auto before = renderer.readTarget(target).value();
  REQUIRE(service.clearCache());
  CHECK(service.readiness(f.scene).value() == ScenePreparationStage::Geometry);
  preparePosePending(service, f.scene);
  CHECK(service.stats()->skins == 1);
  CHECK(renderer.readTarget(target)->pixels == before.pixels);
  REQUIRE(f.scene.setTransform(f.joint, {{1, 0, 0}}));
  CHECK(service.readiness(f.scene).value() == ScenePreparationStage::SkinPoses);
  auto mesh = f.scene.entity(f.owner)->mesh.value();
  mesh.visible = false;
  REQUIRE(f.scene.setMeshRenderer(f.owner, mesh));
  CHECK(service.prepare(f.scene).value() == ScenePreparationStage::Ready);
  CHECK(service.stats()->skins == 0);
  CHECK(renderer.resourceStats()->skins == 0);
  mesh.visible = true;
  REQUIRE(f.scene.setMeshRenderer(f.owner, mesh));
  preparePosePending(service, f.scene);
  REQUIRE(service.clearCache());
  CHECK(renderer.resourceStats()->skins == 0);
  REQUIRE(prepareInitial(service, f.scene) == ScenePreparationStage::Ready);
  REQUIRE(service.renderInto(f.scene, target));
  CHECK(renderer.readTarget(target)->pixels != before.pixels);
  CHECK(service.bounds(f.scene, f.owner)->minimum.x == doctest::Approx(.5));
  REQUIRE(f.scene.setTransform(f.joint, {{2, 0, 0}}));
  CHECK(service.prepare(f.scene).value() == ScenePreparationStage::Ready);
  REQUIRE(service.renderInto(f.scene, target));
  CHECK(service.bounds(f.scene, f.owner)->minimum.x == doctest::Approx(1.5));
}

TEST_CASE("skin.scene_initial_invalid_blend_retains_target_and_recovers_latest_pose") {
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
  REQUIRE(service.clearCache());
  REQUIRE(f.scene.setTransform(tip, {{}, {}, {-1, 1, 1}}));
  preparePosePending(service, f.scene);
  auto result = service.prepare(f.scene);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (result && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    result = service.prepare(f.scene);
  }

  REQUIRE_FALSE(result);
  CHECK(result.error().code == Core::ErrorCode::InvalidArgument);
  auto retained = service.prepare(f.scene);
  REQUIRE_FALSE(retained);
  CHECK(retained.error().message == result.error().message);
  CHECK(renderer.readTarget(target)->pixels == before.pixels);
  REQUIRE(f.scene.setTransform(tip, {{1, 0, 0}}));
  REQUIRE(prepareInitial(service, f.scene) == ScenePreparationStage::Ready);
  REQUIRE(service.renderInto(f.scene, target));
  CHECK(renderer.readTarget(target)->pixels != before.pixels);
}

TEST_CASE("skin.scene_initial_preparation_finishes_while_joints_advance_and_draws_latest_pose") {
  Fixture f;
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  REQUIRE(prepareInitial(service, f.scene) == ScenePreparationStage::Ready);
  REQUIRE(service.clearCache());
  preparePosePending(service, f.scene);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  auto stage = ScenePreparationStage::SkinPoses;
  float position{};

  while (stage != ScenePreparationStage::Ready && std::chrono::steady_clock::now() < deadline) {
    position += .001F;
    REQUIRE(f.scene.setTransform(f.joint, {{position, 0, 0}}));
    auto next = service.prepare(f.scene);
    REQUIRE_MESSAGE(next, (next ? "" : next.error().message));
    stage = *next;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  REQUIRE(stage == ScenePreparationStage::Ready);
  REQUIRE(service.render(f.scene, 32, 32));
  CHECK(service.bounds(f.scene, f.owner)->minimum.x == doctest::Approx(position - .5F));
  CHECK(service.bounds(f.scene, f.owner)->maximum.x == doctest::Approx(position + .5F));
}

namespace {
  auto untilFrame(SceneRenderer& service, const World::Scene& scene, RenderTarget& target,
                  std::uint64_t& tag, std::uint64_t epoch = 1) -> SceneFrameReceipt {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (std::chrono::steady_clock::now() < deadline) {
      auto result = service.renderIntoAsync(scene, target, ++tag, epoch);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      REQUIRE_FALSE(result->error);

      if (result->completed) {
        return *result->completed;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    FAIL("Async scene frame did not complete within the GPU deadline");
    return {};
  }
}  // namespace

TEST_CASE("skin.async_frames_keep_camera_light_material_hud_and_pose_from_one_submission") {
  Fixture f;
  Assets::MaterialDescription material;
  material.baseColor = {.2F, .4F, .8F, 1};
  REQUIRE(Assets::saveMaterial(f.files, "assets/physical.dotmaterial", material));
  REQUIRE(f.scene.setMaterial(f.owner, World::MaterialBinding{"assets/physical.dotmaterial"}));
  const auto light = f.scene.createEntity("Light").value();
  REQUIRE(f.scene.setLight(light, World::Light{.intensity = 4}));
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  REQUIRE(prepareInitial(service, f.scene) == ScenePreparationStage::Ready);
  REQUIRE(f.scene.setTransform(f.joint, {{.5F, 0, 0}}));
  const auto binding = Animation::SceneSkin::bind(f.scene, f.owner, f.skin).value();
  const auto reference = deformMesh(f.geometry, f.skin, binding.palette(f.scene).value()).value();
  REQUIRE(Assets::saveMesh(f.files, "assets/reference.dotmesh", reference));
  auto original = World::decodeScene(World::encodeScene(f.scene).value()).value();
  const auto cloneOwner = original.findEntity(f.owner.value).value();
  REQUIRE(original.setSkin(cloneOwner, std::nullopt));
  REQUIRE(original.setMeshRenderer(
      cloneOwner, World::MeshRenderer{World::MeshKind::Asset, "assets/reference.dotmesh"}));
  auto independent = Renderer::create().value();
  auto referenceService = SceneRenderer::create(independent, f.files).value();
  TextureOptions options;
  options.colorSpace = ColorSpace::Linear;
  options.filter = TextureFilter::Nearest;
  options.mipmaps = false;
  auto red = renderer.uploadTexture({1, 1, {255, 0, 0, 128}}, options).value();
  auto referenceRed = independent.uploadTexture({1, 1, {255, 0, 0, 128}}, options).value();
  std::array screen{ScreenDraw{red, {0, 0}, {.2F, .2F}}};
  const std::array referenceScreen{ScreenDraw{referenceRed, {0, 0}, {.2F, .2F}}};
  const auto expected
      = referenceService.render(original, 64, 64, {0, 0, 0, 1}, {}, referenceScreen).value();
  auto target = renderer.createTarget(64, 64).value();
  REQUIRE(renderer.renderMeshesInto(target, {}, {}, {0, 1, 0, 1}));
  const auto before = renderer.readTarget(target).value();
  auto submitted = service.renderIntoAsync(f.scene, target, 1, 1, {0, 0, 0, 1}, {}, screen);
  REQUIRE(submitted);
  CHECK(submitted->accepted);
  CHECK_FALSE(submitted->completed);
  CHECK(renderer.readTarget(target)->pixels == before.pixels);
  REQUIRE(f.scene.setTransform(f.joint, {{1, 0, 0}}));
  const auto camera = f.scene.primaryCamera().value();
  REQUIRE(f.scene.setTransform(camera, {{-.5F, 0, 5}}));
  REQUIRE(f.scene.setLight(light, World::Light{.color = {1, .1F, .1F}, .intensity = 1}));
  auto blue = renderer.uploadTexture({1, 1, {0, 0, 255, 255}}, options).value();
  screen[0].texture = blue;
  auto post = renderer.postProcess().value();
  post.exposureEv = 1;
  REQUIRE(renderer.setPostProcess(post));
  std::uint64_t tag = 1;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  std::optional<SceneFrameReceipt> completed;

  while (!completed && std::chrono::steady_clock::now() < deadline) {
    auto result = service.renderIntoAsync(f.scene, target, ++tag, 1, {0, 0, 0, 1}, {}, screen);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE_FALSE(result->error);
    completed = result->completed;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  REQUIRE(completed);
  CHECK(completed->tag == 1);
  CHECK(completed->scene == f.scene.id());
  CHECK(renderer.readTarget(target)->pixels == expected.pixels);
  CHECK(renderer.postProcess()->exposureEv == 1);
  CHECK(service.pickAsync(completed->serial, {.625F, .5F}).value() == f.owner);
  CHECK_FALSE(service.pickAsync(completed->serial + 1, {.625F, .5F}));
  CHECK_FALSE(service.pickAsync(completed->serial, {.875F, .5F}).value());
  const auto advanced = untilFrame(service, f.scene, target, tag);
  CHECK(advanced.tag > 1);
  CHECK(renderer.readTarget(target)->pixels != expected.pixels);
}

TEST_CASE("skin.async_frames_cancel_epoch_target_and_cache_without_late_pixels") {
  Fixture f;
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  REQUIRE(prepareInitial(service, f.scene) == ScenePreparationStage::Ready);
  auto first = renderer.createTarget(32, 32).value();
  auto second = renderer.createTarget(32, 32).value();
  CHECK(first.info()->identity != second.info()->identity);
  auto copied = first;
  CHECK(copied.info()->identity == first.info()->identity);
  REQUIRE(renderer.renderMeshesInto(first, {}, {}, {1, 0, 0, 1}));
  REQUIRE(renderer.renderMeshesInto(second, {}, {}, {0, 1, 0, 1}));
  REQUIRE(f.scene.setTransform(f.joint, {{.5F, 0, 0}}));
  REQUIRE(service.renderIntoAsync(f.scene, first, 1, 1)->accepted);
  const auto before = renderer.readTarget(first).value();
  const auto secondBefore = renderer.readTarget(second).value();
  auto epochOnly = service.renderIntoAsync(f.scene, first, 2, 2);
  REQUIRE(epochOnly);
  CHECK(epochOnly->accepted);
  CHECK_FALSE(epochOnly->completed);
  CHECK(service.asyncStats()->cancelled == 1);
  auto replacement = service.renderIntoAsync(f.scene, second, 3, 2);
  REQUIRE(replacement);
  CHECK(replacement->accepted);
  CHECK_FALSE(replacement->completed);
  CHECK(renderer.readTarget(first)->pixels == before.pixels);
  CHECK(renderer.readTarget(second)->pixels == secondBefore.pixels);
  CHECK(service.asyncStats()->cancelled == 2);
  REQUIRE(service.clearCache());
  CHECK_FALSE(service.asyncStats()->pending);
  CHECK_FALSE(service.asyncStats()->picking);
  CHECK(renderer.resourceStats()->skins == 0);
  CHECK(renderer.readTarget(second)->pixels == secondBefore.pixels);
  std::uint64_t tag = 3;
  auto receipt = untilFrame(service, f.scene, second, tag, 3);
  CHECK(receipt.scene == f.scene.id());
  CHECK(renderer.readTarget(second)->pixels != secondBefore.pixels);
  REQUIRE(service.cancelAsync());
  CHECK_FALSE(service.pickAsync(receipt.serial, {.5F, .5F}));
  CHECK(renderer.readTarget(first)->pixels == before.pixels);
}

TEST_CASE("skin.async_invalid_input_does_not_consume_pending_frame_or_change_target") {
  Fixture f;
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  REQUIRE(prepareInitial(service, f.scene) == ScenePreparationStage::Ready);
  auto target = renderer.createTarget(32, 32).value();
  REQUIRE(f.scene.setTransform(f.joint, {{.5F, 0, 0}}));
  REQUIRE(service.renderIntoAsync(f.scene, target, 1, 1)->accepted);
  const auto state = service.asyncStats().value();
  const auto count = target.info()->frames;
  CHECK_FALSE(service.renderIntoAsync(f.scene, target, 1, 1));
  CHECK_FALSE(service.renderIntoAsync(f.scene, target, 2, 0));
  CHECK_FALSE(service.renderIntoAsync(f.scene, target, 2, 1, {-1, 0, 0, 1}));
  auto foreign = Renderer::create().value();
  auto wrongTarget = foreign.createTarget(32, 32).value();
  CHECK_FALSE(service.renderIntoAsync(f.scene, wrongTarget, 2, 1));
  TextureOptions options;
  options.colorSpace = ColorSpace::Linear;
  auto wrong = foreign.uploadTexture({1, 1, {255, 0, 0, 255}}, options).value();
  const std::array layers{ScreenDraw{wrong, {0, 0}, {1, 1}}};
  CHECK_FALSE(service.renderIntoAsync(f.scene, target, 2, 1, {0, 0, 0, 1}, {}, layers));
  CHECK(service.asyncStats()->pending == state.pending);
  CHECK(target.info()->frames == count);
  bool rejected{};
  std::thread worker([&] {
    rejected = !service.renderIntoAsync(f.scene, target, 2, 1) && !service.cancelAsync();
  });
  worker.join();
  CHECK(rejected);
  REQUIRE(renderer.enableGui());
  REQUIRE(renderer.renderGui(
      [&]() -> Core::Result<void> {
        REQUIRE(service.asyncStats());
        CHECK_FALSE(service.renderIntoAsync(f.scene, target, 2, 1));
        CHECK_FALSE(service.cancelAsync());
        CHECK(service.asyncStats()->pending == state.pending);
        return {};
      },
      8, 8));
  REQUIRE(renderer.closeGui());
  std::uint64_t tag = 1;
  REQUIRE(untilFrame(service, f.scene, target, tag).tag == 1);
}

TEST_CASE("skin.async_completed_receipt_survives_failure_to_capture_the_next_frame") {
  Fixture f;
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  REQUIRE(prepareInitial(service, f.scene) == ScenePreparationStage::Ready);
  auto target = renderer.createTarget(32, 32).value();
  REQUIRE(f.scene.setTransform(f.joint, {{.5F, 0, 0}}));
  REQUIRE(service.renderIntoAsync(f.scene, target, 1, 1)->accepted);
  const auto camera = f.scene.primaryCamera().value();
  REQUIRE(f.scene.setCamera(camera, std::nullopt));
  std::optional<SceneAsyncResult> committed;
  std::uint64_t tag = 1;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (!committed && std::chrono::steady_clock::now() < deadline) {
    auto result = service.renderIntoAsync(f.scene, target, ++tag, 1);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));

    if (result->completed) {
      committed = std::move(*result);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  REQUIRE(committed);
  CHECK(committed->completed->tag == 1);
  REQUIRE(committed->error);
  CHECK_FALSE(committed->accepted);
  CHECK(target.info()->frames == 1);
  CHECK(service.asyncStats()->latest->tag == 1);
  CHECK_FALSE(service.asyncStats()->picking);
  World::Camera properties;
  properties.primary = true;
  properties.projection = World::Projection::Orthographic;
  properties.orthographicHeight = 4;
  REQUIRE(f.scene.setCamera(camera, properties));
  auto recovered = untilFrame(service, f.scene, target, tag);
  CHECK(recovered.tag > 1);
  CHECK_FALSE(service.asyncStats()->error);
}

TEST_CASE("skin.async_unchanged_pose_reuses_frozen_identity_for_shadow_pages") {
  Fixture f;
  const auto light = f.scene.createEntity("Shadow").value();
  REQUIRE(f.scene.setLight(
      light,
      World::Light{.shadows = ShadowSettings{.resolution = 128, .cascades = 1, .distance = 12}}));
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  REQUIRE(prepareInitial(service, f.scene) == ScenePreparationStage::Ready);
  auto target = renderer.createTarget(32, 32).value();
  std::uint64_t tag{};
  REQUIRE(untilFrame(service, f.scene, target, tag).serial == 1);
  CHECK(renderer.visibilityStats()->mesh->shadowPagesUpdated == 1);
  REQUIRE(untilFrame(service, f.scene, target, tag).serial == 2);
  CHECK(renderer.visibilityStats()->mesh->shadowPagesReused == 1);
  CHECK(renderer.visibilityStats()->mesh->shadowDraws == 0);
}

TEST_CASE("skin.async_invalid_deformation_preserves_pixels_and_recovers_without_old_frame_leases") {
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
  REQUIRE(prepareInitial(service, f.scene) == ScenePreparationStage::Ready);
  auto target = renderer.createTarget(32, 32).value();
  std::uint64_t tag{};
  REQUIRE(untilFrame(service, f.scene, target, tag).serial == 1);
  const auto before = renderer.readTarget(target).value();
  REQUIRE(f.scene.setTransform(tip, {{}, {}, {-1, 1, 1}}));
  REQUIRE(service.renderIntoAsync(f.scene, target, ++tag, 1)->accepted);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  bool failed{};

  while (!failed && std::chrono::steady_clock::now() < deadline) {
    auto result = service.renderIntoAsync(f.scene, target, ++tag, 1);
    failed = !result;

    if (!result) {
      CHECK(result.error().code == Core::ErrorCode::InvalidArgument);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  REQUIRE(failed);
  CHECK(renderer.readTarget(target)->pixels == before.pixels);
  CHECK(target.info()->frames == 1);
  CHECK_FALSE(service.asyncStats()->picking);
  REQUIRE(f.scene.setTransform(tip, {{1, 0, 0}}));
  REQUIRE(untilFrame(service, f.scene, target, tag).serial == 2);
  CHECK(renderer.readTarget(target)->pixels != before.pixels);
  REQUIRE(service.clearCache());
  CHECK(renderer.resourceStats()->skins == 0);
}

TEST_CASE("skin.scene_geometry_preparation_shares_cpu_skin_and_publishes_one_controller_per_poll") {
  Fixture f;
  const auto other = f.scene.createEntity("Second mesh").value();
  REQUIRE(f.scene.setMeshRenderer(other, f.scene.entity(f.owner)->mesh));
  REQUIRE(f.scene.setSkin(other, f.properties));
  auto renderer = Renderer::create().value();
  auto service = SceneRenderer::create(renderer, f.files).value();
  auto stage = service.prepare(f.scene);
  bool removed{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  while (stage && *stage != ScenePreparationStage::Ready
         && std::chrono::steady_clock::now() < deadline) {
    const auto count = service.stats()->skins;

    if (count == 1 && !removed) {
      REQUIRE(std::filesystem::remove(f.files.root() / f.properties.asset));
      removed = true;
    }

    stage = service.prepare(f.scene);
    CHECK(service.stats()->skins <= count + 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  REQUIRE(stage);
  REQUIRE(*stage == ScenePreparationStage::Ready);
  CHECK(removed);
  CHECK(service.stats()->meshes == 1);
  CHECK(service.stats()->skins == 2);
  REQUIRE(service.render(f.scene, 32, 32));
  REQUIRE(service.clearCache());
  CHECK(renderer.resourceStats()->skins == 0);
  CHECK(renderer.resourceStats()->meshes == 0);
}
