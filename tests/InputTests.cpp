#include <doctest/doctest.h>

#include <limits>
#include <string_view>

import Dot.Platform.Events;
import Dot.Platform.Input;

using namespace Dot::Platform;

TEST_CASE("input.key_edges_and_repeat") {
  InputState input(7);
  CHECK(input.consume({7, 0, KeyChanged{Key::A, true, false}}));
  CHECK(input.key(Key::A).pressed);
  CHECK(input.key(Key::A).down);
  input.beginFrame();
  CHECK(input.key(Key::A).down);
  CHECK_FALSE(input.key(Key::A).pressed);
  CHECK(input.consume({7, 0, KeyChanged{Key::A, true, true}}));
  CHECK_FALSE(input.key(Key::A).pressed);
  CHECK(input.consume({7, 0, KeyChanged{Key::A, false, false}}));
  CHECK(input.key(Key::A).released);
  CHECK_FALSE(input.key(Key::A).down);
  input.beginFrame();
  CHECK_FALSE(input.key(Key::A).released);
}

TEST_CASE("input.repeat_is_not_a_new_press") {
  InputState input;
  CHECK(input.consume({0, 0, KeyChanged{Key::Space, true, true}}));
  CHECK(input.key(Key::Space).down);
  CHECK_FALSE(input.key(Key::Space).pressed);
}

TEST_CASE("input.press_and_release_in_one_frame") {
  InputState input;
  CHECK(input.consume({0, 0, KeyChanged{Key::A, true, false}}));
  CHECK(input.consume({0, 0, KeyChanged{Key::A, false, false}}));
  CHECK(input.key(Key::A).pressed);
  CHECK(input.key(Key::A).released);
  CHECK_FALSE(input.key(Key::A).down);
}

TEST_CASE("input.focus_loss_releases_held_buttons") {
  InputState input(1);
  CHECK(input.consume({1, 0, KeyChanged{Key::LeftShift, true, false}}));
  CHECK(input.consume({1, 0, ButtonChanged{MouseButton::Left, true}}));
  input.beginFrame();
  CHECK(input.consume({1, 0, Focus{false}}));
  CHECK(input.key(Key::LeftShift).released);
  CHECK_FALSE(input.key(Key::LeftShift).down);
  CHECK(input.button(MouseButton::Left).released);
  CHECK_FALSE(input.button(MouseButton::Left).down);
}

TEST_CASE("input.foreign_windows_and_invalid_indices") {
  InputState input(7);
  CHECK_FALSE(input.consume({8, 0, KeyChanged{Key::A, true, false}}));
  CHECK_FALSE(input.key(Key::A).down);
  CHECK_FALSE(input.consume({7, 0, KeyChanged{static_cast<Key>(-1), true, false}}));
  CHECK_FALSE(input.consume({7, 0, KeyChanged{Key::Unknown, true, false}}));
  CHECK_FALSE(input.consume({7, 0, ButtonChanged{MouseButton::Count, true}}));
  CHECK_FALSE(input.key(Key::Count).down);
  CHECK_FALSE(input.button(static_cast<MouseButton>(1000)).down);
  CHECK_FALSE(input.consume({7, 0, Quit{}}));
}

TEST_CASE("input.pointer_and_wheel_accumulate_per_frame") {
  InputState input;
  CHECK(input.consume({0, 0, PointerMoved{100, 50, 4, 2}}));
  CHECK(input.consume({0, 0, PointerMoved{103, 55, 3, 5}}));
  CHECK(input.consume({0, 0, Wheel{1, 2}}));
  CHECK(input.consume({0, 0, Wheel{-1, 3}}));
  CHECK(input.pointer().x == 103);
  CHECK(input.pointer().y == 55);
  CHECK(input.pointerDelta().x == 7);
  CHECK(input.pointerDelta().y == 7);
  CHECK(input.wheel().x == 0);
  CHECK(input.wheel().y == 5);
  input.beginFrame();
  CHECK(input.pointer().x == 103);
  CHECK(input.pointerDelta().x == 0);
  CHECK(input.wheel().y == 0);
}

TEST_CASE("input.reject_nonfinite_motion") {
  InputState input;
  CHECK_FALSE(input.consume({0, 0, PointerMoved{std::numeric_limits<float>::infinity(), 0, 0, 0}}));
  CHECK_FALSE(input.consume({0, 0, Wheel{0, std::numeric_limits<float>::quiet_NaN()}}));
  CHECK(input.pointer().x == 0);
  CHECK(input.wheel().y == 0);
}

TEST_CASE("input.text_is_frame_scoped") {
  InputState input;
  CHECK(input.consume({0, 0, Text{"世界"}}));
  CHECK(input.consume({0, 0, Text{" 🌏"}}));
  CHECK(input.text() == "世界 🌏");
  input.beginFrame();
  CHECK(input.text().empty());
}

TEST_CASE("input.accumulation_overflow_is_transactional") {
  InputState input;
  const auto maximum = std::numeric_limits<float>::max();
  CHECK(input.consume({0, 0, PointerMoved{1, 2, maximum, maximum}}));
  CHECK_FALSE(input.consume({0, 0, PointerMoved{3, 4, maximum, maximum}}));
  CHECK(input.pointer().x == 1);
  CHECK(input.pointerDelta().x == maximum);
  CHECK(input.consume({0, 0, Wheel{maximum, maximum}}));
  CHECK_FALSE(input.consume({0, 0, Wheel{maximum, maximum}}));
  CHECK(input.wheel().x == maximum);
}
