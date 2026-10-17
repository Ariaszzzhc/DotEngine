#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>

import Dot.Script.Hud;
import Dot.Graphics.Text;

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
  std::thread worker(
      [&] { wrongThread = !hud.snapshot() && !hud.perform(R"({"command":"clear"})"); });
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
  CHECK_FALSE(bindings.back().callback("[]"));
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
