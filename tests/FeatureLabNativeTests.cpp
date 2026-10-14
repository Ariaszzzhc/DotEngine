#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

import Dot.Core.Application;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Core.Process;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.GameExport;
import Dot.Script.Compiler;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.World.Scene;
import Dot.Editor.Workspace;

namespace {
  using namespace Dot;

  auto checked(const Core::Result<Script::LiveScriptReport>& result) -> Core::Result<void> {
    if (!result) {
      return std::unexpected(result.error());
    }

    if (result->frame.realmFaulted) {
      return Core::fail(Core::ErrorCode::InvalidState, "Example script realm faulted");
    }

    if (!result->frame.failures.empty()) {
      return std::unexpected(result->frame.failures.front().error);
    }

    return {};
  }
}  // namespace

TEST_CASE("feature_lab.native_graphics_audio_workspace_and_standalone") {
  auto temporary = Core::FileSystem::open(std::filesystem::temp_directory_path()).value();
  auto storage = Core::StagedDirectory::create(temporary).value();
  auto files = storage.files().value();
  std::filesystem::copy(DOT_FEATURE_LAB, files.root() / "project",
                        std::filesystem::copy_options::recursive);
  auto project = Project::Project::open(files.root() / "project").value();
  auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  auto kit = Project::RuntimeKit::open(Core::FileSystem::open(DOT_RUNTIME_KIT).value()).value();
  {
    Core::ApplicationConfig config;
    config.window.title = "Feature Lab native acceptance";
    config.window.width = 960;
    config.window.height = 640;
    config.window.gpuSurface = true;
    config.pauseOnFocusLoss = false;
    auto application = Core::Application::create(config).value();
    auto renderer = Graphics::Renderer::create(&application.window()).value();
    auto sceneRenderer = Graphics::SceneRenderer::create(renderer, project.files().value()).value();
    Project::GameSessionConfig sessionConfig;
    sessionConfig.audioMode = Project::SessionAudio::Playback;
    sessionConfig.audio.output.volume = 0;
    auto created = Project::GameSession::create(project, compiler, sessionConfig);
    REQUIRE_MESSAGE(created, (created ? "" : created.error().message));
    auto session = std::move(*created);
    auto started = session.start();
    REQUIRE_MESSAGE(started, (started ? "" : started.error().message));
    REQUIRE(started->failures.empty());
    REQUIRE_FALSE(started->realmFaulted);
    auto scene = session.scene().value();
    auto before = sceneRenderer.render(*scene, 192, 144).value();
    REQUIRE(scene->occlusion().value());
    CHECK(renderer.occlusion().value() == scene->occlusion().value());
    auto occlusion = renderer.readOcclusion();
    REQUIRE(occlusion);
    CHECK(occlusion->width == 192);
    CHECK(occlusion->height == 144);
    CHECK(renderer.resourceStats()->occlusionBytes == 192 * 144 * 24);
    CHECK(sceneRenderer.stats()->materials == 4);
    CHECK(sceneRenderer.stats()->textures == 2);
    CHECK(renderer.lighting()->lights.size() == 1);
    CHECK(renderer.visibilityStats()->mesh->blendedInstances == 1);
    CHECK(renderer.visibilityStats()->mesh->solidInstances == 8);
    auto entitiesBefore = scene->entities().value();
    auto orthographic = std::ranges::find(entitiesBefore, "Orthographic", &World::EntityInfo::name);
    REQUIRE(orthographic != entitiesBefore.end());
    auto otherProjection
        = sceneRenderer.render(*scene, 192, 144, {0, 0, 0, 1}, orthographic->id).value();
    CHECK(otherProjection.pixels != before.pixels);
    std::size_t presentations = 0;
    Core::LoopCallbacks callbacks;
    callbacks.prepareFrame
        = [&](const auto& input) { return session.publish(input).transform([](auto) {}); };
    callbacks.fixedUpdate = [&](double) { return checked(session.fixedUpdate()); };
    callbacks.update = [&](double seconds) { return checked(session.update(seconds)); };
    callbacks.render = [&](const auto&) {
      return sceneRenderer.present(*scene).transform([&](auto result) {
        if (result == Graphics::PresentResult::Presented) {
          ++presentations;
        }
      });
    };
    auto key = [&](SDL_Scancode code, bool down) {
      SDL_Event focus{};
      focus.window.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
      focus.window.windowID = application.window().id();
      REQUIRE(SDL_PushEvent(&focus));
      SDL_Event event{};
      event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
      event.key.windowID = application.window().id();
      event.key.scancode = code;
      event.key.down = down;
      REQUIRE(SDL_PushEvent(&event));
    };
    key(SDL_SCANCODE_D, true);

    for (int frame = 0; frame < 30; ++frame) {
      key(SDL_SCANCODE_D, true);
      auto advanced = application.step(std::chrono::nanoseconds{16'666'667}, callbacks);
      REQUIRE_MESSAGE(advanced, (advanced ? "" : advanced.error().message));
    }

    key(SDL_SCANCODE_D, false);
    REQUIRE(application.step(std::chrono::nanoseconds{16'666'667}, callbacks));
    auto after = sceneRenderer.render(*scene, 192, 144).value();
    CHECK(before.pixels != after.pixels);
    CHECK(presentations > 0);
    CHECK(scene->name()->find("Feature Lab") != std::string::npos);
    const auto entities = scene->entities().value();
    auto player = std::ranges::find(entities, "Dynamic", &World::EntityInfo::name);
    REQUIRE(player != entities.end());
    CHECK(player->transform.position.x > -1.6F);
    auto stopped = session.close();
    REQUIRE_MESSAGE(stopped, (stopped ? "" : stopped.error().message));
    CHECK(stopped->failures.empty());
    CHECK_FALSE(stopped->realmFaulted);
    auto completedEntities = scene->entities().value();
    auto stored = std::ranges::find(completedEntities, "Report", &World::EntityInfo::name);
    REQUIRE(stored != completedEntities.end());
    auto result = nlohmann::json::parse(stored->script->properties);
    CHECK(result["audio"]["mode"] == "playback");
    CHECK(result["audio"]["playback"]["state"] == "running");
    CHECK(std::stoull(result["audio"]["playback"]["output"]["callbacks"].get<std::string>()) > 0);
    const auto manifest = nlohmann::json::parse(project.files()->readText("coverage.json").value());
    const auto completed = result["checked"].get<std::vector<std::string>>();

    for (const auto& item : manifest["sdk"]) {
      INFO(item);
      CHECK(std::ranges::find(completed, item.get<std::string>()) != completed.end());
    }
  }

  {
    Editor::WorkspaceConfig config;
    config.font = DOT_EDITOR_FONT;
    config.compiler = DOT_TYPESCRIPT_COMPILER;
    config.play.session.audioMode = Project::SessionAudio::Playback;
    config.play.session.audio.output.volume = 0;
    auto opened = Editor::Workspace::open(project, config);
    REQUIRE_MESSAGE(opened, (opened ? "" : opened.error().message));
    auto workspace = std::move(*opened);
    const auto original = workspace.snapshot().value();
    REQUIRE(workspace.step(std::chrono::milliseconds{16}));
    REQUIRE(workspace.captureViewport());
    REQUIRE(workspace.dispatch("{\"command\":\"play\",\"revision\":\"1\"}"));
    REQUIRE(workspace.step(std::chrono::milliseconds{16}));
    REQUIRE(workspace.dispatch("{\"command\":\"stop-play\",\"revision\":\"1\"}"));
    CHECK(workspace.snapshot().value() == original);
  }

  auto exported
      = Project::exportGame(project, project.loadStartup().value(), compiler, kit, files, "game");
  REQUIRE_MESSAGE(exported, (exported ? "" : exported.error().message));
  std::filesystem::rename(exported->root(), files.root() / "relocated");
  std::filesystem::remove_all(project.files()->root());
#ifdef _WIN32
  const auto executable = files.root() / "relocated/DotGame.exe";
#else
  const auto executable = files.root() / "relocated/DotGame";
#endif
  auto played = Core::runProcess({executable, {"--frames", "6"}, files.root()});
  REQUIRE_MESSAGE(played, (played ? "" : played.error().message));
  CHECK_MESSAGE(played->exitCode == 0, played->standardError);
  CHECK(Core::processResources().children == 0);
  CHECK(Core::processResources().handles == 0);
}
