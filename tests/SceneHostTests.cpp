#include <doctest/doctest.h>

#include <algorithm>
#include <array>
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
import Dot.Physics.Body;
import Dot.Audio.Mixer;
import Dot.Audio.Output;

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

namespace {
  constexpr std::array componentNames{"camera",      "mesh",          "rigidBody",
                                      "audioSource", "audioListener", "script"};

  auto component(const HostBindings& services, Dot::World::EntityId entity, std::string_view name)
      -> Json {
    auto result = invoke(services, "component.get", Json::array({id(entity), name}));
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE(result->has_value());
    return Json::parse(**result);
  }

  auto seedComponents(const std::shared_ptr<Dot::World::Scene>& scene, Dot::World::EntityId entity)
      -> void {
    REQUIRE(scene->setCamera(entity, Dot::World::Camera{}));
    REQUIRE(scene->setMeshRenderer(entity, Dot::World::MeshRenderer{}));
    REQUIRE(scene->setRigidBody(entity, Dot::World::RigidBody{}));
    Dot::World::AudioSource audio;
    audio.asset = "audio/音源.wav";
    REQUIRE(scene->setAudioSource(entity, audio));
    REQUIRE(scene->setAudioListener(entity, Dot::World::AudioListener{}));
    REQUIRE(scene->setScript(entity, Dot::World::ScriptComponent{"actor.ts"}));
  }
}  // namespace

TEST_CASE("component_host_all_descriptions_roundtrip_owned_snapshots_and_null_removal") {
  auto native = scene();
  auto a = native->createEntity("Source").value();
  auto b = native->createEntity("Destination").value();
  auto services = bindings(native);
  auto empty = invoke(services, "entity.get", Json::array({id(b)}));
  REQUIRE((empty && *empty));
  auto snapshot = Json::parse(**empty);
  seedComponents(native, a);

  for (const auto* name : componentNames) {
    CHECK(component(services, b, name).is_null());
    CHECK(snapshot.at(name).is_null());
    auto value = component(services, a, name);
    REQUIRE(invoke(services, "component.set", Json::array({id(b), name, value})));
    CHECK(component(services, b, name) == value);
    value["extra"] = 1;
    CHECK(component(services, b, name) != value);
    auto info = invoke(services, "entity.get", Json::array({id(b)}));
    REQUIRE((info && *info));
    CHECK(Json::parse(**info).at(name) == component(services, b, name));
    REQUIRE(invoke(services, "component.set", Json::array({id(b), name, nullptr})));
    REQUIRE(invoke(services, "component.set", Json::array({id(b), name, nullptr})));
    CHECK(component(services, b, name).is_null());
    CHECK_FALSE(component(services, a, name).is_null());
  }
}

TEST_CASE("component_host_strict_shapes_and_transport_fail_before_native_mutation") {
  auto native = scene();
  auto entity = native->createEntity().value();
  seedComponents(native, entity);
  auto services = bindings(native);

  for (const auto* name : componentNames) {
    auto original = component(services, entity, name);
    std::vector<Json> values{0, true, "", Json::array(), Json::object()};
    auto extra = original;
    extra["unexpected"] = 0;
    values.push_back(extra);

    for (const auto& item : original.items()) {
      auto missing = original;
      missing.erase(item.key());
      values.push_back(missing);
    }

    for (const auto& value : values) {
      auto result = invoke(services, "component.set", Json::array({id(entity), name, value}));
      REQUIRE_FALSE(result);
      CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
      CHECK(component(services, entity, name) == original);
    }
  }

  CHECK_FALSE(invoke(services, "component.get", Json::array({id(entity), "unknown"})));
  CHECK_FALSE(invoke(services, "component.set", Json::array({id(entity), "unknown", nullptr})));
  CHECK_FALSE(invoke(services, "component.set", Json::array({id(entity), 0, nullptr})));
  CHECK_FALSE(invoke(services, "component.set", Json::array({id(entity), "camera"})));
  CHECK_FALSE(invoke(services, "component.get", Json::array({id(entity), "camera", 0})));
  auto method = std::ranges::find(services, "component.set", &HostFunction::name);
  REQUIRE(method != services.end());
  auto duplicate
      = "[" + id(entity).dump() + ",\"audioListener\",{\"primary\":false,\"primary\":true}]";
  auto result = method->callback(duplicate);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
  auto deep = "[" + id(entity).dump() + ",\"script\",{\"source\":\"actor.ts\",\"className\":\"default\","
      "\"enabled\":true,\"properties\":{\"deep\":" + std::string(70, '[') + "0"
      + std::string(70, ']') + "}}]";
  REQUIRE_FALSE(method->callback(deep));
  CHECK(component(services, entity, "audioListener")["primary"] == false);
}

