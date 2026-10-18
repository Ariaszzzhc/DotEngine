module;

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Animation.ControllerFiles;

export import Dot.Animation.Controller;
import Dot.Core.Result;
import Dot.Core.Files;

export namespace Dot::Animation {
  /** Strict <=1 MiB/depth-16 controller JSON. Descriptions encode initial values,
   * never live clocks/parameters. Atomic writes require existing parents and
   * portable regular paths under assets/ with a .dotcontroller suffix and no symlinks.
   * 严格 <=1 MiB/16 层控制器 JSON，编码初始定义而非运行时钟/参数；原子写入要求父目录
   * 存在，assets/ 下的 .dotcontroller 可移植普通路径各组件不得为符号链接。 */
  /** Strict structural authoring decode; references/names may be temporarily incomplete.
   * Apply must call Controller::create before sampling or persistence.
   * 严格结构化编辑解码，引用/名称可暂时不完整；应用前须经 Controller::create 校验。 */
  [[nodiscard]] auto decodeControllerForm(std::string_view source)
      -> Core::Result<ControllerDescription>;
  [[nodiscard]] auto decodeController(std::string_view source) -> Core::Result<Controller>;
  [[nodiscard]] auto encodeController(const Controller& controller) -> Core::Result<std::string>;
  [[nodiscard]] auto loadController(const Core::FileSystem& files,
                                    const std::filesystem::path& path) -> Core::Result<Controller>;
  [[nodiscard]] auto saveController(const Core::FileSystem& files,
                                    const std::filesystem::path& path, const Controller& controller)
      -> Core::Result<void>;
  /** Strict named parameter object, <=16 KiB/depth four and 64 assignments.
   * The controller separately checks declared names/types before committing.
   * 严格命名参数对象，<=16 KiB/四层/64 项；控制器另行校验声明名称/类型并原子提交。 */
  [[nodiscard]] auto decodeControllerParameters(std::string_view source)
      -> Core::Result<std::vector<ControllerParameter>>;
}  // namespace Dot::Animation
