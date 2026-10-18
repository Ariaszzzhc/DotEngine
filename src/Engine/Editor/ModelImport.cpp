module;

#include <condition_variable>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

module Dot.Editor.ModelImport;

import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Editor {
  struct ModelImport::Impl {
    std::thread::id m_owner{std::this_thread::get_id()};
    Project::Project m_project;
    std::filesystem::path m_source;
    std::string m_destination;
    std::optional<std::uint32_t> m_scene;
    ResourceLimits m_limits;
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    ModelImportState m_state{ModelImportState::Preparing};
    std::uint64_t m_revision{1};
    std::optional<ModelImportReport> m_result;
    std::optional<Core::Error> m_error;
    std::jthread m_worker;

    Impl(Project::Project project, std::filesystem::path source, std::string destination,
         std::optional<std::uint32_t> scene, ResourceLimits limits)
        : m_project(std::move(project)),
          m_source(std::move(source)),
          m_destination(std::move(destination)),
          m_scene(scene),
          m_limits(limits),
          m_worker([this](std::stop_token stop) { run(stop); }) {
    }

    ~Impl() {
      {
        std::lock_guard lock(m_mutex);
        m_worker.request_stop();
      }

      m_changed.notify_one();

      if (m_worker.joinable()) {
        m_worker.join();
      }
    }

    auto owner() const -> Core::Result<void> {
      return m_owner == std::this_thread::get_id()
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState, "Model import requires owner thread");
    }

    auto complete(ModelImportState state, std::optional<Core::Error> error = {}) -> void {
      std::lock_guard lock(m_mutex);
      m_state = state;
      m_error = std::move(error);
      ++m_revision;
    }

    auto run(std::stop_token stop) -> void {
      try {
        if (stop.stop_requested()) {
          complete(ModelImportState::Cancelled);
          return;
        }

        auto store = ResourceStore::open(m_project, m_limits);

        if (!store) {
          complete(ModelImportState::Failed, store.error());
          return;
        }

        auto prepared = store->prepareModel(m_source, m_destination, m_scene, stop);

        if (!prepared) {
          const bool cancelled = stop.stop_requested()
                                 && prepared.error().code == Core::ErrorCode::Unavailable
                                 && prepared.error().message.starts_with("Model import cancelled");
          complete(cancelled ? ModelImportState::Cancelled : ModelImportState::Failed,
                   cancelled ? std::optional<Core::Error>{} : prepared.error());
          return;
        }

        auto report = prepared->report();
        std::unique_lock lock(m_mutex);
        m_result = std::move(*report);

        if (!stop.stop_requested()) {
          m_state = ModelImportState::Ready;
          ++m_revision;
          m_changed.wait(lock, [&] {
            return stop.stop_requested() || m_state == ModelImportState::Publishing;
          });
        }

        const bool publishing = m_state == ModelImportState::Publishing;
        lock.unlock();

        if (publishing) {
          auto published = store->publishModel(*prepared);

          if (published) {
            complete(ModelImportState::Published);
            return;
          }

          auto discarded = prepared->discard();
          auto error = published.error();

          if (!discarded) {
            error.message += "; cleanup failed: " + discarded.error().message;
          }

          complete(ModelImportState::Failed, std::move(error));
        } else {
          auto discarded = prepared->discard();
          complete(discarded ? ModelImportState::Cancelled : ModelImportState::Failed,
                   discarded ? std::optional<Core::Error>{} : discarded.error());
        }
      } catch (const std::exception& error) {
        complete(ModelImportState::Failed,
                 Core::fail(Core::ErrorCode::External,
                            "Model import worker: " + std::string(error.what()))
                     .error());
      }
    }
  };

  ModelImport::ModelImport(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  ModelImport::ModelImport(ModelImport&&) noexcept = default;

  auto ModelImport::operator=(ModelImport&&) noexcept -> ModelImport& = default;

  ModelImport::~ModelImport() = default;

  auto ModelImport::begin(const Project::Project& project, const std::filesystem::path& source,
                          std::string destination, std::optional<std::uint32_t> scene,
                          const ResourceLimits& limits) -> Core::Result<ModelImport> {
    try {
      const auto bytes = source.generic_u8string();
      const std::string text(bytes.begin(), bytes.end());

      if (!source.is_absolute() || text.empty() || !Core::isValidUtf8(text)
          || text.find('\0') != std::string::npos || !Core::isPortableRelativePath(destination)
          || !destination.starts_with("assets/")
          || destination.find("/.dot-stage-") != std::string::npos || (scene && *scene > 1023)) {
        return Core::fail(
            Core::ErrorCode::InvalidArgument,
            "Model import needs an absolute source, new assets path and scene 0..1023");
      }

      auto files = project.files();

      if (!files) {
        return std::unexpected(files.error());
      }

      return ModelImport(
          std::make_unique<Impl>(project, source, std::move(destination), scene, limits));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Start model import: " + std::string(error.what()));
    }
  }

  auto ModelImport::status() const -> Core::Result<ModelImportStatus> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Model import was moved");
    }

    auto checked = m_impl->owner();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::lock_guard lock(m_impl->m_mutex);
    return ModelImportStatus{m_impl->m_state, m_impl->m_revision, m_impl->m_destination,
                             m_impl->m_result, m_impl->m_error};
  }

  auto ModelImport::publish(std::uint64_t revision) -> Core::Result<void> {
    auto observed = status();

    if (!observed) {
      return std::unexpected(observed.error());
    }

    std::lock_guard lock(m_impl->m_mutex);

    if (m_impl->m_revision != revision || m_impl->m_state != ModelImportState::Ready) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Model import is not ready at this revision");
    }

    m_impl->m_state = ModelImportState::Publishing;
    ++m_impl->m_revision;
    m_impl->m_changed.notify_one();
    return {};
  }

  auto ModelImport::cancel(std::uint64_t revision) -> Core::Result<void> {
    auto observed = status();

    if (!observed) {
      return std::unexpected(observed.error());
    }

    std::lock_guard lock(m_impl->m_mutex);

    if (m_impl->m_revision != revision
        || (m_impl->m_state != ModelImportState::Preparing
            && m_impl->m_state != ModelImportState::Ready
            && m_impl->m_state != ModelImportState::Cancelling)) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Only unpublished model work can cancel at this revision");
    }

    if (m_impl->m_state != ModelImportState::Cancelling) {
      m_impl->m_state = ModelImportState::Cancelling;
      ++m_impl->m_revision;
    }

    m_impl->m_worker.request_stop();
    m_impl->m_changed.notify_one();
    return {};
  }
}  // namespace Dot::Editor
