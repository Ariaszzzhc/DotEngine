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
import Dot.Script.Compiler;
import Dot.Script.Runtime;

namespace {
  using namespace Dot::Script;
  using Json = nlohmann::json;

  struct Fixture {
    Dot::Core::StagedDirectory storage;

    Fixture()
        : storage(Dot::Core::StagedDirectory::create(
                      Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()) {
    }

    auto files() -> Dot::Core::FileSystem {
      return storage.files().value();
    }

    auto compiled() -> CompiledProgram {
      auto access = files();
      REQUIRE(access.createDirectories("scripts/nested"));
      REQUIRE(access.writeTextAtomic(
          "scripts/main.ts",
          "import {value} from './nested/other';export function run(){return value+1;}"));
      REQUIRE(access.writeTextAtomic("scripts/nested/other.ts", "export const value=6;"));
      return TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER)->compileProject(access).value();
    }
  };

  auto run(const CompiledProgram& program) -> std::string {
    auto vm = Runtime::create(program.files().value()).value();
    auto module = vm.loadModule("main.js").value();
    return vm.call(module, "run").value().value();
  }
}  // namespace

TEST_CASE("published_program.executes_without_sources_tools_and_captures_immutable_bytes") {
  Fixture fixture;
  const auto generations = compilerGenerations();
  auto compiled = fixture.compiled();
  auto published = compiled.publish(fixture.files(), "program").value();
  auto loaded = CompiledProgram::loadPublished(published).value();
  CHECK(loaded.modules().value() == std::vector<std::string>{"main.js", "nested/other.js"});
  CHECK(run(loaded) == "7");
  CHECK_FALSE(std::filesystem::exists(published.root() / "modules/main.js.map"));
  CHECK(published.readText("modules/main.js")->find("sourceMappingURL") == std::string::npos);
  std::filesystem::remove_all(fixture.files().root() / "scripts");
  REQUIRE(published.writeTextAtomic("modules/nested/other.js", "broken"));
  CHECK(run(loaded) == "7");
  CHECK_FALSE(CompiledProgram::loadPublished(published));
  const auto snapshot = loaded.files().value().root();
  {
    auto moved = std::move(loaded);
    CHECK_FALSE(loaded.files());
    CHECK_FALSE(loaded.publish(fixture.files(), "bad"));
    CHECK(run(moved) == "7");
  }
  CHECK_FALSE(std::filesystem::exists(snapshot));
  CHECK(compilerGenerations() == generations + 1);
  CHECK(Dot::Core::processResources().children == 0);
  CHECK(Dot::Core::processResources().handles == 0);
}

TEST_CASE("published_program.strict_manifest_limits_and_paths_reject_without_generations") {
  Fixture fixture;
  auto compiled = fixture.compiled();
  auto published = compiled.publish(fixture.files(), "program").value();
  const auto original = Json::parse(published.readText("program.json").value());
  const auto generations = compilerGenerations();
  std::vector<Json> invalid;
  auto value = original;
  value["version"] = 2;
  invalid.push_back(value);
  value = original;
  value["extra"] = 0;
  invalid.push_back(value);
  value = original;
  value["sourceFiles"] = -1;
  invalid.push_back(value);
  value = original;
  value["modules"][0]["path"] = "../escape.js";
  invalid.push_back(value);
  value = original;
  value["modules"][0]["path"] = "COM¹.js";
  invalid.push_back(value);
  value = original;
  value["modules"][1] = value["modules"][0];
  invalid.push_back(value);
  value = original;
  value["modules"][0]["path"] = "Nested/main.js";
  invalid.push_back(value);
  value = original;
  value["modules"][0]["bytes"] = 0;
  invalid.push_back(value);
  value = original;
  value["outputBytes"] = 1;
  invalid.push_back(value);

  for (const auto& manifest : invalid) {
    REQUIRE(published.writeTextAtomic("program.json", manifest.dump()));
    CHECK_FALSE(CompiledProgram::loadPublished(published));
    CHECK(compilerGenerations() == generations);
  }

  REQUIRE(published.writeTextAtomic("program.json", "{\"version\":1,\"version\":1}"));
  CHECK_FALSE(CompiledProgram::loadPublished(published));
  REQUIRE(published.writeTextAtomic("program.json", original.dump()));
  CompilerLimits limits;
  limits.outputBytes = 1;
  CHECK_FALSE(CompiledProgram::loadPublished(published, limits));
  limits = {};
  limits.directoryEntries = 1;
  CHECK_FALSE(CompiledProgram::loadPublished(published, limits));
  limits = {};
  limits.fileBytes = 1;
  CHECK_FALSE(CompiledProgram::loadPublished(published, limits));
  CHECK(compilerGenerations() == generations);
}

TEST_CASE("published_program_tree_missing_extra_symlink_and_invalid_bytes_fail") {
  Fixture fixture;
  auto compiled = fixture.compiled();
  auto published = compiled.publish(fixture.files(), "program").value();
  const auto generations = compilerGenerations();
  const auto original = published.readText("modules/main.js").value();
  REQUIRE(published.writeTextAtomic("unexpected.txt", "extra"));
  CHECK_FALSE(CompiledProgram::loadPublished(published));
  std::filesystem::remove(published.root() / "unexpected.txt");
  std::filesystem::remove(published.root() / "modules/main.js");
  CHECK_FALSE(CompiledProgram::loadPublished(published));
  std::filesystem::create_symlink("nested/other.js", published.root() / "modules/main.js");
  CHECK_FALSE(CompiledProgram::loadPublished(published));
  std::filesystem::remove(published.root() / "modules/main.js");
  REQUIRE(published.writeTextAtomic("modules/main.js", std::string(original.size(), '\0')));
  CHECK_FALSE(CompiledProgram::loadPublished(published));
  REQUIRE(published.writeTextAtomic("modules/main.js",
                                    std::string(original.size(), static_cast<char>(0xFF))));
  CHECK_FALSE(CompiledProgram::loadPublished(published));
  CHECK(compilerGenerations() == generations);
}

TEST_CASE("published_program_publication_never_overwrites_and_failure_cleans_staging") {
  Fixture fixture;
  auto compiled = fixture.compiled();
  auto published = compiled.publish(fixture.files(), "program").value();
  const auto before = published.readText("program.json").value();
  CHECK_FALSE(compiled.publish(fixture.files(), "program"));
  CHECK(published.readText("program.json").value() == before);
  CHECK_FALSE(compiled.publish(fixture.files(), "../escape"));
  CHECK_FALSE(compiled.publish(compiled.files().value(), "nested-output"));
  auto source = compiled.files().value();
  REQUIRE(source.writeTextAtomic("main.js", std::string(1024 * 1024 + 1, ' ')));
  CHECK_FALSE(compiled.publish(fixture.files(), "oversized"));
  CHECK_FALSE(std::filesystem::exists(fixture.files().root() / "oversized"));
  REQUIRE(source.writeTextAtomic("main.js", std::string(1, static_cast<char>(0xFF))));
  CHECK_FALSE(compiled.publish(fixture.files(), "failed"));
  CHECK_FALSE(std::filesystem::exists(fixture.files().root() / "failed"));

  for (const auto& entry : std::filesystem::directory_iterator(fixture.files().root())) {
    CHECK_FALSE(entry.path().filename().string().starts_with(".dot-stage-"));
  }
}
