module;

#include <SDL3/SDL.h>

#include <expected>
#include <format>
#include <memory>
#include <string>
#include <utility>

module Dot.Platform.Window;

import Dot.Core.Result;
import Dot.Platform.System;

namespace Dot::Platform {
  namespace {
    auto windowFailure(std::string_view operation) -> std::unexpected<Core::Error> {
      return Core::fail(Core::ErrorCode::External,
                        std::format("{}: {}", operation, SDL_GetError()));
    }
  }  // namespace

  struct Window::Impl {
    SDL_Window* window{};
    std::uint32_t id{};
    bool videoOwned{false};

    ~Impl() {
      if (window) {
        SDL_DestroyWindow(window);
      }

      if (videoOwned) {
        releaseVideo();
      }
    }
  };

  Window::Window(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Window::Window(Window&&) noexcept = default;

  auto Window::operator=(Window&&) noexcept -> Window& = default;

  Window::~Window() = default;

  auto Window::create(const WindowConfig& config) -> Core::Result<Window> {
    if (config.width <= 0 || config.height <= 0 || config.title.find('\0') != std::string::npos) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid window dimensions or title");
    }

    auto impl = std::make_unique<Impl>();
    auto video = acquireVideo();

    if (!video) {
      return std::unexpected(video.error());
    }

    impl->videoOwned = true;
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (config.hidden) {
      flags |= SDL_WINDOW_HIDDEN;
    }

    if (config.resizable) {
      flags |= SDL_WINDOW_RESIZABLE;
    }
#if defined(__APPLE__)
    if (config.gpuSurface) {
      flags |= SDL_WINDOW_METAL;
    }
#endif
    impl->window = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);

    if (!impl->window) {
      return windowFailure("Window creation failed");
    }

    impl->id = SDL_GetWindowID(impl->window);
    return Window(std::move(impl));
  }

  auto Window::id() const noexcept -> std::uint32_t {
    return m_impl ? m_impl->id : 0;
  }

  auto Window::nativeHandle() const noexcept -> void* {
    return m_impl ? m_impl->window : nullptr;
  }

  auto Window::size() const -> Core::Result<Extent> {
    if (!m_impl || !isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Window size requires a live window on the main thread");
    }

    Extent extent{};

    if (!SDL_GetWindowSize(m_impl->window, &extent.width, &extent.height)) {
      return windowFailure("Cannot query window size");
    }

    return extent;
  }

  auto Window::pixelSize() const -> Core::Result<Extent> {
    if (!m_impl || !isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Pixel size requires a live window on the main thread");
    }

    Extent extent{};

    if (!SDL_GetWindowSizeInPixels(m_impl->window, &extent.width, &extent.height)) {
      return windowFailure("Cannot query pixel size");
    }

    return extent;
  }

  auto Window::setTitle(std::string_view title) -> Core::Result<void> {
    if (!m_impl || !isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Window title requires a live window on the main thread");
    }

    if (title.find('\0') != std::string_view::npos) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Window title must not contain null bytes");
    }

    const std::string ownedTitle(title);

    if (!SDL_SetWindowTitle(m_impl->window, ownedTitle.c_str())) {
      return windowFailure("Cannot set window title");
    }

    return {};
  }

  auto Window::resize(int width, int height) -> Core::Result<void> {
    if (!m_impl || !isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Window resize requires a live window on the main thread");
    }

    if (width <= 0 || height <= 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Window dimensions must be positive");
    }

    if (!SDL_SetWindowSize(m_impl->window, width, height)) {
      return windowFailure("Cannot resize window");
    }

    return {};
  }

  auto Window::show() -> Core::Result<void> {
    if (!m_impl || !isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Showing a window requires the main thread");
    }

    if (!SDL_ShowWindow(m_impl->window)) {
      return windowFailure("Cannot show window");
    }

    return {};
  }

  auto Window::setTextInput(bool enabled) -> Core::Result<void> {
    if (!m_impl || !isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Text input requires a live window on the main thread");
    }

    if (!(enabled ? SDL_StartTextInput(m_impl->window) : SDL_StopTextInput(m_impl->window))) {
      return windowFailure("Cannot change text input mode");
    }

    return {};
  }
}  // namespace Dot::Platform
