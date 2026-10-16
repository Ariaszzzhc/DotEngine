#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

import Dot.Graphics.ShadowAtlas;

using namespace Dot::Graphics;

namespace {
  auto verifyPacking(const ShadowAtlas& atlas, const std::vector<ShadowAtlasRequest>& requests)
      -> void {
    REQUIRE(atlas.sources.size() == requests.size());
    std::vector<ShadowTile> tiles;

    for (std::size_t source = 0; source < requests.size(); ++source) {
      for (std::uint32_t face = 0; face < 6; ++face) {
        const auto tile = atlas.sources[source][face];

        if (face >= requests[source].maps) {
          CHECK(tile == ShadowTile{});
          continue;
        }

        CHECK(tile.size == requests[source].resolution);
        CHECK(tile.layer < atlas.layers);
        CHECK(tile.x + tile.size <= atlas.dimension);
        CHECK(tile.y + tile.size <= atlas.dimension);
        CHECK(tile.x % tile.size == 0);
        CHECK(tile.y % tile.size == 0);

        for (const auto other : tiles) {
          CHECK((tile.layer != other.layer || tile.x + tile.size <= other.x
                 || other.x + other.size <= tile.x || tile.y + tile.size <= other.y
                 || other.y + other.size <= tile.y));
        }

        tiles.push_back(tile);
      }
    }

    CHECK(atlas.bytes == std::uint64_t(atlas.dimension) * atlas.dimension * atlas.layers * 4);
  }
}  // namespace

TEST_CASE("shadow_atlas.mixed_resolution_tiles_keep_identity_and_pack_without_overlap") {
  std::vector<ShadowAtlasRequest> requests{{256, 1}, {128, 4}, {512, 2}, {128, 6}};
  const auto atlas = planShadowAtlas(requests);
  REQUIRE(atlas);
  verifyPacking(*atlas, requests);
  CHECK(atlas->dimension == 512);
  CHECK(atlas->layers == 3);
  CHECK(atlas->sources == planShadowAtlas(requests)->sources);
  const auto snapshot = atlas->sources;
  requests[0].resolution = 2048;
  CHECK(atlas->sources == snapshot);
  CHECK(planShadowAtlas({})->bytes == 0);
  CHECK(planShadowAtlas({})->sources.empty());
}

TEST_CASE("shadow_atlas.layer_growth_padding_budget_and_invalid_work_are_explicit") {
  std::vector<ShadowAtlasRequest> requests(64, {128, 6});
  auto atlas = planShadowAtlas(requests);
  REQUIRE(atlas);
  verifyPacking(*atlas, requests);
  CHECK(atlas->dimension == 256);
  CHECK(atlas->layers == 96);
  CHECK(atlas->bytes == 24 * 1024 * 1024);
  CHECK(planShadowAtlas(requests, atlas->bytes));
  CHECK_FALSE(planShadowAtlas(requests, atlas->bytes - 1));
  CHECK_FALSE(planShadowAtlas(requests, 512 * 1024 * 1024, 1));
  REQUIRE(planShadowAtlas(requests, 512 * 1024 * 1024, 2));
  CHECK(planShadowAtlas(requests, 512 * 1024 * 1024, 2)->dimension == 2048);
  requests.assign(1, {2048, 6});
  CHECK(planShadowAtlas(requests)->bytes == 96 * 1024 * 1024);
  requests.resize(6, {2048, 6});
  CHECK_FALSE(planShadowAtlas(requests, 512 * 1024 * 1024));
  CHECK_FALSE(planShadowAtlas({}, 0));
  CHECK_FALSE(planShadowAtlas({}, 512 * 1024 * 1024 + 1));
  CHECK_FALSE(planShadowAtlas({}, 1, 0));

  for (const auto request : {ShadowAtlasRequest{0, 1}, {129, 1}, {4096, 1}, {128, 0}, {128, 7}}) {
    requests.assign(1, request);
    CHECK_FALSE(planShadowAtlas(requests));
  }

  requests.assign(65, {128, 1});
  CHECK_FALSE(planShadowAtlas(requests));
}
