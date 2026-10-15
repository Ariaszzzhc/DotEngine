#include <doctest/doctest.h>

#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.MaterialDocument;
import Dot.Project;
import Dot.Graphics.RenderQueue;

namespace {
  using namespace Dot;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Project::Project project
        = Project::Project::create(storage.files().value(), "project", {}).value();
    Core::FileSystem files = project.files().value();
    std::string initial = Assets::encodeMaterial({}).value();

    Fixture() {
      REQUIRE(files.writeTextAtomic("assets/surface.dotmaterial", initial));
    }

    auto open() -> Editor::MaterialDocument {
      return Editor::MaterialDocument::open(project, "assets/surface.dotmaterial").value();
    }
  };
}  // namespace

TEST_CASE("material_document.owned_history_noops_save_undo_redo_and_branch") {
  Fixture f;
  auto doc = f.open();
  auto original = doc.snapshot().value();
  REQUIRE(doc.replace(original.value, original.revision));
  CHECK(doc.snapshot()->revision == original.revision);
  auto value = original.value;
  value.roughness = .2F;
  REQUIRE(doc.replace(value, original.revision));
  CHECK(f.files.readText(original.path).value() == f.initial);
  CHECK(doc.snapshot()->dirty);
  CHECK_FALSE(doc.replace(value, original.revision));
  REQUIRE(doc.save(doc.snapshot()->revision));
  CHECK_FALSE(doc.snapshot()->dirty);
  REQUIRE(doc.undo(doc.snapshot()->revision));
  CHECK(doc.snapshot()->dirty);
  CHECK(doc.snapshot()->value.roughness == .5F);
  REQUIRE(doc.redo(doc.snapshot()->revision));
  CHECK_FALSE(doc.snapshot()->dirty);
  REQUIRE(doc.undo(doc.snapshot()->revision));
  value.metallic = .9F;
  REQUIRE(doc.replace(value, doc.snapshot()->revision));
  CHECK_FALSE(doc.snapshot()->canRedo);
  REQUIRE(doc.undo(doc.snapshot()->revision));
  REQUIRE(doc.save(doc.snapshot()->revision));
  CHECK(f.files.readText(original.path).value() == f.initial);
}

TEST_CASE("material_document.invalid_boundaries_conflicts_and_lifetime_preserve_state") {
  Fixture f;
  CHECK_FALSE(Editor::MaterialDocument::open(f.project, "scripts/a.dotmaterial"));
  CHECK_FALSE(Editor::MaterialDocument::open(f.project, "assets/missing.dotmaterial"));
  REQUIRE(f.files.writeTextAtomic("assets/bad.dotmaterial", "{}"));
  CHECK_FALSE(Editor::MaterialDocument::open(f.project, "assets/bad.dotmaterial"));
  auto doc = f.open();
  auto state = doc.snapshot().value();
  auto bad = state.value;
  bad.roughness = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(doc.replace(bad, state.revision));
  CHECK(doc.snapshot()->revision == state.revision);
  CHECK_FALSE(doc.undo(state.revision));
  CHECK_FALSE(doc.redo(state.revision));
  auto value = state.value;
  value.alphaMode = Graphics::AlphaMode::Blend;
  REQUIRE(doc.replace(value, state.revision));
  REQUIRE(f.files.writeTextAtomic(state.path, f.initial + "\n"));
  CHECK_FALSE(doc.save(doc.snapshot()->revision));
  CHECK(doc.snapshot()->dirty);
  CHECK(f.files.readText(state.path).value() == f.initial + "\n");
  bool rejected{};
  std::jthread worker([&] { rejected = !doc.snapshot() && !doc.undo(1); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(doc);
  CHECK_FALSE(doc.snapshot());
  CHECK(moved.snapshot()->value.alphaMode == Graphics::AlphaMode::Blend);
  CHECK_FALSE(doc.replace(value, 1));
  CHECK_FALSE(doc.save(1));
  CHECK_FALSE(doc.redo(1));
}

TEST_CASE("material_document.bounded_history_and_destruction_never_save") {
  Fixture f;

  {
    auto doc = f.open();
    auto value = doc.snapshot()->value;

    for (int i = 0; i < 140; ++i) {
      value.roughness = static_cast<float>(i) / 150;
      REQUIRE(doc.replace(value, doc.snapshot()->revision));
    }

    int undos{};

    while (doc.snapshot()->canUndo) {
      REQUIRE(doc.undo(doc.snapshot()->revision));
      ++undos;
    }

    CHECK(undos == 127);
  }

  CHECK(f.files.readText("assets/surface.dotmaterial").value() == f.initial);
}
