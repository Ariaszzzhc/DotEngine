#pragma once

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

namespace Dot::Tests {
  /** Observe real completion without dispatching extra script/game frames.
   * 观察真实任务完成，不额外分发脚本/游戏帧；不改变虚拟机或进程超时。 */
  template <class Poll>
  auto awaitCompilation(Poll poll) -> decltype(poll()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    auto result = poll();
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));

    while (result->compiling) {
      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
      result = poll();
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    }

    return result;
  }
}  // namespace Dot::Tests
