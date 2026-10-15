#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.Document;
import Dot.Editor.TransformTool;
import Dot.Project;
import Dot.Math;
import Dot.World.SceneFiles;
import Dot.World.Scene;

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

TEST_CASE("editor.group_transform_topmost_roots_active_child_pivot_and_absolute_samples") {
  using namespace Dot;
  Fixture f;
  REQUIRE(f.document.apply(
      Json::array(
          {{{"op", "create"}, {"name", "Parent"}},
           {{"op", "create"}, {"name", "Descendant"}},
           {{"op", "create"}, {"name", "Peer"}},
           {{"op", "parent"}, {"entity", "2"}, {"parent", "3"}},
           {{"op", "parent"}, {"entity", "4"}, {"parent", "2"}},
           {{"op", "transform"},
            {"entity", "3"},
            {"value",
             {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {-2, 3, 1}}}}},
           {{"op", "transform"},
            {"entity", "2"},
            {"value", {{"position", {1, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}},
           {{"op", "transform"},
            {"entity", "5"},
            {"value",
             {{"position", {6, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}})
          .dump(),
      f.document.stats()->revision));
  const auto scene = f.document.scene().value();
  const std::array selected{scene->findEntity(3).value(), scene->findEntity(5).value(),
                            scene->findEntity(2).value()};
  REQUIRE(f.document.selectMany(selected, f.document.stats()->revision));
  const auto before = f.document.snapshot().value();
  auto gesture
      = Editor::TransformGesture::beginSelection(f.document, f.document.stats()->revision).value();
  CHECK(gesture.entities().size() == 3);
  CHECK(gesture.rootCount() == 2);
  CHECK(gesture.entity() == 2);
  const auto draft = gesture.scene().value();
  const auto world = [&](std::uint64_t id) {
    return draft->worldTransform(draft->findEntity(id).value()).value();
  };
  const auto start = gesture.world().value();
  auto translated = start;
  translated.data[12] += 4;
  REQUIRE(gesture.update(translated));
  CHECK(world(3).data[12] == doctest::Approx(4));
  CHECK(world(2).data[12] == doctest::Approx(2));
  CHECK(world(4).data[12] == doctest::Approx(2));
  CHECK(world(5).data[12] == doctest::Approx(10));
  REQUIRE(gesture.rotate(2, 90, false));
  CHECK(world(3).data[12] == doctest::Approx(-2));
  CHECK(world(3).data[13] == doctest::Approx(2));
  CHECK(world(2).data[12] == doctest::Approx(-2));
  CHECK(world(2).data[13] == doctest::Approx(0).epsilon(.00001));
  CHECK(world(5).data[12] == doctest::Approx(-2));
  CHECK(world(5).data[13] == doctest::Approx(8));
  CHECK(draft->entity(draft->findEntity(2).value())->transform.position.x == 1);
  REQUIRE(gesture.scale(0, 2, false));
  CHECK(world(3).data[12] == doctest::Approx(2));
  CHECK(world(5).data[12] == doctest::Approx(14));
  CHECK(f.document.snapshot().value() == before);
  REQUIRE(gesture.update(start));
  CHECK_FALSE(gesture.commit(f.document)->changed);
  CHECK(f.document.snapshot().value() == before);
  CHECK(gesture.entities().empty());
  auto changed
      = Editor::TransformGesture::beginSelection(f.document, f.document.stats()->revision).value();
  REQUIRE(changed.rotate(2, 90, false));
  REQUIRE(changed.commit(f.document));
  CHECK(f.document.stats()->historyEntries == Json::parse(before)["historyEntries"].get<int>() + 1);
  REQUIRE(f.document.undo(f.document.stats()->revision));
  CHECK(Json::parse(f.document.snapshot().value())["scene"] == Json::parse(before)["scene"]);
  CHECK(Json::parse(f.document.snapshot().value())["selection"] == Json::array({"3", "5", "2"}));
  REQUIRE(f.document.redo(f.document.stats()->revision));
  CHECK(f.document.scene()
            .value()
            ->worldTransform(f.document.scene().value()->findEntity(5).value())
            ->data[13]
        == doctest::Approx(8));
}

TEST_CASE("editor.group_transform_local_frame_orbits_and_scales_offsets_with_active_axes") {
  using namespace Dot;
  Fixture f;
  REQUIRE(f.document.apply(
      Json::array({{{"op", "create"}, {"name", "Peer"}},
                   {{"op", "transform"},
                    {"entity", "2"},
                    {"value",
                     {{"position", {0, 0, 0}},
                      {"rotation", {0, 0, .70710678, .70710678}},
                      {"scale", {2, 1, 1}}}}},
                   {{"op", "transform"},
                    {"entity", "3"},
                    {"value",
                     {{"position", {1, 1, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}})
          .dump(),
      f.document.stats()->revision));
  auto source = f.document.scene().value();
  const std::array selected{source->findEntity(3).value(), source->findEntity(2).value()};
  REQUIRE(f.document.selectMany(selected, f.document.stats()->revision));
  auto gesture
      = Editor::TransformGesture::beginSelection(f.document, f.document.stats()->revision).value();
  auto draft = gesture.scene().value();
  REQUIRE(gesture.scale(0, 2, true));
  const auto peer = draft->worldTransform(draft->findEntity(3).value()).value();
  CHECK(peer.data[12] == doctest::Approx(1));
  CHECK(peer.data[13] == doctest::Approx(2));
  CHECK(draft->entity(draft->findEntity(2).value())->transform.scale.x == doctest::Approx(4));
  REQUIRE(gesture.rotate(2, 90, true));
  const auto rotated = draft->worldTransform(draft->findEntity(3).value()).value();
  CHECK(rotated.data[12] == doctest::Approx(-1));
  CHECK(rotated.data[13] == doctest::Approx(1));
  REQUIRE(gesture.rotate(2, 0, true));
  CHECK(draft->entity(draft->findEntity(3).value())->transform.position.x == 1);
  CHECK_FALSE(gesture.commit(f.document)->changed);
}

TEST_CASE("editor.group_transform_later_root_shear_failure_retains_every_preview_and_document") {
  using namespace Dot;
  Fixture f;
  REQUIRE(f.document.apply(
      Json::array({{{"op", "create"}, {"name", "Second"}},
                   {{"op", "create"}, {"name", "External parent"}},
                   {{"op", "parent"}, {"entity", "3"}, {"parent", "4"}},
                   {{"op", "transform"},
                    {"entity", "4"},
                    {"value",
                     {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {2, 3, 1}}}}}})
          .dump(),
      f.document.stats()->revision));
  auto source = f.document.scene().value();
  const std::array selected{source->findEntity(2).value(), source->findEntity(3).value()};
  REQUIRE(f.document.selectMany(selected, f.document.stats()->revision));
  const auto before = f.document.snapshot().value();
  auto gesture
      = Editor::TransformGesture::beginSelection(f.document, f.document.stats()->revision).value();
  auto delta = gesture.world().value();
  delta.data[12] += 1;
  REQUIRE(gesture.update(delta));
  auto draft = gesture.scene().value();
  const auto valid = Dot::World::encodeScene(*draft).value();
  CHECK_FALSE(gesture.rotate(2, 45, false));
  CHECK(Dot::World::encodeScene(*draft).value() == valid);
  CHECK_FALSE(gesture.scale(0, 0, true));
  delta.data[0] = std::numeric_limits<float>::infinity();
  CHECK_FALSE(gesture.update(delta));
  CHECK(Dot::World::encodeScene(*draft).value() == valid);
  CHECK(f.document.snapshot().value() == before);
  bool rejected{};
  std::thread other([&] { rejected = !gesture.rotate(2, 10, false); });
  other.join();
  CHECK(rejected);
}

TEST_CASE("editor.group_transform_large_typed_commit_scope_noop_and_atomic_failures") {
  using namespace Dot;
  Fixture f;
  auto scene = f.project.loadStartup().value();

  for (int i = 0; i < 300; ++i) {
    REQUIRE(scene.createEntity("Group member"));
  }

  REQUIRE(f.project.saveStartup(scene));
  f.document = Editor::Document::open(f.project).value();
  auto source = f.document.scene().value();
  std::vector<World::EntityId> selected;

  for (auto entity : source->entities().value()) {
    if (entity.id.value != 1) {
      selected.push_back(entity.id);
    }
  }

  REQUIRE(f.document.selectMany(selected, f.document.stats()->revision));
  const auto before = f.document.snapshot().value();
  REQUIRE_FALSE(Editor::TransformGesture::beginSelection(f.document, 0));
  CHECK_FALSE(f.document.applyTransforms({}, 0));
  CHECK_FALSE(f.document.applyTransforms({}, f.document.stats()->revision)->changed);
  const World::TransformUpdate first{selected.front(), {{2, 0, 0}}};
  const std::vector excessive(10001, first);
  CHECK_FALSE(f.document.applyTransforms(excessive, f.document.stats()->revision));
  const std::array duplicate{first, first};
  CHECK_FALSE(f.document.applyTransforms(duplicate, f.document.stats()->revision));
  auto foreign = first;
  foreign.entity.scene += 1;
  CHECK_FALSE(f.document.applyTransforms(std::span{&foreign, 1}, f.document.stats()->revision));
  CHECK(f.document.snapshot().value() == before);
  auto gesture
      = Editor::TransformGesture::beginSelection(f.document, f.document.stats()->revision).value();
  CHECK(gesture.rootCount() == 301);
  auto world = gesture.world().value();
  world.data[12] += 2;
  REQUIRE(gesture.update(world));
  REQUIRE(gesture.commit(f.document));
  auto changed = f.document.scene().value();

  for (auto entity : f.document.stats()->selection) {
    CHECK(changed->entity(entity)->transform.position.x == doctest::Approx(2));
  }

  CHECK(f.document.stats()->historyEntries == 2);
  REQUIRE(f.document.undo(f.document.stats()->revision));
  CHECK(Json::parse(f.document.snapshot().value())["scene"] == Json::parse(before)["scene"]);
}
