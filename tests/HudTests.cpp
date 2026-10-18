#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>

import Dot.Script.Hud;
import Dot.Math;
import Dot.Graphics.Text;
import Dot.UI.Layout;

namespace {
  using Json = nlohmann::json;

  auto label(std::string text = "Checkpoint saved") -> Json {
    return {{"text", text},
            {"position", {{"x", .02}, {"y", .02}}},
            {"size", {{"x", .6}, {"y", .12}}},
            {"fontSize", 20},
            {"padding", 8},
            {"color", {{"x", .9}, {"y", .96}, {"z", 1}, {"w", 1}}},
            {"background", {{"x", .035}, {"y", .045}, {"z", .065}, {"w", .9}}},
            {"visible", true}};
  }

  auto set(Dot::Script::Hud& hud, std::string key, Json value) {
    return hud.perform(Json{{"command", "set"}, {"key", key}, {"value", value}}.dump());
  }
}  // namespace

TEST_CASE("hud.authored_layout_hierarchy_reservation_and_shared_order") {
  using namespace Dot;
  UI::Node panel{"menu"};
  panel.box.position = {0, 0};
  panel.box.size = {1, 1};
  UI::Node first{"z-first", "menu", UI::Kind::Button};
  first.box.text = "First";
  first.box.position = {.1F, .1F};
  first.box.size = {.4F, .2F};
  auto second = first;
  second.key = "a-second";
  const auto layout = UI::Layout::create({"Menu", {first, panel, second}}).value();
  auto hud = Script::Hud::create(1, UI::Asset{"assets/menu.dotui", layout}).value();
  CHECK(hud.draws()->at(0).key == "menu");
  CHECK(hud.draws()->at(1).key == "z-first");
  CHECK(hud.draws()->at(2).key == "a-second");
  CHECK_FALSE(hud.perform(R"({"command":"remove","key":"menu"})"));
  CHECK_FALSE(set(hud, "z-first", label()));
  REQUIRE(set(hud, "transient", label()));
  REQUIRE(hud.perform(R"({"command":"clear"})"));
  CHECK(hud.draws()->size() == 3);
  Script::HudInput input;
  input.next = true;
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->focused == "z-first");
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->focused == "a-second");
  input.next = false;
  input.pointer = Math::Vec2{.2F, .2F};
  input.down = input.pressed = true;
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->pressed == "a-second");
  const auto before = hud.perform(R"({"command":"ui-snapshot"})").value();
  CHECK_FALSE(hud.perform(
      R"({"command":"ui-update","changes":[{"key":"menu","visible":false},{"key":"unknown","enabled":false}]})"));
  CHECK(hud.perform(R"({"command":"ui-snapshot"})").value() == before);
  REQUIRE(hud.perform(R"({"command":"ui-visible","key":"menu","value":false})"));
  CHECK_FALSE(hud.snapshot()->focused);
  CHECK_FALSE(hud.snapshot()->pressed);
  CHECK_FALSE(hud.perform(R"({"command":"activate","key":"a-second"})"));
  input.down = input.pressed = false;
  input.released = true;
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->sequence == 0);
  REQUIRE(hud.perform(R"({"command":"ui-reset"})"));
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->sequence == 0);
  input.pointer = Math::Vec2{.9F, .9F};
  CHECK(hud.publish(input)->pointer);
  CHECK_FALSE(hud.snapshot()->hovered);
  auto bindings = hud.bindings().value();
  CHECK(std::ranges::find(bindings, "ui.snapshot", &Script::HostFunction::name)->access
        == Script::HostAccess::ReadOnly);
  hud.retire();
  CHECK(hud.perform(R"({"command":"ui-snapshot"})"));
  CHECK_FALSE(hud.perform(R"({"command":"ui-reset"})"));
}

