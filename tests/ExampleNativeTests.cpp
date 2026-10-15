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

import Dot.Assets.Image;
import Dot.Assets.Material;
import Dot.Editor.Workspace;
import Dot.Core.Application;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Core.Process;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.GameExport;
import Dot.Project.GamePackage;
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
    auto rendered = sceneRenderer.render(*scene, 192, 144);
    REQUIRE_MESSAGE(rendered, (rendered ? "" : rendered.error().message));
    auto before = std::move(*rendered);
    auto capture = Core::FileSystem::open(DOT_EXAMPLE_CAPTURE_ROOT).value();
    auto detailed = sceneRenderer.render(*scene, 1280, 800);
    REQUIRE_MESSAGE(detailed, (detailed ? "" : detailed.error().message));
    REQUIRE(capture.writeAtomic("prism-bay.png", Assets::encodeImage(*detailed).value()));
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

TEST_CASE("prism.physical_bay_pixels_editor_play_and_owned_export_use_actual_assets") {
  using Json = nlohmann::json;
  using namespace std::chrono_literals;
  auto temporary = Core::FileSystem::open(std::filesystem::temp_directory_path()).value();
  auto storage = Core::StagedDirectory::create(temporary).value();
  auto files = storage.files().value();
  std::filesystem::copy(DOT_EXAMPLE_PROJECT, files.root() / "project",
                        std::filesystem::copy_options::recursive);
  auto project = Project::Project::open(files.root() / "project").value();
  auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
  auto capture = Core::FileSystem::open(DOT_EXAMPLE_CAPTURE_ROOT).value();
  auto scene = project.loadStartup().value();
  auto renderer = Graphics::Renderer::create().value();
  const auto baseline = renderer.resourceStats().value();
  std::vector<std::uint8_t> readyPixels;
  auto published = Project::GamePackage::publish(project, scene, compiler, files, "export").value();

  {
    auto drawing = Graphics::SceneRenderer::create(renderer, project.files().value()).value();
    auto image = drawing.render(scene, 640, 400);
    REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
    const auto original = *image;
    CHECK(drawing.stats()->materials >= 8);
    CHECK(drawing.stats()->textures == 6);
    CHECK(drawing.stats()->environments == 1);
    const auto changed = [&](const Graphics::Image& actual) {
      std::size_t pixels{};

      for (std::size_t i = 0; i < original.pixels.size(); i += 4) {
        bool different{};

        for (std::size_t c = 0; c < 3; ++c) {
          different |= std::abs(int(original.pixels[i + c]) - int(actual.pixels[i + c])) > 1;
        }

        pixels += different ? 1U : 0U;
      }

      return pixels;
    };
    auto target = renderer.createTarget(640, 400).value();
    REQUIRE(drawing.renderInto(scene, target));
    const auto linear = renderer.readHdrTarget(target).value();
    float peak{};

    for (std::size_t i = 0; i < linear.pixels.size(); i += 4) {
      peak = std::max({peak, linear.pixels[i], linear.pixels[i + 1], linear.pixels[i + 2]});
    }

    CHECK(peak > 1);
    CHECK(renderer.postProcess()->toneMap == Graphics::ToneMap::AcesFitted);
    const auto environment = scene.environment().value();
    REQUIRE(scene.setEnvironment({}));
    CHECK(changed(drawing.render(scene, 640, 400).value()) > 2048);
    REQUIRE(scene.setEnvironment(environment));
    const auto ao = scene.occlusion().value();
    REQUIRE(scene.setOcclusion({}));
    CHECK(changed(drawing.render(scene, 640, 400).value()) > 64);
    REQUIRE(scene.setOcclusion(ao));

    const auto entities = scene.entities().value();
    const auto key = std::ranges::find(entities, "Bay key", &World::EntityInfo::name);
    REQUIRE(key != entities.end());
    auto unshadowed = *key->light;
    unshadowed.shadows.reset();
    REQUIRE(scene.setLight(key->id, unshadowed));
    CHECK(changed(drawing.render(scene, 640, 400).value()) > 64);
    REQUIRE(scene.setLight(key->id, key->light));
    const auto path = "assets/materials/deck-steel.dotmaterial";
    auto plain = Assets::loadMaterial(project.files().value(), path).value();
    plain.baseColorTexture.reset();
    plain.metallicRoughnessTexture.reset();
    plain.normalTexture.reset();
    plain.occlusionTexture.reset();
    REQUIRE(drawing.previewMaterial(Graphics::MaterialPreview{path, plain}));
    CHECK(changed(drawing.render(scene, 640, 400).value()) > 2048);
    REQUIRE(drawing.previewMaterial({}));
    CHECK(drawing.render(scene, 640, 400)->pixels == original.pixels);
    auto session = Project::GameSession::create(project, compiler).value();
    const auto started = session.start().value();
    REQUIRE(started.failures.empty());
    REQUIRE_FALSE(started.realmFaulted);
    readyPixels = drawing.render(*session.scene().value(), 640, 400)->pixels;
    const auto closed = session.close().value();
    REQUIRE(closed.failures.empty());
  }

  {
    Editor::WorkspaceConfig config;
    config.compiler = DOT_TYPESCRIPT_COMPILER;
    config.play.session.audioMode = Project::SessionAudio::Offline;
    auto workspace = Editor::Workspace::open(project, config).value();
    const auto send = [&](Json request) {
      auto result = workspace.dispatch(request.dump());
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return Json::parse(*result);
    };
    const auto inspect = [&] { return send({{"command", "inspect"}}); };
    const auto action = [&](Json request) {
      request["revision"] = inspect()["revision"];
      return send(std::move(request));
    };
    send({{"command", "set-view"}, {"mode", "game"}});

    for (int frame = 0; frame < 3; ++frame) {
      REQUIRE(workspace.step(16ms));
    }

    const auto authored = inspect()["scene"];
    const auto before = workspace.captureViewport().value();
    action({{"command", "play"}});
    REQUIRE(workspace.step(500ms));
    const auto running = send({{"command", "inspect-play"}});
    CHECK(running["play"]["diagnostics"].empty());
    CHECK(running["play"]["error"].is_null());
    CHECK(inspect()["scene"] == authored);
    auto playedFrame = workspace.captureViewport();
    REQUIRE_MESSAGE(
        playedFrame,
        (playedFrame ? "" : playedFrame.error().message + " view: " + running["view"].dump()));
    const auto played = *playedFrame;
    CHECK(played.pixels != before.pixels);
    REQUIRE(capture.writeAtomic("prism-bay-editor-play.png", Assets::encodeImage(played).value()));
    action({{"command", "stop-play"}});
    REQUIRE(workspace.step(16ms));
    CHECK(workspace.captureViewport()->pixels == before.pixels);
    send({{"command", "close"}, {"discard", true}});
    REQUIRE(workspace.step(16ms));
  }

  std::filesystem::rename(published.root(), files.root() / "relocated");
  auto package
      = Project::GamePackage::open(Core::FileSystem::open(files.root() / "relocated").value())
            .value();
  std::filesystem::remove_all(project.files()->root());
  std::filesystem::remove_all(files.root() / "relocated");

  {
    auto startup = package.scene().value();
    auto session = Project::GameSession::createCompiled(package.files().value(),
                                                        package.description().value(), startup,
                                                        package.program().value())
                       .value();
    const auto started = session.start().value();
    REQUIRE(started.failures.empty());
    REQUIRE_FALSE(started.realmFaulted);
    auto drawing = Graphics::SceneRenderer::create(renderer, package.files().value()).value();
    const auto image = drawing.render(*session.scene().value(), 640, 400).value();
    CHECK(image.pixels == readyPixels);
    REQUIRE(capture.writeAtomic("prism-bay-export.png", Assets::encodeImage(image).value()));
    const auto closed = session.close().value();
    REQUIRE(closed.failures.empty());
  }

  const auto after = renderer.resourceStats().value();
  CHECK(after.meshes == baseline.meshes);
  CHECK(after.vertexBytes == baseline.vertexBytes);
  CHECK(after.indexBytes == baseline.indexBytes);
  CHECK(after.textures == baseline.textures);
  CHECK(after.textureBytes == baseline.textureBytes);
  CHECK(after.environments == baseline.environments);
  CHECK(after.targets == baseline.targets);
}
