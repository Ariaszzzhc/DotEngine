#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import Dot.Assets.Mesh;
import Dot.Core.Result;
import Dot.Graphics.Geometry;
import Dot.Math;

namespace {
  using namespace Dot::Assets;
  constexpr auto triangleObj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";

  auto decodeText(std::string_view text, MeshFormat format) -> Dot::Core::Result<MeshAsset> {
    return decodeMesh(std::as_bytes(std::span(text.data(), text.size())), format);
  }

  auto word(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) -> void {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      bytes[offset++] = static_cast<std::byte>((value >> shift) & 255U);
    }
  }

  auto scalar(std::vector<std::byte>& bytes, std::size_t component, float value) -> void {
    word(bytes, 16 + component * 4, std::bit_cast<std::uint32_t>(value));
  }
}  // namespace

TEST_CASE("assets.mesh_binary_roundtrip_preserves_all_attributes_and_absent_frames") {
  auto mesh = Dot::Graphics::cube({2, 3, 4}).value();
  mesh.vertices[2].color = {0.2F, 0.4F, 0.6F, 0.8F};
  mesh.vertices[2].texCoord = {-2.5F, 3};
  const auto encoded = encodeMesh(mesh).value();
  auto decoded = decodeMesh(encoded, MeshFormat::DotMesh);
  REQUIRE(decoded);
  CHECK(decoded->warnings.empty());
  CHECK(decoded->data.indices == mesh.indices);
  REQUIRE(decoded->data.vertices.size() == mesh.vertices.size());

  for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
    const auto& actual = decoded->data.vertices[index];
    const auto& expected = mesh.vertices[index];
    CHECK(actual.position == expected.position);
    CHECK(actual.color == expected.color);
    CHECK(actual.normal == expected.normal);
    CHECK(actual.texCoord == expected.texCoord);
    CHECK(actual.tangent == expected.tangent);
  }

  auto surface = Dot::Graphics::quad().value();

  for (auto& vertex : surface.vertices) {
    vertex.tangent = {1, 0, 0, -1};
  }

  auto authored = decodeMesh(encodeMesh(surface).value(), MeshFormat::DotMesh);
  REQUIRE(authored);
  CHECK(authored->data.vertices[0].tangent == Dot::Math::Vec4{1, 0, 0, -1});
}

TEST_CASE("assets.mesh_binary_has_fixed_little_endian_header_and_float_words") {
  auto mesh = Dot::Graphics::quad().value();
  mesh.vertices[0].position = {1, -2, 0.5F};
  mesh.indices = {2, 1, 0, 3, 2, 0};
  const auto encoded = encodeMesh(mesh).value();
  const std::array<unsigned, 16> header{'D', 'O', 'T', 'M', 'E', 'S', 'H', 0,
                                        4,   0,   0,   0,   6,   0,   0,   0};
  REQUIRE(encoded.size() == 16 + 4 * 64 + 6 * 4);

  for (std::size_t index = 0; index < header.size(); ++index) {
    CHECK(std::to_integer<unsigned>(encoded[index]) == header[index]);
  }

  const std::array<unsigned, 12> position{0, 0, 128, 63, 0, 0, 0, 192, 0, 0, 0, 63};

  for (std::size_t index = 0; index < position.size(); ++index) {
    CHECK(std::to_integer<unsigned>(encoded[16 + index]) == position[index]);
  }

  CHECK(encoded[16 + 4 * 64] == std::byte{2});
  CHECK(encoded[16 + 4 * 64 + 4] == std::byte{1});
}

