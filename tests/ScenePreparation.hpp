#pragma once

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>

import Dot.Platform.Input;

namespace Dot::Tests {
  /** Poll the real scheduler without replaying script Update or advancing time.
   * 轮询真实调度器，不重复脚本 Update 或推进游戏时间。 */
  template <class Session>
  auto awaitScene(Session& session, std::uint64_t id) -> decltype(session.pollScene()) {
    using Report = typename decltype(session.pollScene())::value_type;
    Report combined;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};

    for (;;) {
      auto result = session.pollScene();

      if (!result) {
        return result;
      }

      combined.failures.insert(combined.failures.end(), result->failures.begin(),
                               result->failures.end());
      combined.calls += result->calls;
      combined.created += result->created;
      combined.removed += result->removed;
      combined.realmFaulted = combined.realmFaulted || result->realmFaulted;
      auto status = session.sceneRequest(id);
      REQUIRE(status);
      using State = decltype(status->state);

      if (status->state != State::Pending && status->state != State::Loading) {
        return combined;
      }

      REQUIRE(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }

  template <class Play>
  auto awaitPlayScene(Play& play, std::uint64_t id) -> void {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};

    for (;;) {
      auto status = play.sceneRequest(id);
      REQUIRE(status);
      using State = decltype(status->state);

      if (status->state != State::Pending && status->state != State::Loading) {
        return;
      }

      REQUIRE(std::chrono::steady_clock::now() < deadline);
      REQUIRE(play.advance(Dot::Platform::InputState{}, std::chrono::nanoseconds{0}));
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }
}  // namespace Dot::Tests
