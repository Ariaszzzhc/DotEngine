#include <doctest/doctest.h>

#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <utility>
#include <vector>

import Dot.Project;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot::Project;
  using Json = nlohmann::json;

  struct Parent {
    std::filesystem::path root;

    Parent() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-project-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
    }

    ~Parent() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }

    auto files() const -> Dot::Core::FileSystem {
      return Dot::Core::FileSystem::open(root).value();
    }
  };

  auto create(const Parent& parent, std::string name = "Project") -> Project {
    Description description;
    description.name = std::move(name);
    auto result = Project::create(parent.files(), "game", description);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }
}  // namespace

TEST_CASE("project_metadata_roundtrip_unicode_boundaries_and_portable_paths") {
  Description description{"游戏🙂", "场景/主.dotscene", "源码", 8192, 1, 240,
                          false,    "metadata-test"};
  auto encoded = encodeDescription(description);
  REQUIRE(encoded);
  CHECK(decodeDescription(*encoded).value() == description);

  for (const auto& path : std::vector<std::string>{
           "", "/scene.dotscene", "../scene.dotscene", "scenes/./main.dotscene",
           "scenes//main.dotscene", "scenes/a ", "C:/main.dotscene", "scenes/a?.dotscene",
           "CON.dotscene", "scenes/LPT1.dotscene", "a\\b.dotscene", "scene.json",
           "scenes/a\n.dotscene", std::string(241, 'x') + ".dotscene"}) {
    auto invalid = description;
    invalid.startScene = path;
    CHECK_FALSE(validateDescription(invalid));
  }

  auto invalid = description;
  invalid.name = std::string(129, 'a');
  CHECK_FALSE(encodeDescription(invalid));
  invalid.name = std::string("x\0y", 3);
  CHECK_FALSE(encodeDescription(invalid));
  invalid = description;
  invalid.scriptsDirectory = "scripts/..";
  CHECK_FALSE(encodeDescription(invalid));
}

TEST_CASE("project_metadata_rejects_unknown_missing_duplicate_versions_types_and_bytes") {
  Description description;
  description.applicationId = "metadata-test";
  const auto original = Json::parse(encodeDescription(description).value());

  for (const auto& item : original.items()) {
    auto missing = original;
    missing.erase(item.key());
    CHECK_FALSE(decodeDescription(missing.dump()));
  }

  auto invalid = original;
  invalid["extra"] = 0;
  CHECK_FALSE(decodeDescription(invalid.dump()));

  for (const auto& value : std::vector<Json>{0, -1, 2, true, "1", 1.0, 1e100}) {
    invalid = original;
    invalid["version"] = value;
    CHECK_FALSE(decodeDescription(invalid.dump()));
  }

  invalid = original;
  invalid["window"]["width"] = 0;
  CHECK_FALSE(decodeDescription(invalid.dump()));
  invalid["window"]["width"] = 8193;
  CHECK_FALSE(decodeDescription(invalid.dump()));
  invalid = original;
  invalid["fixedHz"] = 241;
  CHECK_FALSE(decodeDescription(invalid.dump()));
  invalid = original;
  invalid["hotReload"] = 1;
  CHECK_FALSE(decodeDescription(invalid.dump()));
  CHECK_FALSE(decodeDescription("{\"version\":1,\"version\":1}"));
  CHECK_FALSE(decodeDescription(std::string(65537, ' ')));
  CHECK_FALSE(decodeDescription(std::string("{}\0", 3)));
  CHECK_FALSE(decodeDescription(std::string("\xc0\x80", 2)));
}

TEST_CASE("project_create_publishes_engine_import_script_camera_mesh_and_owned_description") {
  Parent parent;
  auto project = create(parent, "Unicode 项目🙂");
  auto files = project.files().value();
  CHECK(files.root() == std::filesystem::canonical(parent.root / "game"));
  CHECK_FALSE(std::filesystem::exists(files.root() / "scripts/dot"));
  CHECK(files.readText("scripts/mover.ts")->find("dot:engine") != std::string::npos);
  CHECK(files.readText("scripts/mover.ts")->find("class Mover") != std::string::npos);
  auto scene = project.loadStartup();
  REQUIRE(scene);
  auto entities = scene->entities().value();
  REQUIRE(entities.size() == 2);
  CHECK(entities[0].camera->primary);
  CHECK(entities[0].transform.position.z == 4);
  CHECK(entities[1].mesh.has_value());
  CHECK(entities[1].script->source == "mover.ts");
  CHECK(entities[1].script->className == "Mover");
  auto description = project.description().value();
  description.name = "Uncommitted";
  CHECK(project.description()->name == "Unicode 项目🙂");
  auto other = project.loadStartup();
  REQUIRE(other);
  CHECK(other->id() != scene->id());
  CHECK_FALSE(other->entity(entities[1].id));
  CHECK(std::distance(std::filesystem::directory_iterator(parent.root),
                      std::filesystem::directory_iterator{})
        == 1);
}

