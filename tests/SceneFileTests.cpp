#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

import Dot.World.SceneFiles;
import Dot.World.Scene;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Math;

namespace {
  using namespace Dot::World;
  using Json = nlohmann::json;

  auto makeScene() -> Scene {
    auto scene = Scene::create("场景🙂");
    REQUIRE(scene);
    auto parent = scene->createEntityWithId("Parent", 10);
    auto child = scene->createEntityWithId("Mesh", 20);
    auto camera = scene->createEntityWithId("Camera", 40);
    REQUIRE(parent);
    REQUIRE(child);
    REQUIRE(camera);
    REQUIRE(scene->setTransform(*parent, {{2, 3, 4}, {}, {2, 2, 2}}));
    REQUIRE(scene->setTransform(*child, {{1, 0, 0}}));
    REQUIRE(scene->setTransform(*camera, {{0, 0, 5}}));
    REQUIRE(scene->setParent(*child, *parent));
    REQUIRE(scene->setParent(*camera, *parent));
    Camera component;
    component.primary = true;
    REQUIRE(scene->setCamera(*camera, component));
    MeshRenderer mesh;
    mesh.kind = MeshKind::Asset;
    mesh.asset = "geometry/模型.OBJ";
    mesh.tint = {0.2F, 0.4F, 0.6F, 1};
    mesh.visible = false;
    REQUIRE(scene->setMeshRenderer(*child, mesh));
    return std::move(*scene);
  }

  auto source() -> Json {
    auto scene = makeScene();
    auto encoded = encodeScene(scene);
    REQUIRE(encoded);
    return Json::parse(*encoded);
  }

  struct Directory {
    std::filesystem::path root;

    Directory() {
      std::random_device random;

      for (int attempt = 0; attempt < 100; ++attempt) {
        root = std::filesystem::temp_directory_path()
               / ("dot-scene-files-" + std::to_string(random()) + "-" + std::to_string(random()));
        std::error_code error;

        if (std::filesystem::create_directory(root, error)) {
          return;
        }

        if (error) {
          throw std::runtime_error(error.message());
        }
      }

      throw std::runtime_error("Cannot create scene-file test directory");
    }

    ~Directory() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }
  };
}  // namespace

TEST_CASE("scene_files.round_trip_preserves_values_components_and_hierarchy") {
  auto scene = makeScene();
  auto encoded = encodeScene(scene);
  REQUIRE(encoded);
  auto decoded = decodeScene(*encoded);
  REQUIRE_MESSAGE(decoded, (decoded ? "" : decoded.error().message));
  CHECK(decoded->id() != scene.id());
  CHECK(decoded->name().value() == scene.name().value());
  CHECK(decoded->entityCount().value() == 3);
  CHECK(decoded->primaryCamera()->value == 40);
  auto child = decoded->findEntity(20);
  REQUIRE(child);
  auto info = decoded->entity(*child);
  REQUIRE(info);
  CHECK(info->parent->value == 10);
  CHECK(info->parent->scene == decoded->id());
  REQUIRE(info->mesh);
  CHECK(info->mesh->asset == "geometry/模型.OBJ");
  CHECK_FALSE(info->mesh->visible);
  CHECK(decoded->worldTransform(*child)->data[12] == 4);
  CHECK(decoded->worldTransform(*child)->data[13] == 3);
  CHECK_FALSE(decoded->entity(scene.findEntity(20).value()));
  auto restored = encodeScene(*decoded);
  REQUIRE(restored);
  CHECK(*restored == *encoded);
}

TEST_CASE("scene_files.unordered_records_and_optional_components") {
  auto value = source();
  std::reverse(value["entities"].begin(), value["entities"].end());
  auto decoded = decodeScene(value.dump());
  REQUIRE(decoded);
  auto entities = decoded->entities();
  REQUIRE(entities);
  CHECK(entities->front().id.value == 10);
  value["entities"][0].erase("camera");
  value["entities"][1].erase("mesh");
  decoded = decodeScene(value.dump());
  REQUIRE(decoded);
  CHECK_FALSE(decoded->primaryCamera());
}

TEST_CASE("scene_files.empty_scene_and_full_precision_ids") {
  auto scene = Scene::create("Empty");
  REQUIRE(scene);
  auto encoded = encodeScene(*scene);
  REQUIRE(encoded);
  auto decoded = decodeScene(*encoded);
  REQUIRE(decoded);
  CHECK(decoded->entityCount().value() == 0);
  const auto large = std::numeric_limits<std::uint64_t>::max() - 1;
  auto entity = scene->createEntityWithId("Precise", large);
  REQUIRE(entity);
  encoded = encodeScene(*scene);
  REQUIRE(encoded);
  auto json = Json::parse(*encoded);
  CHECK(json["entities"][0]["id"].is_string());
  decoded = decodeScene(*encoded);
  REQUIRE(decoded);
  CHECK(decoded->findEntity(large)->value == large);
  CHECK_FALSE(decoded->createEntity());
}

