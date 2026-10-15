module;

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <source_location>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Dot.Editor.PreviewWorker;

import Dot.Core.Text;
import Dot.Assets.Image;
import Dot.Graphics.Image;

namespace Dot::Editor {
  struct PreviewWorker::Impl {
    struct Queued {
      std::uint64_t ticket;
      PreviewRequest request;
    };

    std::thread::id owner{std::this_thread::get_id()};
    PreviewFactory factory;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<Queued>> pending;
    std::vector<PreviewCompletion> completed;
    std::map<std::string, std::uint64_t> latest;
    PreviewWorkerStats state;
    bool resetProvider{};
    std::jthread thread;

    explicit Impl(PreviewFactory value) : factory(std::move(value)) {
      completed.reserve(8);
    }

    ~Impl() {
      shutdown();
    }

    auto check() const -> Core::Result<void> {
      return owner == std::this_thread::get_id()
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "Preview worker requires owner thread");
    }

    auto shutdown() -> void {
      {
        std::lock_guard lock(mutex);
        state.stopped = true;
        state.discarded += pending.size() + completed.size();
        pending.clear();
        completed.clear();
        latest.clear();
      }

      thread.request_stop();
      wake.notify_all();

      if (thread.joinable()) {
        thread.join();
      }

      std::lock_guard lock(mutex);
      state.ready = false;
    }

