module;

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <format>
#include <thread>

module Dot.Platform.System;

import Dot.Core.Result;

namespace {
#if !defined(__APPLE__)
  const auto mainThread = std::this_thread::get_id();
#endif
}  // namespace

namespace Dot::Platform {
  auto isMainThread() noexcept -> bool {
#if defined(__APPLE__)
    return SDL_IsMainThread();
#else
    return std::this_thread::get_id() == mainThread;
#endif
  }

  auto videoInitialized() noexcept -> bool {
    return SDL_WasInit(SDL_INIT_VIDEO) != 0;
  }

  auto acquireVideo() -> Core::Result<void> {
    if (!isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Video initialization requires the main thread");
    }

    if (!videoInitialized()) {
      SDL_SetMainReady();
    }

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        std::format("Video initialization failed: {}", SDL_GetError()));
    }

    return {};
  }

  auto releaseVideo() noexcept -> void {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
}  // namespace Dot::Platform
