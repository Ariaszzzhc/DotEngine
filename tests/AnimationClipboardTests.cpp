#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <memory>
#include <thread>

import Dot.Editor.AnimationClipboard;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;
  using namespace Dot::Editor;

  auto source() -> Clip {
    return Clip::create({"Copy",
                         {{0,
                           Path::Translation,
                           Interpolation::Linear,
                           {{0, {0, 0, 0, 0}}, {2, {2, 0, 0, 0}}}}}})
        .value();
  }

  const std::array<KeyReference, 2> selection{
      {{0, Path::Translation, 0}, {0, Path::Translation, 2}}};

  auto provenance() -> AnimationClipboardSource {
    return {{7, 10, {{0, 11}}}, 1, 1, "assets/source.dotanimation"};
  }
}  // namespace

TEST_CASE("animation_clipboard.copy_clear_retire_ids_and_owned_snapshots_survive") {
  AnimationClipboard clipboard;
  CHECK_FALSE(clipboard.snapshot());
  CHECK_FALSE(clipboard.read(1));
  CHECK_FALSE(clipboard.clear(1));
  auto metadata = provenance();
  const auto first = clipboard.copy(source(), selection, metadata).value();
  REQUIRE(first->id == 1);
  metadata.path = "assets/changed.dotanimation";
  metadata.context.bindings[0] = 99;
  CHECK(first->source.path == "assets/source.dotanimation");
  CHECK(first->source.context.bindings.at(0) == 11);
  REQUIRE(clipboard.clear(first->id));
  CHECK_FALSE(clipboard.read(first->id));
  CHECK(first->keys.description().value()->keys.size() == 2);
  const auto second = clipboard.copy(source(), selection, provenance()).value();
  CHECK(second->id == 2);
  CHECK_FALSE(clipboard.clear(first->id));
  CHECK_FALSE(clipboard.copy(source(), {}, provenance()));
  CHECK(clipboard.snapshot() == second);
  const auto third = clipboard.copy(source(), selection, provenance()).value();
  CHECK(third->id == 3);
  CHECK_FALSE(clipboard.read(second->id));
  CHECK(second->keys.at(1)->size() == 2);
}

TEST_CASE("animation_clipboard.exact_scene_player_and_full_binding_map_gate_preparation") {
  AnimationClipboard clipboard;
  const auto metadata = provenance();
  const auto copied = clipboard.copy(source(), selection, metadata).value();
  REQUIRE(clipboard.compatible(metadata.context));

  for (auto context :
       {AnimationClipboardContext{8, 10, {{0, 11}}}, AnimationClipboardContext{7, 20, {{0, 11}}},
        AnimationClipboardContext{7, 10, {{0, 12}}},
        AnimationClipboardContext{7, 10, {{0, 11}, {1, 12}}},
        AnimationClipboardContext{7, 10, {}}}) {
    CHECK_FALSE(clipboard.compatible(context));
    CHECK_FALSE(clipboard.prepare(source(), context, copied->id, 1, KeyCollision::Replace));
    CHECK(clipboard.snapshot() == copied);
  }

  const auto target = source();
  auto prepared
      = clipboard.prepare(target, metadata.context, copied->id, 1, KeyCollision::Reject).value();
  REQUIRE(prepared.selection.size() == 2);
  CHECK(prepared.selection[0].time == 1);
  CHECK(prepared.selection[1].time == 3);
  CHECK(prepared.value.description().value()->tracks[0].keys.size() == 4);
  CHECK(target.description().value()->tracks[0].keys.size() == 2);
  CHECK(clipboard.snapshot() == copied);
}

TEST_CASE(
    "animation_clipboard.failed_placement_modes_destinations_and_stale_ids_preserve_content") {
  AnimationClipboard clipboard;
  const auto metadata = provenance();
  const auto copied = clipboard.copy(source(), selection, metadata).value();
  CHECK_FALSE(clipboard.prepare({}, metadata.context, copied->id, 1, KeyCollision::Reject));
  CHECK_FALSE(clipboard.prepare(source(), metadata.context, 99, 1, KeyCollision::Reject));
  CHECK_FALSE(clipboard.prepare(source(), metadata.context, copied->id, 0, KeyCollision::Reject));
  CHECK_FALSE(
      clipboard.prepare(source(), metadata.context, copied->id, 86400, KeyCollision::Replace));
  const std::array<ClipEdit, 1> changes{
      SetInterpolation{0, Path::Translation, Interpolation::Step}};
  const auto step = editClip(source(), changes).value();
  CHECK_FALSE(clipboard.prepare(step, metadata.context, copied->id, 1, KeyCollision::Replace));
  CHECK(clipboard.snapshot() == copied);
  REQUIRE(clipboard.prepare(source(), metadata.context, copied->id, 0, KeyCollision::Replace));
  CHECK(copied->keys.description().value()->keys[0].key.time == 0);
}

TEST_CASE("animation_clipboard.invalid_provenance_and_unbound_keys_never_replace_content") {
  AnimationClipboard clipboard;
  const auto copied = clipboard.copy(source(), selection, provenance()).value();

  for (int index = 0; index < 11; ++index) {
    auto metadata = provenance();

    switch (index) {
      case 0:
        metadata.context.scene = 0;
        break;
      case 1:
        metadata.context.owner = std::numeric_limits<std::uint64_t>::max();
        break;
      case 2:
        metadata.buffer = 0;
        break;
      case 3:
        metadata.revision = 0;
        break;
      case 4:
        metadata.path = "assets/../source.dotanimation";
        break;
      case 5:
        metadata.path = "assets/source.txt";
        break;
      case 6:
        metadata.context.bindings = {{10000, 11}};
        break;
      case 7:
        metadata.context.bindings = {{0, 0}};
        break;
      case 8:
        metadata.context.bindings = {{0, 11}, {1, 11}};
        break;
      case 9:
        metadata.context.bindings = {{1, 12}};
        break;
      case 10:
        metadata.context.bindings.clear();
        break;
    }

    CHECK_FALSE(clipboard.copy(source(), selection, metadata));
    CHECK(clipboard.snapshot() == copied);
  }
}

TEST_CASE("animation_clipboard.snapshots_outlive_service_and_preparation_has_no_hidden_mutation") {
  const auto retained = [&] {
    AnimationClipboard clipboard;
    auto clip = source();
    const auto metadata = provenance();
    auto copied = clipboard.copy(clip, selection, metadata).value();
    clip = {};
    bool prepared{};
    std::thread worker([&] {
      auto result
          = clipboard.prepare(source(), metadata.context, copied->id, 1, KeyCollision::Reject);
      prepared = result && result->selection.size() == 2;
    });
    const auto read = clipboard.read(copied->id).value();
    worker.join();
    REQUIRE(prepared);
    CHECK(read == copied);
    REQUIRE(clipboard.clear(copied->id));
    return read;
  }();
  CHECK(retained->source.context.owner == 10);
  CHECK(retained->keys.at(1)->at(1).key.time == 3);
}