TEST_CASE("hud.pointer_blockers_invalidate_displayed_button_revisions") {
  using namespace Dot;
  UI::Node button{"button", {}, UI::Kind::Button};
  button.box.position = {0, 0};
  button.box.size = {1, 1};
  UI::Node cover{"cover", {}, UI::Kind::Label};
  cover.box.position = {0, 0};
  cover.box.size = {1, 1};
  cover.blocksPointer = true;
  cover.enabled = false;
  const auto layout = UI::Layout::create({"Blocker", {button, cover}}).value();
  auto hud = Script::Hud::create(1, UI::Asset{"assets/blocker.dotui", layout}).value();
  Script::HudInput input;
  input.pointer = Math::Vec2{.5F, .5F};
  input.down = input.pressed = true;
  CHECK(hud.publish(input)->pointer);
  CHECK_FALSE(hud.snapshot()->pressed);
  const auto before = hud.buttonRevision().value();
  REQUIRE(hud.perform(R"({"command":"ui-visible","key":"cover","value":false})"));
  CHECK(hud.buttonRevision().value() == before + 1);
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->pressed == "button");
  REQUIRE(hud.perform(R"({"command":"ui-visible","key":"cover","value":true})"));
  input.down = input.pressed = false;
  input.released = true;
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->sequence == 0);
}

TEST_CASE("hud.authored_full_text_batch_fits_escaped_native_and_sdk_wire_bounds") {
  using namespace Dot;
  UI::Description description{"Full batch", {}};
  auto patches = Json::array();

  for (int i = 0; i < 32; ++i) {
    const auto key = std::to_string(i);
    description.nodes.push_back(UI::Node{key, {}, UI::Kind::Label, {}, true, false});
    patches.push_back({{"key", key}, {"text", std::string(1024, '"')}});
  }

  auto hud = Script::Hud::create(
                 1, UI::Asset{"assets/batch.dotui", UI::Layout::create(description).value()})
                 .value();
  const auto request = Json{{"command", "ui-update"}, {"changes", patches}}.dump();
  CHECK(request.size() > 65536);
  REQUIRE(hud.perform(request));
  CHECK(hud.snapshot()->labels.back().value.text.size() == 1024);
  const auto before = hud.perform(R"({"command":"ui-snapshot"})").value();
  patches[0]["text"] = std::string(1023, '"') + "é";
  CHECK_FALSE(hud.perform(Json{{"command", "ui-update"}, {"changes", patches}}.dump()));
  CHECK(hud.perform(R"({"command":"ui-snapshot"})").value() == before);
  REQUIRE(hud.perform(R"({"command":"ui-reset"})"));
  patches[0]["text"] = std::string(1024, '"');
  auto bindings = hud.bindings().value();
  const auto update = std::ranges::find(bindings, "ui.update", &Script::HostFunction::name);
  REQUIRE(update != bindings.end());
  REQUIRE(update->callback(Json::array({patches}).dump()));
  CHECK(hud.snapshot()->labels.front().value.text.size() == 1024);
}

TEST_CASE("hud.owned_snapshots_atomic_boundaries_noops_and_retired_lifetime") {
  using namespace Dot::Script;
  CHECK_FALSE(Hud::create(0));
  auto hud = Hud::create(9007199254740993ULL).value();
  REQUIRE(set(hud, "z-status", label()));
  REQUIRE(set(hud, "a-controls", label("保存 / F6")));
  auto owned = hud.snapshot().value();
  CHECK(owned.scope == 9007199254740993ULL);
  CHECK(owned.labels.front().key == "a-controls");
  owned.labels.front().value.text = "Changed detached copy";
  CHECK(hud.snapshot()->labels.front().value.text == "保存 / F6");
  const auto before = hud.perform(R"({"command":"snapshot"})").value();
  CHECK(Json::parse(before)["scope"] == "9007199254740993");
  REQUIRE(set(hud, "z-status", label()));
  REQUIRE(hud.perform(R"({"command":"remove","key":"missing"})"));
  CHECK(hud.perform(R"({"command":"snapshot"})").value() == before);

  for (const auto& request :
       {R"({"command":"clear","extra":1})", R"({"command":"snapshot","command":"clear"})",
        R"({"command":"set","key":"x","value":{}})", R"({"command":"remove","key":"bad/key"})"}) {
    CHECK_FALSE(hud.perform(request));
    CHECK(hud.perform(R"({"command":"snapshot"})").value() == before);
  }

  for (const auto& [key, value] : std::array<std::pair<std::string, Json>, 4>{
           {{"fontSize", 7}, {"padding", -1}, {"text", "bad\ttext"}, {"visible", 1}}}) {
    auto invalid = label();
    invalid[key] = value;
    CHECK_FALSE(set(hud, "z-status", invalid));
    CHECK(hud.perform(R"({"command":"snapshot"})").value() == before);
  }

  bool wrongThread{};
  std::thread worker([&] {
    wrongThread = !hud.snapshot() && !hud.draws() && !hud.publish(Dot::Script::HudInput{})
                  && !hud.perform(R"({"command":"clear"})");
  });
  worker.join();
  CHECK(wrongThread);
  auto bindings = hud.bindings().value();
  auto moved = std::move(hud);
  CHECK_FALSE(hud.snapshot());
  moved.retire();
  CHECK_FALSE(set(moved, "z-status", label("Retired")));
  CHECK(moved.perform(R"({"command":"snapshot"})").value() == before);
  CHECK(bindings.front().access == HostAccess::ReadOnly);
  REQUIRE(bindings.front().callback("[]"));
  CHECK_FALSE(std::ranges::find(bindings, "hud.clear", &HostFunction::name)->callback("[]"));
}

