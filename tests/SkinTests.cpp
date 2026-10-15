#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

import Dot.Animation.Skin;
import Dot.Core.Result;
import Dot.Graphics.Geometry;
import Dot.Graphics.Skinning;
import Dot.Math;

namespace {
  using namespace Dot;

  auto description(std::size_t vertices = 4) -> Animation::SkinDescription {
    return {std::vector<Math::Mat4>(2), std::vector<Animation::SkinInfluence>(vertices)};
  }
}  // namespace

TEST_CASE("skin.immutable_eight_weights_normalize_and_retain_owned_bind_data") {
  auto source = description();
  source.inverseBindMatrices.resize(8);
  auto& first = source.vertices[0];

  for (std::uint32_t i = 0; i < 8; ++i) {
    first.joints[i] = i;
    first.weights[i] = .1F;
  }

  auto skin = Animation::Skin::create(source).value();
  source.vertices[0].weights[0] = 1;
  CHECK(skin.description().value()->vertices[0].weights[0] == .125F);
  auto copy = skin;
  auto moved = std::move(skin);
  CHECK_FALSE(skin.description());
  CHECK(copy.description().value() == moved.description().value());
  bool read{};
  std::thread worker([&] { read = copy.description().value()->vertices.size() == 4; });
  worker.join();
  CHECK(read);
  CHECK_FALSE(Animation::Skin{}.description());
}

TEST_CASE("skin.rejects_invalid_weights_indices_matrices_and_capacity_boundaries") {
  auto source = description();
  source.vertices[0].weights = {};
  CHECK_FALSE(Animation::Skin::create(source));
  source = description();
  source.vertices[0].weights[1] = .5F;
  CHECK_FALSE(Animation::Skin::create(source));
  source = description();
  source.vertices[0].joints[7] = 2;
  CHECK_FALSE(Animation::Skin::create(source));

  for (const auto value : {-1.F, 1.1F, std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()}) {
    source = description();
    source.vertices[0].weights[0] = value;
    CHECK_FALSE(Animation::Skin::create(source));
  }

  source = description();
  source.inverseBindMatrices[0].data[0] = 0;
  CHECK_FALSE(Animation::Skin::create(source));
  source = description();
  source.inverseBindMatrices[0].data[3] = 1;
  CHECK_FALSE(Animation::Skin::create(source));
  source = description();
  source.inverseBindMatrices.clear();
  CHECK_FALSE(Animation::Skin::create(source));
  source = description();
  source.inverseBindMatrices.resize(1024);
  REQUIRE(Animation::Skin::create(source));
  source.inverseBindMatrices.resize(1025);
  CHECK_FALSE(Animation::Skin::create(source));
  source = description(0);
  CHECK_FALSE(Animation::Skin::create(source));
  source = description(1'000'000);
  REQUIRE(Animation::Skin::create(source));
  source.vertices.emplace_back();
  CHECK_FALSE(Animation::Skin::create(std::move(source)));
}

TEST_CASE("skin.palette_mesh_local_order_bind_identity_and_invalid_inputs_are_owned") {
  auto source = description();
  const auto mesh = Math::compose({10, 0, 0}).value();
  const auto joint = Math::compose({12, 0, 0}).value();
  source.inverseBindMatrices[0] = Math::compose({-2, 0, 0}).value();
  auto skin = Animation::Skin::create(source).value();
  std::array worlds{joint, mesh};
  auto palette = skin.palette(mesh, worlds).value();
  CHECK(palette[0] == Math::Mat4{});
  CHECK(palette[1] == Math::Mat4{});
  worlds[0] = Math::compose({14, 0, 0}).value();
  CHECK(skin.palette(mesh, worlds)->front().data[12] == 2);
  CHECK(palette[0].data[12] == 0);
  CHECK_FALSE(skin.palette(mesh, {}));
  auto projective = mesh;
  projective.data[3] = 1;
  CHECK_FALSE(skin.palette(projective, worlds));
  worlds[0].data[0] = 0;
  CHECK_FALSE(skin.palette(mesh, worlds));
  std::array<Math::Mat4, 2> excessive{};
  excessive[0].data[12] = 1e8F;
  REQUIRE(Animation::validateSkinPalette(skin, excessive));
  excessive[0].data[12] = 2e8F;
  CHECK_FALSE(Animation::validateSkinPalette(skin, excessive));
}

