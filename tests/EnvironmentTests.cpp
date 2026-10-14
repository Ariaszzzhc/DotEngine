#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <vector>

import Dot.Graphics.Environment;
import Dot.Graphics.Image;
import Dot.Math;

TEST_CASE("environment.cpu_contract_validates_opaque_panorama_dimensions_work_and_bytes") {
  Dot::Graphics::HdrImage image{8, 4, std::vector<float>(8 * 4 * 4, 1)};
  Dot::Graphics::EnvironmentConfig config{16, 4, 16, 16, 32};
  auto info = Dot::Graphics::validateEnvironment(image, config).value();
  CHECK(info.bytes == 35552);
  CHECK(info.radianceLevels == 5);
  CHECK(info.specularLevels == 5);
  image.pixels[0] = 65504;
  CHECK(Dot::Graphics::validateEnvironment(image, config));
  image.pixels[0] = 65505;
  CHECK_FALSE(Dot::Graphics::validateEnvironment(image, config));
  image.pixels[0] = 1;
  image.pixels[3] = 0.5F;
  CHECK_FALSE(Dot::Graphics::validateEnvironment(image, config));
  image.pixels[3] = 1;
  config.samples = 1025;
  CHECK_FALSE(Dot::Graphics::validateEnvironment(image, config));
  config.samples = 32;
  config.specularSize = 17;
  CHECK_FALSE(Dot::Graphics::validateEnvironment(image, config));
  image.width = 4;
  image.pixels.resize(4 * 4 * 4);
  CHECK_FALSE(Dot::Graphics::validateEnvironment(image));
}

TEST_CASE("environment.cpu_face_axes_edges_and_invalid_coordinates_are_explicit") {
  using Dot::Graphics::environmentDirection;
  const std::array<Dot::Math::Vec3, 6> axes{
      {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};

  for (std::uint32_t face = 0; face < 6; ++face) {
    auto direction = environmentDirection(face, {0.5F, 0.5F}).value();
    CHECK(direction.x == axes[face].x);
    CHECK(direction.y == axes[face].y);
    CHECK(direction.z == axes[face].z);
  }

  CHECK(environmentDirection(0, {0, 0.5F})->z > 0);
  CHECK(environmentDirection(4, {1, 0.5F})->x > 0);
  CHECK_FALSE(environmentDirection(6, {0.5F, 0.5F}));
  CHECK_FALSE(environmentDirection(0, {-0.1F, 0.5F}));
  CHECK_FALSE(environmentDirection(0, {std::numeric_limits<float>::quiet_NaN(), 0}));
}