TEST_CASE("assets.mesh_binary_rejects_truncation_trailing_data_magic_and_impossible_counts") {
  const auto good = encodeMesh(Dot::Graphics::quad().value()).value();

  for (std::size_t length = 0; length < good.size(); ++length) {
    CHECK_FALSE(decodeMesh(std::span(good).first(length), MeshFormat::DotMesh));
  }

  auto invalid = good;
  invalid.push_back(std::byte{0});
  CHECK_FALSE(decodeMesh(invalid, MeshFormat::DotMesh));
  invalid = good;
  invalid[7] = std::byte{1};
  CHECK_FALSE(decodeMesh(invalid, MeshFormat::DotMesh));

  for (const auto count : {0U, 1'000'001U, std::numeric_limits<std::uint32_t>::max()}) {
    invalid = good;
    word(invalid, 8, count);
    auto result = decodeMesh(invalid, MeshFormat::DotMesh);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
  }

  invalid = good;
  word(invalid, 12, 3'000'001);
  CHECK_FALSE(decodeMesh(invalid, MeshFormat::DotMesh));
  invalid = good;
  word(invalid, 8, 999'999);
  word(invalid, 12, 3'000'000);
  CHECK_FALSE(decodeMesh(invalid, MeshFormat::DotMesh));
  CHECK_FALSE(decodeText(R"({"vertices":[],"indices":[]})", MeshFormat::DotMesh));
  CHECK_FALSE(decodeText(triangleObj, MeshFormat::DotMesh));
}

TEST_CASE("assets.mesh_binary_validates_nonfinite_values_color_frames_and_topology") {
  const auto good = encodeMesh(Dot::Graphics::quad().value()).value();

  for (std::size_t component = 0; component < 16; ++component) {
    for (auto value :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
      auto invalid = good;
      scalar(invalid, component, value);
      CHECK_FALSE(decodeMesh(invalid, MeshFormat::DotMesh));
    }
  }

  for (const auto [component, value] :
       {std::pair<std::size_t, float>{3, -0.1F}, {6, 2}, {9, 2}, {15, 2}}) {
    auto invalid = good;
    scalar(invalid, component, value);
    CHECK_FALSE(decodeMesh(invalid, MeshFormat::DotMesh));
  }

  for (auto index : {4U, std::numeric_limits<std::uint32_t>::max()}) {
    auto invalid = good;
    word(invalid, 16 + 4 * 64, index);
    CHECK_FALSE(decodeMesh(invalid, MeshFormat::DotMesh));
  }

  auto invalid = good;
  word(invalid, 12, 0);
  invalid.resize(16 + 4 * 64);
  CHECK_FALSE(decodeMesh(invalid, MeshFormat::DotMesh));
  CHECK_FALSE(encodeMesh({}));
}

TEST_CASE("assets.mesh_binary_retains_nonindexed_triangles_and_owned_unaligned_input") {
  const auto indexed = Dot::Graphics::quad().value();
  Dot::Graphics::MeshData mesh;

  for (auto index : indexed.indices) {
    mesh.vertices.push_back(indexed.vertices[index]);
  }

  auto encoded = encodeMesh(mesh).value();
  encoded.insert(encoded.begin(), std::byte{99});
  auto result = decodeMesh(std::span(encoded).subspan(1), MeshFormat::DotMesh);
  REQUIRE(result);
  CHECK(result->data.indices.empty());
  CHECK(result->data.vertices.size() == 6);
  const auto expected = result->data.vertices[0].position;
  std::ranges::fill(encoded, std::byte{0});
  CHECK(result->data.vertices[0].position == expected);
  CHECK(encodeMesh(result->data)->size() == 16 + 6 * 64);
}

TEST_CASE("assets.mesh_binary_preserves_extreme_finite_and_signed_zero_float_bits") {
  auto mesh = Dot::Graphics::quad().value();
  mesh.vertices[0].position
      = {std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min(), -0.0F};
  auto encoded = encodeMesh(mesh).value();
  auto decoded = decodeMesh(encoded, MeshFormat::DotMesh);
  REQUIRE(decoded);
  const auto position = decoded->data.vertices[0].position;
  CHECK(position.x == mesh.vertices[0].position.x);
  CHECK(position.y == mesh.vertices[0].position.y);
  CHECK(std::bit_cast<std::uint32_t>(position.z) == 0x80000000U);
}

TEST_CASE("assets.obj_splits_hard_normal_and_uv_seams_without_losing_authored_values") {
  const auto source
      = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt -2 3\nvt 1 1\n"
        "vn 0 0 2\nvn 0 2 0\nf 1/1/1 2/1/1 3/1/1\nf 1/2/2 3/1/2 2/1/2\n";
  auto result = decodeText(source, MeshFormat::Obj);
  REQUIRE(result);
  REQUIRE(result->data.vertices.size() == 6);
  CHECK(result->warnings.empty());
  CHECK(result->data.vertices[0].position == result->data.vertices[3].position);
  CHECK(result->data.vertices[0].normal == Dot::Math::Vec3{0, 0, 1});
  CHECK(result->data.vertices[3].normal == Dot::Math::Vec3{0, 1, 0});
  CHECK(result->data.vertices[0].texCoord == Dot::Math::Vec2{-2, 3});
  CHECK(result->data.vertices[3].texCoord == Dot::Math::Vec2{1, 1});
  auto zero
      = decodeText("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 0\nf 1//1 2//1 3//1\n", MeshFormat::Obj);
  CHECK_FALSE(zero);
  CHECK_FALSE(decodeText("vt 0 0 2\n" + std::string(triangleObj), MeshFormat::Obj));
  CHECK_FALSE(decodeText("v 0 0 0\nv 1 0 0\nv 0 1 0\n", MeshFormat::Obj));
}

TEST_CASE("assets.obj_triangulates_quads_and_resolves_negative_indices") {
  auto mesh = decodeText("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf -4 -3 -2 -1\n", MeshFormat::Obj);
  REQUIRE_MESSAGE(mesh, (mesh ? "" : mesh.error().message));
  CHECK(mesh->data.vertices.size() == 4);
  CHECK(mesh->data.indices.size() == 6);
  CHECK(mesh->warnings.empty());
  CHECK(Dot::Graphics::validateMesh(mesh->data));
}

TEST_CASE("assets.obj_vertex_colors_and_optional_weight") {
  auto mesh
      = decodeText("v +0 0 0 1 0 0\nv 1 0 0 0 1 0\nv 0 1 0 0 0 1\nf 1 2 3\n", MeshFormat::Obj);
  REQUIRE(mesh);
  CHECK(mesh->data.vertices[0].color.x == 1);
  CHECK(mesh->data.vertices[1].color.y == 1);
  CHECK(mesh->data.vertices[2].color.z == 1);
  CHECK(decodeText("v 0 0 0 1\nv 1 0 0 1\nv 0 1 0 1\nf 1 2 3\n", MeshFormat::Obj));
  CHECK_FALSE(decodeText("v 0 0 0 2\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", MeshFormat::Obj));
}

TEST_CASE("assets.obj_surface_attributes_are_retained_and_bad_references_fail") {
  const std::string prefix = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvn 0 0 1\n";
  auto mesh = decodeText(prefix + "f 1/1/1 2/1/1 3/1/1\n", MeshFormat::Obj);
  REQUIRE(mesh);
  CHECK(mesh->warnings.empty());
  CHECK(mesh->data.vertices[0].normal == Dot::Math::Vec3{0, 0, 1});
  CHECK(mesh->data.vertices[0].texCoord == Dot::Math::Vec2{0, 0});
  CHECK(decodeText(prefix + "f 1//1 2//1 3//1\n", MeshFormat::Obj));
  CHECK_FALSE(decodeText(prefix + "f 1/2/1 2/1/1 3/1/1\n", MeshFormat::Obj));
  CHECK_FALSE(decodeText(prefix + "f 1/1/2 2/1/1 3/1/1\n", MeshFormat::Obj));
  CHECK_FALSE(decodeText(prefix + "f 1//-2 2//1 3//1\n", MeshFormat::Obj));
  auto grouped = decodeText("o triangle\ns 1\n" + std::string(triangleObj), MeshFormat::Obj);
  REQUIRE(grouped);
  CHECK_FALSE(grouped->warnings.empty());
  CHECK_FALSE(decodeText("s garbage\n" + std::string(triangleObj), MeshFormat::Obj));
}

TEST_CASE("assets.obj_rejects_numeric_fallbacks_and_malformed_faces") {
  for (const auto source :
       {"v nope 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", "v 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n",
        "v 1e100 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1x 2 3\n",
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 2 3\n", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 4\n",
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/ 2/ 3/\n", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2\n"}) {
    CHECK_FALSE(decodeText(source, MeshFormat::Obj));
  }
}

TEST_CASE("assets.obj_rejects_materials_and_nontriangle_primitives") {
  for (const auto suffix :
       {"mtllib outside.mtl\n", "usemtl material\n", "l 1 2\n", "p 1\n", "curv 0 1 1 2 3\n"}) {
    auto mesh = decodeText(std::string(triangleObj) + suffix, MeshFormat::Obj);
    REQUIRE_FALSE(mesh);
    CHECK(mesh.error().code == Dot::Core::ErrorCode::Unavailable);
  }
}

TEST_CASE("assets.source_size_nul_and_format_boundaries") {
  CHECK_FALSE(decodeText("", MeshFormat::Obj));
  CHECK_FALSE(decodeText(std::string(64 * 1024 * 1024 + 1, ' '), MeshFormat::Obj));
  CHECK_FALSE(decodeText(std::string(triangleObj) + '\0', MeshFormat::Obj));
  CHECK_FALSE(decodeText(triangleObj, static_cast<MeshFormat>(999)));
  CHECK_FALSE(decodeText("v nan 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", MeshFormat::Obj));
  CHECK_FALSE(decodeText(std::string(1024 * 1024 + 1, ' ') + '\n' + triangleObj, MeshFormat::Obj));
  std::string hugeFace = std::string(triangleObj) + "f";

  for (int index = 0; index < 65; ++index) {
    hugeFace += " 1";
  }

  CHECK_FALSE(decodeText(hugeFace, MeshFormat::Obj));
}
