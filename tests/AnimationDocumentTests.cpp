#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

import Dot.Animation.Files;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.AnimationDocument;
import Dot.Editor.Resources;
import Dot.Project;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Project::Project project = Project::Project::create(storage.files().value(), "project").value();
    Core::FileSystem files = project.files().value();
    std::string path = "assets/move.dotanimation";
    Clip value = Clip::create({"Move",
                               {{0,
                                 Path::Translation,
                                 Interpolation::Linear,
                                 {{0, {0, 0, 0, 0}}, {2, {2, 0, 0, 0}}}}}})
                     .value();
    std::string original = "\n" + encodeClip(value).value() + "\n";

    Fixture() {
      files.writeTextAtomic(path, original).value();
    }

    auto open(Editor::AnimationHistoryLimits limits = {}) -> Editor::AnimationDocument {
      return Editor::AnimationDocument::open(project, path, limits).value();
    }
  };

  auto rename(Editor::AnimationDocument& doc, std::string name) -> Core::Result<void> {
    std::vector<ClipEdit> changes{RenameClip{std::move(name)}};
    return doc.edit(changes, doc.snapshot()->revision);
  }
}  // namespace

TEST_CASE("animation_document.owned_draft_history_save_and_exact_source_undo") {
  Fixture f;
  auto doc = f.open();
  auto initial = doc.snapshot().value();
  CHECK_FALSE(initial.dirty);
  REQUIRE(doc.replace(initial.value, initial.revision));
  CHECK(doc.snapshot()->revision == initial.revision);
  REQUIRE(rename(doc, "Edited"));
  const auto candidateRevision = doc.snapshot()->revision;
  CHECK(doc.historyValue(false, candidateRevision)->description().value()->name == "Move");
  CHECK(doc.snapshot()->revision == candidateRevision);
  CHECK_FALSE(doc.historyValue(true, candidateRevision));
  CHECK_FALSE(doc.historyValue(false, 0));
  CHECK(doc.snapshot()->dirty);
  CHECK(f.files.readText(f.path).value() == f.original);
  CHECK_FALSE(doc.replace(initial.value, initial.revision));
  CHECK_FALSE(doc.replace(doc.snapshot()->value, 0));
  CHECK_FALSE(doc.save(0));
  CHECK_FALSE(doc.undo(0));
  REQUIRE(doc.save(doc.snapshot()->revision));
  CHECK_FALSE(doc.snapshot()->dirty);
  CHECK(loadClip(f.files, f.path)->description().value()->name == "Edited");
  REQUIRE(doc.undo(doc.snapshot()->revision));
  CHECK(doc.snapshot()->dirty);
  REQUIRE(doc.save(doc.snapshot()->revision));
  CHECK(f.files.readText(f.path).value() == f.original);
  REQUIRE(doc.redo(doc.snapshot()->revision));
  CHECK(doc.snapshot()->dirty);
  REQUIRE(doc.undo(doc.snapshot()->revision));
  auto revision = doc.snapshot()->revision;
  REQUIRE(doc.replace(initial.value, revision));
  CHECK(doc.snapshot()->revision == revision);
  CHECK(doc.snapshot()->canRedo);
  REQUIRE(rename(doc, "Branch"));
  CHECK_FALSE(doc.snapshot()->canRedo);
  CHECK(initial.value.description().value()->name == "Move");
}

TEST_CASE("animation_document.invalid_edits_and_external_conflicts_preserve_draft_and_disk") {
  Fixture f;
  auto doc = f.open();
  CHECK_FALSE(doc.replace({}, doc.snapshot()->revision));
  CHECK_FALSE(doc.undo(doc.snapshot()->revision));
  CHECK_FALSE(doc.redo(doc.snapshot()->revision));
  const std::vector<ClipEdit> invalid{RemoveKey{0, Path::Translation, 1}};
  CHECK_FALSE(doc.edit(invalid, doc.snapshot()->revision));
  CHECK(doc.snapshot()->revision == 1);
  REQUIRE(rename(doc, "Draft"));
  REQUIRE(f.files.writeTextAtomic(f.path, f.original + "\n"));
  const auto revision = doc.snapshot()->revision;
  CHECK_FALSE(doc.save(revision));
  CHECK(doc.snapshot()->revision == revision);
  CHECK(doc.snapshot()->dirty);
  CHECK(doc.snapshot()->value.description().value()->name == "Draft");
  CHECK(f.files.readText(f.path).value() == f.original + "\n");
  REQUIRE(doc.undo(revision));
  CHECK_FALSE(doc.snapshot()->dirty);
  CHECK_FALSE(doc.save(doc.snapshot()->revision));
}

TEST_CASE("animation_document.state_and_serialized_byte_limits_evict_old_history") {
  Fixture f;
  auto doc = f.open({3, 4096});
  REQUIRE(rename(doc, "A"));
  REQUIRE(rename(doc, "B"));
  REQUIRE(rename(doc, "C"));
  CHECK(doc.snapshot()->historyStates == 3);
  REQUIRE(doc.undo(doc.snapshot()->revision));
  REQUIRE(doc.undo(doc.snapshot()->revision));
  CHECK_FALSE(doc.undo(doc.snapshot()->revision));
  CHECK(doc.snapshot()->value.description().value()->name == "A");
  auto bytes = f.original.size();
  auto small = f.open({128, bytes + 10});
  REQUIRE(rename(small, "A"));
  CHECK(small.snapshot()->historyStates == 1);
  CHECK(small.snapshot()->historyBytes <= bytes + 10);
  CHECK_FALSE(small.undo(small.snapshot()->revision));
  const auto revision = small.snapshot()->revision;
  CHECK_FALSE(rename(small, std::string(256, 'x')));
  CHECK(small.snapshot()->revision == revision);
  CHECK_FALSE(Editor::AnimationDocument::open(f.project, f.path, {1, 4096}));
  CHECK_FALSE(Editor::AnimationDocument::open(f.project, f.path, {129, 4096}));
  CHECK_FALSE(Editor::AnimationDocument::open(f.project, f.path, {128, 0}));
  CHECK_FALSE(Editor::AnimationDocument::open(f.project, f.path, {128, 128 * 1024 * 1024 + 1}));
  CHECK_FALSE(Editor::AnimationDocument::open(f.project, f.path, {128, bytes - 1}));
}

