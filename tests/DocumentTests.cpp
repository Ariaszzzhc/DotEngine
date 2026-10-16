#include <doctest/doctest.h>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Project;
import Dot.Editor.Document;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot::Editor;
  using Json = nlohmann::json;

  struct Fixture {
    std::filesystem::path root;

    Fixture() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-document-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
      REQUIRE(Dot::Project::Project::create(Dot::Core::FileSystem::open(root).value(), "game", {}));
    }

    ~Fixture() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }

    auto project() const -> Dot::Project::Project {
      return Dot::Project::Project::open(root / "game").value();
    }

    auto document(const DocumentConfig& config = {}) const -> Document {
      auto result = Document::open(project(), config);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto edit(Document& document, const Json& operations) -> EditReport {
    auto result = document.apply(operations.dump(), document.stats()->revision);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }

  auto rename(std::string name = "Changed") -> Json {
    return Json::array({{{"op", "name"}, {"entity", "2"}, {"name", std::move(name)}}});
  }

  auto state(const Document& document) -> Json {
    return Json::parse(document.snapshot().value());
  }

  auto authored(Json scene) -> Json {
    scene.erase("nextEntity");
    return scene;
  }

  auto revision(const Document& document) -> std::uint64_t {
    return document.stats()->revision;
  }

  auto request(Document& document, Json value) -> Json {
    if (value["command"] != "inspect") {
      value["revision"] = std::to_string(revision(document));
    }

    auto result = document.dispatch(value.dump());
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return Json::parse(*result);
  }
}  // namespace

TEST_CASE("document_inspector_prepare_preserves_state_and_commits_primary_transfers_once") {
  Fixture fixture;
  auto document = fixture.document();
  edit(document, Json::array({{{"op", "component"},
                               {"entity", "1"},
                               {"component", "audioListener"},
                               {"value", {{"primary", true}}}}}));
  REQUIRE(document.select(document.scene().value()->findEntity(2).value(), revision(document)));
  const auto before = state(document);
  const auto scope = document.stats()->scene;
  auto draft = before["scene"]["entities"][1];
  draft["name"] = "属性草稿";
  draft["transform"]["position"] = Json::array({1.25, 2.5, -3.75});
  draft["camera"] = before["scene"]["entities"][0]["camera"];
  draft["audioListener"] = {{"primary", true}};
  auto prepared = document.prepareEntityEdit(draft.dump(), revision(document));
  REQUIRE_MESSAGE(prepared, (prepared ? "" : prepared.error().message));
  CHECK(state(document) == before);
  CHECK(document.stats()->scene == scope);
  REQUIRE(document.apply(*prepared, revision(document)));
  const auto after = state(document);
  CHECK(after["historyEntries"].get<unsigned>() == before["historyEntries"].get<unsigned>() + 1);
  CHECK(after["scene"]["entities"][1] == draft);
  CHECK(after["scene"]["entities"][0]["camera"]["primary"] == false);
  CHECK(after["scene"]["entities"][0]["audioListener"]["primary"] == false);
  CHECK(after["selected"] == "2");
  REQUIRE(document.undo(revision(document)));
  CHECK(state(document)["scene"] == before["scene"]);
  REQUIRE(document.redo(revision(document)));
  CHECK(state(document)["scene"] == after["scene"]);
  REQUIRE(document.save(revision(document)));
  CHECK(state(fixture.document())["scene"] == after["scene"]);
}

TEST_CASE("document_inspector_prepare_rejects_invalid_drafts_without_mutation") {
  Fixture fixture;
  auto document = fixture.document();
  const auto before = state(document);
  const auto scope = document.stats()->scene;
  const auto draft = before["scene"]["entities"][1];
  std::vector<Json> invalid;
  auto bad = draft;
  bad["id"] = "999";
  invalid.push_back(bad);
  bad = draft;
  bad.erase("camera");
  invalid.push_back(bad);
  bad = draft;
  bad["unexpected"] = true;
  invalid.push_back(bad);
  bad = draft;
  bad["parent"] = "2";
  invalid.push_back(bad);
  bad = draft;
  bad["transform"]["position"][0] = "zero";
  invalid.push_back(bad);
  bad = draft;
  bad["transform"]["scale"][0] = 0;
  invalid.push_back(bad);
  bad = draft;
  bad["audioListener"] = {{"primary", true}, {"unknown", false}};
  invalid.push_back(bad);

  for (const auto& value : invalid) {
    INFO(value.dump());
    CHECK_FALSE(document.prepareEntityEdit(value.dump(), revision(document)));
    CHECK(state(document) == before);
    CHECK(document.stats()->scene == scope);
  }

  CHECK_FALSE(document.prepareEntityEdit(draft.dump(), revision(document) + 1));
  CHECK_FALSE(document.prepareEntityEdit("{\"id\":\"2\",\"id\":\"2\"}", revision(document)));
  CHECK_FALSE(document.prepareEntityEdit(std::string(1024 * 1024 + 1, ' '), revision(document)));
  bool rejected = false;
  const auto version = revision(document);
  std::thread foreign([&] { rejected = !document.prepareEntityEdit(draft.dump(), version); });
  foreign.join();
  CHECK(rejected);
  CHECK(state(document) == before);
  auto moved = std::move(document);
  CHECK_FALSE(document.prepareEntityEdit(draft.dump(), version));
  CHECK(state(moved) == before);
}

TEST_CASE("document_inspector_noop_and_stale_prepared_plan_preserve_history") {
  Fixture fixture;
  auto document = fixture.document();
  const auto before = state(document);
  auto draft = before["scene"]["entities"][1];
  auto noop = document.prepareEntityEdit(draft.dump(), revision(document));
  REQUIRE(noop);
  CHECK(Json::parse(*noop).empty());
  CHECK(state(document) == before);
  draft["name"] = "Prepared";
  const auto version = revision(document);
  auto prepared = document.prepareEntityEdit(draft.dump(), version);
  REQUIRE(prepared);
  edit(document, rename("Intervening edit"));
  const auto newer = state(document);
  CHECK_FALSE(document.apply(*prepared, version));
  CHECK(state(document) == newer);
  CHECK(Json::parse(*prepared)[0]["name"] == "Prepared");
}

TEST_CASE("document_character_agent_edits_history_and_save_share_strict_components") {
  Fixture fixture;
  auto document = fixture.document();
  const Json character{{"radius", .35},      {"halfHeight", .55},   {"maxSlopeDegrees", 50},
                       {"stepHeight", .35},  {"stickDistance", .3}, {"mass", 75},
                       {"maxStrength", 100}, {"jumpSpeed", 6},      {"maxContacts", 256}};
  const Json operation{
      {"op", "component"}, {"entity", "2"}, {"component", "character"}, {"value", character}};
  auto edited = request(document, {{"command", "edit"}, {"operations", Json::array({operation})}});
  CHECK(edited["scene"]["entities"][1]["character"]["maxContacts"] == 256);
  const auto before = state(document);
  auto bad = operation;
  bad["value"]["maxContacts"] = 4294967297ULL;
  CHECK_FALSE(document.apply(Json::array({bad}).dump(), revision(document)));
  CHECK(state(document) == before);
  REQUIRE(document.undo(revision(document)));
  CHECK(state(document)["scene"]["entities"][1]["character"].is_null());
  REQUIRE(document.redo(revision(document)));
  REQUIRE(document.save(revision(document)));
  auto reopened = fixture.document();
  CHECK(state(reopened)["scene"]["entities"][1]["character"]
        == state(document)["scene"]["entities"][1]["character"]);
}

TEST_CASE("document_open_selection_owned_snapshot_and_stale_requests") {
  Fixture fixture;
  auto document = fixture.document();
  auto initial = state(document);
  CHECK(initial["revision"] == "1");
  CHECK(initial["nextEntity"] == "3");
  CHECK_FALSE(initial["dirty"].get<bool>());
  CHECK_FALSE(document.undo(1));
  CHECK_FALSE(document.redo(1));
  auto entity = document.scene().value()->findEntity(2).value();
  REQUIRE(document.select(entity, 1));
  CHECK(document.stats()->selected == entity);
  CHECK(revision(document) == 2);
  REQUIRE(document.select(entity, 2));
  CHECK(revision(document) == 2);
  CHECK_FALSE(document.stats()->dirty);
  CHECK_FALSE(document.stats()->canUndo);
  CHECK_FALSE(document.apply(rename().dump(), 1));
  CHECK_FALSE(document.save(1));
  CHECK_FALSE(document.select({Dot::World::EntityId{entity.scene + 1, entity.value}}, 2));
  initial["scene"]["entities"].clear();
  CHECK(document.scene().value()->entityCount().value() == 2);
  REQUIRE(document.select({}, 2));
  CHECK_FALSE(document.stats()->selected);
  CHECK(revision(document) == 3);
}

