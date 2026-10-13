#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>

import Dot.Graphics.Geometry;
import Dot.Core.Result;
import Dot.Math;

using namespace Dot::Graphics;

TEST_CASE("geometry.cube_bounds_and_outward_faces") {
  auto mesh = cube({2, 4, 6});
  REQUIRE(mesh);
  REQUIRE(validateMesh(*mesh));
  CHECK(mesh->vertices.size() == 8);
  CHECK(mesh->indices.size() == 36);
  auto bounds = meshBounds(*mesh);
  REQUIRE(bounds);
  CHECK(bounds->minimum == Dot::Math::Vec3{-1, -2, -3});
  CHECK(bounds->maximum == Dot::Math::Vec3{1, 2, 3});

  for (std::size_t face = 0; face < mesh->indices.size(); face += 3) {
    const auto a = mesh->vertices[mesh->indices[face]].position;
    const auto b = mesh->vertices[mesh->indices[face + 1]].position;
    const auto c = mesh->vertices[mesh->indices[face + 2]].position;
    const auto first = b - a;
    const auto second = c - a;
    const Dot::Math::Vec3 normal{first.y * second.z - first.z * second.y,
                                 first.z * second.x - first.x * second.z,
                                 first.x * second.y - first.y * second.x};
    CHECK(Dot::Math::dot(normal, a) > 0);
  }
}

TEST_CASE("geometry.quad_bounds_and_positive_z_winding") {
  auto mesh = quad({2, 4});
  REQUIRE(mesh);
  REQUIRE(validateMesh(*mesh));
  CHECK(mesh->vertices.size() == 4);
  CHECK(mesh->indices.size() == 6);
  auto bounds = meshBounds(*mesh);
  REQUIRE(bounds);
  CHECK(bounds->minimum == Dot::Math::Vec3{-1, -2, 0});
  CHECK(bounds->maximum == Dot::Math::Vec3{1, 2, 0});

  for (std::size_t face = 0; face < mesh->indices.size(); face += 3) {
    const auto a = mesh->vertices[mesh->indices[face]].position;
    const auto b = mesh->vertices[mesh->indices[face + 1]].position;
    const auto c = mesh->vertices[mesh->indices[face + 2]].position;
    CHECK((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) > 0);
  }
}

TEST_CASE("geometry.nonindexed_mesh_and_translated_bounds") {
  MeshData mesh;
  mesh.vertices = {{{2, 3, 4}}, {{5, 1, 6}}, {{0, 7, 2}}};
  REQUIRE(validateMesh(mesh));
  auto bounds = meshBounds(mesh);
  REQUIRE(bounds);
  CHECK(bounds->minimum == Dot::Math::Vec3{0, 1, 2});
  CHECK(bounds->maximum == Dot::Math::Vec3{5, 7, 6});
}

TEST_CASE("geometry.reject_bad_topology_and_indices") {
  CHECK_FALSE(validateMesh({}));
  CHECK_FALSE(meshBounds({}));
  auto mesh = quad();
  REQUIRE(mesh);
  mesh->indices.pop_back();
  CHECK_FALSE(validateMesh(*mesh));
  mesh->indices = {0, 1, 4};
  CHECK_FALSE(validateMesh(*mesh));
  mesh->indices = {0, 1, std::numeric_limits<std::uint32_t>::max()};
  CHECK_FALSE(validateMesh(*mesh));
  mesh->indices.clear();
  CHECK_FALSE(validateMesh(*mesh));
}

TEST_CASE("geometry.reject_nonfinite_vertices_and_invalid_colors") {
  auto mesh = cube();
  REQUIRE(mesh);
  mesh->vertices[0].position.y = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(validateMesh(*mesh));
  CHECK_FALSE(meshBounds(*mesh));
  mesh->vertices[0].position.y = 0;

  for (float value : {-0.1F, 1.1F, std::numeric_limits<float>::infinity()}) {
    mesh->vertices[0].color.w = value;
    CHECK_FALSE(validateMesh(*mesh));
  }

  mesh->vertices[0].color.w = 0;
  CHECK(validateMesh(*mesh));
}

TEST_CASE("geometry.reject_resource_limits") {
  MeshData mesh;
  mesh.vertices.resize(1'000'001);
  CHECK_FALSE(validateMesh(mesh));
  mesh.vertices.resize(3);
  mesh.indices.resize(3'000'001);
  CHECK_FALSE(validateMesh(mesh));
}

TEST_CASE("geometry.primitive_dimensions_validate_extremes") {
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto tiny = std::numeric_limits<float>::denorm_min();
  CHECK_FALSE(cube({0, 1, 1}));
  CHECK_FALSE(cube({1, -1, 1}));
  CHECK_FALSE(cube({1, 1, infinity}));
  CHECK_FALSE(cube({tiny, 1, 1}));
  CHECK_FALSE(quad({0, 1}));
  CHECK_FALSE(quad({1, -1}));
  CHECK_FALSE(quad({1, infinity}));
  CHECK_FALSE(quad({tiny, 1}));
  auto large = cube({std::numeric_limits<float>::max(), 1, 1});
  REQUIRE(large);
  CHECK(validateMesh(*large));
}

