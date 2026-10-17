#include <doctest/doctest.h>

#include <algorithm>
#include <expected>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

import Dot.Script.InputSource;
import Dot.Core.Result;
import Dot.Platform.Input;
import Dot.Platform.Events;

namespace {
  using namespace Dot::Script;
  using namespace Dot::Platform;
  using Json = nlohmann::json;

  auto source(const InputSourceConfig& config = {}) -> InputSource {
    auto result = InputSource::create(config);
    REQUIRE(result);
    return std::move(*result);
  }

  auto bindings(const InputSource& input) -> HostBindings {
    auto result = input.bindings();
    REQUIRE(result);
    return std::move(*result);
  }

  auto invoke(const HostBindings& services, std::string_view name, const Json& args = Json::array())
      -> Dot::Core::Result<std::optional<std::string>> {
    const auto found = std::ranges::find(services, name, &HostFunction::name);
    REQUIRE(found != services.end());
    return found->callback(args.dump());
  }

  auto read(const HostBindings& services, std::string_view name, const Json& args = Json::array())
      -> Json {
    auto result = invoke(services, name, args);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE(result->has_value());
    return Json::parse(**result);
  }
}  // namespace

TEST_CASE("input_host_queries_are_readonly_and_capture_is_a_write_request") {
  auto input = source();
  auto services = bindings(input);
  REQUIRE(services.size() == 13);

  for (const auto& service : services) {
    if (service.name == "input.capturePointer" || service.name == "input.requestTextInput") {
      CHECK(service.access == HostAccess::ReadWrite);
      continue;
    }

    CHECK(service.access == HostAccess::ReadOnly);

    if (service.name == "input.textInputRequested") {
      CHECK_FALSE(read(services, service.name).get<bool>());
      continue;
    }

    auto args = service.name == "input.key"      ? Json::array({"A"})
                : service.name == "input.button" ? Json::array({"Left"})
                                                 : Json::array();
    auto result = service.callback(args.dump());
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::Unavailable);
  }
}

TEST_CASE("input_host_pointer_capture_request_is_distinct_from_native_acknowledgement") {
  auto input = source({1});
  auto services = bindings(input);
  CHECK_FALSE(input.pointerCaptureRequested().value());
  REQUIRE(invoke(services, "input.capturePointer", Json::array({true})));
  CHECK(input.pointerCaptureRequested().value());
  CHECK_FALSE(invoke(services, "input.pointerCaptured"));
  InputState native;
  REQUIRE(input.publish(native));
  CHECK_FALSE(read(services, "input.pointerCaptured").get<bool>());
  REQUIRE(input.publish(native, true));
  CHECK(read(services, "input.pointerCaptured").get<bool>());
  CHECK(read(services, "input.snapshot")["pointerCaptured"] == true);

  for (const auto& args :
       {Json::array(), Json::array({1}), Json::array({"true"}), Json::array({false, true})}) {
    CHECK_FALSE(invoke(services, "input.capturePointer", args));
    CHECK(input.pointerCaptureRequested().value());
  }

  REQUIRE(native.consume({0, 0, Text{"xx"}}));
  CHECK_FALSE(input.publish(native, false));
  CHECK(read(services, "input.pointerCaptured").get<bool>());
  CHECK(input.pointerCaptureRequested().value());
  REQUIRE(invoke(services, "input.capturePointer", Json::array({false})));
  CHECK_FALSE(input.pointerCaptureRequested().value());
  CHECK(read(services, "input.pointerCaptured").get<bool>());
  native.beginFrame();
  REQUIRE(input.publish(native));
  CHECK_FALSE(read(services, "input.pointerCaptured").get<bool>());
}

TEST_CASE("input_host_editor_navigation_keys_publish_exact_native_edges") {
  auto input = source();
  auto services = bindings(input);
  InputState native;
  const std::vector mapping{std::pair{"Delete", Key::Delete},
                            std::pair{"Insert", Key::Insert},
                            std::pair{"Home", Key::Home},
                            std::pair{"End", Key::End},
                            std::pair{"PageUp", Key::PageUp},
                            std::pair{"PageDown", Key::PageDown},
                            std::pair{"KeypadEnter", Key::KeypadEnter}};

  for (const auto& [name, key] : mapping) {
    REQUIRE(native.consume({0, 0, KeyChanged{key, true, false}}));
  }

  REQUIRE(input.publish(native));

  for (const auto& [name, key] : mapping) {
    const auto value = read(services, "input.key", Json::array({name}));
    CHECK(value["down"] == true);
    CHECK(value["pressed"] == true);
    CHECK(value["released"] == false);
    REQUIRE(native.consume({0, 0, KeyChanged{key, false, false}}));
  }

  REQUIRE(input.publish(native));

  for (const auto& [name, key] : mapping) {
    CHECK(read(services, "input.key", Json::array({name}))["released"] == true);
  }
}