TEST_CASE("document_atomic_batch_authors_every_component_and_hierarchy") {
  Fixture fixture;
  auto seed = Dot::World::Scene::create("Seed").value();
  auto id = seed.createEntity().value();
  REQUIRE(seed.setCamera(id, Dot::World::Camera{}));
  REQUIRE(seed.setMeshRenderer(id, Dot::World::MeshRenderer{}));
  REQUIRE(seed.setRigidBody(id, Dot::World::RigidBody{}));
  Dot::World::AudioSource audio;
  audio.asset = "assets/tone.wav";
  REQUIRE(seed.setAudioSource(id, audio));
  REQUIRE(seed.setAudioListener(id, Dot::World::AudioListener{true}));
  REQUIRE(seed.setScript(id, Dot::World::ScriptComponent{"mover.ts", "Mover", "{\"speed\":2}"}));
  auto encoded = Json::parse(Dot::World::encodeScene(seed).value())["entities"][0];
  auto document = fixture.document();
  Json operations = Json::array({{{"op", "create"}, {"name", "Child"}},
                                 {{"op", "parent"}, {"entity", "3"}, {"parent", "2"}},
                                 {{"op", "scene-name"}, {"name", "Authored"}}});
  encoded["transform"]["position"] = {7, 1, 0};
  operations.push_back({{"op", "transform"}, {"entity", "3"}, {"value", encoded["transform"]}});

  for (const auto* component :
       {"camera", "mesh", "rigidBody", "audioSource", "audioListener", "script"}) {
    operations.push_back({{"op", "component"},
                          {"entity", "3"},
                          {"component", component},
                          {"value", encoded[component]}});
  }

  auto report = edit(document, operations);
  CHECK(report.changed);
  CHECK(report.created == std::vector<std::uint64_t>{3});
  CHECK(revision(document) == 2);
  const auto* scene = document.scene().value();
  auto child = scene->entity(scene->findEntity(3).value()).value();
  CHECK(child.transform.position.x == 7);
  CHECK(child.parent->value == 2);
  CHECK(child.camera.has_value());
  CHECK(child.mesh.has_value());
  CHECK(child.rigidBody.has_value());
  CHECK(child.audioSource == audio);
  CHECK(child.audioListener->primary);
  CHECK(child.script->properties == "{\"speed\":2}");
  CHECK(scene->name().value() == "Authored");
  CHECK(document.stats()->dirty);
}

TEST_CASE("document_invalid_later_operations_and_final_scene_validation_are_transactional") {
  Fixture fixture;
  auto document = fixture.document();
  auto before = document.snapshot().value();
  std::vector<Json> bad{
      {{"op", "name"}, {"entity", "2"}, {"name", ""}},
      {{"op", "component"}, {"entity", "2"}, {"component", "mesh"}, {"value", Json::object()}},
      {{"op", "parent"}, {"entity", "2"}, {"parent", "2"}},
      {{"op", "remove"}, {"entity", "999"}},
      {{"op", "component"}, {"entity", "2"}, {"component", "unknown"}, {"value", nullptr}},
      {{"op", "name"}, {"entity", "02"}, {"name", "Changed"}},
      {{"op", "transform"}, {"entity", "2"}, {"value", nullptr}},
      {{"op", "create"}, {"name", 1}},
      {{"op", "scene-name"}, {"name", false}}};

  for (const auto& failure : bad) {
    INFO(failure.dump());
    auto commands = rename();
    commands.push_back({{"op", "create"}, {"name", "Pending"}});
    commands.push_back(failure);
    REQUIRE_FALSE(document.apply(commands.dump(), 1));
    CHECK(document.snapshot().value() == before);
  }

  auto report = edit(document, Json::array({{{"op", "create"}, {"name", "Created"}}}));
  CHECK(report.created == std::vector<std::uint64_t>{3});
}

TEST_CASE("document_undo_redo_branch_preserves_selection_and_never_reuses_published_values") {
  Fixture fixture;
  auto document = fixture.document();
  auto initialScene = document.stats()->scene;
  auto cube = document.scene().value()->findEntity(2).value();
  REQUIRE(document.select(cube, revision(document)));
  edit(document, Json::array({{{"op", "create"}, {"name", "First"}}}));
  CHECK_FALSE(document.scene().value()->entity(cube));
  auto current = document.stats()->selected.value();
  CHECK(current.value == 2);
  CHECK(current.scene != initialScene);
  REQUIRE(document.undo(revision(document)));
  CHECK_FALSE(document.stats()->dirty);
  CHECK(document.scene().value()->entityCount().value() == 2);
  CHECK(document.stats()->selected->value == 2);
  REQUIRE(document.redo(revision(document)));
  CHECK(document.scene().value()->findEntity(3));
  REQUIRE(document.undo(revision(document)));
  auto report = edit(document, Json::array({{{"op", "create"}, {"name", "Branch"}}}));
  CHECK(report.created == std::vector<std::uint64_t>{4});
  CHECK_FALSE(document.stats()->canRedo);
  CHECK_FALSE(document.scene().value()->findEntity(3));
  CHECK(document.stats()->nextEntity == 5);
}

TEST_CASE("document_subtree_removal_restores_selected_descendants_on_undo") {
  Fixture fixture;
  auto document = fixture.document();
  edit(document, Json::array({{{"op", "create"}, {"name", "Child"}},
                              {{"op", "create"}, {"name", "Grandchild"}},
                              {{"op", "parent"}, {"entity", "3"}, {"parent", "2"}},
                              {{"op", "parent"}, {"entity", "4"}, {"parent", "3"}}}));
  REQUIRE(document.select(document.scene().value()->findEntity(4).value(), revision(document)));
  edit(document, Json::array({{{"op", "remove"}, {"entity", "2"}}}));
  CHECK(document.scene().value()->entityCount().value() == 1);
  CHECK_FALSE(document.stats()->selected);
  REQUIRE(document.undo(revision(document)));
  CHECK(document.scene().value()->entityCount().value() == 4);
  CHECK(document.stats()->selected->value == 4);
  REQUIRE(document.redo(revision(document)));
  CHECK_FALSE(document.stats()->selected);
}

TEST_CASE("document_noop_preserves_scope_revision_history_and_redo") {
  Fixture fixture;
  auto document = fixture.document();
  edit(document, rename());
  REQUIRE(document.undo(revision(document)));
  auto before = document.snapshot().value();
  CHECK_FALSE(edit(document, rename("Cube")).changed);
  CHECK(document.snapshot().value() == before);
  CHECK_FALSE(edit(document, Json::array({{{"op", "create"}, {"name", "Temporary"}},
                                          {{"op", "remove"}, {"entity", "3"}}}))
                  .changed);
  CHECK(document.snapshot().value() == before);
  REQUIRE(document.redo(revision(document)));
}

TEST_CASE("document_history_count_bytes_eviction_and_invalid_capacities") {
  Fixture fixture;

  for (auto config : {DocumentConfig{0, 1024}, DocumentConfig{1025, 1024}, DocumentConfig{1, 0},
                      DocumentConfig{1, 512ULL * 1024 * 1024 + 1}}) {
    CHECK_FALSE(Document::open(fixture.project(), config));
  }

  auto document = fixture.document({2, 1024 * 1024});
  edit(document, rename("A"));
  edit(document, rename("B"));
  CHECK(document.stats()->historyEntries == 2);
  REQUIRE(document.undo(revision(document)));
  CHECK_FALSE(document.undo(revision(document)));
  CHECK(state(document)["scene"]["entities"][1]["name"] == "A");
  const auto bytes = fixture.document().stats()->historyBytes;
  CHECK_FALSE(Document::open(fixture.project(), {128, bytes - 1}));
  auto bounded = fixture.document({128, bytes});
  edit(bounded, rename("A"));
  CHECK(bounded.stats()->historyBytes <= bytes);
  CHECK(bounded.stats()->historyEntries == 1);
  CHECK_FALSE(bounded.stats()->canUndo);
  auto before = bounded.snapshot().value();
  CHECK_FALSE(bounded.apply(rename(std::string(256, 'x')).dump(), revision(bounded)));
  CHECK(bounded.snapshot().value() == before);
}

TEST_CASE("document_saved_baseline_dirty_state_and_failed_external_scene_save") {
  Fixture fixture;
  auto document = fixture.document();
  edit(document, rename());
  REQUIRE(document.save(revision(document)));
  CHECK_FALSE(document.stats()->dirty);
  CHECK(fixture.project().loadStartup()->entities()->at(1).name == "Changed");
  REQUIRE(document.undo(revision(document)));
  CHECK(document.stats()->dirty);
  REQUIRE(document.redo(revision(document)));
  CHECK_FALSE(document.stats()->dirty);
  edit(document, rename("Pending"));
  auto disk = fixture.project().loadStartup().value();
  REQUIRE(disk.rename("External"));
  REQUIRE(fixture.project().saveStartup(disk));
  auto before = document.snapshot().value();
  auto saved = document.save(revision(document));
  REQUIRE_FALSE(saved);
  CHECK(saved.error().code == Dot::Core::ErrorCode::InvalidState);
  CHECK(document.snapshot().value() == before);
  CHECK(fixture.project().loadStartup()->name().value() == "External");
}

TEST_CASE("document_metadata_and_missing_startup_conflicts_preserve_unsaved_history") {
  Fixture fixture;
  auto document = fixture.document();
  edit(document, rename());
  auto before = document.snapshot().value();
  auto project = fixture.project();
  auto description = project.description().value();
  description.width = 640;
  REQUIRE(project.save(description));
  CHECK_FALSE(document.save(revision(document)));
  CHECK(document.snapshot().value() == before);
  description.width = 1280;
  REQUIRE(project.save(description));
  std::filesystem::remove(fixture.root / "game/scenes/main.dotscene");
  CHECK_FALSE(document.save(revision(document)));
  CHECK(document.snapshot().value() == before);
}

