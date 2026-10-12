#include <doctest/doctest.h>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

import Dot.Script.SceneHost;
import Dot.Script.Runtime;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.World.Scene;

namespace {
  using namespace Dot::Script;
  using Json = nlohmann::json;

  struct Project {
    std::filesystem::path root;

    Project() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-scene-host-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
    }

    ~Project() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }

    auto files() const -> Dot::Core::FileSystem {
      auto result = Dot::Core::FileSystem::open(root);
      REQUIRE(result);
      return *result;
    }

    auto script(std::string_view code) const -> void {
      REQUIRE(files().writeTextAtomic("main.js", code));
    }
  };

  auto scene() -> std::shared_ptr<Dot::World::Scene> {
    auto created = Dot::World::Scene::create("Script Scene");
    REQUIRE(created);
    return std::make_shared<Dot::World::Scene>(std::move(*created));
  }

  auto bindings(const std::shared_ptr<Dot::World::Scene>& value) -> HostBindings {
    auto result = sceneBindings(value);
    REQUIRE(result);
    return std::move(*result);
  }

  auto invoke(const HostBindings& services, std::string_view method,
              const Json& args = Json::array()) -> Dot::Core::Result<std::optional<std::string>> {
    const auto found = std::ranges::find(services, method, &HostFunction::name);
    REQUIRE(found != services.end());
    return found->callback(args.dump());
  }

  auto id(Dot::World::EntityId value) -> Json {
    return {{"scene", std::to_string(value.scene)}, {"value", std::to_string(value.value)}};
  }

  auto transform() -> Json {
    return {{"position", {{"x", 1}, {"y", 2}, {"z", 3}}},
            {"rotation", {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 2}}},
            {"scale", {{"x", 1}, {"y", 1}, {"z", 1}}}};
  }
}  // namespace

TEST_CASE("scene_host.all_core_methods_mutate_real_scene_and_return_owned_snapshots") {
  auto native = scene();
  auto services = bindings(native);
  auto info = invoke(services, "scene.info");
  REQUIRE((info && *info));
  CHECK(Json::parse(**info).at("entities") == 0);
  REQUIRE(invoke(services, "scene.rename", Json::array({"Renamed"})));
  CHECK(native->name().value() == "Renamed");
  auto created = invoke(services, "entity.create", Json::array({"中文🙂"}));
  REQUIRE((created && *created));
  auto entity = Json::parse(**created);
  auto handle = native->findEntity(std::stoull(entity.at("value").get<std::string>()));
  REQUIRE(handle);
  CHECK(native->entity(*handle)->name == "中文🙂");
  auto first = invoke(services, "entity.get", Json::array({entity}));
  REQUIRE((first && *first));
  CHECK(Json::parse(**first).at("parent").is_null());
  REQUIRE(invoke(services, "entity.rename", Json::array({entity, "Changed"})));
  CHECK(Json::parse(**first).at("name") == "中文🙂");
  CHECK(native->entity(*handle)->name == "Changed");
  REQUIRE(invoke(services, "transform.set", Json::array({entity, transform()})));
  auto local = invoke(services, "transform.get", Json::array({entity}));
  REQUIRE((local && *local));
  CHECK(Json::parse(**local).at("rotation").at("w") == 1);
  auto world = invoke(services, "transform.world", Json::array({entity}));
  REQUIRE((world && *world));
  CHECK(Json::parse(**world).at(12) == 1);
  auto list = invoke(services, "entity.list");
  REQUIRE((list && *list));
  CHECK(Json::parse(**list) == Json::array({entity}));
  REQUIRE(invoke(services, "entity.destroy", Json::array({entity})));
  CHECK(native->entityCount().value() == 0);
  CHECK_FALSE(invoke(services, "entity.get", Json::array({entity})));
}

TEST_CASE("scene_host_hierarchy_cycles_and_subtree_destruction_preserve_native_contracts") {
  auto native = scene();
  auto parent = native->createEntity("Parent");
  auto child = native->createEntity("Child");
  REQUIRE((parent && child));
  auto services = bindings(native);
  REQUIRE(invoke(services, "hierarchy.setParent", Json::array({id(*child), id(*parent)})));
  CHECK(native->entity(*child)->parent == *parent);
  CHECK_FALSE(invoke(services, "hierarchy.setParent", Json::array({id(*parent), id(*child)})));
  CHECK_FALSE(native->entity(*parent)->parent);
  REQUIRE(invoke(services, "hierarchy.setParent", Json::array({id(*child), nullptr})));
  CHECK_FALSE(native->entity(*child)->parent);
  REQUIRE(invoke(services, "hierarchy.setParent", Json::array({id(*child), id(*parent)})));
  REQUIRE(invoke(services, "entity.destroy", Json::array({id(*parent)})));
  CHECK(native->entityCount().value() == 0);
  CHECK_FALSE(invoke(services, "entity.get", Json::array({id(*child)})));
}