    auto run(std::stop_token stop) -> void {
      std::optional<PreviewExecutor> executor;
      std::optional<Core::Error> initialization;

      while (true) {
        std::shared_ptr<Queued> job;
        bool reset{};

        {
          std::unique_lock lock(mutex);
          wake.wait(lock, [&] {
            return state.stopped || (!state.paused && !pending.empty() && completed.size() < 8);
          });

          if (state.stopped) {
            break;
          }

          reset = std::exchange(resetProvider, false);

          if (reset) {
            state.ready = false;
            state.initializationError.clear();
          }

          job = std::move(pending.front());
          pending.pop_front();
          state.active = true;
          ++state.started;
        }

        Core::Result<AssetPreviewImage> result
            = Core::fail(Core::ErrorCode::External, "Preview provider did not run");

        try {
          if (reset) {
            executor.reset();
            initialization.reset();
          }

          if (!executor && !initialization) {
            auto created = factory();

            if (created && *created) {
              executor = std::move(*created);
            } else {
              initialization
                  = created ? Core::Error{Core::ErrorCode::InvalidState, "Empty preview executor",
                                          std::source_location::current()}
                            : created.error();
            }
          }

          result = initialization
                       ? Core::Result<AssetPreviewImage>{std::unexpected(*initialization)}
                       : (*executor)(job->request, stop);

          if (result) {
            auto valid = Graphics::validateImage(result->image);

            if (!valid || result->image.width != job->request.size
                || result->image.height != job->request.size) {
              result = Core::fail(Core::ErrorCode::InvalidArgument,
                                  "Preview provider returned invalid output dimensions/storage");
            }
          }
        } catch (const std::exception& error) {
          result = Core::fail(Core::ErrorCode::External,
                              "Preview provider: " + std::string(error.what()));
        } catch (...) {
          result = Core::fail(Core::ErrorCode::External,
                              "Preview provider threw an unknown exception");
        }

        if (!executor && !initialization && !result) {
          initialization = result.error();
        }

        {
          std::lock_guard lock(mutex);
          state.ready = executor.has_value();
          state.initializationError = initialization ? initialization->message : std::string{};
          state.active = false;
          ++state.finished;
          const auto current = latest.find(job->request.slot);

          if (!state.stopped && current != latest.end() && current->second == job->ticket) {
            completed.push_back({job->ticket, std::move(job->request.slot), std::move(result)});
          } else {
            ++state.discarded;
          }
        }
      }
    }
  };

  PreviewWorker::PreviewWorker(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  PreviewWorker::PreviewWorker(PreviewWorker&&) noexcept = default;

  auto PreviewWorker::operator=(PreviewWorker&&) noexcept -> PreviewWorker& = default;

  PreviewWorker::~PreviewWorker() = default;

  auto PreviewWorker::start(PreviewFactory factory) -> Core::Result<PreviewWorker> {
    if (!factory) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Missing preview provider factory");
    }

    try {
      auto impl = std::make_unique<Impl>(std::move(factory));
      auto* stable = impl.get();
      impl->thread = std::jthread([stable](std::stop_token stop) { stable->run(stop); });
      return PreviewWorker(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Start preview worker: " + std::string(error.what()));
    }
  }

  auto PreviewWorker::submit(PreviewRequest request) -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Preview worker was moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto kind = assetPreviewKind(request.path);

    if (!kind || request.slot.empty() || request.slot.size() > 1024
        || !Core::isValidUtf8(request.slot) || request.slot.find('\0') != std::string::npos
        || request.size < 16 || request.size > 256
        || (request.material && *kind != AssetPreviewKind::Material)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid preview request");
    }

    if (request.material) {
      checked = Assets::validateMaterial(*request.material);

      if (!checked) {
        return std::unexpected(checked.error());
      }
    }

    try {
      std::lock_guard lock(m_impl->mutex);

      if (m_impl->state.stopped
          || m_impl->state.submitted == std::numeric_limits<std::uint64_t>::max()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Preview worker is stopped or tickets exhausted");
      }

      auto pending = m_impl->pending;
      const auto removed = std::erase_if(
          pending, [&](const auto& job) { return job->request.slot == request.slot; });

      if (pending.size() == 64) {
        return Core::fail(Core::ErrorCode::Unavailable, "Preview request queue is full");
      }

      const auto ticket = m_impl->state.submitted + 1;
      auto latest = m_impl->latest;
      latest.insert_or_assign(request.slot, ticket);
      const auto priority = request.priority;
      auto job = std::make_shared<Impl::Queued>(Impl::Queued{ticket, std::move(request)});

      if (priority) {
        pending.push_front(job);
      } else {
        pending.push_back(job);
      }

      m_impl->state.discarded += removed + std::erase_if(m_impl->completed, [&](const auto& item) {
                                   return item.slot == job->request.slot;
                                 });
      m_impl->pending.swap(pending);
      m_impl->latest.swap(latest);
      m_impl->state.submitted = ticket;
      m_impl->wake.notify_one();
      return ticket;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Submit preview: " + std::string(error.what()));
    }
  }

  auto PreviewWorker::takeCompleted() -> Core::Result<std::vector<PreviewCompletion>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Preview worker was moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::lock_guard lock(m_impl->mutex);
    std::vector<PreviewCompletion> result;
    result.reserve(8);
    result.swap(m_impl->completed);

    for (const auto& item : result) {
      m_impl->latest.erase(item.slot);
    }

    m_impl->wake.notify_one();
    return result;
  }

  auto PreviewWorker::cancel(std::string_view slot) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Preview worker was moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    std::lock_guard lock(m_impl->mutex);
    m_impl->state.discarded += std::erase_if(
        m_impl->pending, [&](const auto& job) { return job->request.slot == slot; });
    m_impl->state.discarded
        += std::erase_if(m_impl->completed, [&](const auto& job) { return job.slot == slot; });
    m_impl->latest.erase(std::string(slot));
    m_impl->wake.notify_one();
    return {};
  }

  auto PreviewWorker::invalidate(bool resetProvider) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Preview worker was moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    std::lock_guard lock(m_impl->mutex);
    m_impl->state.discarded += m_impl->pending.size() + m_impl->completed.size();
    m_impl->pending.clear();
    m_impl->completed.clear();
    m_impl->latest.clear();
    m_impl->resetProvider = m_impl->resetProvider || resetProvider;
    m_impl->wake.notify_one();
    return {};
  }

  auto PreviewWorker::setPaused(bool paused) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Preview worker was moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    std::lock_guard lock(m_impl->mutex);

    if (m_impl->state.stopped) {
      return Core::fail(Core::ErrorCode::InvalidState, "Preview worker is stopped");
    }

    m_impl->state.paused = paused;
    m_impl->wake.notify_one();
    return {};
  }

  auto PreviewWorker::stats() const -> Core::Result<PreviewWorkerStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Preview worker was moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::lock_guard lock(m_impl->mutex);
    auto result = m_impl->state;
    result.pending = m_impl->pending.size();
    result.completed = m_impl->completed.size();
    return result;
  }

  auto PreviewWorker::stop() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Preview worker was moved from");
    }

    auto checked = m_impl->check();

    if (checked) {
      m_impl->shutdown();
    }

    return checked;
  }

  auto createAssetPreviewWorker(Core::FileSystem files, std::filesystem::path studio)
      -> Core::Result<PreviewWorker> {
    return PreviewWorker::start(
        [files = std::move(files), studio = std::move(studio)]() -> Core::Result<PreviewExecutor> {
          auto root = Core::FileSystem::open(studio.parent_path());

          if (!root) {
            return std::unexpected(root.error());
          }

          auto hdr = Assets::loadHdrImage(*root, studio.filename());

          if (!hdr) {
            return std::unexpected(hdr.error());
          }

          auto preview = AssetPreview::create(files, &*hdr);

          if (!preview) {
            return std::unexpected(preview.error());
          }

          auto owner = std::make_shared<AssetPreview>(std::move(*preview));
          return PreviewExecutor{
              [owner = std::move(owner)](const PreviewRequest& request,
                                         std::stop_token) -> Core::Result<AssetPreviewImage> {
                return owner->render(request.path, request.size,
                                     request.material ? &*request.material : nullptr);
              }};
        });
  }
}  // namespace Dot::Editor