TEST_CASE("hud.aggregate_capacity_and_script_ranges_reject_without_publishing") {
  auto hud = Dot::Script::Hud::create(1).value();
  const auto full = label(std::string(1024, 'a'));

  for (int i = 0; i < 32; ++i) {
    REQUIRE(set(hud, "label-" + std::to_string(i), full));
  }

  const auto before = hud.perform(R"({"command":"snapshot"})").value();
  CHECK_FALSE(set(hud, "extra", label()));
  std::string wide;

  for (int i = 0; i < 1024; ++i) {
    wide += "é";
  }

  CHECK_FALSE(set(hud, "label-0", label(wide)));
  CHECK(hud.perform(R"({"command":"snapshot"})").value() == before);
  auto box = Dot::Graphics::TextBox{};
  box.text = "中文 / Latin / Ελληνικά\nNext line";
  CHECK(Dot::Graphics::validateTextBox(box));
  box.text = "e\u0301";
  CHECK_FALSE(Dot::Graphics::validateTextBox(box));
  box.text = std::string("bad\0text", 8);
  CHECK_FALSE(Dot::Graphics::validateTextBox(box));
  box.text = "text";
  box.fontSize = std::numeric_limits<float>::infinity();
  CHECK_FALSE(Dot::Graphics::validateTextBox(box));
  std::array<std::byte, 32> invalid{};
  CHECK_FALSE(Dot::Graphics::TextFont::create(invalid));
  REQUIRE(hud.perform(R"({"command":"clear"})"));
  CHECK(hud.snapshot()->labels.empty());
}

namespace {
  auto setButton(Dot::Script::Hud& hud, std::string key, bool enabled = true, bool visible = true) {
    auto value = label("Action / 操作");
    value["enabled"] = enabled;
    value["visible"] = visible;
    value["position"] = {{"x", .1}, {"y", .2}};
    value["size"] = {{"x", .4}, {"y", .2}};
    return hud.perform(Json{{"command", "set-button"}, {"key", key}, {"value", value}}.dump());
  }

  auto events(Dot::Script::Hud& hud, std::string after = "0") -> Json {
    auto result = hud.perform(Json{{"command", "events"}, {"after", after}}.dump());
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return Json::parse(*result);
  }
}  // namespace

