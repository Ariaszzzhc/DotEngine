#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>

import Dot.Core.Result;
import Dot.Graphics.Renderer;
import Dot.Graphics.Skinning;
import Dot.Math;

namespace {
  using namespace Dot;
  using namespace Dot::Graphics;

  auto skinFor(const MeshData& mesh, std::size_t joints = 2) -> Animation::Skin {
    Animation::SkinDescription description;
    description.inverseBindMatrices.resize(joints);
    description.vertices.resize(mesh.vertices.size());
    return Animation::Skin::create(std::move(description)).value();
  }

  auto same(const std::vector<Vertex>& first, const std::vector<Vertex>& second) -> bool {
    return first.size() == second.size()
           && std::memcmp(first.data(), second.data(), first.size() * sizeof(Vertex)) == 0;
  }

  auto changed(Renderer& renderer, const SkinnedMesh& skin, std::span<const Math::Mat4> palette)
      -> void {
    const std::array updates{SkinUpdate{&skin, palette}};
    const auto result = renderer.updateSkins(updates);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  }
}  // namespace

TEST_CASE(
    "skin.gpu_eight_weights_match_reference_positions_normal_tangent_bounds_and_physical_pixels") {
  auto renderer = Renderer::create().value();
  Lighting lighting;
  lighting.viewPosition = {0, 0, 4};
  lighting.lights = {{.direction = {.2F, -.4F, -1}, .intensity = 3}};
  REQUIRE(renderer.setLighting(lighting));
  const auto camera = Math::multiply(Math::orthographic(-3, 3, -3, 3, .1F, 12).value(),
                                     Math::lookAt({0, 0, 4}, {}).value())
                          .value();
  Material material;
  material.model = SurfaceModel::MetallicRoughness;
  material.doubleSided = true;
  material.normalTexture = renderer
                               .uploadTexture(Image{1, 1, {100, 150, 244, 255}},
                                              TextureOptions{.colorSpace = ColorSpace::Linear})
                               .value();

  for (const bool indexed : {true, false}) {
    auto mesh = quad().value();

    if (!indexed) {
      std::vector<Vertex> vertices;

      for (auto index : mesh.indices) {
        vertices.push_back(mesh.vertices[index]);
      }

      mesh.vertices = std::move(vertices);
      mesh.indices.clear();
    }

    for (auto& vertex : mesh.vertices) {
      vertex.normal = Math::normalized(Math::Vec3{1, 0, 1}).value();
      vertex.tangent = {0, 1, 0, 1};
    }

    Animation::SkinDescription description;
    description.inverseBindMatrices.resize(8);
    description.vertices.resize(mesh.vertices.size());

    for (auto& influence : description.vertices) {
      for (std::uint32_t i = 0; i < 8; ++i) {
        influence.joints[i] = i;
        influence.weights[i] = .125F;
      }
    }

    auto skin = Animation::Skin::create(std::move(description)).value();
    auto original = renderer.upload(mesh).value();
    auto controller = renderer.createSkin(original, skin).value();
    auto view = controller.mesh().value();
    const auto initialBytes = renderer.resourceStats()->skinBytes;

    for (const float sign : {1.F, -1.F}) {
      std::array<Math::Mat4, 8> palette;

      for (std::size_t i = 0; i < palette.size(); ++i) {
        palette[i]
            = Math::compose({static_cast<float>(i) * .125F, -.25F, 0},
                            Math::rotationEulerDegrees({0, 25, 0}).value(), {sign * 1.5F, .75F, 2})
                  .value();
      }

      changed(renderer, controller, palette);
      auto reference = deformMesh(mesh, skin, palette).value();
      auto actual = renderer.readSkin(controller).value();
      REQUIRE(actual.size() == reference.vertices.size());

      for (std::size_t i = 0; i < actual.size(); ++i) {
        CHECK(Math::length(actual[i].position - reference.vertices[i].position) < .00001);
        CHECK(Math::length(actual[i].normal - reference.vertices[i].normal) < .00001);
        CHECK(Math::length(Math::Vec3{actual[i].tangent.x - reference.vertices[i].tangent.x,
                                      actual[i].tangent.y - reference.vertices[i].tangent.y,
                                      actual[i].tangent.z - reference.vertices[i].tangent.z})
              < .00001);
        CHECK(actual[i].tangent.w == reference.vertices[i].tangent.w);
        CHECK(actual[i].texCoord == reference.vertices[i].texCoord);
        CHECK(actual[i].color == reference.vertices[i].color);
      }

      auto bounds = meshBounds(reference).value();
      CHECK(Math::length(controller.info()->bounds.minimum - bounds.minimum) < .00001);
      CHECK(Math::length(controller.info()->bounds.maximum - bounds.maximum) < .00001);
      auto visibleReference = reference;

      if (sign < 0) {
        if (visibleReference.indices.empty()) {
          for (std::size_t i = 0; i < visibleReference.vertices.size(); i += 3) {
            std::swap(visibleReference.vertices[i + 1], visibleReference.vertices[i + 2]);
          }
        } else {
          for (std::size_t i = 0; i < visibleReference.indices.size(); i += 3) {
            std::swap(visibleReference.indices[i + 1], visibleReference.indices[i + 2]);
          }
        }
      }

      auto uploaded = renderer.upload(visibleReference).value();
      const std::array expected{MeshDraw{&uploaded, {}, {1, 1, 1, 1}, material}};
      const std::array posed{MeshDraw{&view, {}, {1, 1, 1, 1}, material}};
      auto first = renderer.renderMeshes(expected, camera, 96, 96).value();
      auto second = renderer.renderMeshes(posed, camera, 96, 96).value();
      unsigned difference{};

      for (std::size_t i = 0; i < first.pixels.size(); ++i) {
        difference += static_cast<unsigned>(std::abs(int(first.pixels[i]) - int(second.pixels[i])));
      }

      CHECK(difference < 100);
      CHECK(renderer.resourceStats()->skinBytes == initialBytes);
    }
  }
}

