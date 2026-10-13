#include <SDL3/SDL.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.Workspace;
import Dot.Project;
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

    auto undoShortcut(Workspace& workspace) -> void {
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
      for (const auto& [key, down] :
           {std::pair{modifier, true}, std::pair{SDL_SCANCODE_Z, true},
            std::pair{SDL_SCANCODE_Z, false}, std::pair{modifier, false}}) {
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
      const auto contexts = Graphics::guiContexts();
      std::filesystem::path control;

      {
        WorkspaceConfig invalid;
        invalid.font = parentRoot / "missing-font.ttf";
        require(!Workspace::open(project, invalid), "Missing font must fail without fallback");
        require(Graphics::guiContexts() == contexts, "Failed opening leaked GUI context");
        auto source = checked(Workspace::open(project));
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
        undoShortcut(workspace);
        frame(workspace);
        const auto restored = checked(workspace.captureViewport());
        auto undoState = Json::parse(checked(workspace.snapshot()));
        require(undoState["dirty"] == false, "GUI Undo failed: " + undoState.dump());
        require(before.pixels == restored.pixels, "GUI Undo did not restore scene pixels");
        snapshot = Json::parse(checked(workspace.snapshot()));
        require(snapshot["dirty"] == false, "Undo must restore saved baseline");
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