TEST_CASE("skin.reference_blends_vertices_and_preserves_colors_uvs_indices_and_source") {
  const auto mesh = Graphics::quad().value();
  auto source = description(mesh.vertices.size());

  for (auto& influence : source.vertices) {
    influence.joints[1] = 1;
    influence.weights = {.25F, .75F};
  }

  auto skin = Animation::Skin::create(source).value();
  std::array palette{Math::Mat4{}, Math::compose({2, 0, 0}).value()};
  auto output = Graphics::deformMesh(mesh, skin, palette).value();
  CHECK(output.indices == mesh.indices);
  CHECK(output.vertices[0].position.x == mesh.vertices[0].position.x + 1.5F);
  CHECK(output.vertices[0].color == mesh.vertices[0].color);
  CHECK(output.vertices[0].texCoord == mesh.vertices[0].texCoord);
  CHECK(output.vertices[0].normal == mesh.vertices[0].normal);
  CHECK(mesh.vertices[0].position.x == -.5F);
  CHECK_FALSE(Graphics::deformMesh(mesh, Animation::Skin{}, palette));
  CHECK_FALSE(Graphics::deformMesh(mesh, Animation::Skin::create(description(3)).value(), palette));
}

TEST_CASE("skin.reference_inverse_transpose_nonuniform_reflection_and_tangent_handedness") {
  auto mesh = Graphics::quad().value();

  for (auto& vertex : mesh.vertices) {
    vertex.normal = Math::normalized(Math::Vec3{1, 0, 1}).value();
    vertex.tangent = {0, 1, 0, 1};
  }

  auto skin = Animation::Skin::create(description(mesh.vertices.size())).value();
  const std::array palette{Math::compose({}, {}, {-2, 3, 4}).value(), Math::Mat4{}};
  auto output = Graphics::deformMesh(mesh, skin, palette).value();
  const auto expected = Math::normalized(Math::Vec3{-.5F, 0, .25F}).value();
  CHECK(output.vertices[0].normal.x == doctest::Approx(expected.x));
  CHECK(output.vertices[0].normal.z == doctest::Approx(expected.z));
  CHECK(output.vertices[0].tangent == Math::Vec4{0, 1, 0, -1});
  REQUIRE(Graphics::validateMesh(output));
}

TEST_CASE("skin.reference_rejects_blended_singularity_overflow_and_tangent_seams_without_effects") {
  auto mesh = Graphics::quad().value();
  auto source = description(mesh.vertices.size());

  for (auto& influence : source.vertices) {
    influence.joints[1] = 1;
    influence.weights = {.5F, .5F};
  }

  auto skin = Animation::Skin::create(source).value();
  std::array palette{Math::Mat4{}, Math::compose({}, {}, {-1, 1, 1}).value()};
  CHECK_FALSE(Graphics::deformMesh(mesh, skin, palette));
  CHECK(mesh.vertices[0].position.x == -.5F);
  source = description(mesh.vertices.size());
  skin = Animation::Skin::create(source).value();
  palette[0] = Math::compose({}, {}, {1e8F, 1e8F, 1e8F}).value();
  mesh.vertices[0].position.x = 1e35F;
  CHECK_FALSE(Graphics::deformMesh(mesh, skin, palette));
  mesh = Graphics::quad().value();

  for (auto& vertex : mesh.vertices) {
    vertex.tangent = {1, 0, 0, 1};
  }

  source.vertices[0].joints[0] = 1;
  skin = Animation::Skin::create(source).value();
  palette[0] = Math::Mat4{};
  CHECK_FALSE(Graphics::deformMesh(mesh, skin, palette));
  CHECK(mesh.vertices[0].tangent.w == 1);
}

TEST_CASE("skin.reference_disconnected_reflected_islands_require_separate_primitives") {
  const auto original = Graphics::quad().value();
  Graphics::MeshData mesh;

  for (const auto index : original.indices) {
    mesh.vertices.push_back(original.vertices[index]);
  }

  auto source = description(mesh.vertices.size());

  for (std::size_t i = 3; i < source.vertices.size(); ++i) {
    source.vertices[i].joints[0] = 1;
  }

  auto skin = Animation::Skin::create(std::move(source)).value();
  const auto reflected = Math::compose({}, {}, {-1, 1, 1}).value();
  const std::array mixed{Math::Mat4{}, reflected};
  CHECK_FALSE(Graphics::deformMesh(mesh, skin, mixed));
  const std::array uniform{reflected, reflected};
  REQUIRE(Graphics::deformMesh(mesh, skin, uniform));
  CHECK(mesh.vertices[0].position == original.vertices[0].position);
}
