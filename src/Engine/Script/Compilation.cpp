module;

#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

module Dot.Script.Compilation;

import Dot.Core.Result;
import Dot.Script.Compiler;

namespace Dot::Script {
  struct CompilationJob::Impl {
    struct Completion {
      std::mutex m_mutex;
      std::optional<Core::Result<CompiledProgram>> m_result;
    };

    SourceSnapshot m_source;
    std::thread::id m_owner{std::this_thread::get_id()};
    std::shared_ptr<Completion> m_completion{std::make_shared<Completion>()};
    bool m_taken{};
    std::jthread m_worker;

    Impl(SourceSnapshot source, TypeScriptCompiler compiler)
        : m_source(std::move(source)),
          m_worker([state = m_completion, snapshot = m_source,
                    compiler = std::move(compiler)](std::stop_token stop) {
            auto result = [&]() -> Core::Result<CompiledProgram> {
              try {
                return compiler.compileSnapshot(snapshot, stop);
              } catch (const std::exception& error) {
                return Core::fail(Core::ErrorCode::External,
                                  "Background TypeScript compile: " + std::string(error.what()));
              }
            }();
            std::lock_guard lock(state->m_mutex);
            state->m_result.emplace(std::move(result));
          }) {
    }

    auto check() const -> Core::Result<void> {
      if (m_owner != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Compilation job requires its creating thread");
      }

      return {};
    }
  };

  CompilationJob::CompilationJob(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  CompilationJob::CompilationJob(CompilationJob&&) noexcept = default;

  auto CompilationJob::operator=(CompilationJob&&) noexcept -> CompilationJob& = default;

  CompilationJob::~CompilationJob() = default;

  auto CompilationJob::begin(const TypeScriptCompiler& compiler, const SourceSnapshot& source)
      -> Core::Result<CompilationJob> {
    if (!source.equals(source)) {
      return Core::fail(Core::ErrorCode::InvalidState, "Compilation source has been moved");
    }

    try {
      return CompilationJob{std::make_unique<Impl>(source, compiler)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Start background compilation: " + std::string(error.what()));
    }
  }

  auto CompilationJob::ready() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Compilation job has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::unique_lock lock(m_impl->m_completion->m_mutex, std::try_to_lock);
    return lock.owns_lock() && m_impl->m_completion->m_result.has_value();
  }

  auto CompilationJob::source() const -> Core::Result<SourceSnapshot> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Compilation job has been moved");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<SourceSnapshot>{m_impl->m_source}
                   : std::unexpected(checked.error());
  }

  auto CompilationJob::take() -> Core::Result<CompiledProgram> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Compilation job has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_taken) {
      return Core::fail(Core::ErrorCode::InvalidState, "Compilation result was already taken");
    }

    std::unique_lock lock(m_impl->m_completion->m_mutex, std::try_to_lock);

    if (!lock.owns_lock() || !m_impl->m_completion->m_result) {
      return Core::fail(Core::ErrorCode::Unavailable, "Compilation is still pending");
    }

    m_impl->m_taken = true;
    return std::move(*m_impl->m_completion->m_result);
  }

  auto CompilationJob::requestStop() noexcept -> void {
    if (m_impl) {
      m_impl->m_worker.request_stop();
    }
  }
}  // namespace Dot::Script
