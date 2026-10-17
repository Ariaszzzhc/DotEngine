#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Assets.Image;
import Dot.Assets.ImageExport;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.ViewportCapture;
import Dot.Graphics.Image;

namespace {
  auto staging() -> Dot::Core::StagedDirectory {
    return Dot::Core::StagedDirectory::create(
               Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
        .value();
  }

  auto image() -> Dot::Graphics::Image {
    return {1, 2, {255, 1, 2, 255, 3, 4, 255, 128}};
  }

  auto finish(Dot::Assets::ImageExport& plan) -> decltype(plan.poll()) {
    auto result = plan.poll();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (result && !*result && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      result = plan.poll();
    }

    return result;
  }
}  // namespace

TEST_CASE("image_export.async_owns_pixels_moves_and_commits_once_only_on_owner_poll") {
  auto owner = staging();
  auto files = owner.files().value();
  auto plan = Dot::Assets::ImageExport::prepare(files, "async.png", false).value();
  CHECK_FALSE(plan.poll());
  CHECK_FALSE(plan.cancel());
  CHECK_FALSE(plan.begin({}));
  auto source = image();
  REQUIRE(plan.begin(source));
  source.pixels.assign(8, 0);
  CHECK_FALSE(plan.begin(image()));
  CHECK_FALSE(plan.write(image()));
  CHECK_FALSE(std::filesystem::exists(files.root() / "async.png"));
  bool rejected{};
  std::thread worker([&] { rejected = !plan.poll() && !plan.cancel() && !plan.begin(image()); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(plan);
  CHECK_FALSE(plan.poll());
  CHECK_FALSE(plan.cancel());
  auto result = finish(moved);
  REQUIRE(result);
  REQUIRE(*result);
  CHECK(Dot::Assets::loadImage(files, "async.png")->pixels == image().pixels);
  CHECK(files.readBytes("async.png").value() == Dot::Assets::encodeImage(image()).value());
  const auto report = **result;
  REQUIRE(files.writeTextAtomic("async.png", "external after commit"));
  CHECK(moved.poll()->value().path == report.path);
  CHECK(moved.poll()->value().bytes == report.bytes);
  CHECK(files.readText("async.png").value() == "external after commit");
  CHECK_FALSE(moved.cancel());
}

TEST_CASE("image_export.async_cancellation_and_teardown_never_publish_files") {
  auto owner = staging();
  auto files = owner.files().value();
  REQUIRE(files.writeTextAtomic("keep.png", "keep"));
  auto plan = Dot::Assets::ImageExport::prepare(files, "keep.png", true).value();
  REQUIRE(plan.begin(image()));
  REQUIRE(plan.cancel());
  REQUIRE(plan.cancel());
  auto result = finish(plan);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(plan.poll().error().message == result.error().message);
  CHECK(files.readText("keep.png").value() == "keep");

  for (int i = 0; i < 3; ++i) {
    auto abandoned = Dot::Assets::ImageExport::prepare(files, "abandoned.png", false).value();
    REQUIRE(abandoned.begin(image()));
  }

  CHECK_FALSE(std::filesystem::exists(files.root() / "abandoned.png"));
}

TEST_CASE("image_export.async_rechecks_created_changed_and_lost_destinations_before_commit") {
  auto owner = staging();
  auto files = owner.files().value();
  auto fresh = Dot::Assets::ImageExport::prepare(files, "fresh.png", false).value();
  REQUIRE(fresh.begin(image()));
  REQUIRE(files.writeTextAtomic("fresh.png", "external create"));
  CHECK_FALSE(finish(fresh));
  CHECK(files.readText("fresh.png").value() == "external create");
  auto changed = Dot::Assets::ImageExport::prepare(files, "fresh.png", true).value();
  REQUIRE(changed.begin(image()));
  REQUIRE(files.writeTextAtomic("fresh.png", "external edit"));
  auto failed = finish(changed);
  REQUIRE_FALSE(failed);
  CHECK(changed.poll().error().message == failed.error().message);
  CHECK(files.readText("fresh.png").value() == "external edit");
  REQUIRE(files.createDirectories("parent"));
  auto child = Dot::Core::FileSystem::open(files.root() / "parent").value();
  auto missing = Dot::Assets::ImageExport::prepare(child, "view.png", false).value();
  REQUIRE(missing.begin(image()));
  REQUIRE(std::filesystem::remove(files.root() / "parent"));
  CHECK_FALSE(finish(missing));
  CHECK_FALSE(std::filesystem::exists(files.root() / "parent"));
}

TEST_CASE("viewport_capture.async_save_failure_preserves_snapshot_and_retries_without_recapture") {
  using namespace Dot::Editor;
  auto owner = staging();
  auto files = owner.files().value();
  ViewportCapture capture;
  CaptureContext context{1, 2, 5, ViewportMode::Play};
  auto id = capture.request(context, 10).value();
  REQUIRE(capture.complete(id, {context, 11, 31, 1, 2}, image()));
  auto failed = Dot::Assets::ImageExport::prepare(files, "view.png", false).value();
  REQUIRE(failed.begin(**capture.image(id)));
  REQUIRE(files.writeTextAtomic("view.png", "external create"));
  CHECK_FALSE(finish(failed));
  CHECK(capture.stats().state == CaptureState::Ready);
  CHECK((*capture.image(id))->pixels == image().pixels);
  auto retry = Dot::Assets::ImageExport::prepare(files, "view.png", true).value();
  REQUIRE(retry.begin(**capture.image(id)));
  auto result = finish(retry);
  REQUIRE(result);
  REQUIRE(*result);
  REQUIRE(capture.saved(id));
  CHECK(capture.stats().state == CaptureState::Saved);
  CHECK(capture.stats().frame.playFrame == 31);
  CHECK(Dot::Assets::loadImage(files, "view.png")->pixels == image().pixels);
}

TEST_CASE("image_export.lossless_unicode_png_owned_path_and_single_use") {
  auto owner = staging();
  auto files = owner.files().value();
  auto plan = Dot::Assets::ImageExport::prepare(files, "画面.PNG", false).value();
  auto moved = std::move(plan);
  CHECK_FALSE(plan.write(image()));
  bool rejected{};
  std::thread worker([&] { rejected = !moved.write(image()); });
  worker.join();
  CHECK(rejected);
  CHECK_FALSE(std::filesystem::exists(files.root() / u8"画面.PNG"));
  auto report = moved.write(image());
  REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
  const auto expected = (files.root() / u8"画面.PNG").generic_u8string();
  CHECK(report->path == std::string(expected.begin(), expected.end()));
  CHECK(report->bytes == files.readBytes(u8"画面.PNG")->size());
  auto decoded = Dot::Assets::loadImage(files, u8"画面.PNG");
  REQUIRE(decoded);
  CHECK(decoded->width == 1);
  CHECK(decoded->height == 2);
  CHECK(decoded->pixels == image().pixels);
  CHECK_FALSE(moved.write(image()));
  CHECK_FALSE(Dot::Assets::ImageExport::prepare(files, "画面.PNG", false));
  CHECK_FALSE(Dot::Assets::ImageExport::prepare(files, "画面.png", true));
  CHECK(std::distance(std::filesystem::directory_iterator(files.root()),
                      std::filesystem::directory_iterator{})
        == 1);
}

TEST_CASE("image_export.overwrite_is_explicit_retryable_and_rejects_changed_baseline") {
  auto owner = staging();
  auto files = owner.files().value();
  REQUIRE(files.writeTextAtomic("view.png", "user content"));
  CHECK_FALSE(Dot::Assets::ImageExport::prepare(files, "view.png", false));
  auto plan = Dot::Assets::ImageExport::prepare(files, "view.png", true).value();
  CHECK_FALSE(plan.write({}));
  CHECK(files.readText("view.png").value() == "user content");
  REQUIRE(plan.write(image()));
  auto before = files.readBytes("view.png").value();
  auto changed = Dot::Assets::ImageExport::prepare(files, "view.png", true).value();
  REQUIRE(files.writeTextAtomic("view.png", "external edit"));
  CHECK_FALSE(changed.write(image()));
  CHECK(files.readText("view.png").value() == "external edit");
  REQUIRE(files.writeAtomic("view.png", before));
  REQUIRE(changed.write(image()));
  auto fresh = Dot::Assets::ImageExport::prepare(files, "new.png", true).value();
  REQUIRE(files.writeTextAtomic("new.png", "created after preparation"));
  CHECK_FALSE(fresh.write(image()));
  CHECK(files.readText("new.png").value() == "created after preparation");
}

TEST_CASE("image_export.rejects_invalid_names_links_directories_readonly_and_lost_parent") {
  auto owner = staging();
  auto files = owner.files().value();

  for (const auto* name : {"", ".png", "../view.png", "nested/view.png", "CON.png", "view.jpg",
                           "view.png ", "view\\name.png"}) {
    CHECK_FALSE(Dot::Assets::ImageExport::prepare(files, name, true));
  }

  REQUIRE(files.createDirectories("folder.png"));
  CHECK_FALSE(Dot::Assets::ImageExport::prepare(files, "folder.png", true));
  REQUIRE(files.writeTextAtomic("original.png", "retain"));
  std::error_code linkError;
  std::filesystem::create_symlink(files.root() / "original.png", files.root() / "link.png",
                                  linkError);

  if (!linkError) {
    CHECK_FALSE(Dot::Assets::ImageExport::prepare(files, "link.png", true));
  }

  std::filesystem::permissions(files.root() / "original.png", std::filesystem::perms::owner_read);
  CHECK_FALSE(Dot::Assets::ImageExport::prepare(files, "original.png", true));
  std::filesystem::permissions(files.root() / "original.png", std::filesystem::perms::owner_all);
  CHECK(files.readText("original.png").value() == "retain");
  REQUIRE(files.writeTextAtomic("oversized.png", ""));
  std::filesystem::resize_file(files.root() / "oversized.png", 64 * 1024 * 1024 + 1);
  CHECK_FALSE(Dot::Assets::ImageExport::prepare(files, "oversized.png", true));
  CHECK(std::filesystem::file_size(files.root() / "oversized.png") == 64 * 1024 * 1024 + 1);
  REQUIRE(files.createDirectories("parent"));
  auto child = Dot::Core::FileSystem::open(files.root() / "parent").value();
  auto plan = Dot::Assets::ImageExport::prepare(child, "view.png", false).value();
  REQUIRE(std::filesystem::remove(files.root() / "parent"));
  CHECK_FALSE(plan.write(image()));
  CHECK_FALSE(std::filesystem::exists(files.root() / "parent"));
}

TEST_CASE("viewport_capture.failed_save_retains_frozen_image_for_retry") {
  using namespace Dot::Editor;
  auto owner = staging();
  auto files = owner.files().value();
  ViewportCapture capture;
  CaptureContext context{1, 2, 5, ViewportMode::Play};
  auto id = capture.request(context, 10).value();
  auto source = image();
  REQUIRE(capture.complete(id, {context, 11, 31, 1, 2}, source));
  source.pixels.assign(8, 0);
  REQUIRE(files.writeTextAtomic("view.png", "retain"));
  CHECK_FALSE(Dot::Assets::ImageExport::prepare(files, "view.png", false));
  CHECK(capture.stats().state == CaptureState::Ready);
  auto plan = Dot::Assets::ImageExport::prepare(files, "view.png", true).value();
  REQUIRE(plan.write(**capture.image(id)));
  REQUIRE(capture.saved(id));
  CHECK(Dot::Assets::loadImage(files, "view.png")->pixels == image().pixels);
  CHECK(capture.stats().frame.playFrame == 31);
  CHECK(capture.stats().state == CaptureState::Saved);
}
