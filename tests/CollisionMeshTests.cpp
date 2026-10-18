#include <doctest/doctest.h>

#include <limits>
#include <thread>
#include <utility>

import Dot.Physics.Mesh;
import Dot.Physics.Body;

namespace {
  auto triangle() -> Dot::Physics::MeshDescription {
    return {{{0, 0, 0}, {0, 0, 1}, {1, 0, 0}}, {0, 1, 2}};
  }
}  // namespace

TEST_CASE("collision_mesh.owned_geometry_scale_reflection_and_lifetime") {
  using namespace Dot::Physics;
  auto mesh = TriangleMesh::create(triangle()).value();
  CHECK(mesh.triangles() == 1);
  CHECK(mesh.bytes() == 48);
  CHECK(mesh.validateScale({-2, 3, .5F}));
  CHECK(mesh.view()->indices.size() == 3);
  auto copy = mesh;
  auto moved = std::move(mesh);
  CHECK_FALSE(mesh.view());
  CHECK_FALSE(mesh.validateScale({1, 1, 1}));
  CHECK(mesh.bytes() == 0);
  CHECK(copy == moved);
  bool readable{};
  std::thread reader([&] { readable = copy.view() && copy.validateScale({1, -1, 1}); });
  reader.join();
  CHECK(readable);
  BodyConfig config;
  config.shape = Shape::Mesh;
  config.motion = Motion::Static;
  CHECK_FALSE(validatedBodyConfig(config));
  config.mesh = moved;
  CHECK(validatedBodyConfig(config));
  config.motion = Motion::Dynamic;
  CHECK_FALSE(validatedBodyConfig(config));
  config.motion = Motion::Static;
  config.sensor = true;
  CHECK_FALSE(validatedBodyConfig(config));
  config.sensor = false;
  config.shape = Shape::Box;
  CHECK_FALSE(validatedBodyConfig(config));
}

TEST_CASE("collision_mesh.invalid_geometry_and_scaled_collapse_fail_without_mutating_inputs") {
  using namespace Dot::Physics;

  for (int variant = 0; variant < 7; ++variant) {
    auto input = triangle();

    switch (variant) {
      case 0:
        input.indices.clear();
        break;
      case 1:
        input.indices.push_back(0);
        break;
      case 2:
        input.indices[0] = 3;
        break;
      case 3:
        input.vertices[0].x = std::numeric_limits<float>::infinity();
        break;
      case 4:
        input.vertices[0].x = 10001;
        break;
      case 5:
        input.vertices[2] = input.vertices[1];
        break;
      case 6:
        input.vertices[2] = {0, 0, .5F};
        break;
    }

    CHECK_FALSE(TriangleMesh::create(input));
  }

  auto mesh = TriangleMesh::create(triangle()).value();
  CHECK_FALSE(mesh.validateScale({0, 1, 1}));
  CHECK_FALSE(mesh.validateScale({1, 10001, 1}));
  CHECK_FALSE(mesh.validateScale({1, std::numeric_limits<float>::quiet_NaN(), 1}));
  auto big = triangle();
  big.vertices[1].z = 10000;
  CHECK_FALSE(TriangleMesh::create(big)->validateScale({1, 1, 101}));
  auto many = triangle();
  many.vertices.resize(262145);
  CHECK_FALSE(TriangleMesh::create(many));
  CHECK(mesh.view()->vertices[2].x == 1);
}