TEST_CASE("document_moves_stable_scene_borrows_owner_thread_and_exhausted_entity_values") {
  Fixture fixture;
  auto original = fixture.document();
  auto borrowed = original.scene().value();
  auto document = std::move(original);
  CHECK_FALSE(original.scene());
  CHECK_FALSE(original.undo(1));
  CHECK(document.scene().value() == borrowed);
  bool rejected = false;
  std::thread worker([&] {
    rejected = !document.snapshot() && !document.apply(rename().dump(), 1) && !document.save(1)
               && !document.dispatch("{\"command\":\"inspect\"}");
  });
  worker.join();
  CHECK(rejected);
  edit(document, rename());
  CHECK(document.scene().value() == borrowed);
  auto large = Dot::World::Scene::create().value();
  REQUIRE(large.createEntityWithId("Last", std::numeric_limits<std::uint64_t>::max() - 1));
  REQUIRE(fixture.project().saveStartup(large));
  auto exhausted = fixture.document();
  CHECK_FALSE(exhausted.stats()->nextEntity);
  auto before = exhausted.snapshot().value();
  CHECK_FALSE(exhausted.apply("[{\"op\":\"create\",\"name\":\"Never\"}]", 1));
  CHECK(exhausted.snapshot().value() == before);
}

TEST_CASE("document_protocol_inspect_edit_select_undo_redo_save_share_native_operations") {
  Fixture fixture;
  auto document = fixture.document();
  CHECK(request(document, {{"command", "inspect"}})["ok"] == true);
  auto created = request(
      document,
      {{"command", "edit"}, {"operations", Json::array({{{"op", "create"}, {"name", "New"}}})}});
  CHECK(created["created"] == Json::array({"3"}));
  CHECK(request(document, {{"command", "select"}, {"entity", "3"}})["selected"] == "3");
  CHECK(request(document, {{"command", "undo"}})["scene"]["entities"].size() == 2);
  CHECK(request(document, {{"command", "redo"}})["scene"]["entities"].size() == 3);
  CHECK_FALSE(request(document, {{"command", "save"}})["dirty"].get<bool>());
  CHECK(fixture.project().loadStartup()->entityCount().value() == 3);
}

TEST_CASE("document_json_transport_schema_limits_and_revisions_reject_without_effects") {
  Fixture fixture;
  auto document = fixture.document();
  auto before = document.snapshot().value();

  for (const auto& request : std::vector<std::string>{
           "", "[]", "[", "{}", "{\"command\":7}", "{\"command\":\"inspect\",\"extra\":1}",
           "{\"command\":\"inspect\",\"command\":\"save\"}",
           "{\"command\":\"undo\",\"revision\":1}", "{\"command\":\"undo\",\"revision\":\"01\"}",
           "{\"command\":\"save\",\"revision\":\"2\"}", "{\"command\":\"missing\"}",
           std::string(1024 * 1024 + 1, ' '), std::string("{}\0", 3), "\xc0\x80"}) {
    CHECK_FALSE(document.dispatch(request));
    CHECK(document.snapshot().value() == before);
  }

  for (const auto& operations : std::vector<std::string>{
           "[]", "{}", "[{}]", "[null]", "[{\"op\":\"create\",\"name\":\"N\",\"id\":\"3\"}]",
           "[{\"op\":\"create\",\"op\":\"remove\",\"name\":\"N\"}]",
           Json(std::vector<Json>(257, rename()[0])).dump(),
           std::string(65, '[') + "0" + std::string(65, ']')}) {
    CHECK_FALSE(document.apply(operations, 1));
    CHECK(document.snapshot().value() == before);
  }
}

TEST_CASE("document_validates_final_primary_camera_state_and_component_removal") {
  Fixture fixture;
  auto document = fixture.document();
  auto camera = state(document)["scene"]["entities"][0]["camera"];
  auto inactive = camera;
  inactive["primary"] = false;
  edit(document,
       Json::array(
           {{{"op", "component"}, {"entity", "2"}, {"component", "camera"}, {"value", camera}},
            {{"op", "component"}, {"entity", "1"}, {"component", "camera"}, {"value", inactive}}}));
  CHECK(document.scene().value()->primaryCamera()->value == 2);
  edit(document,
       Json::array(
           {{{"op", "component"}, {"entity", "2"}, {"component", "mesh"}, {"value", nullptr}},
            {{"op", "component"}, {"entity", "2"}, {"component", "script"}, {"value", nullptr}}}));
  auto cube
      = document.scene().value()->entity(document.scene().value()->findEntity(2).value()).value();
  CHECK_FALSE(cube.mesh);
  CHECK_FALSE(cube.script);
  REQUIRE(document.undo(revision(document)));
  cube = document.scene().value()->entity(document.scene().value()->findEntity(2).value()).value();
  CHECK(cube.mesh.has_value());
  CHECK(cube.script.has_value());
}

TEST_CASE("document_failed_batch_preserves_selection_and_repeated_history_roundtrips") {
  Fixture fixture;
  auto document = fixture.document();
  REQUIRE(document.select(document.scene().value()->findEntity(2).value(), revision(document)));
  const auto baseline = state(document)["scene"];

  for (int iteration = 0; iteration < 10; ++iteration) {
    auto before = document.snapshot().value();
    auto bad = Json::array(
        {{{"op", "remove"}, {"entity", "2"}}, {{"op", "name"}, {"entity", "2"}, {"name", "Gone"}}});
    CHECK_FALSE(document.apply(bad.dump(), revision(document)));
    CHECK(document.snapshot().value() == before);
    edit(document, rename());
    REQUIRE(document.undo(revision(document)));
    CHECK(state(document)["scene"] == baseline);
    CHECK(document.stats()->selected->value == 2);
    REQUIRE(document.redo(revision(document)));
    REQUIRE(document.undo(revision(document)));
  }
}

TEST_CASE("document_material_and_light_components_share_history_save_and_agent_validation") {
  Fixture fixture;
  auto document = fixture.document();
  Json material{{"asset", "assets/steel.dotmaterial"}};
  Json light{{"kind", "point"},        {"color", {1, 0.5, 0.25}}, {"intensity", 25}, {"range", 12},
             {"innerConeDegrees", 20}, {"outerConeDegrees", 30},  {"enabled", true}};
  light["shadows"] = nullptr;
  const auto operations = Json::array(
      {{{"op", "component"}, {"entity", "2"}, {"component", "material"}, {"value", material}},
       {{"op", "component"}, {"entity", "2"}, {"component", "light"}, {"value", light}}});
  edit(document, operations);
  CHECK(state(document)["scene"]["entities"][1]["material"] == material);
  REQUIRE(document.undo(revision(document)));
  CHECK(state(document)["scene"]["entities"][1]["material"].is_null());
  REQUIRE(document.redo(revision(document)));
  CHECK(state(document)["scene"]["entities"][1]["light"] == light);
  const auto before = document.snapshot().value();
  auto invalid = operations;
  invalid[1]["value"]["range"] = -1;
  CHECK_FALSE(document.apply(invalid.dump(), revision(document)));
  CHECK(document.snapshot().value() == before);
  REQUIRE(document.save(revision(document)));
  auto saved = fixture.project().loadStartup().value();
  CHECK(saved.entities()->at(1).material->asset == "assets/steel.dotmaterial");
  CHECK(saved.entities()->at(1).light->kind == Dot::World::LightKind::Point);
  auto shadowed = light;
  shadowed["shadows"]
      = {{"resolution", 256},    {"cascades", 2},       {"distance", 30},      {"splitLambda", 0.5},
         {"depthBias", 0.0002F}, {"slopeBias", 0.001F}, {"normalBias", 0.01F}, {"filterRadius", 3}};
  const auto current = document.snapshot().value();
  auto invalidShadow = shadowed;
  invalidShadow["kind"] = "spot";
  invalidShadow["outerConeDegrees"] = 90;
  CHECK_FALSE(document.apply(Json::array({{{"op", "component"},
                                           {"entity", "2"},
                                           {"component", "light"},
                                           {"value", invalidShadow}}})
                                 .dump(),
                             revision(document)));
  CHECK(document.snapshot().value() == current);
  shadowed["kind"] = "point";
  edit(document,
       Json::array(
           {{{"op", "component"}, {"entity", "2"}, {"component", "light"}, {"value", shadowed}}}));
  REQUIRE(document.undo(revision(document)));
  CHECK(state(document)["scene"]["entities"][1]["light"] == light);
  REQUIRE(document.redo(revision(document)));
  CHECK(state(document)["scene"]["entities"][1]["light"] == shadowed);
  REQUIRE(document.save(revision(document)));
  auto reopened = fixture.document();
  CHECK(state(reopened)["scene"]["entities"][1]["light"] == shadowed);

  for (const auto kind : {"spot", "directional"}) {
    const auto previous = shadowed;
    shadowed["kind"] = kind;
    edit(
        reopened,
        Json::array(
            {{{"op", "component"}, {"entity", "2"}, {"component", "light"}, {"value", shadowed}}}));
    REQUIRE(reopened.undo(revision(reopened)));
    CHECK(state(reopened)["scene"]["entities"][1]["light"] == previous);
    REQUIRE(reopened.redo(revision(reopened)));
    REQUIRE(reopened.save(revision(reopened)));
    auto savedKind = fixture.document();
    CHECK(state(savedKind)["scene"]["entities"][1]["light"] == shadowed);
  }
}

