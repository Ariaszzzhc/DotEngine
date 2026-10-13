#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
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

TEST_CASE("prism.native_window_gpu_input_audio_and_exported_player") {
  auto temporary = Core::FileSystem::open(std::filesystem::temp_directory_path()).value();
  auto storage = Core::StagedDirectory::create(temporary).value();
  auto files = storage.files().value();
  std::filesystem::copy(DOT_EXAMPLE_PROJECT, files.root() / "project",
                        std::filesystem::copy_options::recursive);
  auto project = Project::Project::open(files.root() / "project").value();
  auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  auto kit = Project::RuntimeKit::open(Core::FileSystem::open(DOT_RUNTIME_KIT).value()).value();
  {
    Core::ApplicationConfig config;
    config.window.title = "Prism Relay native acceptance";
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
    auto capture = Core::FileSystem::open(DOT_EXAMPLE_CAPTURE_ROOT).value();
    std::string ppm = "P6\n192 144\n255\n";

    for (std::size_t pixel = 0; pixel < before.pixels.size(); pixel += 4) {
      ppm.push_back(static_cast<char>(before.pixels[pixel]));
      ppm.push_back(static_cast<char>(before.pixels[pixel + 1]));
      ppm.push_back(static_cast<char>(before.pixels[pixel + 2]));
    }

    REQUIRE(capture.writeTextAtomic("prism-preview.ppm", ppm));
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
    key(SDL_SCANCODE_RETURN, true);
    REQUIRE(application.step(std::chrono::nanoseconds{16'666'667}, callbacks));
    key(SDL_SCANCODE_RETURN, false);
    key(SDL_SCANCODE_D, true);

    for (int frame = 0; frame < 45; ++frame) {
      key(SDL_SCANCODE_D, true);
      auto advanced = application.step(std::chrono::nanoseconds{16'666'667}, callbacks);
      REQUIRE_MESSAGE(advanced, (advanced ? "" : advanced.error().message));
    }

    key(SDL_SCANCODE_D, false);
    REQUIRE(application.step(std::chrono::nanoseconds{16'666'667}, callbacks));
    auto after = sceneRenderer.render(*scene, 192, 144).value();
    CHECK(before.pixels != after.pixels);
    CHECK(presentations > 0);
    CHECK(scene->name()->find("1/8") != std::string::npos);
    const auto entities = scene->entities().value();
    auto player = std::ranges::find(entities, "Player", &World::EntityInfo::name);
    REQUIRE(player != entities.end());
    CHECK(player->transform.position.x > -3);
    auto stopped = session.close();
    REQUIRE_MESSAGE(stopped, (stopped ? "" : stopped.error().message));
    CHECK(stopped->failures.empty());
    CHECK_FALSE(stopped->realmFaulted);
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
