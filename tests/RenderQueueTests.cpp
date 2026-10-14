#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <vector>
import Dot.Graphics.RenderQueue;

TEST_CASE("render_queue.solid_source_order_blend_depth_ties_and_original_identity") {
  using namespace Dot::Graphics;
  const std::array items{QueueItem{{1}, AlphaMode::Blend, .2},
                         QueueItem{{1}, AlphaMode::Opaque, .1}, QueueItem{{2}, AlphaMode::Mask, .6},
                         QueueItem{{1}, AlphaMode::Blend, .8},
                         QueueItem{{1}, AlphaMode::Blend, .8}};
  auto queue = buildRenderQueue(items).value();
  CHECK((queue.solid.indices == std::vector<std::uint32_t>{1, 2}));
  CHECK((queue.blended.indices == std::vector<std::uint32_t>{3, 4, 0}));
  CHECK(queue.blended.batches.size() == 1);
  CHECK(queue.blended.batches[0].count == 3);
}

TEST_CASE("render_queue.disabled_invalid_boundaries_and_owned_results") {
  using namespace Dot::Graphics;
  CHECK(buildRenderQueue({})->solid.indices.empty());
  CHECK(validateCoverage(AlphaMode::Mask, 2));
  CHECK_FALSE(validateCoverage(AlphaMode::Mask, -1));
  CHECK_FALSE(validateCoverage(static_cast<AlphaMode>(99), .5F));
  std::vector<QueueItem> items(10000, {{1}, AlphaMode::Blend, 1});
  auto queue = buildRenderQueue(items).value();
  items[0].binding.enabled = false;
  CHECK(queue.blended.indices.size() == 10000);
  CHECK(buildRenderQueue(items)->blended.indices.size() == 9999);
  items[0].depth = std::numeric_limits<double>::infinity();
  CHECK_FALSE(buildRenderQueue(items));
  items[0].depth = 0;
  items[0].binding.mesh = 0;
  CHECK_FALSE(buildRenderQueue(items));
  items.resize(10001);
  CHECK_FALSE(buildRenderQueue(items));
}
