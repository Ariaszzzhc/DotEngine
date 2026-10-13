#include <SDL3/SDL.h>

#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.Workspace;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.World.Scene;
import Dot.Script.Compiler;
import Dot.Graphics.Renderer;

namespace Dot::Editor {
  namespace {
    using Json = nlohmann::json;
    using namespace std::chrono_literals;

    auto require(bool condition, std::string_view message) -> void {
      if (!condition) {
        throw std::runtime_error(std::string(message));
      }
    }

    template <class T>
    auto checked(Core::Result<T> value) -> T {
      if (!value) {
        throw std::runtime_error(value.error().message);
      }

      return std::move(*value);
    }

    auto frame(Workspace& workspace) -> void {
      auto result = workspace.step(16ms);
      require(result.has_value(), result ? "" : result.error().message);
    }

    auto key(Workspace& workspace, SDL_Scancode code, bool down) -> void {
      int count = 0;
      auto** windows = SDL_GetWindows(&count);
      require(windows && count == 1, "Acceptance requires one workspace window");
      const auto window = SDL_GetWindowID(windows[0]);
      SDL_free(windows);
      SDL_Event focus{};
      focus.window.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
      focus.window.windowID = window;
      require(SDL_PushEvent(&focus), SDL_GetError());
      SDL_Event event{};
      event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
      event.key.windowID = window;
      event.key.scancode = code;
      event.key.down = down;
      require(SDL_PushEvent(&event), SDL_GetError());
      frame(workspace);
    }

    auto wave(const Core::FileSystem& files) -> Core::Result<void> {
      constexpr std::size_t frames = 4800;
      std::vector<std::byte> bytes(44 + frames * 4);
      const auto put = [&](std::size_t offset, std::uint64_t value, std::size_t count) {
        for (std::size_t byte = 0; byte < count; ++byte) {
          bytes[offset + byte] = static_cast<std::byte>((value >> (8 * byte)) & 255);
        }
      };
      const auto text = [&](std::size_t offset, std::string_view value) {
        for (char character : value) {
          bytes[offset++] = static_cast<std::byte>(character);
        }
      };
      text(0, "RIFF");
      put(4, bytes.size() - 8, 4);
      text(8, "WAVEfmt ");
      put(16, 16, 4);
      put(20, 3, 2);
      put(22, 1, 2);
      put(24, 48000, 4);
      put(28, 48000 * 4, 4);
      put(32, 4, 2);
      put(34, 32, 2);
      text(36, "data");
      put(40, frames * 4, 4);

      for (std::size_t frame = 0; frame < frames; ++frame) {
        put(44 + frame * 4, std::bit_cast<std::uint32_t>(0.25F), 4);
      }

      return files.writeAtomic("assets/tone.wav", bytes);
    }

    auto commandShortcut(Workspace& workspace, SDL_Scancode command) -> void {
      int count = 0;
      auto** windows = SDL_GetWindows(&count);
      require(windows && count == 1, "Acceptance requires one native workspace window");
      const auto window = SDL_GetWindowID(windows[0]);
      SDL_free(windows);
      SDL_Event focus{};
      focus.window.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
      focus.window.windowID = window;
      require(SDL_PushEvent(&focus), SDL_GetError());
#if defined(__APPLE__)
      const auto modifier = SDL_SCANCODE_LGUI;
#else
      const auto modifier = SDL_SCANCODE_LCTRL;
#endif
      for (const auto& [key, down] : {std::pair{modifier, true}, std::pair{command, true},
                                      std::pair{command, false}, std::pair{modifier, false}}) {
        SDL_Event event{};
        event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = window;
        event.key.scancode = key;
        event.key.down = down;
        require(SDL_PushEvent(&event), SDL_GetError());
        frame(workspace);
      }
    }

  }  // namespace

