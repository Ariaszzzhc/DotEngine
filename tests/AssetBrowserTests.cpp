#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.Editor.Workspace;
import Dot.Assets.Image;
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Graphics.Renderer;

namespace {
  using Json = nlohmann::json;

  struct Fixture {
    Dot::Core::StagedDirectory storage
        = Dot::Core::StagedDirectory::create(
              Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Dot::Project::Project project
        = Dot::Project::Project::create(storage.files().value(), "project", {}).value();
    Dot::Core::FileSystem files = project.files().value();

    auto open() -> Dot::Editor::Workspace {
      Dot::Editor::WorkspaceConfig config;
      config.font = DOT_EDITOR_FONT;
      return Dot::Editor::Workspace::open(project, config).value();
    }

    auto image(const std::string& path) -> void {
      REQUIRE(files.writeAtomic(
          path, Dot::Assets::encodeImage({2, 1, {255, 0, 0, 255, 0, 255, 0, 255}}).value()));
    }
  };

  auto send(Dot::Editor::Workspace& workspace, Json value) -> Json {
    auto response = workspace.dispatch(value.dump());
    REQUIRE_MESSAGE(response, (response ? "" : response.error().message));
    return Json::parse(*response);
  }

  auto frames(Dot::Editor::Workspace& workspace, int count = 1) -> void {
    for (int i = 0; i < count; ++i) {
      REQUIRE(workspace.step(std::chrono::milliseconds{16}));
    }
  }

  template <typename Predicate>
  auto awaitPreview(Dot::Editor::Workspace& workspace, Predicate predicate) -> void {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};