TEST_CASE("project_save_and_scene_save_commit_valid_resources_and_preserve_failed_edits") {
  Parent parent;
  auto project = create(parent);
  auto old = project.description().value();
  auto description = old;
  description.name = "Renamed";
  description.width = 640;
  description.hotReload = false;
  REQUIRE(project.save(description));
  CHECK(Project::open(parent.root / "game")->description().value() == description);
  auto files = project.files().value();
  const auto encoded = files.readText("project.dotproject").value();
  auto missing = description;
  missing.startScene = "scenes/missing.dotscene";
  CHECK_FALSE(project.save(missing));
  CHECK(project.description().value() == description);
  CHECK(files.readText("project.dotproject").value() == encoded);
  auto scene = project.loadStartup().value();
  REQUIRE(scene.rename("Saved Scene"));
  REQUIRE(project.saveStartup(scene));
  CHECK(project.loadStartup()->name().value() == "Saved Scene");
  std::filesystem::rename(files.root() / "project.dotproject", files.root() / "manifest-backup");
  REQUIRE(files.createDirectories("project.dotproject"));
  auto failed = description;
  failed.name = "Write failed";
  CHECK_FALSE(project.save(failed));
  CHECK(project.description().value() == description);
}

TEST_CASE("project_open_rejects_missing_bad_scene_and_script_symlink_components") {
  Parent parent;
  auto project = create(parent);
  auto files = project.files().value();
  auto scene = files.readText("scenes/main.dotscene").value();
  REQUIRE(files.writeTextAtomic("scenes/main.dotscene", "{}"));
  CHECK_FALSE(Project::open(files.root()));
  REQUIRE(files.writeTextAtomic("scenes/main.dotscene", scene));
  std::filesystem::rename(files.root() / "scripts", files.root() / "actual-scripts");
  std::filesystem::create_directory_symlink(files.root() / "actual-scripts",
                                            files.root() / "scripts");
  CHECK_FALSE(Project::open(files.root()));
  CHECK(project.loadStartup());
}

TEST_CASE("project_creation_failures_remove_staging_and_never_replace_existing_destinations") {
  Parent parent;
  Description invalid;
  invalid.name = "";
  CHECK_FALSE(Project::create(parent.files(), "absent", invalid));
  CHECK(std::filesystem::is_empty(parent.root));
  Description conflict;
  conflict.scriptsDirectory = "project.dotproject";
  CHECK_FALSE(Project::create(parent.files(), "failed", conflict));
  CHECK(std::filesystem::is_empty(parent.root));
  REQUIRE(parent.files().createDirectories("existing"));
  CHECK_FALSE(Project::create(parent.files(), "existing", Description{}));
  CHECK(std::filesystem::is_empty(parent.root / "existing"));
  REQUIRE(parent.files().writeTextAtomic("existing/user.txt", "untouched"));
  CHECK_FALSE(Project::create(parent.files(), "existing", Description{}));
  CHECK(parent.files().readText("existing/user.txt").value() == "untouched");
  CHECK_FALSE(Project::create(parent.files(), "../escape", Description{}));
  CHECK_FALSE(Project::create(parent.files(), "CON", Description{}));
  CHECK(std::distance(std::filesystem::directory_iterator(parent.root),
                      std::filesystem::directory_iterator{})
        == 1);
}

TEST_CASE("project_relocation_copies_and_moves_preserve_relative_startup_and_owned_state") {
  Parent parent;
  auto project = create(parent);
  auto before = project.loadStartup().value();
  std::filesystem::rename(parent.root / "game", parent.root / "relocated");
  auto opened = Project::open(parent.root / "relocated");
  REQUIRE(opened);
  auto after = opened->loadStartup();
  REQUIRE(after);
  CHECK(after->id() != before.id());
  CHECK(after->entities()->size() == 2);
  auto copy = *opened;
  auto moved = std::move(*opened);
  CHECK_FALSE(opened->files());
  CHECK_FALSE(opened->description());
  CHECK_FALSE(opened->loadStartup());
  CHECK(copy.description().value() == moved.description().value());
  CHECK(copy.files()->root() == moved.files()->root());
}

TEST_CASE("project_save_rejects_manifest_links_without_overwriting_another_project_resource") {
  Parent parent;
  auto project = create(parent);
  auto files = project.files().value();
  const auto before = files.readText("scenes/main.dotscene").value();
  std::filesystem::remove(files.root() / "project.dotproject");
  std::filesystem::create_symlink(files.root() / "scenes/main.dotscene",
                                  files.root() / "project.dotproject");
  auto description = project.description().value();
  description.name = "Must not overwrite scene";
  CHECK_FALSE(project.save(description));
  CHECK(files.readText("scenes/main.dotscene").value() == before);
  CHECK(project.description()->name == "Project");
  CHECK_FALSE(Project::open(files.root()));
}

TEST_CASE("project.application_identity_survives_rename_and_new_projects_are_independent") {
  Parent parent;
  auto first = Project::create(parent.files(), "first").value();
  auto second = Project::create(parent.files(), "second").value();
  auto metadata = first.description().value();
  const auto identity = metadata.applicationId;
  REQUIRE_FALSE(identity.empty());
  CHECK(identity != second.description()->applicationId);
  metadata.name = "Renamed game";
  REQUIRE(first.save(metadata));
  auto reopened = Project::open(first.files()->root()).value();
  CHECK(reopened.description()->applicationId == identity);
  CHECK(reopened.description()->name == "Renamed game");

  for (const auto* value : {"", "../escape", "CON", "nul", "com1", "UPPER", "has space", "a/b"}) {
    auto invalid = metadata;
    invalid.applicationId = value;
    CHECK_FALSE(encodeDescription(invalid));
    CHECK(first.description()->applicationId == identity);
  }
}
