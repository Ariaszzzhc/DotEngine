export module Dot.Platform.System;

import Dot.Core.Result;

export namespace Dot::Platform {
  /** Video resources must be acquired and released on the main thread.
   * 视频资源须在主线程获取和释放。 */
  [[nodiscard]] auto isMainThread() noexcept -> bool;
  [[nodiscard]] auto videoInitialized() noexcept -> bool;
  [[nodiscard]] auto acquireVideo() -> Core::Result<void>;
  auto releaseVideo() noexcept -> void;
}  // namespace Dot::Platform