TEST_CASE("hud.buttons_share_bounds_key_space_and_render_owned_native_states") {
  using namespace Dot::Script;
  auto hud = Hud::create(1).value();
  REQUIRE(set(hud, "caption", label()));
  REQUIRE(setButton(hud, "action"));
  auto state = hud.snapshot().value();
  REQUIRE(state.buttons.size() == 1);
  CHECK(state.labels.size() == 1);
  state.buttons[0].value.text = "Detached";
  CHECK(hud.snapshot()->buttons[0].value.text == "Action / 操作");
  auto before = hud.draws().value();
  REQUIRE(before.size() == 2);
  REQUIRE(hud.perform(R"({"command":"focus","key":"action"})"));
  auto focused = hud.draws().value();
  CHECK(focused[0].value.background != before[0].value.background);
  CHECK(hud.snapshot()->buttons[0].value.background == before[0].value.background);
  const auto revision = hud.snapshot()->revision;
  REQUIRE(hud.perform(R"({"command":"focus","key":"action"})"));
  REQUIRE(setButton(hud, "action"));
  CHECK(hud.snapshot()->revision == revision);
  auto bad = label();
  bad["enabled"] = 0;
  CHECK_FALSE(
      hud.perform(Json{{"command", "set-button"}, {"key", "action"}, {"value", bad}}.dump()));
  CHECK(hud.snapshot()->revision == revision);
  REQUIRE(setButton(hud, "action", false));
  CHECK_FALSE(hud.snapshot()->focused);
  CHECK_FALSE(hud.perform(R"({"command":"focus","key":"action"})"));
  CHECK_FALSE(hud.perform(R"({"command":"activate","key":"action"})"));
  CHECK(hud.draws()->front().value.color.x < before[0].value.color.x);
  REQUIRE(set(hud, "action", label()));
  CHECK(hud.snapshot()->buttons.empty());

  for (int i = 0; i < 30; ++i) {
    REQUIRE(setButton(hud, "button-" + std::to_string(i)));
  }

  CHECK_FALSE(setButton(hud, "over-capacity"));
  CHECK(hud.draws()->size() == 32);
  REQUIRE(hud.perform(R"({"command":"clear"})"));
  CHECK(hud.draws()->empty());
}

TEST_CASE("hud.pointer_arming_hit_order_cancel_and_model_changes_never_activate_stale_controls") {
  using namespace Dot::Script;
  auto hud = Hud::create(1).value();
  REQUIRE(setButton(hud, "a"));
  REQUIRE(setButton(hud, "z"));
  HudInput input;
  input.pointer = Dot::Math::Vec2{.2F, .3F};
  input.down = input.pressed = true;
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->pressed == "z");
  CHECK(hud.snapshot()->focused == "z");
  input.pressed = false;

  for (int i = 0; i < 5; ++i) {
    REQUIRE(hud.publish(input));
  }

  CHECK(events(hud)["sequence"] == "0");
  input.down = false;
  input.released = true;
  input.pointer.reset();
  REQUIRE(hud.publish(input));
  CHECK(events(hud)["sequence"] == "0");
  input = {};
  input.down = input.pressed = true;
  REQUIRE(hud.publish(input));
  input.pointer = Dot::Math::Vec2{.2F, .3F};
  input.down = input.pressed = false;
  input.released = true;
  REQUIRE(hud.publish(input));
  CHECK(events(hud)["sequence"] == "0");
  input.pressed = true;
  REQUIRE(hud.publish(input));
  CHECK(events(hud)["events"][0]["key"] == "z");
  CHECK(events(hud)["sequence"] == "1");
  input = {};
  input.pointer = Dot::Math::Vec2{.2F, .3F};
  input.pressed = input.down = true;
  REQUIRE(hud.publish(input));
  input.enabled = false;
  input.released = true;
  input.down = input.pressed = false;
  REQUIRE(hud.publish(input));
  CHECK(events(hud)["sequence"] == "1");
  REQUIRE(setButton(hud, "z", false));
  input.enabled = true;
  input.pressed = true;
  auto blocked = hud.publish(input);
  REQUIRE(blocked);
  CHECK(blocked->pointer);
  CHECK(events(hud)["sequence"] == "1");
  REQUIRE(setButton(hud, "z", true, false));
  REQUIRE(hud.publish(input));
  CHECK(events(hud)["events"].back()["key"] == "a");
  CHECK(events(hud)["sequence"] == "2");
  input.released = false;
  input.down = true;
  REQUIRE(hud.publish(input));
  auto description = label("Changed action");
  description["enabled"] = true;
  description["position"] = {{"x", .1}, {"y", .2}};
  description["size"] = {{"x", .4}, {"y", .2}};
  REQUIRE(
      hud.perform(Json{{"command", "set-button"}, {"key", "a"}, {"value", description}}.dump()));
  input.pressed = input.down = false;
  input.released = true;
  REQUIRE(hud.publish(input));
  CHECK(events(hud)["sequence"] == "2");
  auto invalid = input;
  invalid.pointer = Dot::Math::Vec2{1, 0};
  CHECK_FALSE(hud.publish(invalid));
  CHECK(events(hud)["sequence"] == "2");
  auto moved = std::move(hud);
  CHECK_FALSE(hud.publish(input));
  CHECK_FALSE(hud.draws());
  moved.retire();
  CHECK_FALSE(moved.publish(input));
  CHECK(events(moved)["sequence"] == "2");
}