TEST_CASE("skin.gpu_batch_failure_preserves_every_prior_buffer_bounds_and_update_counter") {
  auto renderer = Renderer::create().value();
  const auto mesh = quad().value();
  auto source = renderer.upload(mesh).value();
  auto first = renderer.createSkin(source, skinFor(mesh)).value();
  Animation::SkinDescription description;
  description.inverseBindMatrices.resize(2);
  description.vertices.resize(4);

  for (auto& influence : description.vertices) {
    influence.joints[1] = 1;
    influence.weights = {.5F, .5F};
  }

  auto second = renderer.createSkin(source, Animation::Skin::create(description).value()).value();
  std::array<Math::Mat4, 2> initial{};
  changed(renderer, first, initial);
  changed(renderer, second, initial);
  const auto beforeFirst = renderer.readSkin(first).value();
  const auto beforeSecond = renderer.readSkin(second).value();
  const auto info = first.info().value();
  const std::array moved{Math::compose({2, 0, 0}).value(), Math::Mat4{}};
  const std::array singular{Math::Mat4{}, Math::compose({}, {}, {-1, 1, 1}).value()};
  const std::array updates{SkinUpdate{&first, moved}, SkinUpdate{&second, singular}};
  CHECK_FALSE(renderer.updateSkins(updates));
  CHECK(same(renderer.readSkin(first).value(), beforeFirst));
  CHECK(same(renderer.readSkin(second).value(), beforeSecond));
  CHECK(first.info()->bounds.minimum == info.bounds.minimum);
  CHECK(first.info()->updates == info.updates);
  CHECK(second.info()->updates == 1);
  changed(renderer, first, moved);
  CHECK(first.info()->bounds.minimum.x == 1.5F);
  CHECK(first.info()->updates == 2);
}

