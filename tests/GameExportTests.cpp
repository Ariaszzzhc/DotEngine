#include <doctest/doctest.h>

#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Core.Process;
import Dot.Project;
import Dot.Project.GameExport;
import Dot.Project.GamePackage;
import Dot.Project.GameSession;
import Dot.Script.Compiler;
import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.World.Scene;

namespace {
  using Json = nlohmann::json;
  namespace Fs = std::filesystem;
  using namespace Dot::Project;

  struct Fixture {
    Dot::Core::StagedDirectory storage;

    Fixture()
        : storage(Dot::Core::StagedDirectory::create(
                      Dot::Core::FileSystem::open(Fs::temp_directory_path()).value())
                      .value()) {
    }

    auto files() -> Dot::Core::FileSystem {
      return storage.files().value();
    }
  };

  auto sample(Fixture& fixture) -> Dot::Core::FileSystem {
    auto parent = fixture.files();
    REQUIRE(parent.createDirectories("kit/licenses"));
    auto files = Dot::Core::FileSystem::open(parent.root() / "kit").value();
#ifdef _WIN32
    const auto platform = "windows-x64";
    const auto executable = "DotGame.exe";
#elif defined(__APPLE__)
    const auto platform = "macos-arm64";
    const auto executable = "DotGame";
#else
    const auto platform = "linux-x64";
    const auto executable = "DotGame";
#endif
    REQUIRE(files.writeTextAtomic(executable, "test"));
    Fs::permissions(files.root() / executable, Fs::perms::owner_all);
    REQUIRE(files.writeTextAtomic("licenses/notice.txt", "notice"));
    REQUIRE(files.writeTextAtomic("EngineSans.ttf", "font"));
    REQUIRE(files.writeTextAtomic(
        "runtime.json",
        Json{{"version", 1},
             {"platform", platform},
             {"executable", executable},
             {"files", Json::array({{{"path", executable}, {"bytes", 4}},
                                    {{"path", "EngineSans.ttf"}, {"bytes", 4}},
                                    {{"path", "licenses/notice.txt"}, {"bytes", 6}}})}}
            .dump()));
    return files;
  }
}  // namespace

TEST_CASE("runtime_kit.strict_manifest_tree_limits_and_permissions") {
  Fixture fixture;
  auto files = sample(fixture);
  const auto original = files.readText("runtime.json").value();
  REQUIRE(RuntimeKit::open(files));
#ifdef __APPLE__
  REQUIRE(files.writeTextAtomic(".DS_Store", "finder metadata"));
  REQUIRE(RuntimeKit::open(files));
  Fs::remove(files.root() / ".DS_Store");
  Fs::create_symlink("licenses/notice.txt", files.root() / ".DS_Store");
  CHECK_FALSE(RuntimeKit::open(files));
  Fs::remove(files.root() / ".DS_Store");
#endif
  auto manifest = Json::parse(original);
  auto missingFont = manifest;
  missingFont["files"].erase(missingFont["files"].begin() + 1);
  REQUIRE(files.writeTextAtomic("runtime.json", missingFont.dump()));
  Fs::remove(files.root() / "EngineSans.ttf");
  auto absent = RuntimeKit::open(files);
  REQUIRE_FALSE(absent);
  CHECK(absent.error().message.find("font") != std::string::npos);
  REQUIRE(files.writeTextAtomic("EngineSans.ttf", "font"));
  REQUIRE(files.writeTextAtomic("runtime.json", original));

  std::vector<Json> invalid;
  auto changed = manifest;
  changed["version"] = 2;
  invalid.push_back(changed);
  changed = manifest;
  changed["platform"] = "other";
  invalid.push_back(changed);
  changed = manifest;
  changed["files"][0]["path"] = "../escape";
  invalid.push_back(changed);
  changed = manifest;
  changed["files"][0]["bytes"] = 512ULL * 1024 * 1024 + 1;
  invalid.push_back(changed);
  changed = manifest;
  changed["files"][0]["bytes"] = 0;
  invalid.push_back(changed);
  changed = manifest;
  changed["files"] = Json::array();

  for (int index = 0; index < 513; ++index) {
    changed["files"].push_back(manifest["files"][0]);
  }

  invalid.push_back(changed);

  for (const auto& candidate : invalid) {
    REQUIRE(files.writeTextAtomic("runtime.json", candidate.dump()));
    CHECK_FALSE(RuntimeKit::open(files));
  }

  REQUIRE(files.writeTextAtomic("runtime.json", original));
  REQUIRE(files.writeTextAtomic("licenses/notice.txt", "wrong-size"));
  CHECK_FALSE(RuntimeKit::open(files));
  REQUIRE(files.writeTextAtomic("licenses/notice.txt", "notice"));
  REQUIRE(files.writeTextAtomic("extra", "extra"));
  CHECK_FALSE(RuntimeKit::open(files));
  Fs::remove(files.root() / "extra");
  Fs::remove(files.root() / "licenses/notice.txt");
  Fs::create_symlink("../" + manifest["executable"].get<std::string>(),
                     files.root() / "licenses/notice.txt");
  CHECK_FALSE(RuntimeKit::open(files));
#ifndef _WIN32
  Fs::remove(files.root() / "licenses/notice.txt");
  REQUIRE(files.writeTextAtomic("licenses/notice.txt", "notice"));
  Fs::permissions(files.root() / manifest["executable"].get<std::string>(),
                  Fs::perms::owner_read | Fs::perms::owner_write);
  CHECK_FALSE(RuntimeKit::open(files));
#endif
}

