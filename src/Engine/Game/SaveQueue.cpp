module;

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

module Dot.Game.SaveQueue;

namespace Dot::Game {
  namespace {
    std::atomic<std::uint64_t> nextId{1};

    auto terminal(SaveJobState state) -> bool {
      return state != SaveJobState::Pending && state != SaveJobState::Running;
    }

    auto external(const std::exception& error) -> std::unexpected<Core::Error> {
      return Core::fail(Core::ErrorCode::External, "Save queue: " + std::string(error.what()));
    }

    auto execute(Saves& saves, const SaveRequest& request) -> Core::Result<SaveOutput> {
      switch (request.operation) {
        case SaveOperation::List:
          return saves.list().transform([](auto value) { return SaveOutput(std::move(value)); });
        case SaveOperation::Read:
          return saves.read(request.slot).transform([](auto value) {
            return SaveOutput(std::move(value));
          });
        case SaveOperation::Write:
          return saves.write(request.slot, *request.snapshot, request.overwrite)
              .transform([](auto value) { return SaveOutput(std::move(value)); });
        case SaveOperation::Remove:
          return saves.remove(request.slot).transform([] { return SaveOutput{}; });
      }

      return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown save operation");
    }
  }  // namespace

  struct SaveQueue::Impl {
    struct Entry {
      SaveRequest request;
      SaveJob reply;
      std::size_t reservation;
    };

    Core::FileSystem m_files;
    SaveQueueLimits m_limits;
    std::thread::id m_owner{std::this_thread::get_id()};
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::map<std::uint64_t, Entry> m_jobs;
    std::deque<std::uint64_t> m_pending;
    std::size_t m_reserved{};
    bool m_accepting{true};
    std::jthread m_worker;

    Impl(Core::FileSystem files, SaveQueueLimits limits)
        : m_files(std::move(files)), m_limits(limits), m_worker([this] { run(); }) {
    }

    ~Impl() {
      drain();
    }

    auto owner() const -> Core::Result<void> {
      return m_owner == std::this_thread::get_id()
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState, "Save queue requires owner thread");
    }

    auto drain() -> void {
      {
        std::lock_guard lock(m_mutex);
        m_accepting = false;
      }

      m_changed.notify_one();

      if (m_worker.joinable()) {
        m_worker.join();
      }
    }

