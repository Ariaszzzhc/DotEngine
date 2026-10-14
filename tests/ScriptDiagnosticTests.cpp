#include <doctest/doctest.h>

#include <string>

import Dot.Editor.ScriptDiagnostics;

TEST_CASE("script_diagnostics.map_only_project_sources_and_preserve_real_positions") {
  const auto diagnostics
      = Dot::Editor::scriptDiagnostics(
            "TypeScript compile exit 1:\nsrc/actor(1).ts(12,3): error TS2322: bad type\n"
            "src\\nested\\名字.mts(1,2): error TS2304: unknown\n"
            "engine-types/module-0.d.ts(2,2): bad SDK\n"
            "src/../outside.ts(1,1): unsafe\n"
            "src/fake.ts(0,2): bad position\n"
            "src/fake.ts(4294967296,1): overflow\n",
            "game-scripts")
            .value();
  REQUIRE(diagnostics.size() == 2);
  CHECK(diagnostics[0].path == "game-scripts/actor(1).ts");
  CHECK(diagnostics[0].line == 12);
  CHECK(diagnostics[0].column == 3);
  CHECK(diagnostics[0].message == "error TS2322: bad type");
  CHECK(diagnostics[1].path == "game-scripts/nested/名字.mts");
}

TEST_CASE("script_diagnostics.boundaries_never_publish_partial_or_unsafe_actions") {
  CHECK(Dot::Editor::scriptDiagnostics("Nonpositional graph error", "scripts")->empty());
  CHECK_FALSE(Dot::Editor::scriptDiagnostics(std::string("A\0B", 3), "scripts"));
  CHECK_FALSE(Dot::Editor::scriptDiagnostics(std::string(2 * 1024 * 1024 + 1, 'x'), "scripts"));
  CHECK_FALSE(Dot::Editor::scriptDiagnostics("", "../scripts"));
  std::string many;

  for (int index = 0; index < 4096; ++index) {
    many += "src/main.ts(1,1): error TS2322: bad\n";
  }

  CHECK(Dot::Editor::scriptDiagnostics(many, "scripts")->size() == 4096);
  many += "src/main.ts(1,1): error TS2322: bad\n";
  CHECK_FALSE(Dot::Editor::scriptDiagnostics(many, "scripts"));
}