TEST_CASE("skin.gpu_updated_bounds_drive_culling_and_picking_and_recover_normal_map_frames") {
  auto renderer = Renderer::create().value();
  auto data = quad().value();
  auto source = renderer.upload(data).value();
  Animation::SkinDescription description;
  description.inverseBindMatrices.resize(4);
  description.vertices.resize(4);

  for (std::uint32_t i = 0; i < 4; ++i) {
    description.vertices[i].joints[0] = i;
  }

  auto skin = renderer.createSkin(source, Animation::Skin::create(description).value()).value();
  auto view = skin.mesh().value();
  std::array<Math::Mat4, 4> palette;

  for (auto& pose : palette) {
    pose = Math::compose({100, 0, .5F}).value();
  }

  changed(renderer, skin, palette);
  const std::array draws{MeshDraw{&view}};
  REQUIRE(renderer.renderMeshes(draws, {}, 16, 16));
  CHECK(renderer.visibilityStats()->mesh->culledInstances == 1);
  CHECK_FALSE(renderer.pickMeshes(draws, {}, 16, 16, {.5F, .5F}).value());

  for (auto& pose : palette) {
    pose = Math::compose({.5F, 0, .5F}).value();
  }

  changed(renderer, skin, palette);
  CHECK(renderer.pickMeshes(draws, {}, 16, 16, {.9F, .5F}).value() == 0);

  for (std::size_t i = 0; i < palette.size(); ++i) {
    palette[i] = Math::compose(data.vertices[i].position * -1.F).value();
  }

  changed(renderer, skin, palette);
  Material material;
  material.model = SurfaceModel::MetallicRoughness;
  material.normalTexture = renderer
                               .uploadTexture(Image{1, 1, {128, 128, 255, 255}},
                                              TextureOptions{.colorSpace = ColorSpace::Linear})
                               .value();
  const std::array physical{MeshDraw{&view, {}, {1, 1, 1, 1}, material}};
  CHECK_FALSE(renderer.renderMeshes(physical, {}, 16, 16));
  palette = {};
  changed(renderer, skin, palette);
  REQUIRE(renderer.renderMeshes(physical, {}, 16, 16));
  CHECK(same(renderer.readSkin(skin).value(), data.vertices));
}

TEST_CASE("skin.gpu_shared_views_lifetime_budgets_foreign_handles_threads_and_moves") {
  SkinnedMesh survivor;

  {
    RendererConfig config;
    config.maxSkins = 1;
    auto renderer = Renderer::create(nullptr, config).value();
    auto source = renderer.upload(quad().value()).value();
    auto skin = skinFor(quad().value());
    auto controller = renderer.createSkin(source, skin).value();
    auto view = controller.mesh().value();
    const auto stats = renderer.resourceStats().value();
    CHECK(stats.skins == 1);
    CHECK(stats.meshes == 2);
    CHECK(stats.skinBytes == controller.info()->bytes);
    CHECK_FALSE(renderer.createSkin(source, skin));
    CHECK_FALSE(renderer.createSkin(view, skin));
    CHECK_FALSE(renderer.createSkin(Mesh{}, skin));
    CHECK_FALSE(renderer.createSkin(source, Animation::Skin{}));
    std::array<Math::Mat4, 2> palette{};
    const std::array updates{SkinUpdate{&controller, palette}};
    bool rejected{};
    std::thread worker([&] {
      rejected = !controller.info() && !controller.mesh() && !renderer.readSkin(controller)
                 && !renderer.updateSkins(updates);
    });
    worker.join();
    CHECK(rejected);
    const std::array repeated{updates[0], updates[0]};
    CHECK_FALSE(renderer.updateSkins(repeated));
    std::vector<SkinUpdate> tooMany(129, updates[0]);
    CHECK_FALSE(renderer.updateSkins(tooMany));
    auto moved = std::move(controller);
    CHECK_FALSE(controller.info());
    CHECK_FALSE(controller.mesh());
    CHECK_FALSE(renderer.readSkin(controller));
    auto other = Renderer::create().value();
    CHECK_FALSE(other.readSkin(moved));
    const std::array foreign{SkinUpdate{&moved, palette}};
    CHECK_FALSE(other.updateSkins(foreign));
    auto alias = moved;
    source = {};
    moved = {};
    CHECK(renderer.resourceStats()->skins == 1);
    alias = {};
    CHECK(renderer.resourceStats()->skins == 0);
    CHECK(renderer.resourceStats()->skinBytes == 0);
    CHECK(renderer.resourceStats()->meshes == 2);
    view = {};
    CHECK(renderer.resourceStats()->meshes == 0);
    CHECK(renderer.resourceStats()->vertexBytes == 0);
    CHECK(renderer.resourceStats()->indexBytes == 0);
    auto fresh = renderer.upload(quad().value()).value();
    survivor = renderer.createSkin(fresh, skin).value();
  }

  CHECK_FALSE(survivor.info());
  CHECK_FALSE(survivor.mesh());
  RendererConfig config;
  config.maxSkinBytes = 1;
  auto tiny = Renderer::create(nullptr, config).value();
  auto source = tiny.upload(quad().value()).value();
  CHECK_FALSE(tiny.createSkin(source, skinFor(quad().value())));
  CHECK(tiny.resourceStats()->skins == 0);
  config.maxSkins = 0;
  CHECK_FALSE(Renderer::create(nullptr, config));
}

