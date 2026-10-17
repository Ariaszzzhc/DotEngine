#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Graphics.Text;

TEST_CASE("text.font_owns_bytes_wraps_cjk_clips_and_reuses_safely_on_another_thread") {
  using namespace Dot;
  const std::filesystem::path path{DOT_EDITOR_FONT};
  auto files = Core::FileSystem::open(path.parent_path()).value();
  auto bytes = files.readBytes(path.filename(), 32 * 1024 * 1024).value();
  auto font = Graphics::TextFont::create(bytes);
  REQUIRE_MESSAGE(font, (font ? "" : font.error().message));
  bytes.clear();
  Graphics::TextBox box;
  box.text = "|";
  auto small = font->rasterize(box, 80, 48, 480);
  REQUIRE(small);
  CHECK_FALSE(small->overflow);
  box.text = "Checkpoint saved / 检查点\nF7 restore";
  box.fontSize = 20;
  auto raster = font->rasterize(box, 320, 96, 720);
  REQUIRE_MESSAGE(raster, (raster ? "" : raster.error().message));
  CHECK_FALSE(raster->overflow);
  CHECK(raster->lines == 2);
  CHECK(raster->glyphs > 20);
  CHECK(std::ranges::any_of(raster->image.pixels, [](auto p) { return p > 240; }));
  bool shared{};
  std::thread worker([copy = *font, box, expected = raster->image.pixels, &shared] {
    auto next = copy.rasterize(box, 320, 96, 720);
    shared = next && next->image.pixels == expected;
  });
  worker.join();
  CHECK(shared);
  box.text = "one two three four five six";
  auto wrapped = font->rasterize(box, 80, 160, 720);
  REQUIRE(wrapped);
  CHECK(wrapped->lines > 1);
  auto clipped = font->rasterize(box, 80, 8, 720);
  REQUIRE(clipped);
  CHECK(clipped->overflow);
  box.text = "\u0378";
  auto missing = font->rasterize(box, 80, 160, 720);
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == Core::ErrorCode::NotFound);
  box.text = "restored";
  CHECK_FALSE(font->rasterize(box, 0, 100, 720));
  auto moved = std::move(*font);
  CHECK_FALSE(font->rasterize(box, 80, 160, 720));
  CHECK(moved.rasterize(box, 80, 160, 720));
  box.text = std::string(1024, 'A');
  box.fontSize = 64;
  auto tiny = moved.rasterize(box, 1, 1, 8192);
  REQUIRE(tiny);
  CHECK(tiny->overflow);
  CHECK(tiny->glyphs == 1024);
}
