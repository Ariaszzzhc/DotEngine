#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <random>
#include <vector>

import Dot.Graphics.SpatialIndex;
import Dot.Math;

using namespace Dot;

TEST_CASE("spatialIndex.owned_bounds_refit_rebuild_and_failed_update") {
  const auto frustum = Graphics::Frustum::create({}).value();
  const Graphics::Bounds inside{{-0.1F, -0.1F, 0.4F}, {0.1F, 0.1F, 0.6F}};
  const Graphics::Bounds outside{{2, 0, 0}, {3, 1, 1}};
  Graphics::SpatialIndex index;
  CHECK(index.update({}));
  CHECK(index.query(frustum)->hits.empty());
  std::vector boxes(1024, outside);
  boxes[511] = inside;
  auto update = index.update(boxes).value();
  CHECK(update.rebuilt);
  CHECK(update.refittedNodes == 0);
  auto query = index.query(frustum).value();
  REQUIRE(query.hits.size() == 1);
  CHECK(query.hits.front().index == 511);
  CHECK(query.hits.front().inside);
  CHECK(query.nodeTests + query.boundsTests < boxes.size() / 4);
  CHECK_FALSE(index.update(boxes)->rebuilt);
  CHECK(index.update(boxes)->changedBounds == 0);
  boxes[511] = outside;
  CHECK(index.query(frustum)->hits == query.hits);
  update = index.update(boxes).value();
  CHECK_FALSE(update.rebuilt);
  CHECK(update.changedBounds == 1);
  CHECK(update.refittedNodes < 16);
  CHECK(index.query(frustum)->hits.empty());
  boxes[0] = inside;
  CHECK(index.update(boxes));
  auto copy = index;
  boxes[0].minimum.x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(index.update(boxes));
  CHECK(index.query(frustum)->hits.front().index == 0);
  CHECK(copy.query(frustum)->hits == index.query(frustum)->hits);
  boxes.resize(10001);
  CHECK_FALSE(index.update(boxes));
  CHECK(index.query(frustum)->hits.front().index == 0);
  boxes.assign(10000, inside);
  CHECK(index.update(boxes)->rebuilt);
  query = index.query(frustum).value();
  CHECK(query.hits.size() == 10000);
  CHECK(query.nodeTests == 1);
  CHECK(query.boundsTests == 0);
  CHECK(index.update({})->rebuilt);
  CHECK(index.query(frustum)->hits.empty());
  CHECK(copy.query(frustum)->hits.size() == 1);
}

TEST_CASE("spatialIndex.ordered_queries_match_linear_camera_and_motion") {
  std::mt19937 random(714);
  std::uniform_real_distribution<float> position(-20, 20);
  std::uniform_real_distribution<float> size(0, 3);
  std::vector<Graphics::Bounds> boxes;

  for (int i = 0; i < 513; ++i) {
    const Math::Vec3 p{position(random), position(random), position(random)};
    const float extent = size(random);
    boxes.push_back({p, {p.x + extent, p.y + extent, p.z + extent}});
  }

  boxes.push_back({{1, -1, 0}, {1, 1, 1}});
  Graphics::SpatialIndex index;

  for (int step = 0; step < 8; ++step) {
    REQUIRE(index.update(boxes));
    const auto view = Math::lookAt({float(step), 2, 5}, {}).value();

    for (const auto projection : {Math::Mat4{}, Math::perspective(60, 1, 0.1F, 100).value(),
                                  Math::orthographic(-10, 10, -10, 10, 0.1F, 100).value()}) {
      const auto frustum
          = Graphics::Frustum::create(Math::multiply(projection, view).value()).value();
      std::vector<Graphics::SpatialHit> expected;

      for (std::uint32_t i = 0; i < boxes.size(); ++i) {
        const auto state = frustum.classify(boxes[i]).value();

        if (state != Graphics::Containment::Outside) {
          expected.push_back({i, state == Graphics::Containment::Inside});
        }
      }

      CHECK(index.query(frustum)->hits == expected);
    }

    std::ranges::reverse(boxes);
    boxes[0] = {{float(step), 0, 0}, {float(step + 1), 1, 1}};
  }

  const auto identity = Graphics::Frustum::create({}).value();
  CHECK(identity.classify({{1, -1, 0}, {1, 1, 1}}).value() == Graphics::Containment::Intersecting);
  CHECK(identity.classify({{-0.1F, -0.1F, 0.4F}, {0.1F, 0.1F, 0.6F}}).value()
        == Graphics::Containment::Inside);
  CHECK_FALSE(index.update(std::vector<Graphics::Bounds>{{{1, 0, 0}, {-1, 0, 0}}}));
}