TEST_CASE("skin.gpu_shared_deformation_matches_shadow_and_ssao_reference_passes") {
  auto renderer = Renderer::create().value();
  const auto mesh = cube().value();
  Animation::SkinDescription description;
  description.inverseBindMatrices.resize(2);
  description.vertices.resize(mesh.vertices.size());

  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    description.vertices[i].joints[0] = mesh.vertices[i].position.y > 0 ? 1U : 0U;
  }

  auto skin = Animation::Skin::create(description).value();
  auto original = renderer.upload(mesh).value();
  auto controller = renderer.createSkin(original, skin).value();
  auto view = controller.mesh().value();
  auto ground = renderer.upload(cube({4, .2F, 4}).value()).value();
  Lighting lighting;
  lighting.viewPosition = {3, 3, 5};
  lighting.lights = {{.direction = {1, -2, -1},
                      .intensity = 4,
                      .shadows = ShadowSettings{.resolution = 128, .cascades = 1, .distance = 12}}};
  REQUIRE(renderer.setLighting(lighting));
  REQUIRE(renderer.setOcclusion(OcclusionSettings{}));
  const auto camera = Math::multiply(Math::orthographic(-3, 3, -3, 3, .1F, 12).value(),
                                     Math::lookAt(lighting.viewPosition, {}).value())
                          .value();
  Material material;
  material.model = SurfaceModel::MetallicRoughness;
  std::array draws{MeshDraw{&view, {}, {1, 1, 1, 1}, material},
                   MeshDraw{&ground, Math::compose({0, -.7F, 0}).value(), {1, 1, 1, 1}, material}};
  const auto initial = renderer.renderMeshes(draws, camera, 96, 96).value();
  const std::array palette{Math::Mat4{}, Math::compose({.75F, 0, .25F}).value()};
  changed(renderer, controller, palette);
  const auto actual = renderer.renderMeshes(draws, camera, 96, 96).value();
  const auto guides = renderer.readOcclusion().value();
  auto reference = renderer.upload(deformMesh(mesh, skin, palette).value()).value();
  draws[0].mesh = &reference;
  const auto expected = renderer.renderMeshes(draws, camera, 96, 96).value();
  CHECK(actual.pixels == expected.pixels);
  CHECK(guides.pixels == renderer.readOcclusion()->pixels);
  CHECK(actual.pixels != initial.pixels);
  CHECK(renderer.visibilityStats()->mesh->shadowInstances > 0);
  CHECK(renderer.visibilityStats()->mesh->occlusionInstances == 2);
}

