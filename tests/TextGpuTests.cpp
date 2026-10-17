#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <thread>
#include <utility>
#include <vector>

import Dot.Graphics.Renderer;
import Dot.Graphics.Text;
import Dot.Graphics.TextRenderer;

TEST_CASE("screen.layers_are_display_colors_after_exposure_and_keep_invalid_target_unchanged") {
  using namespace Dot::Graphics;
  auto renderer = Renderer::create().value();
  TextureOptions options;
  options.colorSpace = ColorSpace::Linear;
  options.filter = TextureFilter::Nearest;
  options.mipmaps = false;
  auto red = renderer.uploadTexture({1, 1, {255, 0, 0, 255}}, options).value();
  auto blue = renderer.uploadTexture({1, 1, {0, 0, 255, 128}}, options).value();
  std::array screen{ScreenDraw{red, {0, 0}, {1, 1}}, ScreenDraw{blue, {.5F, 0}, {.5F, 1}}};
  auto target = renderer.createTarget(4, 4).value();
  REQUIRE(renderer.renderMeshesInto(target, {}, {}, {0, 1, 0, 1}, screen));
  auto image = renderer.readTarget(target).value();
  CHECK(image.pixels[0] == 255);
  CHECK(image.pixels[1] == 0);
  CHECK(image.pixels[2] == 0);
  CHECK(image.pixels[8] == doctest::Approx(127).epsilon(.01));
  CHECK(image.pixels[10] == doctest::Approx(128).epsilon(.01));
  auto readback = renderer.renderMeshes({}, {}, 4, 4, {0, 0, 0, 1}, screen);
  REQUIRE_MESSAGE(readback, (readback ? "" : readback.error().message));
  CHECK(readback->pixels == image.pixels);
  auto post = renderer.postProcess().value();
  post.exposureEv = -4;
  REQUIRE(renderer.setPostProcess(post));
  CHECK(renderer.renderMeshes({}, {}, 4, 4, {0, 1, 0, 1}, screen)->pixels == image.pixels);
  auto foreign = Renderer::create().value();
  auto wrong = foreign.uploadTexture({1, 1, {0, 255, 0, 255}}, options).value();
  screen[0].texture = wrong;
  CHECK_FALSE(renderer.renderMeshesInto(target, {}, {}, {1, 1, 1, 1}, screen));
  CHECK(target.info()->frames == 1);
  CHECK(renderer.readTarget(target)->pixels == image.pixels);
  screen[0].texture = red;
  screen[0].position.x = -1;
  CHECK_FALSE(renderer.renderMeshesInto(target, {}, {}, {1, 1, 1, 1}, screen));
}

TEST_CASE("text_gpu.cached_layers_reflow_update_and_release_owned_textures") {
  using namespace Dot::Graphics;
  auto renderer = Renderer::create().value();
  const auto baseline = renderer.resourceStats().value();
  auto text = TextRenderer::create(renderer, std::filesystem::path{DOT_EDITOR_FONT}).value();
  TextBox box;
  box.text = "SAVE / 保存";
  box.fontSize = 40;
  std::array labels{TextLayer{"checkpoint", box}};
  {
    auto first = text.prepare(labels, 640, 360);
    REQUIRE_MESSAGE(first, (first ? "" : first.error().message));
    REQUIRE(first->size() == 1);
    CHECK(text.stats()->rasterizations == 1);
    CHECK(text.stats()->overflow.empty());
    REQUIRE(text.prepare(labels, 640, 360));
    CHECK(text.stats()->rasterizations == 1);
    labels[0].value.text = "RESTORED / 恢复";
    REQUIRE(text.prepare(labels, 640, 360));
    CHECK(text.stats()->rasterizations == 2);
    REQUIRE(text.prepare(labels, 1280, 720));
    CHECK(text.stats()->rasterizations == 3);
    auto prior = renderer.resourceStats().value();
    const auto priorBytes = text.stats()->textureBytes;
    auto changed = labels[0];
    changed.value.text = "Discarded candidate";
    auto missing = labels[0];
    missing.key = "missing";
    missing.value.text = "\u0378";
    const std::array invalid{changed, missing};
    CHECK_FALSE(text.prepare(invalid, 1280, 720));
    CHECK(text.stats()->textureBytes == priorBytes);
    CHECK(text.stats()->rasterizations == 4);
    CHECK(renderer.resourceStats()->textures == prior.textures);
    CHECK(renderer.resourceStats()->textureBytes == prior.textureBytes);
    auto large = labels[0];
    large.value.position = {0, 0};
    large.value.size = {1, 1};
    const std::array budget{large};
    auto peak = text.prepare(budget, 2048, 2048);
    REQUIRE_FALSE(peak);
    CHECK(peak.error().message.find("replacement peak") != std::string::npos);
    CHECK_FALSE(text.prepare(budget, 8192, 8192));
    CHECK(text.stats()->textureBytes == priorBytes);
    CHECK(text.stats()->rasterizations == 4);
    CHECK(renderer.resourceStats()->textures == prior.textures);
    CHECK(renderer.resourceStats()->textureBytes == prior.textureBytes);
    bool denied{};
    std::thread worker([&] { denied = !text.prepare(labels, 640, 360); });
    worker.join();
    CHECK(denied);
  }
  REQUIRE(text.prepare({}, 640, 360));
  CHECK(text.stats()->textureBytes == 0);
  CHECK(renderer.resourceStats()->textures == baseline.textures);
  CHECK(renderer.resourceStats()->textureBytes == baseline.textureBytes);
  auto moved = std::move(text);
  CHECK_FALSE(text.prepare(labels, 640, 360));
  CHECK(moved.prepare(labels, 640, 360));
  auto tiny = moved.prepare(labels, 1, 1);
  REQUIRE(tiny);
  CHECK(tiny->empty());
  CHECK(moved.stats()->textureBytes == 0);
  CHECK(moved.stats()->overflow == std::vector<std::string>{"checkpoint"});
}