TEST_CASE("runtime_kit.manifest_generation_excludes_finder_metadata") {
  Fixture fixture;
  auto files = sample(fixture);
  const auto original = Json::parse(files.readText("runtime.json").value());
  REQUIRE(files.writeTextAtomic(".DS_Store", "finder metadata"));
  REQUIRE(files.writeTextAtomic("licenses/.DS_Store", "nested finder metadata"));
  const auto root = files.root().generic_string();
  auto generated = Dot::Core::runProcess(
      {DOT_CMAKE_EXECUTABLE,
       {"-Ddot_kit=" + root, "-Ddot_platform=" + original["platform"].get<std::string>(),
        "-Ddot_runtime_executable=" + original["executable"].get<std::string>(), "-P",
        DOT_RUNTIME_MANIFEST_SCRIPT},
       files.root()});
  REQUIRE_MESSAGE(generated, (generated ? "" : generated.error().message));
  REQUIRE_MESSAGE(generated->exitCode == 0, generated->standardError);
  CHECK_FALSE(Fs::exists(files.root() / ".DS_Store"));
  CHECK_FALSE(Fs::exists(files.root() / "licenses/.DS_Store"));
  const auto manifest = Json::parse(files.readText("runtime.json").value());
  CHECK(manifest["files"] == original["files"]);
  REQUIRE(RuntimeKit::open(files));
}

TEST_CASE("runtime_kit.publication_preserves_sources_destination_and_move_state") {
  Fixture fixture;
  auto source = sample(fixture);
  auto kit = RuntimeKit::open(source).value();
  auto copy = kit.publish(fixture.files(), "copy");
  REQUIRE_MESSAGE(copy, (copy ? "" : copy.error().message));
  REQUIRE(RuntimeKit::open(*copy));
  CHECK(copy->readText("licenses/notice.txt").value() == "notice");
  CHECK(source.readText("licenses/notice.txt").value() == "notice");
  CHECK_FALSE(kit.publish(fixture.files(), "copy"));
  CHECK_FALSE(kit.publish(fixture.files(), "../escape"));
  CHECK_FALSE(kit.publish(source, "nested"));
  auto moved = std::move(kit);
  CHECK_FALSE(kit.publish(fixture.files(), "moved"));
  auto changed = Json::parse(source.readText("runtime.json").value());
  changed["files"][1]["bytes"] = 7;
  REQUIRE(source.writeTextAtomic("licenses/notice.txt", "changed"));
  REQUIRE(source.writeTextAtomic("runtime.json", changed.dump()));
  CHECK_FALSE(moved.publish(fixture.files(), "changed"));
  CHECK_FALSE(Fs::exists(fixture.files().root() / "changed"));
}

TEST_CASE("runtime_distribution.relocated_package_runs_headless_without_authoring_or_tools") {
  Fixture fixture;
  auto parent = fixture.files();
  auto project = Project::create(parent, "authoring", {}).value();
  auto compiler = Dot::Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  auto opened = RuntimeKit::open(Dot::Core::FileSystem::open(DOT_RUNTIME_KIT).value());
  REQUIRE_MESSAGE(opened, (opened ? "" : opened.error().message));
  auto copied = opened->publish(parent, "kit");
  REQUIRE_MESSAGE(copied, (copied ? "" : copied.error().message));
  auto independent = *copied;
  auto checked = RuntimeKit::open(independent);
  REQUIRE_MESSAGE(checked, (checked ? "" : checked.error().message));
  auto kit = std::move(*checked);
  auto published
      = exportGame(project, project.loadStartup().value(), compiler, kit, parent, "game");
  REQUIRE_MESSAGE(published, (published ? "" : published.error().message));
  const auto original = published->readText("runtime.json").value();
  CHECK_FALSE(exportGame(project, project.loadStartup().value(), compiler, kit, parent, "game"));
  CHECK(published->readText("runtime.json").value() == original);
  Fs::rename(published->root(), parent.root() / "relocated game");
  Fs::remove_all(project.files()->root());
  Fs::remove_all(independent.root());
  auto relocated = Dot::Core::FileSystem::open(parent.root() / "relocated game").value();
  auto package
      = GamePackage::open(Dot::Core::FileSystem::open(relocated.root() / "game").value()).value();
  auto session = GameSession::createCompiled(package.files().value(), package.description().value(),
                                             package.scene().value(), package.program().value())
                     .value();
  auto started = session.start().value();
  CHECK(started.failures.empty());
  CHECK_FALSE(started.realmFaulted);
  auto updated = session.update(1.0 / 60).value();
  CHECK(updated.frame.failures.empty());
  CHECK_FALSE(updated.frame.realmFaulted);
  auto closed = session.close().value();
  CHECK(closed.failures.empty());
  CHECK_FALSE(closed.realmFaulted);
  CHECK(Dot::Core::processResources().children == 0);
  CHECK(Dot::Core::processResources().handles == 0);
}