TEST_CASE("animation_document.large_native_resources_do_not_expand_default_text_limits") {
  Fixture f;
  const auto large = std::string(1024 * 1024 + 1, ' ') + f.original;
  REQUIRE(f.files.writeTextAtomic(f.path, large));
  auto ordinary = Editor::ResourceStore::open(f.project).value();
  CHECK_FALSE(ordinary.openText(f.path));
  auto doc = f.open();
  CHECK(doc.snapshot()->historyBytes == large.size());
  REQUIRE(rename(doc, "Large"));
  REQUIRE(doc.undo(doc.snapshot()->revision));
  REQUIRE(doc.save(doc.snapshot()->revision));
  CHECK(f.files.readText(f.path, 2 * 1024 * 1024).value() == large);
  Editor::ResourceLimits limits;
  limits.textBytes = 64 * 1024 * 1024 + 1;
  CHECK_FALSE(Editor::ResourceStore::open(f.project, limits));
}

TEST_CASE("animation_document.path_failures_thread_moves_and_retired_sources_are_explicit") {
  Fixture f;
  CHECK_FALSE(Editor::AnimationDocument::open(f.project, "scripts/a.dotanimation"));
  CHECK_FALSE(Editor::AnimationDocument::open(f.project, "assets/missing.dotanimation"));
  CHECK_FALSE(Editor::AnimationDocument::open(f.project, "assets/../assets/move.dotanimation"));
  REQUIRE(f.files.writeTextAtomic("assets/bad.dotanimation", "{}"));
  CHECK_FALSE(Editor::AnimationDocument::open(f.project, "assets/bad.dotanimation"));
  auto doc = f.open();
  auto state = doc.snapshot().value();
  bool rejected{};
  std::thread worker([&] {
    rejected = !doc.snapshot() && !doc.replace(state.value, state.revision)
               && !doc.edit({}, state.revision) && !doc.save(state.revision)
               && !doc.undo(state.revision) && !doc.redo(state.revision);
    rejected = rejected && !doc.historyValue(false, state.revision);
  });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(doc);
  CHECK_FALSE(doc.snapshot());
  CHECK_FALSE(doc.replace(state.value, 1));
  CHECK_FALSE(doc.edit({}, 1));
  CHECK_FALSE(doc.undo(1));
  CHECK_FALSE(doc.redo(1));
  CHECK_FALSE(doc.save(1));
  CHECK_FALSE(doc.historyValue(false, 1));
  REQUIRE(std::filesystem::remove(f.files.root() / f.path));
  REQUIRE(rename(moved, "Owned"));
  CHECK_FALSE(moved.save(moved.snapshot()->revision));
  CHECK(moved.snapshot()->dirty);
  CHECK(state.value.sample(1).value()[0].value.x == 1);
}

TEST_CASE("animation_document.destruction_never_saves_and_snapshots_outlive_the_document") {
  Fixture f;
  auto state = [&] {
    auto doc = f.open();
    REQUIRE(rename(doc, "Unsaved"));
    return doc.snapshot().value();
  }();
  CHECK(f.files.readText(f.path).value() == f.original);
  CHECK(state.value.description().value()->name == "Unsaved");
  CHECK(state.value.sample(1).value()[0].value.x == 1);
}

TEST_CASE("animation_document.key_move_retains_count_history_and_failed_collision_state") {
  Fixture f;
  auto doc = f.open();
  auto original = doc.snapshot().value();
  auto key = original.value.description().value()->tracks[0].keys[0];
  key.time = 1;
  std::vector<ClipEdit> changes{UpdateKey{0, Path::Translation, 0, key}};
  REQUIRE(doc.edit(changes, original.revision));
  auto changed = doc.snapshot().value();
  CHECK(changed.value.description().value()->tracks[0].keys.size() == 2);
  CHECK(changed.historyStates == 2);
  key.time = 2;
  changes = {UpdateKey{0, Path::Translation, 1, key}};
  CHECK_FALSE(doc.edit(changes, changed.revision));
  CHECK(doc.snapshot()->revision == changed.revision);
  CHECK(doc.snapshot()->historyStates == changed.historyStates);
  CHECK(doc.snapshot()->historyBytes == changed.historyBytes);
  REQUIRE(doc.undo(changed.revision));
  CHECK(doc.snapshot()->value.description().value()->tracks[0].keys[0].time == 0);
  REQUIRE(doc.redo(doc.snapshot()->revision));
  REQUIRE(doc.save(doc.snapshot()->revision));
  CHECK(loadClip(f.files, f.path)->description().value()->tracks[0].keys[0].time == 1);
  CHECK(original.value.description().value()->tracks[0].keys[0].time == 0);
}