TEST_CASE("scene_files.reject_bad_schema_version_types_and_fields") {
  auto value = source();

  for (auto version : {Json(0), Json(2), Json(1.0), Json(true), Json("1")}) {
    auto invalid = value;
    invalid["version"] = version;
    CHECK_FALSE(decodeScene(invalid.dump()));
  }

  auto invalid = value;
  invalid["unknown"] = 1;
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][0]["unknown"] = 1;
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][0].erase("transform");
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["name"] = "";
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][0]["name"] = 1;
  CHECK_FALSE(decodeScene(invalid.dump()));
}

TEST_CASE("scene_files.reject_duplicate_noncanonical_and_numeric_ids") {
  auto value = source();

  for (auto id : {Json("0"), Json("01"), Json("+1"), Json("-1"), Json(""), Json("1x"),
                  Json("18446744073709551615"), Json("18446744073709551616"), Json(10)}) {
    auto invalid = value;
    invalid["entities"][0]["id"] = id;
    CHECK_FALSE(decodeScene(invalid.dump()));
  }

  value["entities"][1]["id"] = value["entities"][0]["id"];
  CHECK_FALSE(decodeScene(value.dump()));
}

TEST_CASE("scene_files.reject_missing_parents_cycles_and_duplicate_primary") {
  auto value = source();
  auto invalid = value;
  invalid["entities"][0]["parent"] = "999";
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][0]["parent"] = "20";
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][0]["camera"] = value["entities"][2]["camera"];
  CHECK_FALSE(decodeScene(invalid.dump()));
}

TEST_CASE("scene_files.reject_invalid_transform_camera_and_mesh_values") {
  auto value = source();
  auto invalid = value;
  invalid["entities"][0]["transform"]["rotation"] = {0, 0, 0, 0};
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][0]["transform"]["position"] = {0, 0};
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][0]["transform"]["scale"][0] = 1e100;
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][2]["camera"]["projection"] = "unknown";
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][2]["camera"]["primary"] = 1;
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][2]["camera"]["nearPlane"] = 0;
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][1]["mesh"]["kind"] = "unknown";
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][1]["mesh"]["asset"] = "../outside.obj";
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][1]["mesh"]["visible"] = 1;
  CHECK_FALSE(decodeScene(invalid.dump()));
  invalid = value;
  invalid["entities"][1]["mesh"]["tint"][0] = -1;
  CHECK_FALSE(decodeScene(invalid.dump()));
}

TEST_CASE("scene_files.duplicate_depth_byte_and_entity_limits") {
  CHECK_FALSE(decodeScene(""));
  CHECK_FALSE(decodeScene(std::string(64 * 1024 * 1024 + 1, ' ')));
  CHECK_FALSE(decodeScene(std::string{"A\0B", 3}));
  CHECK_FALSE(decodeScene(R"({"version":1,"version":1,"name":"n","entities":[]})"));
  CHECK_FALSE(decodeScene(std::string(70, '[') + "0" + std::string(70, ']')));
  auto value = source();
  value["entities"] = Json::array();

  for (int index = 0; index < 10001; ++index) {
    value["entities"].push_back(nullptr);
  }

  CHECK_FALSE(decodeScene(value.dump()));
}

TEST_CASE("scene_files.failures_leave_live_scene_intact") {
  auto scene = makeScene();
  auto before = encodeScene(scene);
  REQUIRE(before);
  auto invalid = Json::parse(*before);
  invalid["entities"][2]["camera"]["nearPlane"] = 0;

  for (int iteration = 0; iteration < 20; ++iteration) {
    CHECK_FALSE(decodeScene(invalid.dump()));
  }

  auto after = encodeScene(scene);
  REQUIRE(after);
  CHECK(*after == *before);
}

TEST_CASE("scene_files.atomic_unicode_save_load_and_failed_replace") {
  Directory directory;
  auto files = Dot::Core::FileSystem::open(directory.root);
  REQUIRE(files);
  auto scene = makeScene();
  const auto path = std::filesystem::path(u8"场景.DOTSCENE");
  REQUIRE(saveScene(*files, path, scene));
  auto loaded = loadScene(*files, path);
  REQUIRE(loaded);
  CHECK(loaded->entityCount().value() == 3);
  auto before = files->readText(path);
  REQUIRE(before);
  auto moved = std::move(scene);
  CHECK_FALSE(saveScene(*files, path, scene));
  auto after = files->readText(path);
  REQUIRE(after);
  CHECK(*after == *before);
  CHECK_FALSE(saveScene(*files, "missing/main.dotscene", moved));
  CHECK_FALSE(saveScene(*files, "main.json", moved));
  CHECK_FALSE(loadScene(*files, "../outside.dotscene"));
  auto missing = loadScene(*files, "missing.dotscene");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == Dot::Core::ErrorCode::NotFound);
  CHECK(std::distance(std::filesystem::directory_iterator(directory.root),
                      std::filesystem::directory_iterator{})
        == 1);
}

