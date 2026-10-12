module;

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Script.Host;

import Dot.Core.Result;

export namespace Dot::Script {
  /** Synchronous owned service callable. Arguments are a strict finite JSON array
   * of at most 64 values; the view lives only during the call. A result is owned
   * finite JSON, or nullopt for undefined. Errors become catchable DotHostError
   * values with a stable Core error-code name. Capture owned dependencies or keep
   * borrowed dependencies alive; callbacks run on the runtime's creating thread.
   * Native work is not preempted; observed time excess faults the realm after
   * return. Do not destroy/move the active Runtime or retain argument views.
   * 同步、拥有的服务回调；参数为最多 64 项的严格有限 JSON 数组，视图仅在调用期间有效。
   * 结果为拥有的有限 JSON，nullopt 表示 undefined；错误成为可捕获 DotHostError，含稳定
   * Core 错误码名称。捕获拥有的依赖或确保借用依赖存活；在运行时创建线程调用，不抢占
   * 原生工作，返回后观察超时会使 realm 故障。不得销毁/移动活动 Runtime 或保留参数视图。 */
  using HostCallback = std::function<Core::Result<std::optional<std::string>>(std::string_view)>;

  struct HostFunction {
    std::string name;
    HostCallback callback;
  };

  /** Runtime copies up to 256 unique methods at creation; no subsequent mutation.
   * Names are 1..128 ASCII bytes, dot-separated identifier segments. An empty
   * registry exposes no native module. Sharing captures requires caller ownership.
   * 创建 Runtime 时复制最多 256 个唯一方法，之后不可修改；名称为 1..128 字节的点分
   * ASCII 标识符。不注册方法时不暴露原生模块，共享回调捕获须由调用者保证所有权。 */
  using HostBindings = std::vector<HostFunction>;
}  // namespace Dot::Script
