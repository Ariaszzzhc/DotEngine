#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Control;
import Dot.Core.StagedDirectory;
import Dot.Core.Result;
import Dot.Assets.Mesh;
import Dot.Graphics.Geometry;
import Dot.Editor.Resources;
import Dot.Project;

namespace {
  using namespace Dot::Editor;

  struct Fixture {
    Dot::Core::StagedDirectory storage;
    Dot::Project::Project project;

    Fixture()
        : storage(Dot::Core::StagedDirectory::create(
                      Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()),
          project(Dot::Project::Project::create(storage.files().value(), "project", {}).value()) {
    }

    auto files() -> Dot::Core::FileSystem {
      return project.files().value();
    }
  };
}  // namespace

TEST_CASE("resources.list_create_unicode_and_readonly_sdk_are_actual_files") {
  Fixture fixture;
  auto resources = ResourceStore::open(fixture.project).value();
  REQUIRE(resources.createDirectory("scripts/行为"));
  REQUIRE(resources.createText("scripts/行为/player.ts", "export const 游戏 = 1;"));
  auto entries = resources.list().value();
  CHECK(std::ranges::is_sorted(entries, {}, &ResourceEntry::path));
  auto code = std::ranges::find(entries, "scripts/行为/player.ts", &ResourceEntry::path);
  REQUIRE(code != entries.end());
  CHECK(code->text);
  CHECK_FALSE(code->readOnly);
  CHECK(code->bytes == std::string("export const 游戏 = 1;").size());
  REQUIRE(fixture.files().createDirectories("scripts/dot"));
  REQUIRE(
      fixture.files().writeTextAtomic("scripts/dot/protected.ts", "export const marker = true;"));
  auto sdk = resources.openText("scripts/dot/protected.ts").value();
  CHECK(sdk.snapshot()->readOnly);
  CHECK_FALSE(sdk.replace("bad", 1));
  CHECK_FALSE(sdk.save(1));
  CHECK_FALSE(resources.createDirectory("scripts/dot/new"));
  CHECK_FALSE(resources.createText("scripts/dot/new.ts", ""));
  CHECK_FALSE(resources.createText("scripts/DoT/new.ts", ""));
  CHECK_FALSE(resources.createText("scripts/行为/player.ts", "overwrite"));
  CHECK_FALSE(resources.createText("scripts/行为/PLAYER.ts", "alias"));
  CHECK(fixture.files().readText("scripts/行为/player.ts").value() == "export const 游戏 = 1;");
}

TEST_CASE("resources.native_meshes_are_binary_assets_without_text_editing_routes") {
  Fixture fixture;
  REQUIRE(
      Dot::Assets::saveMesh(fixture.files(), "assets/cube.dotmesh", Dot::Graphics::cube().value()));
  auto store = ResourceStore::open(fixture.project).value();
  auto entries = store.list().value();
  const auto mesh = std::ranges::find(entries, "assets/cube.dotmesh", &ResourceEntry::path);
  REQUIRE(mesh != entries.end());
  CHECK_FALSE(mesh->text);
  CHECK(mesh->bytes > 0);
  CHECK_FALSE(store.openText("assets/cube.dotmesh"));
  CHECK_FALSE(store.externalTextPath("assets/cube.dotmesh"));
}

TEST_CASE("resources.text_revisions_noops_save_and_external_conflict_preserve_draft") {
  Fixture fixture;
  auto resources = ResourceStore::open(fixture.project).value();
  REQUIRE(resources.createText("scripts/edit.ts", "export const n=1;\r\n"));
  auto text = resources.openText("scripts/edit.ts").value();
  REQUIRE(text.replace("export const n=1;\r\n", 1));
  REQUIRE(text.save(1));
  CHECK(text.snapshot()->revision == 1);
  REQUIRE(text.replace("export const n=2;", 1));
  CHECK(text.snapshot()->dirty);
  CHECK_FALSE(text.replace("stale", 1));
  CHECK_FALSE(text.save(1));
  REQUIRE(fixture.files().writeTextAtomic("scripts/edit.ts", "external"));
  CHECK_FALSE(text.save(2));
  CHECK(text.snapshot()->text == "export const n=2;");
  CHECK(text.snapshot()->revision == 2);
  CHECK(fixture.files().readText("scripts/edit.ts").value() == "external");
  REQUIRE(fixture.files().writeTextAtomic("scripts/edit.ts", "export const n=1;\r\n"));
  REQUIRE(text.save(2));
  CHECK_FALSE(text.snapshot()->dirty);
  CHECK(text.snapshot()->revision == 3);
  CHECK(fixture.files().readText("scripts/edit.ts").value() == "export const n=2;");
}

TEST_CASE("resources.import_preserves_binary_source_and_rejects_invalid_text_and_overwrite") {
  Fixture fixture;
  auto resources = ResourceStore::open(fixture.project).value();
  auto source = fixture.storage.files().value();
  const std::string binary("A\0B\xff", 4);
  REQUIRE(source.writeTextAtomic("source.bin", binary));
  REQUIRE(resources.importFile(source.root() / "source.bin", "assets/owned.bin"));
  CHECK(fixture.files().readText("assets/owned.bin").value() == binary);
  CHECK(source.readText("source.bin").value() == binary);
  CHECK_FALSE(resources.importFile(source.root() / "source.bin", "assets/owned.bin"));
  CHECK_FALSE(resources.importFile(source.root() / "source.bin", "scripts/binary.ts"));
  CHECK_FALSE(std::filesystem::exists(fixture.files().root() / "scripts/binary.ts"));
  CHECK_FALSE(resources.importFile(source.root(), "assets/directory.bin"));
}

TEST_CASE("resources.path_text_capacity_metadata_and_missing_parent_boundaries") {
  Fixture fixture;
  ResourceLimits limits;
  limits.textBytes = 8;
  limits.binaryBytes = 4;
  auto resources = ResourceStore::open(fixture.project, limits).value();

  for (const auto* name : {"../escape.ts", "scripts/../escape.ts", "outside/scene.ts",
                           "assets/CON.txt", "assets/COM².txt", "assets/missing/file.txt"}) {
    CHECK_FALSE(resources.createText(name, ""));
  }

  REQUIRE(resources.createText("assets/exact.txt", "12345678"));
  CHECK_FALSE(resources.createText("assets/large.txt", "123456789"));
  CHECK_FALSE(resources.createText("assets/nul.txt", std::string("x\0", 2)));
  CHECK_FALSE(resources.createText("assets/utf.txt", std::string(1, static_cast<char>(0xFF))));
  auto source = fixture.storage.files().value();
  REQUIRE(source.writeTextAtomic("too-large.bin", "12345"));
  CHECK_FALSE(resources.importFile(source.root() / "too-large.bin", "assets/too-large.bin"));
  CHECK_FALSE(std::filesystem::exists(fixture.files().root() / "assets/too-large.bin"));
  CHECK_FALSE(resources.importFile(std::filesystem::path(std::string("x\0", 2)), "assets/nul.bin"));
  auto text = resources.openText("assets/exact.txt").value();
  CHECK_FALSE(text.replace("123456789", 1));
  CHECK(text.snapshot()->revision == 1);
  auto description = fixture.project.description().value();
  description.name = "External metadata";
  REQUIRE(fixture.project.save(description));
  CHECK_FALSE(resources.createText("assets/changed.txt", ""));
  CHECK_FALSE(resources.list());
  REQUIRE(text.replace("draft", 1));
  CHECK_FALSE(text.save(2));
  CHECK(text.snapshot()->dirty);
  limits.entries = 0;
  CHECK_FALSE(ResourceStore::open(fixture.project, limits));
}

TEST_CASE("resources.symlinks_listing_budget_and_deleted_files_fail_without_write") {
  Fixture fixture;
  auto resources = ResourceStore::open(fixture.project).value();
  REQUIRE(resources.createText("assets/real.txt", "original"));
  auto text = resources.openText("assets/real.txt").value();
  REQUIRE(text.replace("draft", 1));
  std::filesystem::remove(fixture.files().root() / "assets/real.txt");
  CHECK_FALSE(text.save(2));
  CHECK(text.snapshot()->dirty);
  REQUIRE(fixture.files().writeTextAtomic("assets/real.txt", "original"));
  std::filesystem::create_symlink("real.txt", fixture.files().root() / "assets/link.txt");
  CHECK_FALSE(resources.openText("assets/link.txt"));
  CHECK_FALSE(resources.list());
  CHECK_FALSE(resources.importFile(fixture.files().root() / "assets/link.txt", "assets/copy.txt"));
  std::filesystem::remove(fixture.files().root() / "assets/link.txt");
  ResourceLimits limits;
  limits.entries = 1;
  CHECK_FALSE(ResourceStore::open(fixture.project, limits)->list());
  REQUIRE(resources.createDirectory("assets/deep"));
  REQUIRE(resources.createText("assets/deep/file.txt", ""));
  limits.entries = 8192;
  limits.depth = 1;
  CHECK_FALSE(ResourceStore::open(fixture.project, limits)->list());
  CHECK(fixture.files().readText("assets/real.txt").value() == "original");
}

TEST_CASE("resources.moves_store_destruction_workers_and_no_implicit_save") {
  Fixture fixture;
  std::optional<TextDocument> retained;
  {
    auto source = ResourceStore::open(fixture.project).value();
    auto store = std::move(source);
    CHECK_FALSE(source.list());
    retained.emplace(store.openText("scripts/mover.ts").value());
  }

  auto snapshot = retained->snapshot().value();
  auto text = std::move(*retained);
  CHECK_FALSE(retained->snapshot());
  CHECK_FALSE(retained->replace("", 1));
  CHECK_FALSE(retained->save(1));
  bool rejected = false;
  auto worker
      = std::thread([&] { rejected = !text.snapshot() && !text.replace("", 1) && !text.save(1); });
  worker.join();
  CHECK(rejected);
  REQUIRE(text.replace("export const pending = 1;", 1));
  CHECK(fixture.files().readText("scripts/mover.ts").value() == snapshot.text);
  REQUIRE(text.save(2));
  CHECK(fixture.files().readText("scripts/mover.ts").value() == "export const pending = 1;");
}

TEST_CASE("resources.external_text_path_reuses_metadata_exact_paths_and_readonly_guards") {
  Fixture fixture;
  auto resources = ResourceStore::open(fixture.project).value();
  CHECK(resources.externalTextPath("scripts/mover.ts").value()
        == fixture.files().root() / "scripts/mover.ts");
  REQUIRE(fixture.files().writeTextAtomic("scripts/repair.ts", std::string("\xc0\x80", 2)));
  CHECK_FALSE(resources.openText("scripts/repair.ts"));
  CHECK(resources.externalTextPath("scripts/repair.ts"));
  REQUIRE(fixture.files().createDirectories("scripts/dot"));
  REQUIRE(fixture.files().writeTextAtomic("scripts/dot/protected.ts", "protected"));
  CHECK_FALSE(resources.externalTextPath("scripts/dot/protected.ts"));

  for (const auto* path :
       {"Scripts/mover.ts", "scripts/missing.ts", "../escape.ts", "project.dotproject", "assets"}) {
    CHECK_FALSE(resources.externalTextPath(path));
  }

  std::filesystem::create_symlink("mover.ts", fixture.files().root() / "scripts/link.ts");
  CHECK_FALSE(resources.externalTextPath("scripts/link.ts"));
  auto moved = std::move(resources);
  CHECK_FALSE(resources.externalTextPath("scripts/mover.ts"));
  CHECK(moved.externalTextPath("scripts/mover.ts"));
  auto description = fixture.project.description().value();
  description.name = "Changed";
  REQUIRE(fixture.project.save(description));
  CHECK_FALSE(moved.externalTextPath("scripts/mover.ts"));
}

TEST_CASE("resources.scene_tree_supports_native_files_without_exposing_project_metadata") {
  Fixture fixture;
  auto resources = ResourceStore::open(fixture.project).value();
  auto entries = resources.list().value();
  CHECK(std::ranges::find(entries, "scenes/main.dotscene", &ResourceEntry::path) != entries.end());
  REQUIRE(resources.createDirectory("scenes/levels"));
  REQUIRE(resources.createText("scenes/levels/empty.dotscene", "{}"));
  auto text = resources.openText("scenes/levels/empty.dotscene");
  REQUIRE(text);
  CHECK_FALSE(text->snapshot()->readOnly);
  CHECK(resources.externalTextPath("scenes/levels/empty.dotscene"));
  CHECK_FALSE(resources.createText("scenes/LEVELS/alias.dotscene", "{}"));
  CHECK_FALSE(resources.openText("project.dotproject"));
  CHECK_FALSE(resources.createText("outside/scene.dotscene", "{}"));
  CHECK_FALSE(resources.createDirectory(".dot-private"));
  CHECK_FALSE(resources.createDirectory("assets/.dot-stage-user"));
  CHECK_FALSE(resources.createText("scenes/.dot-stage-user.dotscene", "{}"));
}

TEST_CASE("resources.live_control_and_retained_replies_stay_outside_asset_catalogs") {
  Fixture f;
  auto resources = ResourceStore::open(f.project).value();
  const auto original = resources.list()->size();
  auto parent = Dot::Core::FileSystem::open(f.files().root() / "assets").value();
  auto control = Dot::Core::Control::create(parent).value();
  const auto receipts = control.receipts()->root();
  CHECK(resources.list()->size() == original);
  REQUIRE(control.close());
  CHECK(resources.list()->size() == original);
  REQUIRE(Dot::Core::clearControlReceipts(receipts));
  CHECK(resources.list()->size() == original);
}

TEST_CASE("resources.independent_text_and_import_writers_keep_one_complete_resource") {
  Fixture f;
  constexpr std::size_t count = 8;
  const auto root = f.project.files()->root();
  const auto source = f.storage.files().value();
  const std::array payloads{std::string(256 * 1024, 'a'), std::string(512 * 1024, 'b')};
  REQUIRE(source.createTextAtomic("external.txt", payloads[1]));
  std::barrier start{static_cast<std::ptrdiff_t>(count + 1)};
  std::atomic<unsigned> winners{};
  std::atomic<unsigned> failures{};
  std::vector<std::jthread> writers;

  for (std::size_t i = 0; i < count; ++i) {
    writers.emplace_back([&, i] {
      auto project = Dot::Project::Project::open(root).value();
      auto store = ResourceStore::open(project).value();
      start.arrive_and_wait();
      auto result = i % 2 ? store.createText("assets/shared.txt", payloads[0])
                          : store.importFile(source.root() / "external.txt", "assets/shared.txt");

      if (result) {
        ++winners;
      } else if (result.error().code != Dot::Core::ErrorCode::InvalidState) {
        ++failures;
      }
    });
  }

  start.arrive_and_wait();
  writers.clear();
  CHECK(winners.load() == 1);
  CHECK(failures.load() == 0);
  const auto actual = f.files().readText("assets/shared.txt").value();
  CHECK(std::ranges::find(payloads, actual) != payloads.end());
  CHECK(source.readText("external.txt").value() == payloads[1]);
  const auto store = ResourceStore::open(f.project).value();
  const auto entries = store.list().value();
  CHECK(std::ranges::none_of(entries, [](const auto& entry) {
    return entry.path.find(".dot-stage-") != std::string::npos;
  }));
  CHECK(std::distance(std::filesystem::directory_iterator(root / "assets"),
                      std::filesystem::directory_iterator{})
        == 1);
}