TEST_CASE("input_host_published_points_text_buttons_and_owned_snapshot_are_real_native_state") {
  InputState native(7);
  REQUIRE(native.consume({7, 0, KeyChanged{Key::A, true, false}}));
  REQUIRE(native.consume({7, 0, ButtonChanged{MouseButton::Left, true}}));
  REQUIRE(native.consume({7, 0, PointerMoved{10, -3, 2, 4}}));
  REQUIRE(native.consume({7, 0, PointerMoved{12, 8, 1, -2}}));
  REQUIRE(native.consume({7, 0, Wheel{0.5F, 2}}));
  REQUIRE(native.consume({7, 0, Wheel{-0.5F, -1}}));
  REQUIRE(native.consume({7, 0, Text{"中文🙂"}}));
  CHECK_FALSE(native.consume({8, 0, KeyChanged{Key::B, true, false}}));
  auto input = source();
  auto services = bindings(input);
  REQUIRE(input.publish(native).value() == 1);
  auto frame = read(services, "input.snapshot");
  CHECK(frame["index"] == "1");
  CHECK(frame["keys"]["A"] == Json{{"down", true}, {"pressed", true}, {"released", false}});
  CHECK(frame["keys"]["B"]["down"] == false);
  CHECK(frame["buttons"]["Left"]["pressed"] == true);
  CHECK(frame["pointer"] == Json{{"x", 12}, {"y", 8}});
  CHECK(frame["pointerDelta"] == Json{{"x", 3}, {"y", 2}});
  CHECK(frame["wheel"] == Json{{"x", 0}, {"y", 1}});
  CHECK(frame["text"] == "中文🙂");

  for (const auto* field : {"index", "pointer", "pointerDelta", "wheel", "text"}) {
    CHECK(read(services, "input." + std::string(field)) == frame[field]);
  }

  frame["keys"]["A"]["down"] = false;
  CHECK(read(services, "input.key", Json::array({"A"}))["down"] == true);
  native.beginFrame();
  CHECK(read(services, "input.text") == "中文🙂");
  REQUIRE(input.publish(native).value() == 2);
  CHECK(read(services, "input.index") == "2");
  CHECK(read(services, "input.text") == "");
  CHECK(read(services, "input.pointerDelta") == Json{{"x", 0}, {"y", 0}});
}

TEST_CASE("input_host_all_native_keys_and_mouse_buttons_have_unique_names_and_exact_states") {
  InputState native;
  auto input = source();
  auto services = bindings(input);
  REQUIRE(input.publish(native));
  auto empty = read(services, "input.snapshot");
  CHECK(empty["keys"].size() == static_cast<std::size_t>(Key::Count) - 1);
  CHECK(empty["buttons"].size() == static_cast<std::size_t>(MouseButton::Count) - 1);

  for (int code = 1; code < static_cast<int>(Key::Count); ++code) {
    InputState single;
    REQUIRE(single.consume({0, 0, KeyChanged{static_cast<Key>(code), true, false}}));
    REQUIRE(input.publish(single));
    auto frame = read(services, "input.snapshot");
    int pressed = 0;

    for (const auto& item : frame["keys"].items()) {
      CHECK(read(services, "input.key", Json::array({item.key()})) == item.value());
      pressed += item.value()["pressed"].get<bool>() ? 1 : 0;
    }

    CHECK(pressed == 1);
  }

  for (int code = 1; code < static_cast<int>(MouseButton::Count); ++code) {
    InputState single;
    REQUIRE(single.consume({0, 0, ButtonChanged{static_cast<MouseButton>(code), true}}));
    REQUIRE(input.publish(single));
    auto frame = read(services, "input.snapshot");
    int pressed = 0;

    for (const auto& item : frame["buttons"].items()) {
      CHECK(read(services, "input.button", Json::array({item.key()})) == item.value());
      pressed += item.value()["pressed"].get<bool>() ? 1 : 0;
    }

    CHECK(pressed == 1);
  }
}

TEST_CASE("input_host_edges_are_non_consuming_with_repeat_focus_and_same_frame_release") {
  InputState native;
  auto input = source();
  auto services = bindings(input);
  REQUIRE(native.consume({0, 0, KeyChanged{Key::Space, true, false}}));
  REQUIRE(native.consume({0, 0, KeyChanged{Key::Space, false, false}}));
  REQUIRE(input.publish(native));
  const auto both = Json{{"down", false}, {"pressed", true}, {"released", true}};
  CHECK(read(services, "input.key", Json::array({"Space"})) == both);
  CHECK(read(services, "input.key", Json::array({"Space"})) == both);
  native.beginFrame();
  REQUIRE(native.consume({0, 0, KeyChanged{Key::Space, true, true}}));
  REQUIRE(native.consume({0, 0, ButtonChanged{MouseButton::Right, true}}));
  REQUIRE(input.publish(native));
  CHECK(read(services, "input.key", Json::array({"Space"}))["pressed"] == false);
  CHECK(read(services, "input.key", Json::array({"Space"}))["down"] == true);
  native.beginFrame();
  REQUIRE(native.consume({0, 0, Focus{false}}));
  REQUIRE(input.publish(native));
  CHECK(read(services, "input.key", Json::array({"Space"}))["released"] == true);
  CHECK(read(services, "input.button", Json::array({"Right"}))["released"] == true);
  CHECK(read(services, "input.button", Json::array({"Right"}))["down"] == false);
}

