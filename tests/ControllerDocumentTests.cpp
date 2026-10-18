#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>

import Dot.Animation.ControllerFiles;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.ControllerDocument;
import Dot.Project;

namespace {
  using namespace Dot;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Project::Project project
        = Project::Project::create(storage.files().value(), "Controller").value();
    Core::FileSystem files = project.files().value();
    Animation::Controller graph
        = Animation::Controller::create(
              {"Locomotion", "Idle", {{"Speed", 0.0}}, {{"Idle", "assets/idle.dotanimation"}}, {}})
              .value();
    std::string path{"assets/motion.dotcontroller"};
    std::string original = "\n" + Animation::encodeController(graph).value() + "\n";

    Fixture() {
      files.writeTextAtomic(path, original).value();
    }

    auto renamed(std::string name) const -> Animation::Controller {
      auto spec = *graph.description().value();
      spec.name = std::move(name);
      return Animation::Controller::create(std::move(spec)).value();
    }
  };
}  // namespace

TEST_CASE("controller_document.history_noops_exact_undo_save_and_owned_lifetime") {
  Fixture f;
  auto doc = Editor::ControllerDocument::open(f.project, f.path).value();
  REQUIRE(doc.replace(f.renamed("Walking"), 1));
  CHECK(doc.snapshot()->revision == 2);
  REQUIRE(doc.undo(2));
  CHECK_FALSE(doc.snapshot()->dirty);
  CHECK(doc.snapshot()->canRedo);
  auto runtime
      = f.graph.withParameters(std::array{Animation::ControllerParameter{"Speed", 1.0}}).value();
  REQUIRE(doc.replace(runtime, 3));
  CHECK(doc.snapshot()->revision == 3);
  CHECK(doc.snapshot()->canRedo);
  CHECK(doc.snapshot()->value.snapshot()->parameters.front().value
        == Animation::ParameterValue{0.0});
  REQUIRE(doc.redo(3));
  REQUIRE(doc.save(4));
  CHECK_FALSE(doc.snapshot()->dirty);
  CHECK(doc.snapshot()->revision == 5);
  CHECK(Animation::loadController(f.files, f.path)->description().value()->name == "Walking");
  REQUIRE(doc.undo(5));
  REQUIRE(doc.save(6));
  CHECK(f.files.readText(f.path).value() == f.original);
  const auto owned = doc.snapshot().value();
  auto moved = std::move(doc);
  CHECK_FALSE(doc.snapshot());
  CHECK_FALSE(doc.replace(f.graph, 1));
  bool foreignRejected{};
  std::thread caller([&] { foreignRejected = !moved.snapshot() && !moved.save(7); });
  caller.join();
  CHECK(foreignRejected);
  CHECK(owned.value.description().value()->name == "Locomotion");
  CHECK_FALSE(moved.historyValue(false, 7));
}

TEST_CASE("controller_document.conflicts_budgets_and_stale_edits_preserve_draft") {
  Fixture f;
  CHECK_FALSE(Editor::ControllerDocument::open(f.project, "scripts/a.dotcontroller"));
  CHECK_FALSE(Editor::ControllerDocument::open(f.project, f.path, {1, 1024}));
  CHECK_FALSE(Editor::ControllerDocument::open(f.project, f.path, {2, 1}));
  auto doc = Editor::ControllerDocument::open(f.project, f.path, {2, 4096}).value();
  REQUIRE(doc.replace(f.renamed("Second"), 1));
  CHECK_FALSE(doc.replace(f.renamed("Stale"), 1));
  CHECK_FALSE(doc.replace({}, 2));
  CHECK(doc.snapshot()->revision == 2);
  REQUIRE(f.files.writeTextAtomic(f.path, "external edit"));
  CHECK_FALSE(doc.save(2));
  CHECK(doc.snapshot()->dirty);
  CHECK(f.files.readText(f.path).value() == "external edit");
  REQUIRE(doc.replace(f.renamed("Third"), 2));
  CHECK(doc.snapshot()->historyStates == 2);
  REQUIRE(doc.undo(3));
  CHECK(doc.snapshot()->value.description().value()->name == "Second");
  CHECK_FALSE(doc.undo(4));
  CHECK(doc.snapshot()->canRedo);
}
