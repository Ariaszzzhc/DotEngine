#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>

import Dot.Core.Result;
import Dot.Project;
import Dot.Project.GamePackage;
import Dot.Script.Compiler;

namespace Dot::Runtime {
  /** Native loop options; loading frames do not consume the game-frame budget.
   * 原生循环配置；加载帧不消耗游戏帧预算。 */
  struct PlayerOptions {
    std::size_t frames{0};
    bool pauseOnFocusLoss{true};
    float volume{1};
    std::optional<std::filesystem::path> profile;
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