TEST_CASE("scene_files.large_scene_round_trip_uses_batch_import") {
  auto scene = Scene::create();
  REQUIRE(scene);
  std::vector<ParentUpdate> parents;
  auto root = scene->createEntity();
  REQUIRE(root);

  for (int index = 0; index < 999; ++index) {
    auto entity = scene->createEntity();
    REQUIRE(entity);
    parents.push_back({*entity, *root});
  }

  REQUIRE(scene->setParents(parents));
  auto encoded = encodeScene(*scene);
  REQUIRE(encoded);
  auto decoded = decodeScene(*encoded);
  REQUIRE(decoded);
  CHECK(decoded->entityCount().value() == 1000);
}

TEST_CASE("scene_files_material_and_light_roundtrip_and_strict_component_rejection") {
  auto scene = makeScene();
  const auto entity = scene.entities()->front().id;
  REQUIRE(scene.setMaterial(entity, MaterialBinding{"assets/金属.dotmaterial"}));
  Light light;
  light.kind = LightKind::Spot;
  light.color = {1, 0.5F, 0.2F};
  light.intensity = 42;
  light.range = 20;
  light.innerConeDegrees = 10;
  light.outerConeDegrees = 35;
  light.enabled = false;
  REQUIRE(scene.setLight(entity, light));
  const auto source = encodeScene(scene).value();
  auto decoded = decodeScene(source);
  REQUIRE(decoded);
  const auto loaded = decoded->entities()->front();
  CHECK(loaded.material->asset == "assets/金属.dotmaterial");
  CHECK(loaded.light->kind == LightKind::Spot);
  CHECK(loaded.light->color == light.color);
  CHECK(loaded.light->intensity == 42);
  CHECK_FALSE(loaded.light->enabled);
  CHECK(encodeScene(*decoded).value() == source);
  auto json = Json::parse(source);
  json["entities"][0]["light"]["kind"] = "unknown";
  CHECK_FALSE(decodeScene(json.dump()));
  json = Json::parse(source);
  json["entities"][0]["light"]["range"] = -1;
  CHECK_FALSE(decodeScene(json.dump()));
  json = Json::parse(source);
  json["entities"][0]["material"]["unknown"] = true;
  CHECK_FALSE(decodeScene(json.dump()));
  json = Json::parse(source);
  json["entities"][0]["material"]["asset"] = "../escape.dotmaterial";
  CHECK_FALSE(decodeScene(json.dump()));
}

TEST_CASE("scene_file.environment_roundtrip_strict_fields_numbers_and_absence") {
  auto scene = makeScene();
  const Dot::World::Environment expected{
      "assets/studio.hdr", 1.5F, -90, false, {64, 16, 64, 64, 128}};
  REQUIRE(scene.setEnvironment(expected));
  const auto encoded = Dot::World::encodeScene(scene).value();
  auto decoded = Dot::World::decodeScene(encoded).value();
  CHECK(decoded.environment().value() == expected);
  CHECK(Dot::World::encodeScene(decoded).value() == encoded);
  const auto canonical = Json::parse(encoded);

  for (const auto& replacement : {Json(-1), Json(1025), Json(32.5), Json("32"), Json(true),
                                  Json(std::numeric_limits<std::uint64_t>::max())}) {
    auto bad = canonical;
    bad["environment"]["samples"] = replacement;
    CHECK_FALSE(Dot::World::decodeScene(bad.dump()));
  }

  auto bad = canonical;
  bad["environment"]["unknown"] = true;
  CHECK_FALSE(Dot::World::decodeScene(bad.dump()));
  bad = canonical;
  bad["environment"].erase("asset");
  CHECK_FALSE(Dot::World::decodeScene(bad.dump()));
  bad = canonical;
  bad["environment"]["background"] = 1;
  CHECK_FALSE(Dot::World::decodeScene(bad.dump()));
  bad = canonical;
  bad["environment"]["asset"] = "../outside.hdr";
  CHECK_FALSE(Dot::World::decodeScene(bad.dump()));
  auto absent = canonical;
  absent["environment"] = nullptr;
  CHECK_FALSE(Dot::World::decodeScene(absent.dump())->environment().value());
  absent.erase("environment");
  CHECK_FALSE(Dot::World::decodeScene(absent.dump())->environment().value());
}
