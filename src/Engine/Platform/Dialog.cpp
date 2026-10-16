module;

#include <SDL3/SDL.h>

#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

module Dot.Platform.Dialog;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Platform.System;
import Dot.Platform.Window;

namespace Dot::Platform {
  namespace {
    auto SDLCALL completed(void* data, const char* const* paths, int) -> void {
      std::unique_ptr<std::shared_ptr<DialogRequest>> ticket(
          static_cast<std::shared_ptr<DialogRequest>*>(data));
      (*ticket)->complete(paths, paths ? nullptr : SDL_GetError());
    }

    constexpr SDL_DialogFileFilter pngFilter[]{{"PNG image", "png"}};
  }  // namespace

  DialogRequest::DialogRequest() = default;

  auto DialogRequest::open(const Window& parent, DialogKind kind, std::string_view initial)
      -> Core::Result<std::shared_ptr<DialogRequest>> {
    if (!isMainThread() || !parent.nativeHandle()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Native dialog requires a live parent on the main thread");
    }

    if ((kind != DialogKind::Folder && kind != DialogKind::PngSave) || initial.size() > 16384
        || initial.find('\0') != std::string_view::npos || !Core::isValidUtf8(initial)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid native dialog kind/location");
    }

    try {
      auto request = std::make_shared<DialogRequest>();
      request->m_initial = initial;
      auto ticket = std::make_unique<std::shared_ptr<DialogRequest>>(request);
      auto* window = static_cast<SDL_Window*>(parent.nativeHandle());
      const auto* location = request->m_initial.empty() ? nullptr : request->m_initial.c_str();

      if (kind == DialogKind::Folder) {
        SDL_ShowOpenFolderDialog(completed, ticket.release(), window, location, false);
      } else {
        SDL_ShowSaveFileDialog(completed, ticket.release(), window, pngFilter, 1, location);
      }

      return request;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Start native dialog: " + std::string(error.what()));
    }
  }

  auto DialogRequest::complete(const char* const* paths, const char* error) noexcept -> void {
    try {
      std::lock_guard guard(m_mutex);

      if (m_completed) {
        return;
      }

      m_completed = true;

      if (!paths) {
        m_reply = DialogReply{
            DialogStatus::Failed, {}, error && *error ? error : "Native path dialog failed"};
      } else if (!paths[0]) {
        m_reply = DialogReply{DialogStatus::Cancelled, {}, {}};
      } else {
        std::size_t length{};

        while (length <= 16384 && paths[0][length]) {
          ++length;
        }

        if (length == 0 || length > 16384 || paths[1]
            || !Core::isValidUtf8(std::string_view(paths[0], length))) {
          m_reply = DialogReply{DialogStatus::Failed, {}, "Native dialog returned an invalid path"};
        } else {
          m_reply = DialogReply{DialogStatus::Selected, std::string(paths[0], length), {}};
        }
      }
    } catch (...) {
      m_failed.store(true);
    }
  }

  auto DialogRequest::take() -> std::optional<DialogReply> {
    std::lock_guard guard(m_mutex);

    if (m_failed.exchange(false)) {
      return DialogReply{DialogStatus::Failed, {}, "Cannot copy native dialog result"};
    }

    return std::exchange(m_reply, {});
  }
}  // namespace Dot::Platform
