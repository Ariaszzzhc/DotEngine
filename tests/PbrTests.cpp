#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <thread>
#include <utility>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Math;
import Dot.Core.Result;

using namespace Dot::Graphics;

namespace {
  auto physicalQuad(Dot::Math::Vec3 normal = {0, 0, 1}) -> MeshData {
    auto data = quad({2, 2}).value();
    const std::array<Dot::Math::Vec2, 4> uv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};

    for (std::size_t index = 0; index < data.vertices.size(); ++index) {
      data.vertices[index].normal = normal;
      data.vertices[index].texCoord = uv[index];
    }

    return data;
  }

  auto renderer() -> Renderer {
    RendererConfig config;
    config.lighting.viewPosition = {0, 0, 2};
    config.lighting.lights = {{.direction = {0, 0, -1}, .intensity = std::numbers::pi_v<float>}};
    return Renderer::create(nullptr, config).value();
  }

  auto material() -> Material {
    Material value;
    value.model = SurfaceModel::MetallicRoughness;
    value.baseColor = {0.5F, 0.5F, 0.5F, 1};
    value.roughness = 1;
    return value;
  }

  auto sample(Renderer& device, const Mesh& mesh, const Material& value,
              Dot::Math::Mat4 transform = {}) -> std::array<float, 4> {
    auto target = device.createTarget(1, 1).value();
    const std::array draws{MeshDraw{&mesh, transform, {1, 1, 1, 1}, value}};
    auto rendered = device.renderMeshesInto(target, draws, {});
    REQUIRE_MESSAGE(rendered, (rendered ? "" : rendered.error().message));
    auto hdr = device.readHdrTarget(target).value();
    return {hdr.pixels[0], hdr.pixels[1], hdr.pixels[2], hdr.pixels[3]};
  }
}  // namespace

TEST_CASE("pbr.frontal_lambert_smith_ggx_reference_distinguishes_dielectric_and_metal") {
  auto device = renderer();
  auto mesh = device.upload(physicalQuad()).value();
  auto surface = material();
  const auto dielectric = sample(device, mesh, surface);
  CHECK(dielectric[0] == doctest::Approx(0.49).epsilon(0.002));
  surface.metallic = 1;
  const auto metal = sample(device, mesh, surface);
  CHECK(metal[0] == doctest::Approx(0.125).epsilon(0.002));
  CHECK(metal[0] < dielectric[0]);
  surface.roughness = 0.2F;
  const auto polished = sample(device, mesh, surface);
  CHECK(polished[0] > metal[0] * 100);
  CHECK(polished[0] > 1);
  CHECK(polished[3] == 1);
}

TEST_CASE("pbr.directional_point_and_spot_lights_use_world_space_attenuation_and_cones") {
  auto device = renderer();
  auto mesh = device.upload(physicalQuad()).value();
  const auto surface = material();
  Lighting frame;
  frame.viewPosition = {0, 0, 2};
  frame.lights = {{.kind = LightKind::Point, .position = {0, 0, 2}, .range = 100}};
  REQUIRE(device.setLighting(frame));
  const auto near = sample(device, mesh, surface)[0];
  frame.lights[0].position.z = 4;
  REQUIRE(device.setLighting(frame));
  const auto far = sample(device, mesh, surface)[0];
  CHECK(near / far == doctest::Approx(4).epsilon(0.005));
  frame.lights[0].range = 4;
  REQUIRE(device.setLighting(frame));
  CHECK(sample(device, mesh, surface)[0] == 0);
  frame.lights[0] = {.kind = LightKind::Spot, .direction = {0, 0, -1}, .position = {0, 0, 2}};
  REQUIRE(device.setLighting(frame));
  CHECK(sample(device, mesh, surface)[0] > 0);
  frame.lights[0].direction = {1, 0, 0};
  REQUIRE(device.setLighting(frame));
  CHECK(sample(device, mesh, surface)[0] == 0);
  frame.lights[0] = {.direction = {0, 0, 1}};
  REQUIRE(device.setLighting(frame));
  CHECK(sample(device, mesh, surface)[0] == 0);
  frame.lights.clear();
  REQUIRE(device.setLighting(frame));
  CHECK(sample(device, mesh, surface)[0] == 0);
  auto glowing = surface;
  glowing.emissive = {2, 1, 0};
  CHECK(sample(device, mesh, glowing) == std::array<float, 4>{2, 1, 0, 1});
}

