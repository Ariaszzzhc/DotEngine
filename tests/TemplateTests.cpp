#include <doctest/doctest.h>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.Project.Templates;
import Dot.Script.Compiler;

namespace {
  using namespace Dot;

  struct Fixture {
    Core::StagedDirectory storage;

    Fixture()
        : storage(Core::StagedDirectory::create(
                      Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()) {
    }

    auto files() -> Core::FileSystem {
      return storage.files().value();
    }

    auto source() -> Core::FileSystem {
      std::filesystem::copy(DOT_TEMPLATE_PROJECT, files().root() / "source",
                            std::filesystem::copy_options::recursive);
      return Core::FileSystem::open(files().root() / "source").value();
    }

    auto create(const Core::FileSystem& source, std::string_view folder = "project")
        -> Core::Result<Project::Project> {
      auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
      return Project::instantiateTemplate(source, files(), folder, "游戏", compiler);
    }
  };
}  // namespace

TEST_CASE("templates.copy_complete_project_with_engine_imports_without_source_changes") {
  Fixture fixture;
  auto source = fixture.source();
  const auto manifest = source.readText("project.dotproject").value();
  REQUIRE(source.writeTextAtomic("assets/authored.txt", "owned resource"));
  const auto generations = Script::compilerGenerations();
  auto created = fixture.create(source);
  REQUIRE_MESSAGE(created, (created ? "" : created.error().message));
  CHECK(created->description()->name == "游戏");
  CHECK(created->description()->applicationId
        != Project::Project::open(source.root())->description()->applicationId);
  CHECK(created->loadStartup()->entityCount().value()
        == Project::Project::open(source.root())->loadStartup()->entityCount().value());
  CHECK(created->files()->readBytes("assets/collision-duct.dotmesh").value()
        == source.readBytes("assets/collision-duct.dotmesh").value());
  CHECK_FALSE(std::filesystem::exists(created->files()->root() / "scripts/dot"));
  CHECK(created->files()->readText("assets/authored.txt").value() == "owned resource");
  CHECK(source.readText("assets/authored.txt").value() == "owned resource");
  CHECK(source.readText("project.dotproject").value() == manifest);
  CHECK_FALSE(std::filesystem::exists(created->files()->root() / "validation"));
  CHECK(Script::compilerGenerations() == generations);
  auto again = fixture.create(source);
  CHECK_FALSE(again);
  CHECK(created->description()->name == "游戏");
}

TEST_CASE("templates.invalid_sources_paths_and_compile_failures_leave_no_output") {
  Fixture fixture;
  auto source = fixture.source();
  CHECK_FALSE(fixture.create(source, "../escape"));
  auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  CHECK_FALSE(Project::instantiateTemplate(source, source, "nested", "Name", compiler));
  const auto player = source.readText("scripts/camera.ts").value();
  REQUIRE(source.writeTextAtomic("scripts/camera.ts", "export const bad:number='x';"));
  CHECK_FALSE(fixture.create(source));
  CHECK_FALSE(std::filesystem::exists(fixture.files().root() / "project"));
  REQUIRE(source.writeTextAtomic("scripts/camera.ts", player));
  std::filesystem::create_symlink("collision-duct.dotmesh", source.root() / "assets/link.obj");
  CHECK_FALSE(fixture.create(source));
  std::filesystem::remove(source.root() / "assets/link.obj");
  REQUIRE(source.createDirectories(".dot-stage-private"));
  CHECK_FALSE(fixture.create(source));

  for (const auto& entry : std::filesystem::directory_iterator(fixture.files().root())) {
    CHECK_FALSE(entry.path().filename().string().starts_with(".dot-stage-"));
  }
}
