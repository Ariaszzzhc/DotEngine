#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>

import Dot.Core.Application;
import Dot.Core.Result;
import Dot.Graphics.SceneRenderer;
import Dot.Project;
import Dot.Project.GamePackage;
import Dot.Project.GameSession;
import Dot.Script.Compiler;

namespace Dot::Runtime {
  /** Private synchronous loop configuration. A driver borrows services/callbacks
   * only during its call; session start/close and teardown stay with the player.
   * Application callbacks still publish input, update scripts and present scenes.
   * 私有同步循环配置；驱动仅在调用期间借用服务与回调，播放器仍负责会话启动/关闭和
   * 销毁。应用回调继续负责发布输入、更新脚本和呈现场景。 */
  struct PlayerOptions {
    std::size_t frames{0};
    bool pauseOnFocusLoss{true};
    float volume{1};
    std::optional<std::filesystem::path> profile;
    std::function<Core::Result<void>(Core::Application&, Project::GameSession&,
                                     Graphics::SceneRenderer&, Core::LoopCallbacks&,
                                     const std::size_t&)>
        driver{};
  };

  auto runProject(const Project::Project& project, const Script::TypeScriptCompiler& compiler,
                  const PlayerOptions& options) -> Core::Result<void>;
  auto runGame(const Project::GamePackage& game, const PlayerOptions& options)
      -> Core::Result<void>;
  auto playProject(const std::filesystem::path& root, const std::filesystem::path& compiler,
                   std::size_t frames, std::optional<std::filesystem::path> profile = {}) -> int;
  auto playGame(const std::filesystem::path& root, std::size_t frames,
                std::optional<std::filesystem::path> profile = {}) -> int;
}  // namespace Dot::Runtime