TEST_CASE("component_host_native_numeric_asset_and_property_constraints_preserve_old_state") {
  auto native = scene();
  auto entity = native->createEntity().value();
  seedComponents(native, entity);
  auto services = bindings(native);
  auto invalid = [&](std::string_view name, std::string_view field, Json value) {
    INFO(name, field);
    const auto original = component(services, entity, name);
    auto changed = original;
    changed[Json::json_pointer(std::string(field))] = std::move(value);
    auto result = invoke(services, "component.set", Json::array({id(entity), name, changed}));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
    CHECK(component(services, entity, name) == original);
  };
  invalid("camera", "/projection", "panorama");
  invalid("camera", "/nearPlane", -1);
  invalid("camera", "/farPlane", 0.1);
  invalid("camera", "/verticalFovDegrees", 180);
  invalid("camera", "/primary", 1);
  invalid("camera", "/orthographicHeight", 1e100);
  invalid("mesh", "/kind", "triangle");
  invalid("mesh", "/asset", "cube.obj");
  invalid("mesh", "/tint/x", -1);
  invalid("mesh", "/tint/w", 1e100);
  invalid("mesh", "/visible", "yes");
  invalid("rigidBody", "/motion", "automatic");
  invalid("rigidBody", "/shape", "mesh");
  invalid("rigidBody", "/collisionDetection", "automatic");
  invalid("rigidBody", "/collisionDetection", true);
  invalid("rigidBody", "/halfExtents/x", 0);
  invalid("rigidBody", "/velocity/y", 501);
  invalid("rigidBody", "/mass", 0);
  invalid("rigidBody", "/radius", true);
  invalid("rigidBody", "/restitution", 1.01);
  invalid("audioSource", "/asset", "../unsafe.wav");
  invalid("audioSource", "/voice/pitch", 4.1);
  invalid("audioSource", "/voice/pan", -1.1);
  invalid("audioSource", "/spatial/attenuation", "none");
  invalid("audioSource", "/spatial/minDistance", 1000);
  invalid("audioSource", "/spatial/outerConeDegrees", 359);
  invalid("audioListener", "/primary", "yes");
  invalid("script", "/source", "../actor.ts");
  invalid("script", "/className", "bad-name");
  invalid("script", "/properties", Json::array());
  invalid("script", "/properties/deep", std::string(65536, 'x'));
  invalid("script", "/enabled", 1);
  Json deep = 0;

  for (int level = 0; level < 34; ++level) {
    deep = Json::array({deep});
  }

  invalid("script", "/properties/deep", deep);
}

