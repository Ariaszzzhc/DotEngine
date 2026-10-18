#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

import Dot.Core.Result;
import Dot.Project;
import Dot.Project.GamePackage;
import Dot.Script.Compiler;

namespace Dot::Runtime {
  /** Executable resource layout is independent of project versus package data.
   * Development players use resources/, standalone exports use adjacent files.
   * 可执行程序资源布局独立于项目/数据包类型；开发播放器使用 resources/，独立导出使用同级文件。 */
  enum class PlayerLayout { Development, Standalone };

  /** Native loop options; loading frames do not consume the game-frame budget.
   * 原生循环配置；加载帧不消耗游戏帧预算。 */
  struct PlayerOptions {
    std::size_t frames{0};
    bool pauseOnFocusLoss{true};
    float volume{1};
    std::optional<std::filesystem::path> profile;
    /** Opt-in agent mode keeps the native loop active without focus; pointer
     * capture still reflects the OS. Data directory must already exist and is
     * explicit caller-owned storage, never an implicit fallback.
     * 显式智能体模式在失焦时继续原生循环，鼠标捕获仍以 OS 为准；数据目录须已存在，
     * 是调用者明确拥有的存储，不作隐式回退。 */
    std::optional<std::filesystem::path> control;
    std::optional<std::filesystem::path> dataDirectory;
  };

  auto runProject(const Project::Project& project, const Script::TypeScriptCompiler& compiler,
                  const PlayerOptions& options) -> Core::Result<void>;
  auto runGame(const Project::GamePackage& game, const PlayerOptions& options, PlayerLayout layout)
      -> Core::Result<void>;
  auto playProject(const std::filesystem::path& root, const std::filesystem::path& compiler,
                   const PlayerOptions& options) -> int;
  auto playGame(const std::filesystem::path& root, const PlayerOptions& options,
                PlayerLayout layout) -> int;
  auto parsePlayerOptions(std::span<const std::string_view> arguments, bool positionalBudget)
      -> Core::Result<PlayerOptions>;
  auto playerReceiptAction(std::string_view command, std::string_view target) -> int;
  auto sendPlayerRequest(std::string_view control, std::string_view id, std::string_view request)
      -> int;
}  // namespace Dot::Runtime
