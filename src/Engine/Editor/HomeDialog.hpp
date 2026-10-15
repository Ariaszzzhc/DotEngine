#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace Dot::Editor::Detail {
  enum class FolderStatus { Selected, Cancelled, Failed };

  struct FolderReply {
    FolderStatus status;
    std::string path;
    std::string error;
  };

  /** Callback-owned mailbox, never a borrow of Home/GUI/window. SDL may finish
   * on another thread or after the caller is destroyed. Copy its borrowed strings
   * before returning; a single consumed reply is applied on the GUI owner thread.
   * 回调拥有的邮箱，不借用 Home/GUI/窗口；SDL 可从其他线程或在调用方销毁后完成。
   * 返回前复制借用字符串，结果只在 GUI 所有者线程消费一次。 */
  class FolderRequest {
  public:
    auto complete(const char* const* files, const char* error) noexcept -> void {
      try {
        std::lock_guard guard(m_mutex);

        if (m_completed) {
          return;
        }

        m_completed = true;
        m_reply = !files      ? FolderReply{FolderStatus::Failed,
                                            {},
                                            error && *error ? error : "Native folder dialog failed"}
                  : !files[0] ? FolderReply{FolderStatus::Cancelled, {}, {}}
                              : FolderReply{FolderStatus::Selected, files[0], {}};
      } catch (...) {
        m_failed.store(true);
      }
    }

    auto take() -> std::optional<FolderReply> {
      std::lock_guard guard(m_mutex);

      if (m_failed.exchange(false)) {
        return FolderReply{FolderStatus::Failed, {}, "Cannot copy native folder selection"};
      }

      return std::exchange(m_reply, {});
    }

  private:
    std::mutex m_mutex;
    std::optional<FolderReply> m_reply;
    std::atomic<bool> m_failed{};
    bool m_completed{};
  };
}  // namespace Dot::Editor::Detail