    auto run() -> void {
      std::optional<Saves> store;
      for (;;) {
        std::unique_lock lock(m_mutex);
        m_changed.wait(lock, [this] { return !m_pending.empty() || !m_accepting; });

        if (m_pending.empty()) {
          return;
        }

        const auto id = m_pending.front();
        m_pending.pop_front();
        auto& entry = m_jobs.at(id);

        if (entry.reply.state == SaveJobState::Cancelled) {
          continue;
        }

        entry.reply.state = SaveJobState::Running;
        lock.unlock();
        auto result = [&]() -> Core::Result<SaveOutput> {
          try {
            if (!store) {
              auto opened = Saves::open(m_files, m_limits.saves);

              if (!opened) {
                return std::unexpected(opened.error());
              }

              store.emplace(std::move(*opened));
            }

            return execute(*store, entry.request);
          } catch (const std::exception& error) {
            return external(error);
          }
        }();
        lock.lock();
        entry.request.snapshot.reset();

        if (result) {
          entry.reply.output = std::move(*result);
          entry.reply.state = SaveJobState::Succeeded;
        } else {
          entry.reply.error = std::move(result.error());
          entry.reply.state = SaveJobState::Failed;
        }
      }
    }
  };

  SaveQueue::SaveQueue(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  SaveQueue::SaveQueue(SaveQueue&&) noexcept = default;

  auto SaveQueue::operator=(SaveQueue&&) noexcept -> SaveQueue& = default;

  SaveQueue::~SaveQueue() = default;

  auto validateSaveQueueLimits(const SaveQueueLimits& limits) -> Core::Result<void> {
    auto valid = normalizeStateData("{}", limits.saves.state);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (limits.saves.slots == 0 || limits.saves.slots > 1024 || limits.jobs == 0
        || limits.jobs > 256 || limits.bytes < 1024 || limits.bytes > 64 * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid save queue root or capacities");
    }
    return {};
  }

  auto SaveQueue::create(const Core::FileSystem& files, const SaveQueueLimits& limits)
      -> Core::Result<SaveQueue> {
    auto valid = validateSaveQueueLimits(limits);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (files.root().empty()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Save queue root was moved");
    }

    try {
      return SaveQueue(std::make_unique<Impl>(files, limits));
    } catch (const std::exception& error) {
      return external(error);
    }
  }

  auto SaveQueue::submit(SaveRequest request) -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Save queue was moved");
    }

    auto checked = m_impl->owner();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      if (request.operation < SaveOperation::List || request.operation > SaveOperation::Remove
          || (request.operation == SaveOperation::List ? !request.slot.empty()
                                                       : !validateSaveSlot(request.slot))
          || (request.operation != SaveOperation::Write && (request.snapshot || request.overwrite))
          || (request.operation == SaveOperation::Write
              && (!request.snapshot || request.snapshot->revision == 0
                  || request.snapshot->revision == std::numeric_limits<std::uint64_t>::max()))) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid save queue request");
      }

      std::size_t reservation = 1024;

      if (request.operation == SaveOperation::Write) {
        auto data = normalizeStateData(request.snapshot->data, m_impl->m_limits.saves.state);

        if (!data) {
          return std::unexpected(data.error());
        }

        request.snapshot->data = std::move(*data);
        reservation += request.snapshot->data.size();
      } else if (request.operation == SaveOperation::Read) {
        reservation += m_impl->m_limits.saves.state.bytes;
      } else if (request.operation == SaveOperation::List) {
        reservation += 4096 * 64;
      }

      std::lock_guard lock(m_impl->m_mutex);

      if (!m_impl->m_accepting) {
        return Core::fail(Core::ErrorCode::InvalidState, "Save queue is closed");
      }

      if (m_impl->m_jobs.size() >= m_impl->m_limits.jobs
          || reservation > m_impl->m_limits.bytes - m_impl->m_reserved) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Save queue capacity exhausted; take completed receipts");
      }

      auto id = nextId.load();

      do {
        if (id == std::numeric_limits<std::uint64_t>::max()) {
          return Core::fail(Core::ErrorCode::Unavailable, "Save job identities exhausted");
        }
      } while (!nextId.compare_exchange_weak(id, id + 1));

      SaveJob reply{id, request.operation, SaveJobState::Pending, {}, {}};
      auto [entry, added] = m_impl->m_jobs.emplace(
          id, Impl::Entry{std::move(request), std::move(reply), reservation});
      static_cast<void>(added);

      try {
        m_impl->m_pending.push_back(id);
      } catch (...) {
        m_impl->m_jobs.erase(entry);
        throw;
      }

      m_impl->m_reserved += reservation;
      m_impl->m_changed.notify_one();
      return id;
    } catch (const std::exception& error) {
      return external(error);
    }
  }

  auto SaveQueue::status(std::uint64_t id) const -> Core::Result<SaveJob> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Save queue was moved");
    }

    auto checked = m_impl->owner();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::lock_guard lock(m_impl->m_mutex);
    const auto found = m_impl->m_jobs.find(id);

    if (found == m_impl->m_jobs.end()) {
      return Core::fail(Core::ErrorCode::NotFound, "Save job is unknown or already consumed");
    }

    const auto& job = found->second.reply;
    return SaveJob{job.id, job.operation, job.state, job.error, {}};
  }

  auto SaveQueue::jobs() const -> Core::Result<std::vector<SaveJob>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Save queue was moved");
    }

    auto checked = m_impl->owner();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::lock_guard lock(m_impl->m_mutex);
    std::vector<SaveJob> result;
    result.reserve(m_impl->m_jobs.size());

    for (const auto& [id, entry] : m_impl->m_jobs) {
      const auto& job = entry.reply;
      result.push_back({id, job.operation, job.state, job.error, {}});
    }

    return result;
  }

  auto SaveQueue::take(std::uint64_t id) -> Core::Result<SaveJob> {
    auto observed = status(id);

    if (!observed || !terminal(observed->state)) {
      return observed;
    }

    std::lock_guard lock(m_impl->m_mutex);
    auto found = m_impl->m_jobs.find(id);
    auto result = std::move(found->second.reply);
    m_impl->m_reserved -= found->second.reservation;
    std::erase(m_impl->m_pending, id);
    m_impl->m_jobs.erase(found);
    return result;
  }

  auto SaveQueue::cancel(std::uint64_t id) -> Core::Result<void> {
    auto observed = status(id);

    if (!observed) {
      return std::unexpected(observed.error());
    }

    std::lock_guard lock(m_impl->m_mutex);
    auto& entry = m_impl->m_jobs.at(id);

    if (entry.reply.state != SaveJobState::Pending) {
      return Core::fail(Core::ErrorCode::InvalidState, "Only queued save jobs may be cancelled");
    }

    entry.reply.state = SaveJobState::Cancelled;
    entry.request.snapshot.reset();
    return {};
  }

  auto SaveQueue::finish() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Save queue was moved");
    }

    m_impl->drain();
    std::lock_guard lock(m_impl->m_mutex);

    for (const auto& [id, entry] : m_impl->m_jobs) {
      static_cast<void>(id);

      if (entry.reply.state == SaveJobState::Failed
          && (entry.request.operation == SaveOperation::Write
              || entry.request.operation == SaveOperation::Remove)) {
        return std::unexpected(*entry.reply.error);
      }
    }

    return {};
  }
}  // namespace Dot::Game