TEST_CASE("skin.gpu_rejects_float_overflow_and_reflected_tangent_seams_and_obeys_exact_budget") {
  auto renderer = Renderer::create().value();
  auto geometry = quad().value();

  for (auto& vertex : geometry.vertices) {
    vertex.tangent = {1, 0, 0, 1};
  }

  Animation::SkinDescription description;
  description.inverseBindMatrices.resize(2);
  description.vertices.resize(4);
  description.vertices[0].joints[0] = 1;
  auto source = renderer.upload(geometry).value();
  auto skin = renderer.createSkin(source, Animation::Skin::create(description).value()).value();
  const std::array seam{Math::Mat4{}, Math::compose({}, {}, {-1, 1, 1}).value()};
  const std::array seamUpdate{SkinUpdate{&skin, seam}};
  CHECK_FALSE(renderer.updateSkins(seamUpdate));
  CHECK(skin.info()->updates == 0);
  CHECK(same(renderer.readSkin(skin).value(), geometry.vertices));
  geometry.vertices[0].position.x = 1e35F;
  auto excessive = renderer.upload(geometry).value();
  auto overflow = renderer.createSkin(excessive, skinFor(geometry)).value();
  const std::array large{Math::compose({}, {}, {1e8F, 1e8F, 1e8F}).value(), Math::Mat4{}};
  const std::array overflowing{SkinUpdate{&overflow, large}};
  CHECK_FALSE(renderer.updateSkins(overflowing));
  CHECK(overflow.info()->updates == 0);
  CHECK(same(renderer.readSkin(overflow).value(), geometry.vertices));
  RendererConfig exact;
  exact.maxSkinBytes = 688;
  auto bounded = Renderer::create(nullptr, exact).value();
  auto quadSource = bounded.upload(quad().value()).value();
  auto fit = bounded.createSkin(quadSource, skinFor(quad().value()));
  REQUIRE(fit);
  CHECK(fit->info()->bytes == exact.maxSkinBytes);
  CHECK_FALSE(bounded.createSkin(quadSource, skinFor(quad().value())));
  CHECK(bounded.resourceStats()->skinBytes == exact.maxSkinBytes);
}

TEST_CASE(
    "skin.gpu_mixed_disconnected_orientations_fail_atomically_and_uniform_reflection_recovers") {
  const auto original = quad().value();
  MeshData mesh;

  for (const auto index : original.indices) {
    mesh.vertices.push_back(original.vertices[index]);
  }

  Animation::SkinDescription description;
  description.inverseBindMatrices.resize(2);
  description.vertices.resize(mesh.vertices.size());

  for (std::size_t i = 3; i < description.vertices.size(); ++i) {
    description.vertices[i].joints[0] = 1;
  }

  auto renderer = Renderer::create().value();
  auto source = renderer.upload(mesh).value();
  auto controller
      = renderer.createSkin(source, Animation::Skin::create(std::move(description)).value())
            .value();
  const std::array identity{Math::Mat4{}, Math::Mat4{}};
  changed(renderer, controller, identity);
  const auto before = renderer.readSkin(controller).value();
  const auto updates = controller.info()->updates;
  const auto reflected = Math::compose({}, {}, {-1, 1, 1}).value();
  const std::array mixed{Math::Mat4{}, reflected};
  const std::array invalid{SkinUpdate{&controller, mixed}};
  CHECK_FALSE(renderer.updateSkins(invalid));
  CHECK(controller.info()->updates == updates);
  CHECK(same(renderer.readSkin(controller).value(), before));
  const std::array uniform{reflected, reflected};
  changed(renderer, controller, uniform);
  CHECK(controller.info()->updates == updates + 1);
}