TEST_CASE("scene_host_ids_above_js_integer_precision_roundtrip_and_foreign_ids_fail") {
  Project project;
  auto native = scene();
  constexpr std::uint64_t high = 9007199254740993ULL;
  auto entity = native->createEntityWithId("High", high);
  REQUIRE(entity);
  project.script(
      "import {invoke} from 'dot:host';export function run(){const ids=invoke('entity.list',[]);"
      "const value=invoke('entity.get',[ids[0]]);return "
      "[value.id.value,value.id.scene,value.name];}");
  auto runtime = Runtime::create(project.files(), {}, bindings(native));
  REQUIRE(runtime);
  auto module = runtime->loadModule("main.js");
  REQUIRE(module);
  auto result = runtime->call(*module, "run");
  REQUIRE((result && *result));
  CHECK(Json::parse(**result)
        == Json::array({std::to_string(high), std::to_string(native->id()), "High"}));
  auto other = scene();
  auto foreign = other->createEntity("Foreign");
  REQUIRE(foreign);
  CHECK_FALSE(invoke(bindings(native), "entity.get", Json::array({id(*foreign)})));
}

TEST_CASE("scene_host_malformed_ids_and_argument_shapes_fail_before_mutation") {
  auto native = scene();
  auto entity = native->createEntity("Original");
  REQUIRE(entity);
  auto services = bindings(native);

  for (const auto& value : std::vector<Json>{
           0, nullptr, "1", Json{{"scene", "1"}}, Json{{"scene", "1"}, {"value", 1}},
           Json{{"scene", "1"}, {"value", "01"}}, Json{{"scene", "1"}, {"value", "+1"}},
           Json{{"scene", "1"}, {"value", "-1"}}, Json{{"scene", "1"}, {"value", "0"}},
           Json{{"scene", "1"}, {"value", "18446744073709551615"}},
           Json{{"scene", "1"}, {"value", "18446744073709551616"}},
           Json{{"scene", "1"}, {"value", "1"}, {"extra", 0}}}) {
    CHECK_FALSE(invoke(services, "entity.destroy", Json::array({value})));
  }

  CHECK_FALSE(invoke(services, "entity.rename", Json::array({id(*entity), 42})));
  CHECK_FALSE(invoke(services, "entity.destroy", Json::array({id(*entity), 0})));
  CHECK_FALSE(invoke(services, "entity.create", Json::array({""})));
  CHECK_FALSE(invoke(services, "scene.rename", Json::array({42})));
  CHECK_FALSE(invoke(services, "scene.info", Json::array({42})));
  CHECK_FALSE(invoke(services, "entity.list", Json::array({42})));
  CHECK(native->entity(*entity)->name == "Original");
  CHECK(native->entityCount().value() == 1);
}

TEST_CASE("scene_host_complete_transform_validation_preserves_previous_state_on_failure") {
  auto native = scene();
  auto entity = native->createEntity("Entity");
  REQUIRE(entity);
  auto services = bindings(native);
  auto original = native->entity(*entity)->transform;
  std::vector<Json> invalid{nullptr, Json::object()};
  auto missing = transform();
  missing.erase("scale");
  invalid.push_back(missing);
  auto extra = transform();
  extra["extra"] = 1;
  invalid.push_back(extra);
  auto singular = transform();
  singular["scale"]["x"] = 0;
  invalid.push_back(singular);
  auto quaternion = transform();
  quaternion["rotation"]["w"] = 0;
  invalid.push_back(quaternion);
  auto overflow = transform();
  overflow["position"]["x"] = 1e100;
  invalid.push_back(overflow);
  auto boolean = transform();
  boolean["scale"]["y"] = true;
  invalid.push_back(boolean);

  for (const auto& value : invalid) {
    CHECK_FALSE(invoke(services, "transform.set", Json::array({id(*entity), value})));
    auto actual = native->entity(*entity)->transform;
    CHECK(actual.position == original.position);
    CHECK(actual.rotation == original.rotation);
    CHECK(actual.scale == original.scale);
  }
}

TEST_CASE("scene_host_owned_lifetime_moved_scene_and_foreign_thread_are_explicit") {
  CHECK_FALSE(sceneBindings({}));
  auto native = scene();
  std::weak_ptr<Dot::World::Scene> weak = native;
  auto services = bindings(native);
  native.reset();
  CHECK_FALSE(weak.expired());
  CHECK(invoke(services, "scene.info"));
  auto retained = weak.lock();
  REQUIRE(retained);
  bool rejected = false;
  std::thread worker(
      [&] { rejected = !sceneBindings(retained) && !invoke(services, "scene.info"); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(*retained);
  CHECK_FALSE(invoke(services, "scene.info"));
  CHECK_FALSE(sceneBindings(retained));
  retained.reset();
  services.clear();
  CHECK(weak.expired());
  CHECK(moved.name());
}

TEST_CASE("scene_host_direct_callbacks_reject_malformed_transport_without_changes") {
  auto native = scene();
  auto services = bindings(native);
  const auto found = std::ranges::find(services, "entity.create", &HostFunction::name);
  REQUIRE(found != services.end());

  for (const auto& text :
       std::vector<std::string>{"", "{", "{}", "null", "[1e309]", "[\"\\ud800\"]",
                                std::string{"[]\0", 3}, std::string{"\xc0\x80"}}) {
    CHECK_FALSE(found->callback(text));
  }

  CHECK(native->entityCount().value() == 0);
}