TEST_CASE("pbr.texture_channels_normal_frames_and_emission_affect_actual_radiance") {
  auto device = renderer();
  auto mesh = device.upload(physicalQuad()).value();
  auto surface = material();
  surface.metallic = 1;
  auto packed
      = device.uploadTexture(Image{1, 1, {23, 255, 0, 255}}, {.colorSpace = ColorSpace::Linear})
            .value();
  surface.metallicRoughnessTexture = packed;
  CHECK(sample(device, mesh, surface)[0] == doctest::Approx(0.49).epsilon(0.002));
  auto metallic
      = device.uploadTexture(Image{1, 1, {23, 255, 255, 255}}, {.colorSpace = ColorSpace::Linear})
            .value();
  surface.metallicRoughnessTexture = metallic;
  CHECK(sample(device, mesh, surface)[0] == doctest::Approx(0.125).epsilon(0.002));
  surface.metallic = 0;
  surface.metallicRoughnessTexture.reset();
  surface.normalTexture
      = device.uploadTexture(Image{1, 1, {218, 128, 218, 255}}, {.colorSpace = ColorSpace::Linear})
            .value();
  const auto tilted = sample(device, mesh, surface)[0];
  CHECK(tilted < 0.4F);
  CHECK(tilted > 0.25F);
  surface.normalScale = 0;
  CHECK(sample(device, mesh, surface)[0] == doctest::Approx(0.49).epsilon(0.002));
  surface.normalTexture.reset();
  Lighting dark;
  REQUIRE(device.setLighting(dark));
  surface.emissive = {8, 4, 2};
  surface.emissiveTexture = device.uploadTexture(Image{1, 1, {128, 0, 255, 255}}).value();
  const auto emission = sample(device, mesh, surface);
  CHECK(emission[0] == doctest::Approx(8 * 0.21586).epsilon(0.002));
  CHECK(emission[1] == 0);
  CHECK(emission[2] == 2);
}

TEST_CASE("pbr.inverse_transpose_normals_and_reflected_winding_preserve_surface_contract") {
  auto device = renderer();
  const auto authored = Dot::Math::normalized(Dot::Math::Vec3{1, 0, 1}).value();
  auto mesh = device.upload(physicalQuad(authored)).value();
  const auto surface = material();
  auto transformed = sample(device, mesh, surface, Dot::Math::compose({}, {}, {2, 1, 3}).value());
  const auto corrected = Dot::Math::normalized(Dot::Math::Vec3{0.5F, 0, 1.0F / 3}).value();
  auto reference = device.upload(physicalQuad(corrected)).value();
  CHECK(transformed[0] == doctest::Approx(sample(device, reference, surface)[0]).epsilon(0.002));
  auto front = device.upload(physicalQuad()).value();
  const auto original = sample(device, front, surface);
  const auto mirrored
      = sample(device, front, surface, Dot::Math::compose({}, {}, {-1, 1, 1}).value());
  CHECK(original == mirrored);
  auto backData = physicalQuad();

  for (std::size_t triangle = 0; triangle < backData.indices.size(); triangle += 3) {
    std::swap(backData.indices[triangle], backData.indices[triangle + 1]);
  }

  auto back = device.upload(backData).value();
  CHECK(sample(device, back, surface)[0] == 0);
  auto twoSided = surface;
  twoSided.doubleSided = true;
  CHECK(sample(device, back, twoSided)[0] == doctest::Approx(original[0]).epsilon(0.002));
}

TEST_CASE("pbr.invalid_material_geometry_textures_and_transforms_retain_last_frame") {
  auto device = renderer();
  auto mesh = device.upload(physicalQuad()).value();
  auto missing = device.upload(quad({2, 2}).value()).value();
  auto target = device.createTarget(1, 1).value();
  std::array draws{MeshDraw{&mesh, {}, {1, 1, 1, 1}, material()}};
  REQUIRE(device.renderMeshesInto(target, draws, {}));
  const auto before = device.readHdrTarget(target).value();
  draws[0].mesh = &missing;
  CHECK_FALSE(device.renderMeshesInto(target, draws, {}));
  draws[0].mesh = &mesh;
  draws[0].material.metallic = 2;
  CHECK_FALSE(device.renderMeshesInto(target, draws, {}));
  draws[0].material = material();
  draws[0].material.roughness = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(device.renderMeshesInto(target, draws, {}));
  draws[0].material = material();
  draws[0].material.normalScale = -1;
  CHECK_FALSE(device.renderMeshesInto(target, draws, {}));
  draws[0].material = material();
  draws[0].material.normalTexture = device.uploadTexture(Image{1, 1, {128, 128, 255, 255}}).value();
  CHECK_FALSE(device.renderMeshesInto(target, draws, {}));
  draws[0].material = material();
  draws[0].material.normalTexture
      = device.uploadTexture(Image{1, 1, {128, 128, 0, 255}}, {.colorSpace = ColorSpace::Linear})
            .value();
  CHECK_FALSE(device.renderMeshesInto(target, draws, {}));
  draws[0].material = material();
  draws[0].transform.data[0] = 0;
  CHECK_FALSE(device.renderMeshesInto(target, draws, {}));
  draws[0].transform = {};
  draws[0].transform.data[3] = 0.1F;
  CHECK_FALSE(device.renderMeshesInto(target, draws, {}));
  CHECK(device.readHdrTarget(target)->pixels == before.pixels);
  CHECK(target.info()->frames == 1);
  draws[0].transform = {};
  REQUIRE(device.renderMeshesInto(target, draws, {}));
}