TEST_CASE("input_host_text_budget_unicode_and_argument_failures_keep_the_published_frame") {
  CHECK_FALSE(InputSource::create({0}));
  CHECK_FALSE(InputSource::create({1024 * 1024 + 1}));
  auto input = source({4});
  auto services = bindings(input);
  InputState native;
  REQUIRE(native.consume({0, 0, Text{"🙂"}}));
  REQUIRE(input.publish(native).value() == 1);
  auto old = read(services, "input.snapshot");
  REQUIRE(native.consume({0, 0, Text{"x"}}));
  CHECK_FALSE(input.publish(native));
  CHECK(read(services, "input.snapshot") == old);
  native.beginFrame();
  REQUIRE(native.consume({0, 0, Text{std::string("\xc0\x80", 2)}}));
  CHECK_FALSE(input.publish(native));
  CHECK(read(services, "input.snapshot") == old);
  native.beginFrame();
  REQUIRE(native.consume({0, 0, Text{std::string("x\0y", 3)}}));
  REQUIRE(input.publish(native).value() == 2);
  CHECK(read(services, "input.text").get<std::string>() == std::string("x\0y", 3));

  for (const auto& service : services) {
    CHECK_FALSE(service.callback("{}"));
    CHECK_FALSE(service.callback("[0,1]"));
    CHECK_FALSE(service.callback(std::string("[]\0", 3)));
    CHECK_FALSE(service.callback(std::string(4097, ' ')));
  }

  for (const auto& value : std::vector<Json>{0, nullptr, "a", "Unknown", "Count", "", "SDL_A"}) {
    auto result = invoke(services, "input.key", Json::array({value}));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
  }

  CHECK_FALSE(invoke(services, "input.button", Json::array({"left"})));
  CHECK_FALSE(invoke(services, "input.key"));
  CHECK_FALSE(invoke(services, "input.snapshot", Json::array({0})));
  CHECK(read(services, "input.index") == "2");
}

TEST_CASE("input_host_moves_thread_checks_and_publisher_release_preserve_binding_ownership") {
  HostBindings services;
  InputState native;

  {
    auto input = source();
    services = bindings(input);
    auto moved = std::move(input);
    CHECK_FALSE(input.bindings());
    CHECK_FALSE(input.publish(native));
    CHECK_FALSE(input.pointerCaptureRequested());
    REQUIRE(moved.publish(native));
    bool rejected = false;
    std::thread worker([&] {
      rejected = !moved.bindings() && !moved.publish(native) && !moved.pointerCaptureRequested()
                 && !invoke(services, "input.snapshot")
                 && !invoke(services, "input.capturePointer", Json::array({true}));
    });
    worker.join();
    CHECK(rejected);
    CHECK(read(services, "input.index") == "1");
  }

  CHECK(read(services, "input.index") == "1");
  REQUIRE(invoke(services, "input.capturePointer", Json::array({true})));
  std::thread release([values = std::move(services)]() mutable { values.clear(); });
  release.join();
}

TEST_CASE("input_host_text_request_is_independent_of_pointer_and_actual_native_mode") {
  auto input = source({1});
  auto services = bindings(input);
  CHECK_FALSE(input.textInputRequested().value());
  REQUIRE(invoke(services, "input.requestTextInput", Json::array({true})));
  CHECK(input.textInputRequested().value());
  CHECK_FALSE(input.pointerCaptureRequested().value());
  CHECK_FALSE(invoke(services, "input.textInputActive"));
  InputState native;
  REQUIRE(input.publish(native, false, false));
  CHECK_FALSE(read(services, "input.textInputActive").get<bool>());
  CHECK(read(services, "input.textInputRequested").get<bool>());
  REQUIRE(input.publish(native, false, true));
  CHECK(read(services, "input.snapshot")["textInputActive"] == true);
  REQUIRE(invoke(services, "input.requestTextInput", Json::array({false})));
  CHECK_FALSE(input.textInputRequested().value());
  CHECK(read(services, "input.textInputActive").get<bool>());
  const auto before = read(services, "input.snapshot");
  REQUIRE(native.consume({0, 0, Text{"xx"}}));
  CHECK_FALSE(input.publish(native, true, false));
  CHECK(read(services, "input.snapshot") == before);

  for (const auto& args : {Json::array(), Json::array({1}), Json::array({true, false})}) {
    CHECK_FALSE(invoke(services, "input.requestTextInput", args));
    CHECK_FALSE(input.textInputRequested().value());
  }
}