  /** Exercise the real project window, GPU preview and live command mailbox.
   * 验证真实项目窗口、GPU 预览以及在线命令邮箱。 */
  auto testWorkspace(const std::filesystem::path& sdkRoot, const std::filesystem::path& parentRoot)
      -> int {
    try {
      auto parent = checked(Core::FileSystem::open(parentRoot));
      auto sdk = checked(Core::FileSystem::open(sdkRoot));
      auto storage = checked(Core::StagedDirectory::create(parent));
      auto project
          = checked(Project::Project::create(checked(storage.files()), "workspace", {}, sdk));
      auto game = project.loadStartup().value();
      const auto actor = game.entities()->at(1).id;
      require(game.setRigidBody(actor, World::RigidBody{}).has_value(),
              "Prepare native rigid body");
      World::AudioSource audio;
      audio.asset = "assets/tone.wav";
      audio.voice.looping = true;
      audio.spatial.enabled = false;
      require(game.setAudioSource(actor, audio).has_value(), "Prepare native audio source");
      require(project.saveStartup(game).has_value(), "Save acceptance fixture");
      require(wave(project.files().value()).has_value(), "Prepare audio fixture");
      require(project.files()
                  ->writeTextAtomic(
                      "scripts/mover.ts",
                      "import {ScriptComponent,input} from './dot/engine.js';export class Mover "
                      "extends ScriptComponent "
                      "{onStart(){this.self.playAudio();}onFixedUpdate(){this.self.setVelocity({x:"
                      "input.key('D').down?1.5:0,y:0,z:0});}onUpdate(){this.self.name=this.self."
                      "audioVoice.playing?'Playing':'Silent';}onStop(){this.self.name='Stopped';}}")
                  .has_value(),
              "Prepare real physics/audio input script");
      const auto contexts = Graphics::guiContexts();
      std::filesystem::path control;

      {
        WorkspaceConfig invalid;
        invalid.font = parentRoot / "missing-font.ttf";
        require(!Workspace::open(project, invalid), "Missing font must fail without fallback");
        require(Graphics::guiContexts() == contexts, "Failed opening leaked GUI context");
        WorkspaceConfig configuration;
        configuration.play.session.audio.output.volume = 0;
        configuration.play.session.pollInterval = 0ms;
        auto source = checked(Workspace::open(project, configuration));
        control = checked(source.controlDirectory());
        auto workspace = std::move(source);
        require(!source.snapshot(), "Moved workspace must reject operations");
        require(!workspace.captureViewport(), "Unrendered preview must fail");
        bool workerRejected = false;
        auto worker = std::thread(
            [&] { workerRejected = !workspace.snapshot() && !Workspace::open(project); });
        worker.join();
        require(workerRejected, "Worker operations must fail");
        frame(workspace);
        frame(workspace);
        frame(workspace);
        const auto before = checked(workspace.captureViewport());
        auto snapshot = Json::parse(checked(workspace.snapshot()));
        require(snapshot["scene"]["entities"].size() == 2, "Initial project entities");
        auto cube = snapshot["scene"]["entities"][1];
        auto transform = cube["transform"];
        transform["position"][0] = 2;
        Json request{
            {"command", "edit"},
            {"revision", snapshot["revision"]},
            {"operations",
             Json::array({{{"op", "transform"}, {"entity", cube["id"]}, {"value", transform}}})}};
        auto response = checked(submitWorkspaceRequest(control, "001-edit", request.dump()));
        require(!std::filesystem::exists(response), "Submission must not pretend to apply");
        require(!submitWorkspaceRequest(control, "../bad", request.dump()), "Invalid ID accepted");
        require(!submitWorkspaceRequest(control, "001-edit", "{\"command\":\"inspect\"}"),
                "Conflicting ID accepted");
        require(!submitWorkspaceRequest(control, "bad-json", "{\"command\":1,\"command\":2}"),
                "Duplicate fields accepted");
        frame(workspace);
        const auto moved = checked(workspace.captureViewport());
        require(before.width == moved.width && before.height == moved.height
                    && before.pixels != moved.pixels,
                "Live edit must change actual GPU pixels");
        auto files = checked(Core::FileSystem::open(control));
        auto record = Json::parse(checked(files.readText("responses/001-edit.json")));
        require(record["response"]["ok"] == true, "Live edit response failed");
        const auto edited = checked(workspace.snapshot());
        checked(submitWorkspaceRequest(control, "001-edit", request.dump()));
        frame(workspace);
        require(checked(workspace.snapshot()) == edited, "Request retry re-executed effects");
        checked(submitWorkspaceRequest(control, "002-stale", request.dump()));
        frame(workspace);
        record = Json::parse(checked(files.readText("responses/002-stale.json")));
        require(record["response"]["ok"] == false, "Stale revision must fail");
        require(checked(workspace.snapshot()) == edited, "Stale request mutated document");

        // Activate GUI Undo through the platform's native command shortcut.
        // 通过平台原生命令快捷键激活 GUI 撤销。
        commandShortcut(workspace, SDL_SCANCODE_Z);
        frame(workspace);
        const auto restored = checked(workspace.captureViewport());
        auto undoState = Json::parse(checked(workspace.snapshot()));
        require(undoState["dirty"] == false, "GUI Undo failed: " + undoState.dump());
        require(before.pixels == restored.pixels, "GUI Undo did not restore scene pixels");
        snapshot = Json::parse(checked(workspace.snapshot()));
        require(snapshot["dirty"] == false, "Undo must restore saved baseline");
        transform["position"][0] = 0.25;
        request["revision"] = snapshot["revision"];
        request["operations"][0]["value"] = transform;
        checked(workspace.dispatch(request.dump()));
        frame(workspace);
        frame(workspace);
        const auto authoring = checked(workspace.snapshot());
        const auto authoringPixels = checked(workspace.captureViewport());
        const auto generations = Script::compilerGenerations();
        key(workspace, SDL_SCANCODE_F5, true);
        key(workspace, SDL_SCANCODE_F5, false);
        auto startupStatus = Json::parse(checked(workspace.dispatch("{\"command\":\"inspect\"}")));
        require(startupStatus["workspace"]["play"]["state"] == "running",
                "Native F5 Play: " + startupStatus.dump());
        auto play = Json::parse(checked(workspace.dispatch("{\"command\":\"inspect-play\"}")));
        require(play["play"]["state"] == "running", "Native F5 did not activate Play");
        checked(workspace.dispatch("{\"command\":\"capture-input\",\"capture\":true}"));
        frame(workspace);
        key(workspace, SDL_SCANCODE_D, true);

        for (int tick = 0; tick < 20; ++tick) {
          frame(workspace);
        }

        play = Json::parse(checked(workspace.dispatch("{\"command\":\"inspect-play\"}")));
        require(play["scene"]["entities"][1]["transform"]["position"][0].get<double>() > 0.3,
                "Captured SDL input did not advance real physics");
        require(play["scene"]["entities"][1]["name"] == "Playing",
                "Native audio voice not playing");
        require(checked(workspace.captureViewport()).pixels != authoringPixels.pixels,
                "Runtime movement did not change GPU pixels");
        require(checked(workspace.snapshot()) == authoring, "Play changed the authoring document");
        key(workspace, SDL_SCANCODE_ESCAPE, true);
        key(workspace, SDL_SCANCODE_ESCAPE, false);
        auto status = Json::parse(checked(workspace.dispatch("{\"command\":\"inspect\"}")));
        require(status["workspace"]["play"]["captureInput"] == false,
                "Escape failed to release capture");
        key(workspace, SDL_SCANCODE_D, false);
        auto opened = Json::parse(checked(workspace.dispatch(
            "{\"command\":\"open-text\",\"path\":\"scripts/mover.ts\",\"discard\":false}")));
        auto file = opened["file"];
        auto code = file["text"].get<std::string>();
        const auto speed = code.find("?1.5:");
        require(speed != std::string::npos, "Acceptance script speed marker");
        code.replace(speed, 5, "?3:");
        checked(workspace.dispatch(Json{
            {"command", "replace-text"},
            {"buffer", file["buffer"]},
            {"revision", file["revision"]},
            {"text", code}}.dump()));
        commandShortcut(workspace, SDL_SCANCODE_S);
        auto resources = Json::parse(checked(workspace.dispatch("{\"command\":\"resources\"}")));
        require(resources["file"]["dirty"] == false, "Native code-page Cmd/Ctrl+S did not save");
        require(project.files()->readText("scripts/mover.ts").value() == code,
                "Code save did not persist bytes");
        checked(workspace.dispatch("{\"command\":\"check-scripts\"}"));
        const auto afterReload
            = Json::parse(checked(workspace.dispatch("{\"command\":\"inspect-play\"}")));
        const auto x
            = afterReload["scene"]["entities"][1]["transform"]["position"][0].get<double>();
        checked(workspace.dispatch("{\"command\":\"capture-input\",\"capture\":true}"));
        frame(workspace);
        key(workspace, SDL_SCANCODE_D, true);

        for (int tick = 0; tick < 20; ++tick) {
          frame(workspace);
        }

        auto reloaded = Json::parse(checked(workspace.dispatch("{\"command\":\"inspect-play\"}")));
        require(
            reloaded["scene"]["entities"][1]["transform"]["position"][0].get<double>() > x + 0.8,
            "Saved source did not hot reload into faster real physics");
        key(workspace, SDL_SCANCODE_ESCAPE, true);
        key(workspace, SDL_SCANCODE_ESCAPE, false);
        key(workspace, SDL_SCANCODE_D, false);
        key(workspace, SDL_SCANCODE_F5, true);
        key(workspace, SDL_SCANCODE_F5, false);
        frame(workspace);
        require(checked(workspace.captureViewport()).pixels == authoringPixels.pixels,
                "Stop did not restore unsaved authoring pixels");
        require(checked(workspace.snapshot()) == authoring, "Stop mutated the authoring document");
        require(Script::compilerGenerations() == generations, "Stop retained script generation");
        play = Json::parse(checked(workspace.dispatch("{\"command\":\"inspect-play\"}")));
        require(
            play["play"]["state"] == "stopped" && play["scene"]["entities"][1]["name"] == "Stopped",
            "Stop did not release services and invoke hooks");
        status = Json::parse(authoring);
        checked(
            workspace.dispatch(Json{{"command", "undo"}, {"revision", status["revision"]}}.dump()));
        frame(workspace);
        auto invalidCommand = workspace.dispatch("{\"command\":\"close\",\"discard\":\"yes\"}");
        require(!invalidCommand, "Malformed close accepted");
        auto close
            = Json::parse(checked(workspace.dispatch("{\"command\":\"close\",\"discard\":false}")));
        require(close["running"] == false, "Clean close must stop application");
      }

      require(!std::filesystem::exists(control), "Workspace control directory leaked");
      require(!submitWorkspaceRequest(control, "gone", "{\"command\":\"inspect\"}"),
              "Dead session accepted submission");
      require(Graphics::guiContexts() == contexts, "Workspace leaked GUI context");
      std::println(
          "Workspace acceptance passed: native GUI shortcut, GPU preview, live requests, lifetime");
      return 0;
    } catch (const std::exception& error) {
      std::println(stderr, "Workspace acceptance: {}", error.what());
      return 1;
    }
  }
}  // namespace Dot::Editor