    while (!predicate()) {
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      frames(workspace);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }
}  // namespace

TEST_CASE("asset_browser.native_real_thumbnails_navigation_search_and_scene_isolation") {
  Fixture f;
  REQUIRE(f.files.createDirectories("assets/sub"));
  f.image("assets/sub/picture.png");
  REQUIRE(Dot::Assets::saveMesh(f.files, "assets/shape.dotmesh", Dot::Graphics::cube().value()));
  auto workspace = f.open();
  CHECK_FALSE(workspace.captureAssetPreview("assets/sub/picture.png"));
  send(workspace, {{"command", "request-preview"}, {"path", "assets/sub/picture.png"}});
  frames(workspace, 3);
  const auto scene = workspace.snapshot().value();
  const auto viewport = workspace.captureViewport()->pixels;
  const auto before = send(workspace, {{"command", "inspect"}})["workspace"]["view"];
  awaitPreview(workspace,
               [&] { return workspace.captureAssetPreview("assets/sub/picture.png").has_value(); });
  const auto image = workspace.captureAssetPreview("assets/sub/picture.png").value();
  CHECK(image.width == 128);
  CHECK(image.pixels[(64 * 128 + 30) * 4] > 200);
  send(workspace, {{"command", "select-asset"}, {"path", "assets/shape.dotmesh"}});
  frames(workspace, 4);
  awaitPreview(workspace,
               [&] { return workspace.captureAssetPreview("assets/shape.dotmesh").has_value(); });
  CHECK(workspace.captureAssetPreview("assets/shape.dotmesh"));
  CHECK(workspace.captureViewport()->pixels == viewport);
  CHECK(workspace.snapshot().value() == scene);
  const auto after = send(workspace, {{"command", "inspect"}})["workspace"]["view"];
  CHECK(after["camera"] == before["camera"]);
  CHECK(after["draws"] == before["draws"]);
  send(workspace, {{"command", "set-asset-directory"}, {"path", "assets/sub"}});
  send(workspace, {{"command", "set-assets-view"}, {"mode", "list"}});
  frames(workspace);
  CHECK(send(workspace, {{"command", "inspect"}})["workspace"]["displayed"]["resources"]
        == Json::array({"assets/sub/picture.png"}));
  send(workspace, {{"command", "filter-panel"}, {"panel", "resources"}, {"query", ".dotmesh"}});
  frames(workspace);
  CHECK(send(workspace, {{"command", "inspect"}})["workspace"]["displayed"]["resources"]
        == Json::array({"assets/shape.dotmesh"}));
  CHECK_FALSE(workspace.dispatch(
      "{\"command\":\"set-asset-directory\",\"path\":\"assets/shape.dotmesh\"}"));
  CHECK_FALSE(workspace.dispatch("{\"command\":\"set-assets-view\",\"mode\":\"bad\"}"));
  send(workspace, {{"command", "refresh-previews"}});
  CHECK_FALSE(workspace.captureAssetPreview("assets/shape.dotmesh"));
}

TEST_CASE("asset_browser.native_material_sphere_follows_unsaved_form_and_real_failures") {
  Fixture f;
  Dot::Assets::MaterialDescription value;
  value.model = Dot::Graphics::SurfaceModel::Unlit;
  value.baseColor = {1, 0, 0, 1};
  REQUIRE(Dot::Assets::saveMaterial(f.files, "assets/surface.dotmaterial", value));
  const auto source = f.files.readText("assets/surface.dotmaterial").value();
  auto workspace = f.open();
  auto state = send(workspace, {{"command", "open-material"},
                                {"path", "assets/surface.dotmaterial"},
                                {"discard", false}})["material"];
  frames(workspace, 3);
  awaitPreview(workspace, [&] {
    return workspace.captureAssetPreview("assets/surface.dotmaterial").has_value();
  });
  const auto red = workspace.captureAssetPreview("assets/surface.dotmaterial").value();
  CHECK(red.width == 256);
  auto form = state["value"];
  form["baseColor"] = {0, 0, 1, 1};
  send(workspace, {{"command", "stage-material"},
                   {"buffer", state["buffer"]},
                   {"revision", state["revision"]},
                   {"value", form}});
  CHECK_FALSE(workspace.captureAssetPreview("assets/surface.dotmaterial"));
  auto pending = send(workspace, {{"command", "preview-assets"}})["previews"];
  CHECK_FALSE(pending["materialReady"].get<bool>());
  CHECK(pending["materialPending"].get<bool>());
  const auto count
      = send(workspace, {{"command", "preview-assets"}})["previews"]["jobs"].get<int>();
  frames(workspace);
  awaitPreview(workspace, [&] {
    return workspace.captureAssetPreview("assets/surface.dotmaterial").has_value();
  });
  const auto blue = workspace.captureAssetPreview("assets/surface.dotmaterial").value();
  CHECK(blue.pixels != red.pixels);
  CHECK(f.files.readText("assets/surface.dotmaterial").value() == source);
  CHECK(send(workspace, {{"command", "preview-assets"}})["previews"]["jobs"].get<int>()
        >= count + 1);
  CHECK_FALSE(workspace.dispatch("{\"command\":\"clear-asset-selection\"}"));
  form["baseColorTexture"] = {{"path", "assets/missing.png"},
                              {"sampling",
                               {{"colorSpace", "srgb"},
                                {"filter", "linear"},
                                {"addressU", "repeat"},
                                {"addressV", "repeat"},
                                {"mipmaps", true},
                                {"maxAnisotropy", 1}}}};
  send(workspace, {{"command", "stage-material"},
                   {"buffer", state["buffer"]},
                   {"revision", state["revision"]},
                   {"value", form}});
  frames(workspace);
  awaitPreview(workspace, [&] {
    return !send(workspace, {{"command", "preview-assets"}})["previews"]["materialPending"]
                .get<bool>();
  });
  auto status = send(workspace, {{"command", "preview-assets"}})["previews"];
  CHECK_FALSE(status["materialReady"].get<bool>());
  CHECK(status["materialError"].get<std::string>().find("missing.png") != std::string::npos);
  CHECK_FALSE(workspace.captureAssetPreview("assets/surface.dotmaterial"));
  send(workspace, {{"command", "reset-material"},
                   {"buffer", state["buffer"]},
                   {"revision", state["revision"]}});
  frames(workspace);
  awaitPreview(workspace, [&] {
    return workspace.captureAssetPreview("assets/surface.dotmaterial").has_value();
  });
  CHECK(workspace.captureAssetPreview("assets/surface.dotmaterial")->pixels == red.pixels);
  send(workspace, {{"command", "close-material"},
                   {"buffer", state["buffer"]},
                   {"revision", state["revision"]},
                   {"discard", false}});
  const auto closed = send(workspace, {{"command", "preview-assets"}})["previews"];
  CHECK_FALSE(closed["materialReady"].get<bool>());
  CHECK_FALSE(closed["materialPending"].get<bool>());
}

TEST_CASE("asset_browser.native_queue_cache_bounds_and_failed_sources_remain_explicit") {
  Fixture f;

  for (int i = 0; i < 70; ++i) {
    f.image("assets/" + std::to_string(i) + ".png");
  }

  REQUIRE(f.files.writeTextAtomic("assets/bad.png", "bad image"));
  auto workspace = f.open();

  for (int i = 0; i < 70; ++i) {
    send(workspace,
         {{"command", "request-preview"}, {"path", "assets/" + std::to_string(i) + ".png"}});
  }

  auto state = send(workspace, {{"command", "preview-assets"}})["previews"];
  CHECK(state["entries"].size() == 64);
  CHECK(state["pending"] == 64);
  CHECK(state["jobs"] == 0);
  CHECK_FALSE(state["device"].get<bool>());
  send(workspace, {{"command", "refresh-previews"}});
  send(workspace, {{"command", "request-preview"}, {"path", "assets/bad.png"}});
  frames(workspace);
  awaitPreview(workspace, [&] {
    const auto entries = send(workspace, {{"command", "preview-assets"}})["previews"]["entries"];

    for (const auto& entry : entries) {
      if (entry["path"] == "assets/bad.png" && !entry["error"].get<std::string>().empty()) {
        return true;
      }
    }

    return false;
  });
  state = send(workspace, {{"command", "preview-assets"}})["previews"];
  CHECK(state["entries"].size() <= 64);
  bool failed{};

  for (const auto& entry : state["entries"]) {
    if (entry["path"] == "assets/bad.png") {
      failed = !entry["ready"].get<bool>() && !entry["error"].get<std::string>().empty();
    }
  }

  CHECK(failed);
  CHECK_FALSE(workspace.captureAssetPreview("assets/bad.png"));
}

TEST_CASE("asset_browser.native_async_latest_form_refresh_reopen_and_joined_close") {
  Fixture f;
  f.image("assets/source.png");
  Dot::Assets::MaterialDescription material;
  material.model = Dot::Graphics::SurfaceModel::Unlit;
  material.baseColor = {1, 0, 0, 1};
  REQUIRE(Dot::Assets::saveMaterial(f.files, "assets/surface.dotmaterial", material));
  auto workspace = f.open();
  auto opened = send(workspace, {{"command", "open-material"},
                                 {"path", "assets/surface.dotmaterial"},
                                 {"discard", false}})["material"];
  auto form = opened["value"];
  awaitPreview(workspace, [&] {
    return workspace.captureAssetPreview("assets/surface.dotmaterial").has_value();
  });
  const auto original = workspace.captureAssetPreview("assets/surface.dotmaterial").value();

  for (int i = 0; i < 12; ++i) {
    form["baseColor"] = i % 2 == 0 ? Json::array({0, 1, 0, 1}) : Json::array({0, 0, 1, 1});
    send(workspace, {{"command", "stage-material"},
                     {"buffer", opened["buffer"]},
                     {"revision", opened["revision"]},
                     {"value", form}});
    frames(workspace);
  }

  awaitPreview(workspace, [&] {
    return workspace.captureAssetPreview("assets/surface.dotmaterial").has_value();
  });
  const auto blue = workspace.captureAssetPreview("assets/surface.dotmaterial").value();
  const auto middle = (128 * 256 + 128) * 4;
  CHECK(blue.pixels[middle] == 0);
  CHECK(blue.pixels[middle + 2] > 200);
  send(workspace, {{"command", "refresh-previews"}});
  CHECK_FALSE(workspace.captureAssetPreview("assets/surface.dotmaterial"));
  frames(workspace);
  send(workspace, {{"command", "close-material"},
                   {"buffer", opened["buffer"]},
                   {"revision", opened["revision"]},
                   {"discard", true}});
  send(workspace,
       {{"command", "open-material"}, {"path", "assets/surface.dotmaterial"}, {"discard", false}});
  awaitPreview(workspace, [&] {
    return workspace.captureAssetPreview("assets/surface.dotmaterial").has_value();
  });
  const auto red = workspace.captureAssetPreview("assets/surface.dotmaterial").value();
  CHECK(red.pixels[middle] > 200);
  CHECK(red.pixels == original.pixels);
  send(workspace, {{"command", "request-preview"}, {"path", "assets/source.png"}});
  frames(workspace);
  send(workspace, {{"command", "close"}, {"discard", true}});
  const auto status = send(workspace, {{"command", "preview-assets"}})["previews"];
  CHECK(status["worker"]["stopped"].get<bool>());
  CHECK_FALSE(status["worker"]["active"].get<bool>());
  CHECK(status["worker"]["pending"] == 0);
  CHECK(status["worker"]["completed"] == 0);
  CHECK_FALSE(status["device"].get<bool>());
  CHECK(status["pending"] == 0);
  CHECK_FALSE(status["materialPending"].get<bool>());
  CHECK_FALSE(
      workspace.dispatch("{\"command\":\"request-preview\",\"path\":\"assets/source.png\"}"));
}
