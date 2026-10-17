#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.AssetPreview;
import Dot.Editor.PreviewWorker;
import Dot.Assets.Image;
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Graphics.Renderer;
import Dot.Math;

namespace {
  struct Fixture {
    Dot::Core::StagedDirectory storage
        = Dot::Core::StagedDirectory::create(
              Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Dot::Core::FileSystem files = storage.files().value();

    auto image(const char* path, Dot::Graphics::Image value) -> void {
      REQUIRE(files.writeAtomic(path, Dot::Assets::encodeImage(value).value()));
    }
  };

  auto pixel(const Dot::Graphics::Image& image, unsigned x, unsigned y)
      -> std::array<std::uint8_t, 4> {
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2],
            image.pixels[offset + 3]};
  }
}  // namespace

TEST_CASE("asset_preview.image_pixels_aspect_source_failures_and_owner_lifetime") {
  using namespace Dot;
  Fixture f;
  f.image("source.png", {2, 1, {128, 0, 0, 255, 0, 255, 0, 255}});
  const auto source = f.files.readBytes("source.png").value();
  auto preview = Editor::AssetPreview::create(f.files).value();
  auto image = preview.render("source.png");
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(pixel(image->image, 30, 64) == std::array<std::uint8_t, 4>{128, 0, 0, 255});
  CHECK(pixel(image->image, 98, 64) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
  CHECK(pixel(image->image, 64, 2) != pixel(image->image, 98, 64));
  CHECK(f.files.readBytes("source.png").value() == source);
  CHECK_FALSE(preview.render("source.png", 15));
  CHECK_FALSE(preview.render("source.png", 513));
  CHECK_FALSE(preview.render("../source.png"));
  CHECK_FALSE(preview.render("missing.png"));
  CHECK_FALSE(preview.render("script.ts"));
  REQUIRE(f.files.writeTextAtomic("bad.png", "bad"));
  CHECK_FALSE(preview.render("bad.png"));
  Assets::MaterialDescription draft;
  CHECK_FALSE(preview.render("source.png", 128, &draft));
  bool rejected{};
  std::jthread worker([&] { rejected = !preview.render("source.png") && !preview.clearCache(); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(preview);
  CHECK_FALSE(preview.stats());
  CHECK_FALSE(preview.render("source.png"));
  REQUIRE(moved.clearCache());
  CHECK(moved.stats()->textures == 0);
  CHECK(moved.stats()->meshes == 0);
}

TEST_CASE("asset_preview.material_sphere_mesh_bounds_hdr_normal_errors_and_no_source_writes") {
  using namespace Dot;
  Fixture f;
  Graphics::HdrImage studio{4, 2, std::vector<float>(32, 1)};
  auto created = Editor::AssetPreview::create(f.files, &studio);
  REQUIRE_MESSAGE(created, (created ? "" : created.error().message));
  auto preview = std::move(*created);
  Assets::MaterialDescription material;
  material.model = Graphics::SurfaceModel::Unlit;
  material.baseColor = {1, 0, 0, 1};
  REQUIRE(Assets::saveMaterial(f.files, "red.dotmaterial", material));
  const auto source = f.files.readText("red.dotmaterial").value();
  auto red = preview.render("red.dotmaterial");
  REQUIRE(red);
  CHECK(pixel(red->image, 64, 64)[0] > 200);
  CHECK(pixel(red->image, 64, 64)[1] == 0);
  material.baseColor = {0, 0, 1, 1};
  auto blue = preview.render("red.dotmaterial", 128, &material);
  REQUIRE(blue);
  CHECK(pixel(blue->image, 64, 64)[2] > 200);
  CHECK(red->image.pixels != blue->image.pixels);
  CHECK(f.files.readText("red.dotmaterial").value() == source);
  material.model = Graphics::SurfaceModel::MetallicRoughness;
  f.image("normal.png", {1, 1, {128, 128, 255, 255}});
  material.normalTexture
      = Assets::TextureSource{"normal.png", {.colorSpace = Graphics::ColorSpace::Linear}};
  REQUIRE(preview.render("red.dotmaterial", 32, &material));
  f.image("normal.png", {1, 1, {128, 128, 0, 255}});
  REQUIRE(preview.render("red.dotmaterial", 32, &material));
  REQUIRE(Assets::saveMesh(f.files, "mesh.dotmesh", Graphics::cube({100, 30, 40}).value()));
  auto mesh = preview.render("mesh.dotmesh", 64);
  REQUIRE(mesh);
  CHECK(pixel(mesh->image, 32, 32) != pixel(mesh->image, 0, 0));
  REQUIRE(preview.clearCache());
  CHECK(preview.stats()->textures == 0);
  CHECK(preview.stats()->meshes == 0);
  CHECK_FALSE(Editor::assetPreviewKind("../bad.png"));
  CHECK_FALSE(Editor::assetPreviewKind("scene.dotscene"));
  CHECK(Editor::assetPreviewKind("mesh.obj") == Editor::AssetPreviewKind::Mesh);
}

TEST_CASE("asset_preview.real_worker_owns_offscreen_preview_and_reports_missing_studio") {
  using namespace Dot;
  using namespace std::chrono_literals;
  Fixture f;
  f.image("source.png", {1, 1, {255, 0, 0, 255}});
  const auto studio = std::filesystem::path(DOT_ENVIRONMENT_ASSETS) / "studio_small_09_1k.hdr";
  auto worker = Editor::createAssetPreviewWorker(f.files, studio).value();
  const auto ticket = worker.submit({"image", "source.png", 64, {}, false}).value();
  const auto deadline = std::chrono::steady_clock::now() + 15s;
  std::vector<Editor::PreviewCompletion> completed;

  while (completed.empty() && std::chrono::steady_clock::now() < deadline) {
    completed = worker.takeCompleted().value();
    std::this_thread::sleep_for(1ms);
  }

  REQUIRE(completed.size() == 1);
  CHECK(completed[0].ticket == ticket);
  REQUIRE_MESSAGE(completed[0].result,
                  (completed[0].result ? "" : completed[0].result.error().message));
  CHECK(pixel(completed[0].result->image, 32, 32) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  REQUIRE(worker.stop());
  auto bad = Editor::createAssetPreviewWorker(f.files, f.files.root() / "missing.hdr").value();
  REQUIRE(bad.submit({"image", "source.png", 64, {}, false}));
  const auto failedDeadline = std::chrono::steady_clock::now() + 3s;

  while (bad.stats()->completed == 0 && std::chrono::steady_clock::now() < failedDeadline) {
    std::this_thread::sleep_for(1ms);
  }

  auto failed = bad.takeCompleted().value();
  REQUIRE(failed.size() == 1);
  CHECK_FALSE(failed[0].result);
  CHECK_FALSE(bad.stats()->ready);
  CHECK_FALSE(bad.stats()->initializationError.empty());
}
