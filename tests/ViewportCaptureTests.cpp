#include <doctest/doctest.h>

#include <limits>
#include <utility>
#include <vector>

import Dot.Editor.ViewportCapture;
import Dot.Core.Result;
import Dot.Graphics.Image;

namespace {
  const Dot::Editor::CaptureContext context{1, 2, 0, Dot::Editor::ViewportMode::Editor, false};

  auto frame() -> Dot::Editor::CaptureFrame {
    return {context, 11, 0, 1, 2, {}, false};
  }

  auto pixels() -> Dot::Graphics::Image {
    return {1, 2, {255, 1, 2, 255, 3, 4, 255, 128}};
  }
}  // namespace

TEST_CASE("viewport_capture.owns_frozen_pixels_until_save_or_discard") {
  using namespace Dot::Editor;
  ViewportCapture capture;
  CHECK(capture.stats().state == CaptureState::Empty);
  CHECK_FALSE(capture.image(0));
  auto id = capture.request(context, 10).value();
  CHECK(capture.stats().state == CaptureState::Queued);
  CHECK_FALSE(capture.request(context, 10));
  CHECK_FALSE(capture.saved(id));
  auto source = pixels();
  REQUIRE(capture.complete(id, frame(), source));
  source.pixels.assign(8, 0);
  CHECK((*capture.image(id))->pixels == pixels().pixels);
  CHECK(capture.stats().frame.serial == 11);
  CHECK(capture.stats().frame.height == 2);
  CHECK_FALSE(capture.request(context, 11));
  CHECK_FALSE(capture.saved(id + 1));
  CHECK_FALSE(capture.discard(id + 1));
  CHECK_FALSE(capture.complete(id, frame(), pixels()));
  REQUIRE(capture.saved(id));
  CHECK(capture.stats().state == CaptureState::Saved);
  CHECK_FALSE(capture.image(id));
  CHECK_FALSE(capture.saved(id));
  auto next = capture.request(context, 12).value();
  CHECK(next > id);
  CHECK_FALSE(capture.discard(id));
  REQUIRE(capture.discard(next));
  CHECK(capture.stats().state == CaptureState::Cancelled);
  CHECK_FALSE(capture.complete(next, frame(), pixels()));
  auto third = capture.request(context, 12).value();
  auto newer = frame();
  newer.serial = 13;
  REQUIRE(capture.complete(third, newer, pixels()));
  REQUIRE(capture.discard(third));
  CHECK_FALSE(capture.image(third));
}

TEST_CASE("viewport_capture.rejects_stale_frames_scope_revision_mode_and_session") {
  using namespace Dot::Editor;
  ViewportCapture capture;
  const auto id = capture.request(context, 10).value();
  auto value = frame();
  value.serial = 10;
  CHECK_FALSE(capture.complete(id, value, pixels()));
  value = frame();
  ++value.context.revision;
  CHECK_FALSE(capture.complete(id, value, pixels()));
  value = frame();
  ++value.context.scene;
  CHECK_FALSE(capture.complete(id, value, pixels()));
  value = frame();
  value.context.mode = ViewportMode::Game;
  CHECK_FALSE(capture.complete(id, value, pixels()));
  value = frame();
  value.context.transform = true;
  CHECK_FALSE(capture.complete(id, value, pixels()));
  CHECK_FALSE(capture.complete(id + 1, frame(), pixels()));
  REQUIRE(capture.complete(id, frame(), pixels()));
  REQUIRE(capture.discard(id));
  auto play = context;
  play.mode = ViewportMode::Play;
  CHECK_FALSE(capture.request(play, 11));
  play.session = 4;
  auto running = capture.request(play, 11).value();
  value = {play, 12, 300, 1, 2, {}, true};
  ++value.context.session;
  CHECK_FALSE(capture.complete(running, value, pixels()));
  --value.context.session;
  REQUIRE(capture.complete(running, value, pixels()));
  CHECK(capture.stats().frame.playFrame == 300);
  CHECK(capture.stats().frame.materialPreview);
}

TEST_CASE("viewport_capture.validates_metadata_and_recovers_after_explicit_failure") {
  using namespace Dot::Editor;
  ViewportCapture capture;
  auto invalid = context;
  invalid.scene = 0;
  CHECK_FALSE(capture.request(invalid, 0));
  invalid = context;
  invalid.session = 1;
  CHECK_FALSE(capture.request(invalid, 0));
  invalid = context;
  invalid.mode = static_cast<ViewportMode>(99);
  CHECK_FALSE(capture.request(invalid, 0));
  CHECK_FALSE(capture.request(context, std::numeric_limits<std::uint64_t>::max()));
  auto id = capture.request(context, 10).value();
  auto value = frame();
  value.width = 2;
  CHECK_FALSE(capture.complete(id, value, pixels()));
  value = frame();
  value.viewProjection.data[0] = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(capture.complete(id, value, pixels()));
  value = frame();
  value.playFrame = 1;
  CHECK_FALSE(capture.complete(id, value, pixels()));
  CHECK_FALSE(capture.complete(id, frame(), {}));
  CHECK(capture.stats().state == CaptureState::Queued);
  auto error = Dot::Core::fail(Dot::Core::ErrorCode::Io, "Readback unavailable").error();
  CHECK_FALSE(capture.fail(id + 1, error));
  REQUIRE(capture.fail(id, error));
  error.message.clear();
  CHECK(capture.stats().error->message == "Readback unavailable");
  CHECK(capture.stats().state == CaptureState::Failed);
  CHECK_FALSE(capture.image(id));
  CHECK_FALSE(capture.fail(id, error));
  id = capture.request(context, 10).value();
  CHECK_FALSE(capture.stats().error);
  REQUIRE(capture.complete(id, frame(), pixels()));
}

TEST_CASE("viewport_capture.freezes_animation_provenance_and_validates_its_context") {
  using namespace Dot::Editor;
  ViewportCapture capture;
  auto animated = context;
  animated.animation = true;
  CHECK_FALSE(capture.request(animated, 10));
  animated.session = 4;
  auto id = capture.request(animated, 10).value();
  auto value = frame();
  value.context = animated;
  CHECK_FALSE(capture.complete(id, value, pixels()));
  value.animation = AnimationCapture{3, 12, 1, .25};
  auto invalid = value;
  invalid.animation->time = std::numeric_limits<double>::quiet_NaN();
  CHECK_FALSE(capture.complete(id, invalid, pixels()));
  REQUIRE(capture.complete(id, value, pixels()));
  value.animation->time = 1;
  CHECK(capture.stats().frame.animation->time == .25);
  CHECK(capture.stats().frame.context.session == 4);
  REQUIRE(capture.discard(id));
  animated.mode = ViewportMode::Play;
  CHECK_FALSE(capture.request(animated, 11));
  animated.mode = ViewportMode::Editor;
  animated.transform = true;
  CHECK_FALSE(capture.request(animated, 11));
}
