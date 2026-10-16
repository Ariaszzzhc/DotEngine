#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.Editor.PlayInput;
import Dot.Platform.Events;
import Dot.Platform.Input;

namespace {
  using namespace Dot::Editor;
  using namespace Dot::Platform;
  using Json = nlohmann::json;

  auto request(std::uint32_t frames = 2) -> Json {
    return {{"frames", frames},     {"keys", {"W", "Space"}},  {"buttons", {"Left"}},
            {"pointer", {100, 50}}, {"pointerDelta", {3, -2}}, {"wheel", {0, 1}},
            {"text", "晶体 🌏"}};
  }

  auto next(PlayInput& input) -> InputState {
    auto result = input.next();
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE(result->has_value());
    return std::move(**result);
  }
}  // namespace

TEST_CASE("play_input.press_hold_release_use_native_edges_and_owned_frames") {
  PlayInput input;
  CHECK_FALSE(input.next().value());
  REQUIRE(input.submit(request().dump()));
  CHECK(input.stats().remaining == 2);
  CHECK(input.stats().published == 0);
  const auto first = next(input);
  CHECK(first.key(Key::W).down);
  CHECK(first.key(Key::W).pressed);
  CHECK(first.key(Key::Space).pressed);
  CHECK(first.button(MouseButton::Left).pressed);
  CHECK(first.pointer().x == 100);
  CHECK(first.pointerDelta().x == 3);
  CHECK(first.wheel().y == 1);
  CHECK(first.text() == "晶体 🌏");
  const auto held = next(input);
  CHECK(held.key(Key::W).down);
  CHECK_FALSE(held.key(Key::W).pressed);
  CHECK_FALSE(held.button(MouseButton::Left).pressed);
  CHECK(held.pointerDelta().x == 3);
  CHECK(held.wheel().y == 1);
  CHECK(held.text().empty());
  CHECK(input.stats().active);
  CHECK(input.stats().remaining == 0);
  const auto released = next(input);
  CHECK_FALSE(released.key(Key::W).down);
  CHECK(released.key(Key::W).released);
  CHECK(released.button(MouseButton::Left).released);
  CHECK(released.pointerDelta().x == 0);
  CHECK(released.wheel().y == 0);
  CHECK_FALSE(input.stats().active);
  CHECK(input.stats().published == 3);
  CHECK_FALSE(input.next().value());
  CHECK(first.key(Key::W).pressed);
  CHECK(first.text() == "晶体 🌏");
}

TEST_CASE("play_input.active_request_rejects_replacement_and_cancel_releases_once") {
  PlayInput input;
  REQUIRE(input.submit(request(600).dump()));
  CHECK_FALSE(input.submit(request().dump()));
  CHECK(input.stats().remaining == 600);
  const auto first = next(input);
  input.cancel();
  input.cancel();
  CHECK(input.stats().active);
  CHECK(input.stats().remaining == 0);
  CHECK_FALSE(input.submit(request().dump()));
  const auto released = next(input);
  CHECK(released.key(Key::Space).released);
  CHECK_FALSE(released.key(Key::Space).pressed);
  CHECK(input.stats().published == 2);
  CHECK_FALSE(input.next().value());
  REQUIRE(input.submit(request(1).dump()));
  CHECK(next(input).key(Key::Space).pressed);
  input.reset();
  CHECK_FALSE(input.stats().active);
  CHECK(input.stats().published == 0);
  CHECK_FALSE(input.next().value());
  REQUIRE(input.submit(request().dump()));
  input.cancel();
  const auto cancelled = next(input);
  CHECK_FALSE(cancelled.key(Key::W).down);
  CHECK_FALSE(cancelled.key(Key::W).released);
  CHECK_FALSE(cancelled.key(Key::W).pressed);
  CHECK(cancelled.text().empty());
  CHECK(first.key(Key::W).pressed);
}