TEST_CASE("pbr.lighting_is_owned_transactional_and_thread_bound") {
  auto device = renderer();
  const auto before = device.lighting().value();
  Lighting invalid = before;
  invalid.lights[0].intensity = -1;
  CHECK_FALSE(device.setLighting(invalid));
  CHECK(device.lighting()->lights[0].intensity == before.lights[0].intensity);
  Lighting frame = before;
  frame.lights[0].intensity = 4;
  REQUIRE(device.setLighting(frame));
  frame.lights.clear();
  CHECK(device.lighting()->lights.size() == 1);
  bool rejected{};
  std::jthread worker([&] { rejected = !device.setLighting({}) && !device.lighting(); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(device);
  CHECK_FALSE(device.lighting());
  CHECK_FALSE(device.setLighting({}));
  CHECK(moved.lighting()->lights[0].intensity == 4);
}

TEST_CASE("pbr.mirrored_uvs_preserve_normal_map_handedness_and_invalid_uvs_are_rejected") {
  auto device = renderer();
  Lighting frame;
  frame.viewPosition = {0, 0, 2};
  frame.lights = {{.direction = {-1, 0, -1}, .intensity = 3}};
  REQUIRE(device.setLighting(frame));
  auto data = physicalQuad();
  auto original = device.upload(data).value();

  for (auto& vertex : data.vertices) {
    vertex.texCoord.x = 1 - vertex.texCoord.x;
  }

  auto mirrored = device.upload(data).value();
  auto surface = material();
  surface.normalTexture
      = device.uploadTexture(Image{1, 1, {218, 128, 218, 255}}, {.colorSpace = ColorSpace::Linear})
            .value();
  CHECK(sample(device, original, surface)[0] > 0.25F);
  CHECK(sample(device, mirrored, surface)[0] < 0.005F);

  for (auto& vertex : data.vertices) {
    vertex.texCoord = {};
  }

  auto degenerate = device.upload(data).value();
  auto target = device.createTarget(1, 1).value();
  const std::array draws{MeshDraw{&degenerate, {}, {1, 1, 1, 1}, surface}};
  CHECK_FALSE(device.renderMeshesInto(target, draws, {}));
  CHECK(target.info()->frames == 0);
}

TEST_CASE("pbr.full_light_array_keeps_last_entry_and_accumulates_independent_colors") {
  auto device = renderer();
  auto mesh = device.upload(physicalQuad()).value();
  Lighting frame;
  frame.viewPosition = {0, 0, 2};
  frame.lights.assign(64, Light{.direction = {0, 0, -1}, .intensity = 0});
  frame.lights.back().color = {0, 0, 1};
  frame.lights.back().intensity = std::numbers::pi_v<float>;
  REQUIRE(device.setLighting(frame));
  auto result = sample(device, mesh, material());
  CHECK(result[0] == 0);
  CHECK(result[1] == 0);
  CHECK(result[2] == doctest::Approx(0.49).epsilon(0.002));
  frame.lights.front().color = {1, 0, 0};
  frame.lights.front().intensity = std::numbers::pi_v<float>;
  REQUIRE(device.setLighting(frame));
  result = sample(device, mesh, material());
  CHECK(result[0] == doctest::Approx(0.49).epsilon(0.002));
  CHECK(result[1] == 0);
  CHECK(result[2] == doctest::Approx(0.49).epsilon(0.002));
}

TEST_CASE("pbr.orthographic_views_use_parallel_rays_instead_of_position_dependent_highlights") {
  auto device = renderer();
  auto mesh = device.upload(physicalQuad()).value();
  auto value = material();
  value.roughness = 0.2F;
  auto target = device.createTarget(3, 1).value();
  const std::array draws{MeshDraw{&mesh, {}, {1, 1, 1, 1}, value}};
  REQUIRE(device.renderMeshesInto(target, draws, {}));
  auto perspective = device.readHdrTarget(target).value();
  CHECK(perspective.pixels[4] > perspective.pixels[0] * 2);
  auto frame = device.lighting().value();
  frame.viewDirection = Dot::Math::Vec3{0, 0, -1};
  REQUIRE(device.setLighting(frame));
  REQUIRE(device.renderMeshesInto(target, draws, {}));
  auto orthographic = device.readHdrTarget(target).value();
  CHECK(orthographic.pixels[0] == orthographic.pixels[4]);
  CHECK(orthographic.pixels[4] == orthographic.pixels[8]);
  CHECK(orthographic.pixels[4] > 1);
}