TEST_CASE("document.environment_edits_undo_redo_atomic_failure_save_and_reopen") {
  Fixture fixture;
  auto document = fixture.document();
  Json settings{{"asset", "assets/studio.hdr"}, {"intensity", 2.0},   {"rotationDegrees", 45},
                {"background", false},          {"radianceSize", 16}, {"diffuseSize", 4},
                {"specularSize", 16},           {"brdfSize", 16},     {"samples", 32}};
  const auto operations = Json::array({{{"op", "environment"}, {"value", settings}}});
  edit(document, operations);
  CHECK(state(document)["scene"]["environment"] == settings);
  const auto changed = revision(document);
  auto bad = settings;
  bad["intensity"] = -1;
  CHECK_FALSE(document.apply(Json::array({{{"op", "scene-name"}, {"name", "Must rollback"}},
                                          {{"op", "environment"}, {"value", bad}}})
                                 .dump(),
                             changed));
  CHECK(revision(document) == changed);
  CHECK(state(document)["scene"]["environment"] == settings);
  REQUIRE(document.undo(revision(document)));
  CHECK(state(document)["scene"]["environment"].is_null());
  REQUIRE(document.redo(revision(document)));
  CHECK(state(document)["scene"]["environment"] == settings);
  REQUIRE(document.save(revision(document)));
  auto reopened = fixture.document();
  CHECK(state(reopened)["scene"]["environment"] == settings);
  edit(reopened, Json::array({{{"op", "environment"}, {"value", nullptr}}}));
  CHECK(state(reopened)["scene"]["environment"].is_null());
}

TEST_CASE("document.occlusion_edits_are_atomic_undoable_and_saved") {
  Fixture fixture;
  auto document = fixture.document();
  const Json settings{{"radius", 0.75F},  {"bias", 0.025F}, {"strength", 2.0F},
                      {"directions", 16}, {"steps", 8},     {"filterRadius", 4}};
  edit(document, Json::array({{{"op", "occlusion"}, {"value", settings}}}));
  CHECK(state(document)["scene"]["occlusion"] == settings);
  const auto changed = revision(document);
  auto bad = settings;
  bad["steps"] = 2.5;
  CHECK_FALSE(document.apply(Json::array({{{"op", "scene-name"}, {"name", "No commit"}},
                                          {{"op", "occlusion"}, {"value", bad}}})
                                 .dump(),
                             changed));
  CHECK(revision(document) == changed);
  CHECK(state(document)["scene"]["occlusion"] == settings);
  REQUIRE(document.undo(changed));
  CHECK(state(document)["scene"]["occlusion"].is_null());
  REQUIRE(document.redo(revision(document)));
  REQUIRE(document.save(revision(document)));
  auto reopened = fixture.document();
  CHECK(state(reopened)["scene"]["occlusion"] == settings);
  edit(reopened, Json::array({{{"op", "occlusion"}, {"value", nullptr}}}));
  CHECK(state(reopened)["scene"]["occlusion"].is_null());
}

TEST_CASE("document.multiselection_is_ordered_scoped_atomic_and_budgeted") {
  Fixture fixture;
  auto document = fixture.document();
  const auto original = state(document);
  auto selected = request(document, {{"command", "select-many"}, {"entities", {"2", "1"}}});
  CHECK(selected["selection"] == Json::array({"2", "1"}));
  CHECK(selected["selected"] == "1");
  CHECK(selected["scene"] == original["scene"]);
  CHECK_FALSE(selected["dirty"].get<bool>());
  CHECK(selected["historyEntries"] == 1);
  CHECK(document.stats()->historyBytes
        == std::stoull(original["historyBytes"].get<std::string>()) + 16);
  const auto revisionBefore = revision(document);
  REQUIRE(document.selectMany(document.stats()->selection, revisionBefore));
  CHECK(revision(document) == revisionBefore);
  const auto before = document.snapshot().value();

  for (const auto& entities : {Json::array({"1", "1"}), Json::array({"1", "999"}),
                               Json::array({"01"}), Json::array({1}), Json::object()}) {
    CHECK_FALSE(document.dispatch(Json{
        {"command", "select-many"},
        {"revision", std::to_string(revision(document))},
        {"entities", entities}}.dump()));
    CHECK(document.snapshot().value() == before);
  }

  auto handles = document.stats()->selection;
  handles[0].scene += 1;
  CHECK_FALSE(document.selectMany(handles, revision(document)));
  CHECK_FALSE(document.selectMany({}, revision(document) - 1));
  CHECK(document.snapshot().value() == before);
  bool rejected{};
  std::thread foreign([&] { rejected = !document.selectMany({}, revisionBefore); });
  foreign.join();
  CHECK(rejected);
  REQUIRE(document.selectMany({}, revision(document)));
  CHECK(state(document)["selection"].empty());
  CHECK_FALSE(document.stats()->selected);
  const auto bytes = fixture.document().stats()->historyBytes;
  auto bounded = fixture.document({128, bytes});
  const auto limited = bounded.snapshot().value();
  CHECK_FALSE(bounded.select(bounded.scene().value()->findEntity(2).value(), 1));
  CHECK(bounded.snapshot().value() == limited);
}

TEST_CASE("document.group_duplicate_remaps_overlaps_selection_and_undo_once") {
  Fixture fixture;
  auto document = fixture.document();
  edit(document, Json::array({{{"op", "create"}, {"name", "Child"}},
                              {{"op", "parent"}, {"entity", "3"}, {"parent", "2"}}}));
  request(document, {{"command", "select-many"}, {"entities", {"3", "2"}}});
  const auto before = state(document);
  auto copies = request(document, {{"command", "duplicate-selection"}});
  CHECK(copies["created"] == Json::array({"4", "5"}));
  CHECK(copies["selection"] == Json::array({"5", "4"}));
  CHECK(copies["selected"] == "4");
  CHECK(copies["historyEntries"].get<int>() == before["historyEntries"].get<int>() + 1);
  auto scene = document.scene().value();
  CHECK(scene->entity(scene->findEntity(5).value())->parent->value == 4);
  CHECK(scene->entity(scene->findEntity(4).value())->mesh.has_value());
  CHECK(scene->entity(scene->findEntity(4).value())->name == "Cube Copy");
  REQUIRE(document.undo(revision(document)));
  CHECK(authored(state(document)["scene"]) == authored(before["scene"]));
  CHECK(state(document)["scene"]["nextEntity"] == state(document)["nextEntity"]);
  CHECK(state(document)["selection"] == before["selection"]);
  REQUIRE(document.redo(revision(document)));
  CHECK(state(document)["selection"] == copies["selection"]);
  REQUIRE(document.undo(revision(document)));
  const auto branch = request(document, {{"command", "duplicate-selection"}});
  CHECK(branch["created"] == Json::array({"6", "7"}));
  CHECK_FALSE(document.stats()->canRedo);
  auto removed = request(document, {{"command", "delete-selection"}});
  CHECK(removed["selection"].empty());
  CHECK(removed["scene"]["entities"].size() == 3);
  REQUIRE(document.undo(revision(document)));
  CHECK(state(document)["selection"] == branch["selection"]);
  CHECK(document.scene().value()->entityCount().value() == 5);
}

TEST_CASE("document.group_edits_keep_external_parents_demote_primary_and_rollback") {
  Fixture fixture;
  auto document = fixture.document();
  edit(document, Json::array({{{"op", "parent"}, {"entity", "2"}, {"parent", "1"}}}));
  request(document, {{"command", "select"}, {"entity", "2"}});
  auto child = request(document, {{"command", "duplicate-selection"}});
  CHECK(document.scene()
            .value()
            ->entity(document.scene().value()->findEntity(3).value())
            ->parent->value
        == 1);
  request(document, {{"command", "select"}, {"entity", "1"}});
  auto root = request(document, {{"command", "duplicate-selection"}});
  CHECK(root["created"] == Json::array({"4", "5", "6"}));
  CHECK_FALSE(document.scene()
                  .value()
                  ->entity(document.scene().value()->findEntity(4).value())
                  ->camera->primary);
  CHECK(document.scene().value()->primaryCamera()->value == 1);
  const auto before = document.snapshot().value();
  const auto next = document.stats()->nextEntity;

  for (const auto& bad : {Json{{"op", "remove-many"}, {"entities", {"4", "4"}}},
                          Json{{"op", "duplicate"}, {"entities", {"4", "999"}}},
                          Json{{"op", "remove-many"}, {"entities", Json::array()}}}) {
    CHECK_FALSE(document.apply(
        Json::array({{{"op", "duplicate"}, {"entities", {"2"}}}, bad}).dump(), revision(document)));
    CHECK(document.snapshot().value() == before);
    CHECK(document.stats()->nextEntity == next);
  }

  auto removed = edit(document, Json::array({{{"op", "remove-many"}, {"entities", {"4", "5"}}}}));
  CHECK(removed.changed);
  CHECK(document.scene().value()->entityCount().value() == 3);
  REQUIRE(document.undo(revision(document)));
  CHECK(state(document)["selected"] == "4");
  REQUIRE(document.save(revision(document)));
  CHECK(fixture.project().loadStartup()->entityCount().value() == 6);
}