TEST_CASE("play_input.strict_names_schema_and_bounds_reject_before_publication") {
  std::vector<Json> invalid;
  const auto base = request();

  for (auto frames : {Json(0), Json(-1), Json(601), Json(1.5), Json(true), Json("2"),
                      Json(std::numeric_limits<std::uint64_t>::max())}) {
    auto bad = base;
    bad["frames"] = frames;
    invalid.push_back(std::move(bad));
  }

  for (const auto& key : {"w", "Unknown", "Escape", "F5"}) {
    auto bad = base;
    bad["keys"] = {key};
    invalid.push_back(std::move(bad));
  }

  for (const auto& field : {"keys", "buttons"}) {
    auto bad = base;
    bad[field] = {"Left", "Left"};
    invalid.push_back(bad);
    bad[field] = "Left";
    invalid.push_back(bad);
    bad[field] = {123};
    invalid.push_back(bad);
    bad[field] = Json::array();
    bad[field].push_back(nullptr);
    invalid.push_back(bad);
  }

  for (const auto& field : {"pointer", "pointerDelta", "wheel"}) {
    auto bad = base;
    bad[field] = {1e20, 0};
    invalid.push_back(bad);
    bad[field] = {0};
    invalid.push_back(bad);
    bad[field] = {"zero", 0};
    invalid.push_back(bad);
    bad[field] = {nullptr, 0};
    invalid.push_back(bad);
  }

  auto bad = base;
  bad["unexpected"] = 1;
  invalid.push_back(bad);
  bad = base;
  bad.erase("pointerDelta");
  invalid.push_back(bad);
  bad = base;
  bad["text"] = std::string("a\0b", 3);
  invalid.push_back(bad);
  bad["text"] = std::string(16 * 1024 + 1, 'a');
  invalid.push_back(bad);

  PlayInput input;

  for (const auto& value : invalid) {
    INFO(value.dump());
    const auto result = input.submit(value.dump());
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
    CHECK_FALSE(input.stats().active);
    CHECK(input.stats().published == 0);
    CHECK_FALSE(input.next().value());
  }

  CHECK_FALSE(input.submit("{\"frames\":1,\"frames\":2}"));
  CHECK_FALSE(input.submit(std::string(64 * 1024 + 1, ' ')));
  CHECK_FALSE(input.submit(std::string("\xc0\x80", 2)));
  CHECK_FALSE(input.submit(std::string("{}\0", 3)));
  REQUIRE(input.submit(base.dump()));
  CHECK(next(input).key(Key::W).pressed);
}

TEST_CASE("play_input.maximum_request_has_exact_budget_and_neutral_release") {
  auto value = request(600);
  value["keys"] = Json::array();
  value["buttons"] = Json::array();
  value["pointer"] = {-1e6, 1e6};
  value["pointerDelta"] = {1e6, -1e6};
  value["wheel"] = {1000, -1000};
  value["text"] = std::string(16 * 1024, 'x');
  PlayInput input;
  REQUIRE(input.submit(value.dump()));

  for (unsigned frame = 0; frame < 600; ++frame) {
    const auto published = next(input);
    CHECK(published.pointer().x == -1e6F);
    CHECK(published.pointerDelta().x == 1e6F);
    CHECK(published.wheel().x == 1000);
    CHECK(published.text().size() == (frame == 0 ? 16 * 1024 : 0));
  }

  CHECK(input.stats().remaining == 0);
  CHECK(input.stats().published == 600);
  const auto released = next(input);
  CHECK(released.pointerDelta().x == 0);
  CHECK(released.wheel().x == 0);
  CHECK(input.stats().published == 601);
  CHECK_FALSE(input.next().value());
}

TEST_CASE("input.canonical_names_cover_keys_and_buttons_without_aliases") {
  std::set<std::string_view> keys;
  std::set<Key> values;

  for (const auto& [name, key] : keyNames()) {
    CHECK_FALSE(name.empty());
    CHECK(keys.insert(name).second);
    CHECK(values.insert(key).second);
    CHECK(key != Key::Unknown);
  }

  CHECK(keys.size() + 1 == static_cast<std::size_t>(Key::Count));
  std::set<std::string_view> buttons;
  std::set<MouseButton> buttonValues;

  for (const auto& [name, button] : buttonNames()) {
    CHECK(buttons.insert(name).second);
    CHECK(buttonValues.insert(button).second);
    CHECK(button != MouseButton::Unknown);
  }

  CHECK(buttons.size() + 1 == static_cast<std::size_t>(MouseButton::Count));
}

TEST_CASE("play_input.source_handoff_uses_published_state_without_ghost_edges") {
  InputState native(7);
  REQUIRE(native.consume({7, 0, KeyChanged{Key::W, true, false}}));
  REQUIRE(native.consume({7, 0, KeyChanged{Key::A, true, false}}));
  REQUIRE(native.consume({7, 0, ButtonChanged{MouseButton::Right, true}}));
  PlayInput input;
  REQUIRE(input.submit(request().dump(), native));
  const auto first = next(input);
  CHECK(first.key(Key::W).down);
  CHECK_FALSE(first.key(Key::W).pressed);
  CHECK(first.key(Key::A).released);
  CHECK(first.key(Key::Space).pressed);
  CHECK(first.button(MouseButton::Right).released);
  CHECK(first.button(MouseButton::Left).pressed);
  input.reset();
  REQUIRE(input.submit(request().dump(), native));
  input.cancel();
  const auto cancelled = next(input);
  CHECK(cancelled.key(Key::W).released);
  CHECK(cancelled.key(Key::A).released);
  CHECK(cancelled.button(MouseButton::Right).released);
  CHECK_FALSE(cancelled.key(Key::Space).pressed);
  CHECK_FALSE(cancelled.key(Key::Space).released);
  CHECK_FALSE(cancelled.button(MouseButton::Left).released);
}
