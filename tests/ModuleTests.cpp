#include <doctest/doctest.h>

#include <filesystem>
#include <string>

import Dot.Script.Modules;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;

TEST_CASE("modules.engine_registry_owns_matching_source_and_exact_reserved_names") {
  const auto modules = Dot::Script::engineModules();
  REQUIRE(modules.size() == 2);
  auto engine = Dot::Script::engineModule("dot:engine");
  REQUIRE(engine);
  CHECK(engine->source.find("ScriptComponent") != std::string_view::npos);
  CHECK(engine->declarations.find("declare module 'dot:engine'") != std::string_view::npos);
  CHECK(engine->declarations.find("export class ScriptComponent") != std::string_view::npos);
  CHECK(Dot::Script::engineModule("dot:host")->source.empty());
  CHECK_FALSE(Dot::Script::engineModule("dot:unknown"));
  CHECK_FALSE(Dot::Script::engineModule("dot:engine.js"));
  CHECK_FALSE(Dot::Script::engineModule("Dot:engine"));
}

TEST_CASE("modules.project_resolution_is_extensionless_unique_rooted_and_exact_case") {
  auto parent = Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value();
  auto storage = Dot::Core::StagedDirectory::create(parent).value();
  auto files = storage.files().value();
  REQUIRE(files.createDirectories("nested"));
  REQUIRE(files.writeTextAtomic("counter.js", "export const value=42;"));
  REQUIRE(files.writeTextAtomic("名字.mjs", "export {};"));
  REQUIRE(files.writeTextAtomic("some.name.js", "export {};"));
  CHECK(Dot::Script::resolveImport(files, "nested/main.js", "../counter").value() == "counter.js");
  CHECK(Dot::Script::resolveImport(files, "main.js", "./名字").value() == "名字.mjs");
  CHECK(Dot::Script::resolveImport(files, "main.js", "./some.name").value() == "some.name.js");
  CHECK(Dot::Script::resolveImport(files, "main.js", "dot:engine").value() == "dot:engine");

  for (const auto* path :
       {"./counter.js", "./counter.ts", "counter", "../escape", "./missing", "./Counter",
        "./nested/", "./nested//counter", "dot:unknown", "node:fs"}) {
    CHECK_FALSE(Dot::Script::resolveImport(files, "main.js", path));
  }

  REQUIRE(files.writeTextAtomic("counter.mjs", "export {};"));
  CHECK_FALSE(Dot::Script::resolveImport(files, "main.js", "./counter"));
  std::filesystem::create_symlink("some.name.js", files.root() / "linked.js");
  CHECK_FALSE(Dot::Script::resolveImport(files, "main.js", "./linked"));
}