TEST_CASE("document.group_boundaries_names_capacity_handles_and_opaque_script_data") {
  Fixture fixture;
  auto document = fixture.document();
  std::string name;

  for (int i = 0; i < 85; ++i) {
    name += "晶";
  }

  name += "x";
  edit(document, Json::array({{{"op", "name"}, {"entity", "2"}, {"name", name}},
                              {{"op", "component"},
                               {"entity", "1"},
                               {"component", "audioListener"},
                               {"value", {{"primary", true}}}}}));
  request(document, {{"command", "select-many"}, {"entities", {"1", "2"}}});
  const auto originalScript
      = document.scene().value()->entity(document.scene().value()->findEntity(2).value())->script;
  auto copies = request(document, {{"command", "duplicate-selection"}});
  const auto scene = document.scene().value();
  auto clonedCamera = scene->entity(scene->findEntity(3).value()).value();
  auto clonedMesh = scene->entity(scene->findEntity(4).value()).value();
  CHECK_FALSE(clonedCamera.camera->primary);
  CHECK_FALSE(clonedCamera.audioListener->primary);
  CHECK(clonedMesh.name.size() <= 256);
  CHECK(clonedMesh.name.ends_with(" Copy"));
  CHECK(clonedMesh.script == originalScript);
  const auto before = document.snapshot().value();
  std::vector<Dot::World::EntityId> excessive(10001, scene->findEntity(2).value());
  CHECK_FALSE(document.selectMany(excessive, revision(document)));
  CHECK(document.snapshot().value() == before);
  auto moved = std::move(document);
  CHECK_FALSE(document.selectMany({}, revision(moved)));
  CHECK(moved.stats()->selection.size() == 2);
  auto exhaustedScene = Dot::World::Scene::create().value();
  REQUIRE(exhaustedScene.createEntityWithId("Last", std::numeric_limits<std::uint64_t>::max() - 1));
  REQUIRE(fixture.project().saveStartup(exhaustedScene));
  auto exhausted = fixture.document();
  request(exhausted, {{"command", "select"}, {"entity", "18446744073709551614"}});
  const auto exhaustedState = exhausted.snapshot().value();
  CHECK_FALSE(exhausted.dispatch(
      Json{{"command", "duplicate-selection"}, {"revision", std::to_string(revision(exhausted))}}
          .dump()));
  CHECK(exhausted.snapshot().value() == exhaustedState);
}