TEST_CASE("runtime_distribution.compile_failure_discards_private_output") {
  Fixture fixture;
  auto parent = fixture.files();
  auto project = Project::create(parent, "authoring", {}).value();
  auto compiler = Dot::Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  auto files = sample(fixture);
  auto kit = RuntimeKit::open(files).value();
  REQUIRE(project.files()->writeTextAtomic("scripts/mover.ts", "export const bad:number='x';"));
  const auto generations = Dot::Script::compilerGenerations();
  CHECK_FALSE(exportGame(project, project.loadStartup().value(), compiler, kit, parent, "failed"));
  CHECK_FALSE(Fs::exists(parent.root() / "failed"));
  CHECK(Dot::Script::compilerGenerations() == generations);
  CHECK(Dot::Core::processResources().children == 0);
  CHECK(Dot::Core::processResources().handles == 0);
  CHECK(files.readText("licenses/notice.txt").value() == "notice");

  for (const auto& entry : Fs::directory_iterator(parent.root())) {
    CHECK_FALSE(entry.path().filename().string().starts_with(".dot-stage-"));
  }
}

TEST_CASE("runtime_distribution.saved_animation_survives_relocation_without_authoring") {
  using namespace Dot;
  Fixture fixture;
  auto parent = fixture.files();
  auto project = Dot::Project::Project::create(parent, "authoring").value();
  auto scene = project.loadStartup().value();
  const auto cube = scene.findEntity(2).value();
  auto clip = Animation::Clip::create({"Move",
                                       {{0,
                                         Animation::Path::Translation,
                                         Animation::Interpolation::Linear,
                                         {{0, {0, 0, 0, 0}}, {2, {2, 0, 0, 0}}}}}})
                  .value();
  REQUIRE(Animation::saveClip(project.files().value(), "assets/move.dotanimation", clip));
  REQUIRE(scene.setAnimationPlayer(cube,
                                   World::AnimationPlayer{{"assets/move.dotanimation"}, {{0, 2}}}));
  REQUIRE(project.files()->writeTextAtomic(
      "scripts/mover.ts",
      "import {ScriptComponent} from 'dot:engine';\n"
      "export class Mover extends ScriptComponent {\n"
      "  private observed = false;\n"
      "  private started = false;\n"
      "\n"
      "  onStart(): void { this.started = true; }\n"
      "\n"
      "  onUpdate(): void { if(this.self.transform.position.x > .00001) this.observed = true; }\n"
      "  onStop(): void { if(this.started && !this.observed) throw Error('Native animation never "
      "advanced'); }\n"
      "}\n"));
  auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  auto kit = RuntimeKit::open(Core::FileSystem::open(DOT_RUNTIME_KIT).value()).value();
  auto exported = exportGame(project, scene, compiler, kit, parent, "animated");
  REQUIRE_MESSAGE(exported, (exported ? "" : exported.error().message));
  Fs::rename(exported->root(), parent.root() / "relocated animation");
  Fs::remove_all(project.files()->root());
  auto package = GamePackage::open(
                     Core::FileSystem::open(parent.root() / "relocated animation/game").value())
                     .value();
  auto session = GameSession::createCompiled(package.files().value(), package.description().value(),
                                             package.scene().value(), package.program().value())
                     .value();
  auto started = session.start().value();
  CHECK(started.failures.empty());
  CHECK_FALSE(started.realmFaulted);

  for (int frame = 0; frame < 30; ++frame) {
    auto updated = session.update(1.0 / 60).value();
    REQUIRE(updated.frame.failures.empty());
    REQUIRE_FALSE(updated.frame.realmFaulted);
  }

  CHECK(session.scene().value()->findEntity(2).has_value());
  CHECK(
      session.scene().value()->transform(session.scene().value()->findEntity(2).value())->position.x
      > .1F);
  auto closed = session.close().value();
  CHECK(closed.failures.empty());
  CHECK_FALSE(closed.realmFaulted);
  CHECK(Core::processResources().children == 0);
  CHECK(Core::processResources().handles == 0);
}
