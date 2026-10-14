#include <doctest/doctest.h>

#include <array>
#include <vector>

import Dot.Graphics.DrawBatches;

TEST_CASE("draw_batches.stable_runs_skip_disabled_keep_texture_identity_and_source_indices") {
  using namespace Dot::Graphics;
  const std::array<BatchItem, 7> items{{{1}, {2, {}, false}, {1}, {2}, {1}, {1, {9}}, {1, {9}}}};
  auto result = buildDrawBatches(items);
  REQUIRE(result);
  CHECK(result->indices == std::vector<std::uint32_t>{0, 2, 3, 4, 5, 6});
  CHECK(result->batches == std::vector<DrawBatch>{{0, 2}, {2, 1}, {3, 1}, {4, 2}});
  CHECK(items[1].enabled == false);
  auto moved = std::move(*result);
  CHECK(moved.indices.size() == 6);
}

TEST_CASE("draw_batches.empty_disabled_zero_identity_and_maximum_boundaries") {
  using namespace Dot::Graphics;
  CHECK(buildDrawBatches({})->batches.empty());
  std::vector<BatchItem> items(10000, BatchItem{1});
  auto full = buildDrawBatches(items);
  REQUIRE(full);
  CHECK(full->batches.size() == 1);
  CHECK(full->batches[0].count == 10000);
  CHECK(full->indices.back() == 9999);
  items.push_back({1});
  CHECK_FALSE(buildDrawBatches(items));
  CHECK_FALSE(buildDrawBatches(std::array{BatchItem{0, {}, false}}));
  CHECK(buildDrawBatches(std::array{BatchItem{1, {}, false}})->indices.empty());
}