TEST_CASE("component_host_variant_updates_map_all_fields_to_native_descriptions") {
  auto native = scene();
  auto entity = native->createEntity().value();
  seedComponents(native, entity);
  auto services = bindings(native);
  auto write = [&](std::string_view name, const Json& value) {
    REQUIRE(invoke(services, "component.set", Json::array({id(entity), name, value})));
    CHECK(component(services, entity, name) == value);
  };
  auto camera = component(services, entity, "camera");
  camera.update({{"projection", "orthographic"}, {"orthographicHeight", 24}, {"primary", true}});
  write("camera", camera);
  CHECK(native->entity(entity)->camera->projection == Dot::World::Projection::Orthographic);
  CHECK(native->entity(entity)->camera->orthographicHeight == 24);
  auto mesh = component(services, entity, "mesh");
  mesh.update({{"kind", "asset"}, {"asset", "models/中文.obj"}, {"visible", false}});
  write("mesh", mesh);
  CHECK(native->entity(entity)->mesh->asset == "models/中文.obj");
  mesh.update({{"kind", "quad"}, {"asset", ""}});
  write("mesh", mesh);
  auto body = component(services, entity, "rigidBody");
  body.update({{"motion", "kinematic"},
               {"shape", "capsule"},
               {"mass", 10},
               {"sensor", true},
               {"radius", 2},
               {"halfHeight", 3},
               {"friction", 0},
               {"restitution", 1},
               {"gravityFactor", 0}});
  body["velocity"] = {{"x", 1}, {"y", 2}, {"z", 3}};
  write("rigidBody", body);
  CHECK(native->entity(entity)->rigidBody->velocity.y == 2);
  CHECK(native->entity(entity)->rigidBody->sensor);
  body.update({{"motion", "static"}, {"shape", "box"}});
  body["velocity"] = {{"x", 0}, {"y", 0}, {"z", 0}};
  write("rigidBody", body);
  auto audio = component(services, entity, "audioSource");
  audio["playOnStart"] = false;
  audio["voice"].update({{"volume", 0.5}, {"pitch", 2}, {"looping", true}});
  audio["spatial"].update({{"enabled", true},
                           {"attenuation", "linear"},
                           {"rolloff", 2},
                           {"dopplerFactor", 1},
                           {"innerConeDegrees", 45},
                           {"outerConeDegrees", 90},
                           {"outerConeGain", 0.5}});
  write("audioSource", audio);
  CHECK(native->entity(entity)->audioSource->spatial.enabled);
  CHECK(native->entity(entity)->audioSource->voice.looping);
  audio["spatial"]["attenuation"] = "exponential";
  write("audioSource", audio);
  auto script = component(services, entity, "script");
  script.update({{"source", "other.mts"}, {"className", "Actor"}, {"enabled", false}});
  script["properties"] = {{"nested", Json::array({nullptr, true, 2, "中文"})}};
  write("script", script);
  CHECK(native->entity(entity)->script->className == "Actor");
  CHECK_FALSE(native->entity(entity)->script->enabled);
}

TEST_CASE("component_host_primary_uniqueness_exists_foreign_and_stale_handles") {
  auto native = scene();
  auto a = native->createEntity().value();
  auto b = native->createEntity().value();
  auto services = bindings(native);
  CHECK(invoke(services, "scene.primaryCamera").value().value() == "null");
  CHECK(invoke(services, "scene.primaryListener").value().value() == "null");
  seedComponents(native, a);

  for (const auto& pair : {std::pair{"camera", "scene.primaryCamera"},
                           std::pair{"audioListener", "scene.primaryListener"}}) {
    auto value = component(services, a, pair.first);
    value["primary"] = true;
    REQUIRE(invoke(services, "component.set", Json::array({id(a), pair.first, value})));
    CHECK(Json::parse(invoke(services, pair.second).value().value()) == id(a));
    CHECK_FALSE(invoke(services, "component.set", Json::array({id(b), pair.first, value})));
    CHECK(component(services, b, pair.first).is_null());
    REQUIRE(invoke(services, "component.set", Json::array({id(a), pair.first, nullptr})));
    CHECK(invoke(services, pair.second).value().value() == "null");
    CHECK_FALSE(invoke(services, pair.second, Json::array({0})));
  }

  auto other = scene();
  auto foreign = other->createEntity().value();
  CHECK_FALSE(invoke(services, "entity.exists", Json::array({id(foreign)})));
  CHECK(invoke(services, "entity.exists", Json::array({id(a)})).value().value() == "true");
  REQUIRE(native->destroyEntity(a));
  CHECK(invoke(services, "entity.exists", Json::array({id(a)})).value().value() == "false");

  for (const auto handle : {a, foreign}) {
    for (const auto* name : componentNames) {
      CHECK_FALSE(invoke(services, "component.get", Json::array({id(handle), name})));
      CHECK_FALSE(invoke(services, "component.set", Json::array({id(handle), name, nullptr})));
    }
  }
}