TEST_CASE("hud.keyboard_and_product_activation_share_a_bounded_observational_event_history") {
  using namespace Dot::Script;
  auto hud = Hud::create(1).value();
  REQUIRE(setButton(hud, "a"));
  REQUIRE(setButton(hud, "disabled", false));
  REQUIRE(setButton(hud, "hidden", true, false));
  REQUIRE(setButton(hud, "z"));
  HudInput input;
  input.previous = true;
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->focused == "z");
  input = {};
  input.next = true;
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->focused == "a");
  input.next = false;
  input.confirm = true;
  REQUIRE(hud.publish(input));
  CHECK(events(hud)["events"][0]["key"] == "a");
  input = {};
  REQUIRE(hud.publish(input));
  CHECK(events(hud)["sequence"] == "1");
  input.pointer = Dot::Math::Vec2{.2F, .3F};
  input.pressed = input.released = input.confirm = true;
  REQUIRE(hud.publish(input));
  CHECK(events(hud)["sequence"] == "2");

  for (int i = 0; i < 64; ++i) {
    REQUIRE(hud.perform(R"({"command":"activate","key":"a"})"));
  }

  auto history = events(hud);
  CHECK(history["lost"] == true);
  CHECK(history["sequence"] == "66");
  CHECK(history["events"].size() == 64);
  CHECK(history["events"][0]["sequence"] == "3");
  CHECK(events(hud) == history);
  CHECK(events(hud, "66")["events"].empty());
  CHECK(events(hud, "2")["lost"] == false);
  CHECK_FALSE(hud.perform(R"({"command":"events","after":"67"})"));
  CHECK_FALSE(hud.perform(R"({"command":"events","after":"01"})"));
  CHECK_FALSE(hud.perform(R"({"command":"events","after":0})"));
  auto bindings = hud.bindings().value();
  const auto& read = *std::ranges::find(bindings, "hud.events", &HostFunction::name);
  CHECK(read.access == HostAccess::ReadOnly);
  hud.retire();
  REQUIRE(read.callback(R"(["66"])"));
  CHECK_FALSE(hud.perform(R"({"command":"activate","key":"a"})"));
  CHECK_FALSE(hud.perform(R"({"command":"focus","key":null})"));
}

TEST_CASE("hud.button_revision_tracks_definitions_without_breaking_held_input") {
  using namespace Dot::Script;
  auto hud = Hud::create(1).value();
  CHECK(hud.buttonRevision().value() == 1);
  REQUIRE(set(hud, "caption", label()));
  CHECK(hud.buttonRevision().value() == 1);
  REQUIRE(setButton(hud, "button"));
  const auto revision = hud.buttonRevision().value();
  CHECK(Json::parse(hud.perform(R"({"command":"snapshot"})").value())["buttonRevision"]
        == std::to_string(revision));
  REQUIRE(hud.perform(R"({"command":"focus","key":"button"})"));
  REQUIRE(setButton(hud, "button"));
  HudInput input;
  input.pointer = Dot::Math::Vec2{.2F, .3F};
  input.pressed = input.down = true;
  REQUIRE(hud.publish(input));
  CHECK(hud.buttonRevision().value() == revision);
  CHECK(hud.snapshot()->pressed == "button");
  input.pressed = input.down = false;
  input.released = true;
  REQUIRE(hud.publish(input));
  CHECK(hud.snapshot()->sequence == 1);
  CHECK(hud.buttonRevision().value() == revision);
  REQUIRE(set(hud, "caption", label("Changed status")));
  CHECK(hud.buttonRevision().value() == revision);
  REQUIRE(setButton(hud, "button", false));
  CHECK(hud.buttonRevision().value() == revision + 1);
  REQUIRE(hud.perform(R"({"command":"remove","key":"button"})"));
  CHECK(hud.buttonRevision().value() == revision + 2);
  REQUIRE(hud.perform(R"({"command":"clear"})"));
  CHECK(hud.buttonRevision().value() == revision + 2);
  hud.retire();
  CHECK(hud.buttonRevision().value() == revision + 2);
  bool rejected{};
  std::thread worker([&] { rejected = !hud.buttonRevision(); });
  worker.join();
  CHECK(rejected);
}
