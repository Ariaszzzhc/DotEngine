module;

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

export module Dot.Platform.Dialog;

import Dot.Core.Result;
import Dot.Platform.Window;

export namespace Dot::Platform {
  enum class DialogKind { Folder, PngSave };

  enum class DialogStatus { Selected, Cancelled, Failed };

  struct DialogReply {
    DialogStatus status;
    std::string path;
    std::string error;
  };

  /** Callback-owned one-shot result, without a borrow of the caller or GUI.
   * Native completion may run on another thread or after the caller is retired.
   * Strings are copied before completion returns; take consumes the reply once.
   * 回调拥有的一次性结果，不借用调用方或 GUI。原生回调可来自其他线程或晚于调用方销毁；
   * 返回前复制字符串，take 仅消费一次结果。 */
  class DialogRequest {
  public:
    DialogRequest();
    DialogRequest(const DialogRequest&) = delete;
    auto operator=(const DialogRequest&) -> DialogRequest& = delete;
    /** Start SDL's native single-path folder/save dialog on the main thread.
     * The parent window must remain alive until completion. Success means the
     * request was submitted; cancellation and native failures arrive in take().
     * 在主线程发起 SDL 原生单路径目录/保存对话框；父窗口须存活到回调完成。
     * 成功仅表示已提交，请通过 take() 观察取消或原生失败。 */
    [[nodiscard]] static auto open(const Window& parent, DialogKind kind,
                                   std::string_view initial = {})
        -> Core::Result<std::shared_ptr<DialogRequest>>;
    auto complete(const char* const* paths, const char* error) noexcept -> void;
    [[nodiscard]] auto take() -> std::optional<DialogReply>;

  private:
    std::mutex m_mutex;
    std::optional<DialogReply> m_reply;
    std::atomic<bool> m_failed{};
    bool m_completed{};
    std::string m_initial;
  };
}  // namespace Dot::Platform