TEST_CASE("skin.document_duplicate_instance_and_history_remap_joint_references_transactionally") {
  Fixture fixture;
  auto scene = Dot::World::Scene::create().value();
  const auto root = scene.createEntity("Character").value();
  const auto mesh = scene.createEntity("Mesh").value();
  const auto joint = scene.createEntity("Joint").value();
  REQUIRE(scene.setParent(mesh, root));
  REQUIRE(scene.setParent(joint, root));
  REQUIRE(scene.setMeshRenderer(mesh, Dot::World::MeshRenderer{}));
  REQUIRE(scene.setSkin(mesh, Dot::World::SkinBinding{"assets/rig.dotskin", {joint.value}}));
  REQUIRE(fixture.project().saveStartup(scene));
  auto document = fixture.document();
  const auto before = document.snapshot().value();
  CHECK_FALSE(document.apply(Json::array({{{"op", "duplicate"}, {"entities", {"2"}}}}).dump(),
                             revision(document)));
  CHECK(document.snapshot().value() == before);
  const auto copies = edit(document, Json::array({{{"op", "duplicate"}, {"entities", {"1"}}}}));
  REQUIRE(copies.created.size() == 3);
  auto copied = document.scene().value();
  const auto copiedMesh = copied->entity(copied->findEntity(copies.created[1]).value()).value();
  CHECK(copiedMesh.skin->joints == std::vector<std::uint64_t>{copies.created[2]});
  REQUIRE(document.undo(revision(document)));
  CHECK(document.scene().value()->entityCount().value() == 3);
  REQUIRE(document.redo(revision(document)));
  CHECK(document.scene().value()->entityCount().value() == 6);
  REQUIRE(Dot::World::saveScene(fixture.project().files().value(), "assets/model.dotscene", scene));
  const auto instance = edit(
      document,
      Json::array(
          {{{"op", "instantiate-scene"},
            {"path", "assets/model.dotscene"},
            {"parent", nullptr},
            {"transform",
             {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}}));
  REQUIRE(instance.created.size() == 4);
  copied = document.scene().value();
  CHECK(copied->entity(copied->findEntity(instance.created[2]).value())->skin->joints
        == std::vector<std::uint64_t>{instance.created[3]});
  auto invalid = Json::array(
      {{{"op", "component"},
        {"entity", "2"},
        {"component", "skin"},
        {"value", {{"asset", "assets/rig.dotskin"}, {"joints", {"999"}}, {"enabled", true}}}}});
  const auto stable = document.snapshot().value();
  CHECK_FALSE(document.apply(invalid.dump(), revision(document)));
  CHECK(document.snapshot().value() == stable);
  edit(document,
       Json::array(
           {{{"op", "component"}, {"entity", "2"}, {"component", "skin"}, {"value", nullptr}},
            {{"op", "remove"}, {"entity", "3"}}}));
  REQUIRE(document.save(revision(document)));
  auto saved = fixture.project().loadStartup().value();
  CHECK_FALSE(saved.findEntity(3));
}

TEST_CASE("document_continuous_collision_edits_history_and_save_use_strict_body_contract") {
  Fixture fixture;
  auto seed = Dot::World::Scene::create().value();
  const auto id = seed.createEntity().value();
  REQUIRE(seed.setRigidBody(id, Dot::World::RigidBody{}));
  auto body = Json::parse(Dot::World::encodeScene(seed).value())["entities"][0]["rigidBody"];
  body["collisionDetection"] = "continuous";
  const Json operation{
      {"op", "component"}, {"entity", "2"}, {"component", "rigidBody"}, {"value", body}};
  auto document = fixture.document();
  auto edited = request(document, {{"command", "edit"}, {"operations", Json::array({operation})}});
  CHECK(edited["scene"]["entities"][1]["rigidBody"]["collisionDetection"] == "continuous");
  const auto before = state(document);
  auto bad = operation;
  bad["value"]["sensor"] = true;
  CHECK_FALSE(document.apply(Json::array({bad}).dump(), revision(document)));
  CHECK(state(document) == before);
  REQUIRE(document.undo(revision(document)));
  CHECK(state(document)["scene"]["entities"][1]["rigidBody"].is_null());
  REQUIRE(document.redo(revision(document)));
  REQUIRE(document.save(revision(document)));
  auto reopened = fixture.document();
  CHECK(state(reopened)["scene"]["entities"][1]["rigidBody"]["collisionDetection"] == "continuous");
}

TEST_CASE("document.scene_files_switch_cleanly_keep_scope_guards_and_never_reuse_allocations") {
  Fixture fixture;
  auto document = fixture.document();
  const auto* stable = document.scene().value();
  const auto original = document.snapshot().value();
  const auto path = document.stats()->scenePath;
  CHECK(path == "scenes/main.dotscene");
  const auto oldScope = stable->id();
  auto report = edit(document, Json::array({{{"op", "create"}, {"name", "Reserved"}}}));
  REQUIRE(report.created.size() == 1);
  const auto reserved = report.created[0];
  const auto editedScope = stable->id();
  const auto dirty = document.snapshot().value();
  CHECK_FALSE(document.createScene("scenes/second.dotscene", "Second", revision(document)));
  CHECK(document.snapshot().value() == dirty);
  REQUIRE(document.saveAs("scenes/副本.dotscene", revision(document)));
  CHECK(document.scene().value() == stable);
  CHECK(stable->id() == editedScope);
  CHECK_FALSE(document.stats()->dirty);
  CHECK(document.stats()->canUndo);
  REQUIRE(document.undo(revision(document)));
  CHECK(document.stats()->dirty);
  REQUIRE(document.redo(revision(document)));
  CHECK_FALSE(document.stats()->dirty);
  CHECK(fixture.project().loadStartup()->entities()->size() + 1 == stable->entities()->size());
  const auto beforeSwitch = revision(document);
  REQUIRE(document.createScene("scenes/second.dotscene", "Second", beforeSwitch));
  CHECK(document.scene().value() == stable);
  CHECK(stable->id() != oldScope);
  CHECK(document.stats()->scenePath == "scenes/second.dotscene");
  CHECK_FALSE(document.stats()->canUndo);
  CHECK_FALSE(document.stats()->canRedo);
  CHECK_FALSE(document.stats()->dirty);
  CHECK(document.stats()->selection.empty());
  CHECK(stable->entities()->empty());
  CHECK_FALSE(document.save(beforeSwitch));
  auto created = edit(document, Json::array({{{"op", "create"}, {"name", "New entity"}}}));
  CHECK(created.created.at(0) > reserved);
  CHECK_FALSE(document.openScene(path, revision(document)));
  REQUIRE(document.openScene(path, revision(document), true));
  CHECK(document.stats()->scenePath == path);
  CHECK_FALSE(document.stats()->dirty);
  CHECK(stable->entities()->size() == fixture.project().loadStartup()->entities()->size());
  REQUIRE(document.openScene("scenes/副本.dotscene", revision(document)));
  CHECK(stable->entities()->size() == fixture.project().loadStartup()->entities()->size() + 1);
  REQUIRE(document.openScene("scenes/second.dotscene", revision(document)));
  CHECK(stable->entities()->empty());
  CHECK(Json::parse(original)["scenePath"] == path);
}

TEST_CASE("document.scene_file_failures_preserve_document_and_never_overwrite_targets") {
  Fixture fixture;
  auto document = fixture.document();
  auto files = fixture.project().files().value();
  const auto initial = document.snapshot().value();
  const auto startup = files.readText("scenes/main.dotscene").value();
  REQUIRE(files.writeTextAtomic("scenes/broken.dotscene", "not json"));

  for (const auto* path : {"../escape.dotscene", "other/scene.dotscene", "scenes/alias.DOTSCENE",
                           "scenes/MAIN.dotscene", "scripts/dot/scene.dotscene",
                           "scenes/missing.dotscene", "scenes/broken.dotscene"}) {
    CHECK_FALSE(document.openScene(path, revision(document), true));
    CHECK(document.snapshot().value() == initial);
  }

  for (const auto* path :
       {"scenes/main.dotscene", "scenes/broken.dotscene", "scenes/sub/missing.dotscene",
        "../escape.dotscene", "scripts/saved.dotscene", "scenes/.dot-stage-private.dotscene"}) {
    CHECK_FALSE(document.saveAs(path, revision(document)));
    CHECK_FALSE(document.createScene(path, "Second", revision(document), true));
    CHECK(document.snapshot().value() == initial);
  }

  CHECK(files.readText("scenes/main.dotscene").value() == startup);
  CHECK(files.readText("scenes/broken.dotscene").value() == "not json");
  CHECK_FALSE(document.createScene("scenes/new.dotscene", "", revision(document)));
  CHECK_FALSE(std::filesystem::exists(files.root() / "scenes/new.dotscene"));
  REQUIRE(document.saveAs("scenes/copy.dotscene", revision(document)));
  edit(document, Json::array({{{"op", "scene-name"}, {"name", "Changed"}}}));
  const auto changed = document.snapshot().value();
  REQUIRE(files.writeTextAtomic("scenes/copy.dotscene", startup));
  auto external = Json::parse(startup);
  external["name"] = "External";
  REQUIRE(files.writeTextAtomic("scenes/copy.dotscene", external.dump()));
  CHECK_FALSE(document.save(revision(document)));
  CHECK(document.snapshot().value() == changed);
  CHECK(Json::parse(files.readText("scenes/copy.dotscene").value())["name"] == "External");
  CHECK(files.readText("scenes/main.dotscene").value() == startup);
  REQUIRE(files.writeTextAtomic("scenes/main.dotscene", "broken startup"));
  REQUIRE(document.saveAs("scenes/rescued.dotscene", revision(document)));
  CHECK_FALSE(document.stats()->dirty);
  CHECK(Json::parse(files.readText("scenes/rescued.dotscene").value())["name"] == "Changed");
  CHECK(files.readText("scenes/main.dotscene").value() == "broken startup");
}

TEST_CASE("document.scene_file_commands_are_strict_and_save_the_active_file_only") {
  Fixture fixture;
  auto document = fixture.document();
  auto request = [&](Json value) {
    value["revision"] = std::to_string(revision(document));
    return document.dispatch(value.dump());
  };
  REQUIRE(request({{"command", "create-scene"},
                   {"path", "scenes/level.dotscene"},
                   {"name", "Level"},
                   {"discard", false}}));
  CHECK(Json::parse(document.snapshot().value())["scenePath"] == "scenes/level.dotscene");
  edit(document, Json::array({{{"op", "create"}, {"name", "Authored"}}}));
  REQUIRE(request({{"command", "save"}}));
  REQUIRE(
      request({{"command", "open-scene"}, {"path", "scenes/main.dotscene"}, {"discard", false}}));
  REQUIRE(
      request({{"command", "open-scene"}, {"path", "scenes/level.dotscene"}, {"discard", false}}));
  CHECK(state(document)["scene"]["entities"][0]["name"] == "Authored");
  const auto before = document.snapshot().value();

  for (auto value :
       {Json{{"command", "open-scene"}, {"path", "scenes/main.dotscene"}},
        Json{{"command", "open-scene"}, {"path", "scenes/main.dotscene"}, {"discard", 1}},
        Json{{"command", "save-scene-as"}, {"path", "scenes/extra.dotscene"}, {"extra", true}},
        Json{{"command", "create-scene"},
             {"path", "scenes/extra.dotscene"},
             {"name", 1},
             {"discard", false}}}) {
    CHECK_FALSE(request(value));
    CHECK(document.snapshot().value() == before);
  }
}

TEST_CASE("document.scene_load_budget_thread_and_metadata_failures_preserve_state") {
  Fixture fixture;
  auto files = fixture.project().files().value();
  const auto baseline = Dot::World::encodeScene(fixture.project().loadStartup().value()).value();
  auto document = fixture.document({128, baseline.size()});
  auto large = Dot::World::Scene::create("Large").value();

  for (int i = 0; i < 100; ++i) {
    REQUIRE(large.createEntity("Entity " + std::to_string(i)));
  }

  REQUIRE(files.writeTextAtomic("scenes/large.dotscene", Dot::World::encodeScene(large).value()));
  const auto before = document.snapshot().value();
  CHECK_FALSE(document.openScene("scenes/large.dotscene", revision(document), true));
  CHECK(document.snapshot().value() == before);
  const auto version = revision(document);
  bool opened{}, created{}, copied{};
  std::thread other([&] {
    opened = document.openScene("scenes/main.dotscene", version).has_value();
    created = document.createScene("scenes/thread.dotscene", "Thread", version).has_value();
    copied = document.saveAs("scenes/thread-copy.dotscene", version).has_value();
  });
  other.join();
  CHECK_FALSE(opened);
  CHECK_FALSE(created);
  CHECK_FALSE(copied);
  CHECK(document.snapshot().value() == before);
  auto project = fixture.project();
  auto description = project.description().value();
  description.name = "Externally changed project";
  REQUIRE(project.save(description));
  CHECK_FALSE(document.openScene("scenes/main.dotscene", version));
  CHECK_FALSE(document.createScene("scenes/blocked.dotscene", "Blocked", version));
  CHECK_FALSE(document.saveAs("scenes/blocked-copy.dotscene", version));
  CHECK(document.snapshot().value() == before);
  CHECK_FALSE(std::filesystem::exists(files.root() / "scenes/blocked.dotscene"));
  CHECK_FALSE(std::filesystem::exists(files.root() / "scenes/blocked-copy.dotscene"));
  auto moved = std::move(document);
  CHECK_FALSE(document.openScene("scenes/main.dotscene", version));
  CHECK_FALSE(document.createScene("scenes/moved.dotscene", "Moved", version));
  CHECK_FALSE(document.saveAs("scenes/moved-copy.dotscene", version));
  CHECK(moved.snapshot().value() == before);
}

TEST_CASE(
    "document.hierarchy_group_and_ungroup_preserve_world_selection_history_and_saved_content") {
  Fixture fixture;
  auto document = fixture.document();
  edit(
      document,
      Json::array({{{"op", "create"}, {"name", "Parent"}},
                   {{"op", "transform"},
                    {"entity", "3"},
                    {"value",
                     {{"position", {10, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}},
                   {{"op", "parent"}, {"entity", "2"}, {"parent", "3"}}}));
  request(document, {{"command", "select-many"}, {"entities", {"1", "2"}}});
  const auto before = state(document);
  const auto history = document.stats()->historyEntries;
  const auto oldScope = document.stats()->scene;
  auto grouped = request(document, {{"command", "group-selection"}, {"name", "Gameplay 组"}});
  REQUIRE(grouped["created"].size() == 1);
  const auto group = grouped["created"][0].get<std::string>();
  CHECK(grouped["selection"] == Json::array({group}));
  CHECK(document.stats()->historyEntries == history + 1);
  CHECK(document.stats()->scene != oldScope);
  auto current = document.scene().value();
  CHECK(current->worldTransform(current->findEntity(2).value())->data[12] == 10);
  CHECK(current->worldTransform(current->findEntity(1).value())->data[14] == 4);
  CHECK(current->entity(current->findEntity(1).value())->camera->primary);
  REQUIRE(document.undo(revision(document)));
  CHECK(authored(state(document)["scene"]) == authored(before["scene"]));
  CHECK(state(document)["scene"]["nextEntity"] == state(document)["nextEntity"]);
  CHECK(state(document)["selection"] == before["selection"]);
  REQUIRE(document.redo(revision(document)));
  CHECK(state(document)["selection"] == Json::array({group}));
  auto ungrouped = request(document, {{"command", "ungroup-selection"}});
  CHECK(ungrouped["selection"] == Json::array({"1", "2"}));
  CHECK_FALSE(document.scene().value()->findEntity(std::stoull(group)));
  current = document.scene().value();
  CHECK(current->worldTransform(current->findEntity(2).value())->data[12] == 10);
  REQUIRE(document.undo(revision(document)));
  CHECK(state(document)["selection"] == Json::array({group}));
  REQUIRE(document.save(revision(document)));
  auto saved = fixture.project().loadStartup().value();
  CHECK(saved.entity(saved.findEntity(std::stoull(group)).value())->name == "Gameplay 组");
  CHECK(saved.worldTransform(saved.findEntity(2).value())->data[12] == 10);
  REQUIRE(document.undo(revision(document)));
  auto branched = request(document, {{"command", "group-selection"}, {"name", "New group"}});
  CHECK(std::stoull(branched["created"][0].get<std::string>()) > std::stoull(group));
}

TEST_CASE("document.hierarchy_reparent_minimal_roots_noops_and_failure_preserve_state") {
  Fixture fixture;
  auto document = fixture.document();
  edit(document,
       Json::array(
           {{{"op", "create"}, {"name", "Child"}},
            {{"op", "parent"}, {"entity", "3"}, {"parent", "2"}},
            {{"op", "transform"},
             {"entity", "2"},
             {"value",
              {{"position", {5, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}}));
  const auto move = Json::array(
      {{{"op", "reparent"}, {"entities", {"2", "3"}}, {"parent", "1"}, {"space", "world"}}});
  auto edited = edit(document, move);
  CHECK(edited.changed);
  auto native = document.scene().value();
  CHECK(native->entity(native->findEntity(3).value())->parent->value == 2);
  CHECK(native->worldTransform(native->findEntity(2).value())->data[12] == 5);
  CHECK(native->worldTransform(native->findEntity(3).value())->data[12] == 5);
  CHECK_FALSE(edit(document, move).changed);
  const auto before = state(document);
  CHECK_FALSE(document.apply(
      Json::array(
          {{{"op", "reparent"}, {"entities", {"2", "3"}}, {"parent", "3"}, {"space", "world"}}})
          .dump(),
      revision(document)));
  CHECK_FALSE(document.apply(
      Json::array({{{"op", "group"}, {"entities", {"2", "2"}}, {"name", "Invalid"}}}).dump(),
      revision(document)));
  CHECK_FALSE(document.apply(Json::array({{{"op", "ungroup"}, {"entities", {"2"}}}}).dump(),
                             revision(document)));
  CHECK_FALSE(document.dispatch(
      Json{{"command", "group-selection"}, {"revision", "1"}, {"name", "Stale"}}.dump()));
  CHECK(state(document) == before);
}

TEST_CASE("document.hierarchy_history_budget_failure_does_not_consume_identity_or_selection") {
  Fixture fixture;
  const auto bytes = Dot::World::encodeScene(fixture.project().loadStartup().value())->size();
  auto document = fixture.document({128, bytes + 128});
  request(document, {{"command", "select-many"}, {"entities", {"1", "2"}}});
  const auto before = state(document);
  auto rejected = document.dispatch(Json{{"command", "group-selection"},
                                         {"revision", std::to_string(revision(document))},
                                         {"name", "Beyond history"}}
                                        .dump());
  CHECK_FALSE(rejected);
  CHECK(state(document) == before);
  CHECK(document.stats()->nextEntity == 3);
}

TEST_CASE("document.scene_content_save_and_instance_share_history_selection_and_high_water") {
  Fixture fixture;
  auto document = fixture.document();
  edit(document,
       Json::array(
           {{{"op", "transform"},
             {"entity", "2"},
             {"value",
              {{"position", {5, 1, 2}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}}));
  request(document, {{"command", "select-many"}, {"entities", {"2"}}});
  const auto original = state(document);
  auto saved = request(document, {{"command", "save-selection-scene"},
                                  {"path", "assets/cube-content.dotscene"},
                                  {"name", "Cube content"}});
  CHECK(saved["contentPath"] == "assets/cube-content.dotscene");
  CHECK(state(document) == original);
  auto content
      = Dot::World::loadScene(fixture.project().files().value(), "assets/cube-content.dotscene")
            .value();
  CHECK(content.entityCount().value() == 2);
  CHECK(content.worldTransform(content.findEntity(2).value())->data[12] == 0);
  auto operation = Json::array(
      {{{"op", "instantiate-scene"},
        {"path", "assets/cube-content.dotscene"},
        {"parent", "1"},
        {"transform",
         {{"position", {10, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}});
  const auto inserted = edit(document, operation);
  REQUIRE(inserted.created.size() == 3);
  const auto root = inserted.created.front();
  CHECK(state(document)["selection"] == Json::array({std::to_string(root)}));
  auto scene = document.scene().value();
  CHECK(scene->entity(scene->findEntity(root).value())->parent->value == 1);
  const auto pose
      = scene->worldTransform(scene->findEntity(inserted.created.back()).value()).value();
  CHECK(pose.data[12] == 10);
  CHECK(pose.data[14] == 4);
  CHECK(scene->primaryCamera()->value == 1);
  REQUIRE(document.undo(revision(document)));
  CHECK(authored(state(document)["scene"]) == authored(original["scene"]));
  CHECK(state(document)["scene"]["nextEntity"] == state(document)["nextEntity"]);
  CHECK(state(document)["selection"] == original["selection"]);
  REQUIRE(document.redo(revision(document)));
  CHECK(document.scene().value()->entityCount().value() == 5);
  REQUIRE(document.save(revision(document)));
  CHECK(fixture.project().loadStartup()->entityCount().value() == 5);
  REQUIRE(document.undo(revision(document)));
  const auto branched = edit(document, operation);
  CHECK(branched.created.front() > inserted.created.back());
}

TEST_CASE("document.scene_content_file_conflicts_paths_and_stale_requests_preserve_authoring") {
  Fixture fixture;
  auto document = fixture.document();
  CHECK_FALSE(document.saveSelectionAs("assets/empty.dotscene", "Empty", revision(document)));
  request(document, {{"command", "select-many"}, {"entities", {"2"}}});
  const auto before = state(document);
  CHECK_FALSE(
      document.saveSelectionAs("scenes/content.dotscene", "Wrong tree", revision(document)));
  CHECK_FALSE(document.saveSelectionAs("assets/missing/content.dotscene", "Missing parent",
                                       revision(document)));
  CHECK_FALSE(document.saveSelectionAs("assets/content.dotscene", "Stale", 0));
  REQUIRE(document.saveSelectionAs("assets/Content.dotscene", "Content", revision(document)));
  auto files = fixture.project().files().value();
  const auto bytes = files.readText("assets/Content.dotscene").value();
  CHECK_FALSE(document.saveSelectionAs("assets/Content.dotscene", "Replace", revision(document)));
  CHECK_FALSE(document.saveSelectionAs("assets/content.dotscene", "Alias", revision(document)));
  CHECK_FALSE(document.dispatch(Json{
      {"command", "save-selection-scene"},
      {"revision", std::to_string(revision(document))},
      {"path", "assets/unknown.dotscene"},
      {"name", "Unknown"},
      {"extra", true}}.dump()));
  const auto alias = Json::array(
      {{{"op", "instantiate-scene"},
        {"path", "assets/content.dotscene"},
        {"parent", nullptr},
        {"transform",
         {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}});
  CHECK_FALSE(document.apply(alias.dump(), revision(document)));
  CHECK(files.readText("assets/Content.dotscene").value() == bytes);
  CHECK(state(document) == before);
  auto description = fixture.project().description().value();
  description.name = "Externally edited";
  REQUIRE(files.writeTextAtomic("project.dotproject",
                                Dot::Project::encodeDescription(description).value()));
  CHECK_FALSE(document.saveSelectionAs("assets/changed-project.dotscene", "Changed project",
                                       revision(document)));
  CHECK(state(document) == before);
}

TEST_CASE("document.scene_content_history_rejection_and_invalid_batch_keep_allocations_private") {
  Fixture fixture;
  const auto bytes = Dot::World::encodeScene(fixture.project().loadStartup().value())->size();
  auto document = fixture.document({128, bytes + 128});
  request(document, {{"command", "select-many"}, {"entities", {"2"}}});
  REQUIRE(document.saveSelectionAs("assets/content.dotscene", "Content", revision(document)));
  const auto before = state(document);
  auto insertion = Json::array(
      {{{"op", "instantiate-scene"},
        {"path", "assets/content.dotscene"},
        {"parent", nullptr},
        {"transform",
         {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}});
  CHECK_FALSE(document.apply(insertion.dump(), revision(document)));
  CHECK(state(document) == before);
  CHECK(document.stats()->nextEntity == 3);
  auto normal = fixture.document();
  const auto initial = state(normal);
  insertion[0]["parent"] = "999";
  insertion.insert(insertion.begin(), {{"op", "create"}, {"name", "Unpublished"}});
  CHECK_FALSE(normal.apply(insertion.dump(), revision(normal)));
  CHECK(state(normal) == initial);
  CHECK(normal.stats()->nextEntity == 3);
}

TEST_CASE("document.saved_deleted_node_values_are_retired_across_independent_reopen") {
  Fixture fixture;
  auto document = fixture.document();
  const auto first = edit(document, Json::array({{{"op", "create"}, {"name", "Durable node"}}}));
  REQUIRE(first.created.size() == 1);
  REQUIRE(document.save(revision(document)));
  edit(document, Json::array({{{"op", "remove"}, {"entity", std::to_string(first.created[0])}}}));
  REQUIRE(document.save(revision(document)));
  auto disk = fixture.project().loadStartup().value();
  CHECK(disk.nextEntityValue().value() == first.created[0] + 1);
  auto reopened = fixture.document();
  CHECK(reopened.stats()->nextEntity == first.created[0] + 1);
  const auto second = edit(reopened, Json::array({{{"op", "create"}, {"name", "Replacement"}}}));
  CHECK(second.created[0] > first.created[0]);
}

TEST_CASE("document.undo_keeps_allocation_floor_and_save_as_publishes_live_metadata") {
  Fixture fixture;
  auto document = fixture.document();
  const auto original = state(document);
  const auto created = edit(document, Json::array({{{"op", "create"}, {"name", "Undone node"}}}));
  REQUIRE(document.undo(revision(document)));
  CHECK_FALSE(document.stats()->dirty);
  CHECK(state(document)["scene"]["entities"] == original["scene"]["entities"]);
  CHECK(document.scene().value()->nextEntityValue().value() == created.created[0] + 1);
  CHECK(state(document)["scene"]["nextEntity"] == state(document)["nextEntity"]);
  REQUIRE(document.saveAs("scenes/other.dotscene", revision(document)));
  auto files = fixture.project().files().value();
  auto disk = Dot::World::loadScene(files, "scenes/other.dotscene").value();
  CHECK(disk.nextEntityValue().value() == created.created[0] + 1);
  auto reopened = fixture.document();
  REQUIRE(reopened.openScene("scenes/other.dotscene", revision(reopened)));
  const auto next = edit(reopened, Json::array({{{"op", "create"}, {"name", "New source node"}}}));
  CHECK(next.created[0] > created.created[0]);
  auto changed = Json::parse(files.readText("scenes/other.dotscene").value());
  changed["nextEntity"] = "100";
  REQUIRE(files.writeTextAtomic("scenes/other.dotscene", changed.dump()));
  const auto before = state(document);
  CHECK_FALSE(document.save(revision(document)));
  CHECK(state(document) == before);
  CHECK(Json::parse(files.readText("scenes/other.dotscene").value())["nextEntity"] == "100");
}

TEST_CASE("document.unpublished_noop_and_failed_batch_do_not_consume_persistent_values") {
  Fixture fixture;
  auto document = fixture.document();
  const auto original = state(document);
  const auto result = edit(document, Json::array({{{"op", "create"}, {"name", "Unpublished"}},
                                                  {{"op", "remove"}, {"entity", "3"}}}));
  CHECK_FALSE(result.changed);
  CHECK(state(document) == original);
  CHECK(document.scene().value()->nextEntityValue().value() == 3);
  CHECK_FALSE(document.apply(Json::array({{{"op", "create"}, {"name", "Rejected"}},
                                          {{"op", "parent"}, {"entity", "3"}, {"parent", "999"}}})
                                 .dump(),
                             revision(document)));
  CHECK(state(document) == original);
  REQUIRE(document.save(revision(document)));
  CHECK(fixture.project().loadStartup()->nextEntityValue().value() == 3);
}

namespace {
  auto linkedContent(Fixture& fixture) -> Dot::World::Scene {
    auto source = Dot::World::Scene::create("Reusable panels").value();
    const auto panel = source.createEntity("Source panel").value();
    REQUIRE(source.setMeshRenderer(panel, Dot::World::MeshRenderer{}));
    REQUIRE(source.createEntity("Source child"));
    REQUIRE(source.setParent({source.id(), 2}, panel));
    REQUIRE(
        Dot::World::saveScene(fixture.project().files().value(), "assets/panels.dotscene", source));
    return source;
  }

  auto linkOperation() -> Json {
    return {{"op", "link-scene"},
            {"path", "assets/panels.dotscene"},
            {"parent", nullptr},
            {"transform",
             {{"position", {10, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}};
  }
}  // namespace

TEST_CASE("document.linked_scene_refresh_overrides_save_reopen_undo_and_unlink_share_history") {
  Fixture fixture;
  auto source = linkedContent(fixture);
  auto document = fixture.document();
  const auto inserted = edit(document, Json::array({linkOperation()}));
  CHECK(inserted.created == std::vector<std::uint64_t>{3, 4, 5});
  edit(document, Json::array({{{"op", "name"}, {"entity", "4"}, {"name", "Local panel name"}}}));
  REQUIRE(document.save(revision(document)));
  auto reopened = fixture.document();
  CHECK(reopened.scene().value()->sceneLinks()->size() == 1);
  REQUIRE(source.createEntity("New source node"));
  auto mesh = source.entity({source.id(), 1})->mesh.value();
  mesh.visible = false;
  REQUIRE(source.setMeshRenderer({source.id(), 1}, mesh));
  REQUIRE(
      Dot::World::saveScene(fixture.project().files().value(), "assets/panels.dotscene", source));
  const auto before = state(reopened);
  auto updated = edit(reopened, Json::array({{{"op", "refresh-scene-instance"}, {"entity", "3"}}}));
  CHECK(updated.created == std::vector<std::uint64_t>{6});
  auto panel = reopened.scene().value()->entity({reopened.scene().value()->id(), 4}).value();
  CHECK(panel.name == "Local panel name");
  CHECK_FALSE(panel.mesh->visible);
  const auto after = state(reopened);
  REQUIRE(reopened.undo(revision(reopened)));
  CHECK(authored(state(reopened)["scene"]) == authored(before["scene"]));
  CHECK(reopened.stats()->nextEntity == 7);
  REQUIRE(reopened.redo(revision(reopened)));
  CHECK(state(reopened)["scene"] == after["scene"]);
  const auto repeated = state(reopened);
  CHECK_FALSE(
      edit(reopened, Json::array({{{"op", "refresh-scene-instance"}, {"entity", "3"}}})).changed);
  CHECK(state(reopened) == repeated);
  edit(reopened, Json::array({{{"op", "revert-scene-instance"}, {"entity", "3"}}}));
  CHECK(reopened.scene().value()->entity({reopened.scene().value()->id(), 4})->name
        == "Source panel");
  REQUIRE(reopened.undo(revision(reopened)));
  edit(reopened, Json::array({{{"op", "unlink-scene-instance"}, {"entity", "3"}}}));
  CHECK(reopened.scene().value()->sceneLinks()->empty());
  CHECK(reopened.scene().value()->entityCount().value() == 6);
  REQUIRE(reopened.undo(revision(reopened)));
  REQUIRE(reopened.save(revision(reopened)));
  auto final = fixture.document();
  CHECK(final.scene().value()->sceneLinks()->front().mapping.size() == 3);
  CHECK(final.scene().value()->entity({final.scene().value()->id(), 4})->name
        == "Local panel name");
  CHECK(std::filesystem::remove(fixture.root / "game/assets/panels.dotscene"));
  CHECK(fixture.project().loadStartup());
  CHECK_FALSE(final.apply(Json::array({{{"op", "refresh-scene-instance"}, {"entity", "3"}}}).dump(),
                          revision(final)));
}

TEST_CASE("document.linked_scene_failures_and_later_batch_failure_publish_no_state_or_ids") {
  Fixture fixture;
  auto source = linkedContent(fixture);
  auto document = fixture.document();
  const auto initial = state(document);
  auto invalid = linkOperation();
  invalid["path"] = "scenes/main.dotscene";
  CHECK_FALSE(document.apply(Json::array({invalid}).dump(), revision(document)));
  invalid["path"] = "../panels.dotscene";
  CHECK_FALSE(document.apply(Json::array({invalid}).dump(), revision(document)));
  CHECK_FALSE(
      document.apply(Json::array({linkOperation(), {{"op", "remove"}, {"entity", "999"}}}).dump(),
                     revision(document)));
  CHECK(state(document) == initial);
  edit(document, Json::array({linkOperation()}));
  const auto before = state(document);
  REQUIRE(source.createEntity("Not published by a failed batch"));
  REQUIRE(
      Dot::World::saveScene(fixture.project().files().value(), "assets/panels.dotscene", source));
  CHECK_FALSE(document.apply(Json::array({{{"op", "refresh-scene-instance"}, {"entity", "3"}},
                                          {{"op", "parent"}, {"entity", "1"}, {"parent", "999"}}})
                                 .dump(),
                             revision(document)));
  CHECK(state(document) == before);
  CHECK_FALSE(
      document.apply(Json::array({{{"op", "refresh-scene-instance"}, {"entity", "3"}}}).dump(),
                     revision(document) - 1));
  CHECK(state(document) == before);
  auto unknown = Json{{"op", "refresh-scene-instance"}, {"entity", "3"}, {"ignored", true}};
  CHECK_FALSE(document.apply(Json::array({unknown}).dump(), revision(document)));
  CHECK(state(document) == before);
}

TEST_CASE("document.linked_root_duplication_is_independent_and_deletion_retires_only_its_link") {
  Fixture fixture;
  auto source = linkedContent(fixture);
  auto document = fixture.document();
  edit(document, Json::array({linkOperation()}));
  edit(document, Json::array({{{"op", "duplicate"}, {"entities", {"3"}}}}));
  CHECK(document.scene().value()->sceneLinks()->size() == 1);
  CHECK(document.scene().value()->entityCount().value() == 8);
  edit(document, Json::array({{{"op", "remove-many"}, {"entities", {"3"}}}}));
  CHECK(document.scene().value()->sceneLinks()->empty());
  CHECK(document.scene().value()->entityCount().value() == 5);
  REQUIRE(document.undo(revision(document)));
  CHECK(document.scene().value()->sceneLinks()->size() == 1);
  edit(document, Json::array({{{"op", "remove"}, {"entity", "3"}}}));
  CHECK(document.scene().value()->sceneLinks()->empty());
}