TEST_CASE("component_host_repeated_authoring_retains_scene_only_and_allocates_no_backend") {
  const auto vm = Runtime::backendMemory();
  const auto mixer = Dot::Audio::Mixer::backendMemory();
  const auto output = Dot::Audio::Output::backendMemory();

  for (int iteration = 0; iteration < 30; ++iteration) {
    auto native = scene();
    std::weak_ptr<Dot::World::Scene> weak = native;
    auto entity = native->createEntity().value();
    seedComponents(native, entity);
    auto services = bindings(native);
    native.reset();
    CHECK_FALSE(weak.expired());

    for (const auto* name : componentNames) {
      auto value = component(services, entity, name);
      REQUIRE(invoke(services, "component.set", Json::array({id(entity), name, value})));
    }

    services.clear();
    CHECK(weak.expired());
    CHECK(Runtime::backendMemory().bytes == vm.bytes);
    CHECK(Runtime::backendMemory().allocations == vm.allocations);
    CHECK(Dot::Audio::Mixer::backendMemory().bytes == mixer.bytes);
    CHECK(Dot::Audio::Mixer::backendMemory().allocations == mixer.allocations);
    CHECK(Dot::Audio::Output::backendMemory().bytes == output.bytes);
    CHECK(Dot::Audio::Output::backendMemory().allocations == output.allocations);
  }
}

TEST_CASE("scene_host.named_lookup_is_read_only_exact_and_strict") {
  auto native = scene();
  auto first = native->createEntity("目标").value();
  REQUIRE(native->createEntity("Other"));
  auto last = native->createEntity("目标").value();
  auto services = bindings(native);
  const auto found = std::ranges::find(services, "entity.findNamed", &HostFunction::name);
  REQUIRE(found != services.end());
  CHECK(found->access == HostAccess::ReadOnly);
  auto result = invoke(services, "entity.findNamed", Json::array({"目标"}));
  REQUIRE((result && *result));
  CHECK(Json::parse(**result) == Json::array({id(first), id(last)}));
  CHECK(Json::parse(**invoke(services, "entity.findNamed", Json::array({"missing"}))).empty());

  for (const auto& args : {Json::array(), Json::array({"目标", 1}), Json::array({42}),
                           Json::array({nullptr}), Json::array({""})}) {
    CHECK_FALSE(invoke(services, "entity.findNamed", args));
  }

  REQUIRE(native->renameEntity(first, "Other"));
  CHECK(Json::parse(**invoke(services, "entity.findNamed", Json::array({"目标"})))
        == Json::array({id(last)}));
}

TEST_CASE("scene_host.direct_children_are_read_only_strict_and_keep_scoped_identity") {
  auto native = scene();
  const auto root = native->createEntity("Root").value();
  const auto a = native->createEntity("Duplicate").value();
  const auto b = native->createEntity("Duplicate").value();
  REQUIRE(native->setParent(b, root));
  REQUIRE(native->setParent(a, root));
  auto services = bindings(native);
  const auto method = std::ranges::find(services, "entity.children", &HostFunction::name);
  REQUIRE(method != services.end());
  CHECK(method->access == HostAccess::ReadOnly);
  const auto result = invoke(services, "entity.children", Json::array({id(root)}));
  REQUIRE((result && *result));
  CHECK(Json::parse(**result) == Json::array({id(a), id(b)}));

  for (const auto& args : {Json::array(), Json::array({id(root), 1}), Json::array({42}),
                           Json::array({nullptr}), Json::array({{{"scene", "1"}}})}) {
    CHECK_FALSE(invoke(services, "entity.children", args));
  }

  CHECK_FALSE(invoke(services, "entity.children", Json::array({id({root.scene + 1, root.value})})));
  REQUIRE(native->destroyEntity(a));
  CHECK(Json::parse(**invoke(services, "entity.children", Json::array({id(root)})))
        == Json::array({id(b)}));
  CHECK(Json::parse(**result) == Json::array({id(a), id(b)}));
  REQUIRE(native->destroyEntity(root));
  CHECK_FALSE(invoke(services, "entity.children", Json::array({id(root)})));
}
