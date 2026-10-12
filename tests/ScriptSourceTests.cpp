#include <doctest/doctest.h>

#include <expected>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Script.Source;
import Dot.Script.Runtime;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot::Script;
  using Json = nlohmann::json;

  auto source() -> ComponentProperties {
    return {"actors/中文🙂.ts", "Actor", "{\"nested\":{\"label\":\"中文🙂\"},\"speed\":2}", false};
  }

  auto document() -> Json {
    auto scene = Dot::World::Scene::create("Scripts");
    REQUIRE(scene);
    auto entity = scene->createEntity();
    REQUIRE(entity);
    REQUIRE(scene->setScript(*entity, source()));
    return Json::parse(Dot::World::encodeScene(*scene).value());
  }
}  // namespace

TEST_CASE("script_source_canonical_properties_and_emitted_module_do_not_create_vm") {
  const auto before = Runtime::backendMemory();
  auto value = source();
  value.properties = " { \"b\": 1, \"a\": [true,null,\"中文🙂\"] } ";
  auto result = normalizeComponentProperties(value);
  REQUIRE(result);
  CHECK(result->properties == "{\"a\":[true,null,\"中文🙂\"],\"b\":1}");
  CHECK(emittedModule(*result).value() == "actors/中文🙂.js");
  result->source = "actors/main.mts";
  CHECK(emittedModule(*result).value() == "actors/main.mjs");
  CHECK(Runtime::backendMemory().bytes == before.bytes);
}

TEST_CASE("script_source_portable_paths_and_export_names_reject_boundaries") {
  for (const auto& path :
       std::vector<std::string>{"", "/a.ts", "../a.ts", "./a.ts", "a//b.ts", "a\\b.ts", "C:/a.ts",
                                "a /b.ts", "a./b.ts", "a.js", "a.tsx", "a.cts", "a.d.ts", "a.d.mts",
                                "a.TS", "a\n.ts", std::string{"a\0.ts", 5}}) {
    auto value = source();
    value.source = path;
    CHECK_FALSE(normalizeComponentProperties(value));
  }

  for (const auto& name :
       std::vector<std::string>{"", "1Actor", "a.b", "中文", "a-b", std::string(129, 'x')}) {
    auto value = source();
    value.className = name;
    CHECK_FALSE(normalizeComponentProperties(value));
  }

  auto value = source();
  value.className = "$Actor_1";
  CHECK(normalizeComponentProperties(value));
  value.className = "default";
  CHECK(normalizeComponentProperties(value));
}

TEST_CASE("script_source_properties_reject_nonobjects_duplicates_nonfinite_unicode_and_depth") {
  for (const auto& text : std::vector<std::string>{
           "", "{", "[]", "null", "42", "{\"a\":1,\"a\":2}", "{\"n\":1e309}", "{\"s\":\"\\ud800\"}",
           std::string{"{}\0", 3}, std::string{"\xc0\x80"}}) {
    auto value = source();
    value.properties = text;
    CHECK_FALSE(normalizeComponentProperties(value));
  }

  auto value = source();
  value.properties = "{\"a\":" + std::string(34, '[') + "0" + std::string(34, ']') + "}";
  CHECK_FALSE(normalizeComponentProperties(value));
  value.properties = "{\"a\":\"" + std::string(64 * 1024 - 8, 'x') + "\"}";
  REQUIRE(value.properties.size() == 64 * 1024);
  CHECK(normalizeComponentProperties(value));
  value.properties.insert(value.properties.size() - 2, 1, 'x');
  CHECK_FALSE(normalizeComponentProperties(value));
}

TEST_CASE(
    "scene_script_snapshots_are_owned_invalid_edits_preserve_state_and_removal_is_idempotent") {
  auto scene = Dot::World::Scene::create();
  REQUIRE(scene);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  CHECK_FALSE(scene->entity(*entity)->script);
  auto value = source();
  REQUIRE(scene->setScript(*entity, value));
  value.source = "changed.ts";
  auto snapshot = scene->entity(*entity);
  REQUIRE(snapshot);
  REQUIRE(snapshot->script);
  CHECK(snapshot->script == source());
  snapshot->script->properties = "{}";
  CHECK(scene->entity(*entity)->script == source());
  value.properties = "[]";
  CHECK_FALSE(scene->setScript(*entity, value));
  CHECK(scene->entity(*entity)->script == source());
  REQUIRE(scene->setScript(*entity, std::nullopt));
  REQUIRE(scene->setScript(*entity, std::nullopt));
  CHECK_FALSE(scene->entity(*entity)->script);
}

TEST_CASE("scene_script_roundtrip_preserves_description_values_and_refreshes_scope") {
  const auto data = document();
  auto scene = Dot::World::decodeScene(data.dump());
  REQUIRE(scene);
  auto entities = scene->entities();
  REQUIRE(entities);
  REQUIRE(entities->size() == 1);
  CHECK(entities->front().script == source());
  CHECK(Json::parse(Dot::World::encodeScene(*scene).value()) == data);
  auto second = Dot::World::decodeScene(data.dump());
  REQUIRE(second);
  CHECK(second->id() != scene->id());
  CHECK(second->entities()->front().id.value == entities->front().id.value);
}

TEST_CASE("scene_script_missing_and_null_fields_preserve_older_scene_compatibility") {
  auto data = document();
  data["entities"][0].erase("script");
  auto older = Dot::World::decodeScene(data.dump());
  REQUIRE(older);
  CHECK_FALSE(older->entities()->front().script);
  data["entities"][0]["script"] = nullptr;
  CHECK(Dot::World::decodeScene(data.dump()));
}

TEST_CASE("scene_script_strict_fields_and_values_reject_malformed_documents") {
  for (const auto& invalid : std::vector<Json>{42, Json::object(), Json{{"source", "actor.ts"}},
                                               Json{{"source", "actor.ts"},
                                                    {"className", "Actor"},
                                                    {"properties", "{}"},
                                                    {"enabled", true}},
                                               Json{{"source", "actor.d.ts"},
                                                    {"className", "Actor"},
                                                    {"properties", Json::object()},
                                                    {"enabled", true}},
                                               Json{{"source", "actor.ts"},
                                                    {"className", "Actor"},
                                                    {"properties", Json::object()},
                                                    {"enabled", 1}},
                                               Json{{"source", "actor.ts"},
                                                    {"className", "Actor"},
                                                    {"properties", Json::object()},
                                                    {"enabled", true},
                                                    {"extra", 1}}}) {
    auto data = document();
    data["entities"][0]["script"] = invalid;
    CHECK_FALSE(Dot::World::decodeScene(data.dump()));
  }
}

TEST_CASE("scene_script_scope_thread_and_moved_owner_reject_without_changes") {
  auto scene = Dot::World::Scene::create();
  auto other = Dot::World::Scene::create();
  REQUIRE(scene);
  REQUIRE(other);
  auto entity = scene->createEntity();
  REQUIRE(entity);
  CHECK_FALSE(other->setScript(*entity, source()));
  bool rejected = false;
  std::thread worker([&] { rejected = !scene->setScript(*entity, source()); });
  worker.join();
  CHECK(rejected);
  CHECK_FALSE(scene->entity(*entity)->script);
  auto moved = std::move(*scene);
  CHECK_FALSE(scene->setScript(*entity, source()));
  REQUIRE(moved.setScript(*entity, source()));
  REQUIRE(moved.destroyEntity(*entity));
  CHECK_FALSE(moved.setScript(*entity, source()));
}
