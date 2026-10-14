#include <doctest/doctest.h>

#include <filesystem>
#include <nlohmann/json.hpp>
#include <thread>
#include <utility>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.Document;
import Dot.Editor.TransformTool;
import Dot.Project;
import Dot.Math;

namespace {
  using Json = nlohmann::json;

  struct Fixture {
    Dot::Core::StagedDirectory storage
        = Dot::Core::StagedDirectory::create(
              Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Dot::Project::Project project
        = Dot::Project::Project::create(storage.files().value(), "game", {}).value();
    Dot::Editor::Document document = Dot::Editor::Document::open(project).value();
  };
}  // namespace

TEST_CASE("editor.transform_gesture_owned_preview_one_commit_undo_redo_cancel_and_move") {
  using namespace Dot;
  Fixture f;
  const auto before = f.document.snapshot().value();
  const auto stats = f.document.stats().value();
  auto gesture = Editor::TransformGesture::begin(f.document, stats.revision, 2);
  REQUIRE(gesture);
  const auto* draft = gesture->scene().value();
  auto world = gesture->world().value();
  world.data[12] += 2;
  REQUIRE(gesture->update(world));
  CHECK(f.document.snapshot().value() == before);
  CHECK(gesture->scene().value() == draft);
  auto moved = std::move(*gesture);
  CHECK_FALSE(gesture->world());
  CHECK(moved.scene().value() == draft);
  world.data[12] += 1;
  REQUIRE(moved.update(world));
  auto result = moved.commit(f.document);
  REQUIRE(result);
  CHECK(result->changed);
  CHECK_FALSE(moved.scene());
  CHECK(f.document.stats()->historyEntries == stats.historyEntries + 1);
  const auto changed = f.document.snapshot().value();
  REQUIRE(f.document.undo(f.document.stats()->revision));
  CHECK(Json::parse(f.document.snapshot().value())["scene"] == Json::parse(before)["scene"]);
  REQUIRE(f.document.redo(f.document.stats()->revision));
  CHECK(Json::parse(f.document.snapshot().value())["scene"] == Json::parse(changed)["scene"]);
  const auto afterRedo = f.document.snapshot().value();
  {
    auto cancel
        = Editor::TransformGesture::begin(f.document, f.document.stats()->revision, 2).value();
    world.data[12] += 4;
    REQUIRE(cancel.update(world));
  }

  CHECK(f.document.snapshot().value() == afterRedo);
}

TEST_CASE("editor.transform_gesture_parent_local_mirror_shear_failures_conflicts_and_threads") {
  using namespace Dot;
  Fixture f;
  auto edit = f.document.apply(
      Json::array({{{"op", "create"}, {"name", "Parent"}},
                   {{"op", "transform"},
                    {"entity", "3"},
                    {"value",
                     {{"position", {2, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {-2, 3, 4}}}}},
                   {{"op", "parent"}, {"entity", "2"}, {"parent", "3"}}})
          .dump(),
      f.document.stats()->revision);
  REQUIRE(edit);
  auto gesture
      = Editor::TransformGesture::begin(f.document, f.document.stats()->revision, 2).value();
  auto world = gesture.world().value();
  world.data[12] += 4;
  REQUIRE(gesture.update(world));
  const auto* draft = gesture.scene().value();
  auto child = draft->entity(draft->findEntity(2).value()).value();
  CHECK(child.transform.position.x == doctest::Approx(-2));
  auto invalid = world;
  invalid.data[4] += 0.2F;
  CHECK_FALSE(gesture.update(invalid));
  CHECK(gesture.world().value() == world);
  invalid.data.fill(0);
  CHECK_FALSE(gesture.update(invalid));
  CHECK_FALSE(Editor::TransformGesture::begin(f.document, 0, 2));
  CHECK_FALSE(Editor::TransformGesture::begin(f.document, f.document.stats()->revision, 9000));
  bool threadRejected = false;
  std::thread worker([&] { threadRejected = !gesture.world() && !gesture.update(world); });
  worker.join();
  CHECK(threadRejected);
  Fixture foreign;
  CHECK_FALSE(gesture.commit(foreign.document));
  REQUIRE(
      f.document.apply(Json::array({{{"op", "name"}, {"entity", "2"}, {"name", "Other"}}}).dump(),
                       f.document.stats()->revision));
  const auto latest = f.document.snapshot().value();
  CHECK_FALSE(gesture.commit(f.document));
  CHECK(f.document.snapshot().value() == latest);
}

TEST_CASE("editor.transform_gesture_noop_return_to_origin_has_no_history_entry") {
  Fixture f;
  const auto before = f.document.snapshot().value();
  auto gesture
      = Dot::Editor::TransformGesture::begin(f.document, f.document.stats()->revision, 2).value();
  const auto start = gesture.world().value();
  auto changed = start;
  changed.data[13] += 4;
  REQUIRE(gesture.update(changed));
  REQUIRE(gesture.update(start));
  auto result = gesture.commit(f.document);
  REQUIRE(result);
  CHECK_FALSE(result->changed);
  CHECK(f.document.snapshot().value() == before);
}

TEST_CASE("editor.transform_gesture_rotation_scaling_pivot_local_parent_and_shear_boundaries") {
  using namespace Dot;
  Fixture f;
  REQUIRE(f.document.apply(
      Json::array({{{"op", "create"}, {"name", "Parent"}},
                   {{"op", "transform"},
                    {"entity", "3"},
                    {"value",
                     {{"position", {2, 1, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {-2, 3, 4}}}}},
                   {{"op", "transform"},
                    {"entity", "2"},
                    {"value",
                     {{"position", {1, 2, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {-1, 2, 1}}}}},
                   {{"op", "parent"}, {"entity", "2"}, {"parent", "3"}}})
          .dump(),
      f.document.stats()->revision));
  const auto original = f.document.snapshot().value();
  auto gesture
      = Editor::TransformGesture::begin(f.document, f.document.stats()->revision, 2).value();
  const auto start = gesture.world().value();
  REQUIRE(gesture.rotate(2, 30, true));
  const auto initialDraft = gesture.scene().value();
  CHECK(initialDraft->entity(initialDraft->findEntity(2).value())->transform.rotation.z
        == doctest::Approx(0.258819F));
  REQUIRE(gesture.rotate(2, 90, true));
  CHECK(gesture.world()->data[12] == start.data[12]);
  CHECK(gesture.world()->data[13] == start.data[13]);
  CHECK(f.document.snapshot().value() == original);
  auto draft = gesture.scene().value();
  auto child = draft->entity(draft->findEntity(2).value()).value();
  CHECK(child.transform.scale.x == -1);
  CHECK(child.transform.scale.y == 2);
  CHECK(Math::eulerDegrees(child.transform.rotation)->z == doctest::Approx(90));
  REQUIRE(gesture.scale(0, 2, true));
  child = draft->entity(draft->findEntity(2).value()).value();
  CHECK(child.transform.scale.x == -2);
  CHECK(child.transform.rotation.w == 1);
  REQUIRE(gesture.rotate(2, 0, true));
  CHECK(gesture.world().value() == start);
  CHECK_FALSE(gesture.rotate(2, 45, false));
  CHECK(gesture.world().value() == start);
  CHECK_FALSE(gesture.scale(0, 0, true));
  CHECK_FALSE(gesture.scale(0, -1, true));
  CHECK_FALSE(gesture.rotate(3, 10, true));
  auto frame = Editor::transformFrame(*draft, draft->findEntity(2).value(), true).value();
  CHECK(frame.data[0] == -2);
  CHECK(frame.data[5] == 3);
  CHECK(frame.data[12] == start.data[12]);
  REQUIRE(gesture.scale(1, 1.5F, false));
  CHECK(gesture.world()->data[12] == start.data[12]);
  REQUIRE(gesture.commit(f.document));
  CHECK(f.document.stats()->historyEntries
        == Json::parse(original)["historyEntries"].get<int>() + 1);
}