TEST_CASE("geometry.flat_normals_preserve_winding_surface_attributes_and_source") {
  auto source = cube({2, 4, 6}).value();
  source.vertices[0].texCoord = {-2, 3};
  source.vertices[0].color = {0.2F, 0.4F, 0.6F, 1};
  auto result = withFlatNormals(source);
  REQUIRE(result);
  CHECK(result->indices.empty());
  REQUIRE(result->vertices.size() == 36);
  CHECK(validateMesh(*result));

  for (std::size_t face = 0; face < result->vertices.size(); face += 3) {
    const auto normal = result->vertices[face].normal;
    CHECK(Dot::Math::length(normal) == doctest::Approx(1));
    CHECK(Dot::Math::dot(normal, result->vertices[face].position) > 0);
    CHECK(result->vertices[face + 1].normal == normal);
    CHECK(result->vertices[face + 2].normal == normal);
  }

  CHECK(result->vertices[0].texCoord == source.vertices[0].texCoord);
  CHECK(result->vertices[0].color == source.vertices[0].color);
  CHECK(source.vertices[0].normal == Dot::Math::Vec3{});
  CHECK(source.vertices.size() == 8);
}

TEST_CASE("geometry.flat_normals_handle_extreme_scales_and_reject_degenerate_expansion") {
  for (const float scale :
       {std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max()}) {
    MeshData source;
    source.vertices = {{{0, 0, 0}}, {{scale, 0, 0}}, {{0, scale, 0}}};
    auto result = withFlatNormals(source);
    REQUIRE(result);
    CHECK(result->vertices[0].normal == Dot::Math::Vec3{0, 0, 1});
  }

  MeshData degenerate;
  degenerate.vertices = {{{0, 0, 0}}, {{1, 0, 0}}, {{2, 0, 0}}};
  CHECK_FALSE(withFlatNormals(degenerate));
  CHECK(degenerate.vertices[0].normal == Dot::Math::Vec3{});
  auto over = quad().value();
  over.indices.resize(1'000'002, 0);
  auto rejected = withFlatNormals(over);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(over.vertices.size() == 4);
}

TEST_CASE("geometry.surface_attributes_require_finite_uvs_and_unit_or_absent_normals") {
  auto mesh = quad().value();
  mesh.vertices[0].texCoord = {-2, 3};
  mesh.vertices[0].normal = {0, 0, 1};
  CHECK(validateMesh(mesh));
  mesh.vertices[0].normal = {0, 0, 2};
  CHECK_FALSE(validateMesh(mesh));
  mesh.vertices[0].normal = {0, 0, std::numeric_limits<float>::denorm_min()};
  CHECK_FALSE(validateMesh(mesh));
  mesh.vertices[0].normal = {0, 0, std::numeric_limits<float>::infinity()};
  CHECK_FALSE(validateMesh(mesh));
  mesh.vertices[0].normal = {};
  mesh.vertices[0].texCoord.x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(validateMesh(mesh));
}

TEST_CASE("geometry.uv_sphere_has_radial_normals_seams_and_outward_nondegenerate_faces") {
  auto mesh = sphere(2, 16, 8);
  REQUIRE(mesh);
  REQUIRE(mesh->vertices.size() == 17 * 9);
  CHECK(mesh->indices.size() == 16 * 7 * 6);
  CHECK(validateMesh(*mesh));

  for (const auto& vertex : mesh->vertices) {
    CHECK(Dot::Math::length(vertex.position) == doctest::Approx(2));
    CHECK(Dot::Math::length(vertex.normal) == doctest::Approx(1));
    CHECK(vertex.texCoord.x >= 0);
    CHECK(vertex.texCoord.x <= 1);
    CHECK(vertex.texCoord.y >= 0);
    CHECK(vertex.texCoord.y <= 1);
  }

  for (std::size_t row = 0; row <= 8; ++row) {
    CHECK(mesh->vertices[row * 17].position == mesh->vertices[row * 17 + 16].position);
    CHECK(mesh->vertices[row * 17].texCoord.x == 0);
    CHECK(mesh->vertices[row * 17 + 16].texCoord.x == 1);
  }

  auto flat = withFlatNormals(*mesh);
  REQUIRE(flat);

  for (std::size_t index = 0; index < flat->vertices.size(); index += 3) {
    CHECK(Dot::Math::dot(flat->vertices[index].normal, flat->vertices[index].position) > 0);
  }
}

TEST_CASE("geometry.sphere_rejects_bad_tessellation_radius_and_collapsed_precision") {
  CHECK(sphere(1, 3, 2));
  CHECK(sphere(1, 256, 128));
  CHECK_FALSE(sphere(0));
  CHECK_FALSE(sphere(-1));
  CHECK_FALSE(sphere(std::numeric_limits<float>::infinity()));
  CHECK_FALSE(sphere(std::numeric_limits<float>::denorm_min()));
  CHECK_FALSE(sphere(1, 2));
  CHECK_FALSE(sphere(1, 257));
  CHECK_FALSE(sphere(1, 8, 1));
  CHECK_FALSE(sphere(1, 8, 129));
}
