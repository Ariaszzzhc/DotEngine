#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Result;
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

TEST_CASE("environment.cpu_prepared_source_owns_weighted_mips_and_shared_immutable_levels") {
  using namespace Dot::Graphics;
  HdrImage image{8, 4, std::vector<float>(128, 1)};
  const std::array<float, 4> rows{1, 2, 4, 8};

  for (std::size_t y = 0; y < 4; ++y) {
    for (std::size_t x = 0; x < 8; ++x) {
      image.pixels[(y * 8 + x) * 4] = rows[y];
    }
  }

  auto source = EnvironmentSource::prepare(std::move(image)).value();
  auto levels = source.levels().value();
  REQUIRE(levels.size() == 4);
  CHECK(levels[1].width == 4);
  CHECK(levels[1].height == 2);
  CHECK(levels[1].pixels[0] == doctest::Approx(1 + 1 / std::sqrt(2.0)));
  CHECK(levels[1].pixels[16] == doctest::Approx(8 - 4 / std::sqrt(2.0)));
  CHECK(levels.back().width == 1);
  CHECK(levels.back().height == 1);
  CHECK(levels.back().pixels[0] == doctest::Approx(4.5 - 1.5 / std::sqrt(2.0)));
  CHECK(levels.back().pixels[3] == 1);
  auto shared = source;
  CHECK(shared.levels()->data() == levels.data());
  auto moved = std::move(source);
  CHECK_FALSE(source.levels());
  CHECK_FALSE(source.description({}));
  CHECK(moved.levels()->data() == shared.levels()->data());
  bool allowed{};
  std::thread worker(
      [&] { allowed = shared.levels().has_value() && shared.description({}).has_value(); });
  worker.join();
  CHECK(allowed);
}

TEST_CASE("environment.cpu_prepared_source_rejects_invalid_input_config_and_cancellation") {
  using namespace Dot::Graphics;
  CHECK_FALSE(EnvironmentSource::prepare({}));
  CHECK_FALSE(EnvironmentSource::prepare({2, 2, std::vector<float>(16, 1)}));
  HdrImage image{4, 2, std::vector<float>(32, 1)};
  image.pixels[0] = 65505;
  CHECK_FALSE(EnvironmentSource::prepare(image));
  image.pixels[0] = 1;
  image.pixels[3] = .5F;
  CHECK_FALSE(EnvironmentSource::prepare(image));
  image.pixels[3] = 1;
  std::stop_source stop;
  stop.request_stop();
  auto canceled = EnvironmentSource::prepare(image, stop.get_token());
  REQUIRE_FALSE(canceled);
  CHECK(canceled.error().code == Dot::Core::ErrorCode::Unavailable);
  auto source = EnvironmentSource::prepare(std::move(image)).value();
  CHECK_FALSE(source.description({8, 4, 16, 16, 32}));
}
