#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>

import Dot.Graphics.Geometry;
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
