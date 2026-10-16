module;

#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE
#endif

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <stdio.h>
#endif

module Dot.Core.StagedDirectory;

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.Text;

namespace Dot::Core {
  namespace {
    std::atomic<std::uint64_t> sequence{};

    auto renameExclusive(const std::filesystem::path& from, const std::filesystem::path& to)
        -> Result<void> {
#if defined(_WIN32)
      if (!MoveFileExW(from.c_str(), to.c_str(), 0)) {
        return fail(ErrorCode::Io,
                    "Publish directory: Win32 error " + std::to_string(GetLastError()));
      }
#elif defined(__APPLE__)
      if (renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) != 0) {
        return fail(ErrorCode::Io, "Publish directory: "
                                       + std::error_code(errno, std::generic_category()).message());
      }
#else
      if (renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) != 0) {
        return fail(ErrorCode::Io, "Publish directory: "
                                       + std::error_code(errno, std::generic_category()).message());
      }
#endif
      return {};
    }
  }  // namespace

  StagedDirectory::StagedDirectory(FileSystem parent, std::filesystem::path staging) noexcept
      : m_parent(std::move(parent)), m_staging(std::move(staging)) {
  }

  StagedDirectory::StagedDirectory(StagedDirectory&& other) noexcept
      : m_parent(std::move(other.m_parent)), m_staging(std::exchange(other.m_staging, {})) {
  }

  auto StagedDirectory::operator=(StagedDirectory&& other) noexcept -> StagedDirectory& {
    if (this != &other) {
      clear();
      m_parent = std::move(other.m_parent);
      m_staging = std::exchange(other.m_staging, {});
    }

    return *this;
  }

  StagedDirectory::~StagedDirectory() {
    clear();
  }

  auto StagedDirectory::clear() noexcept -> void {
    if (!m_staging.empty()) {
      std::error_code ignored;
      std::filesystem::remove_all(m_staging, ignored);
      m_staging.clear();
    }
  }

  auto StagedDirectory::discard() -> Result<void> {
    if (m_staging.empty()) {
      return {};
    }

    try {
      std::error_code error;
      std::filesystem::remove_all(m_staging, error);

      if (error) {
        const auto bytes = m_staging.generic_u8string();
        return fail(ErrorCode::Io, "Remove temporary directory '"
                                       + std::string(bytes.begin(), bytes.end())
                                       + "': " + error.message());
      }

      m_staging.clear();
      return {};
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Discard temporary directory: " + std::string(error.what()));
    }
  }

  auto StagedDirectory::create(const FileSystem& parent) -> Result<StagedDirectory> {
    if (parent.root().empty()) {
      return fail(ErrorCode::InvalidArgument, "Staging parent was moved");
    }

    try {
      auto ownedParent = parent;
      std::random_device random;

      for (int attempt = 0; attempt < 64; ++attempt) {
        auto path = parent.root()
                    / (".dot-stage-" + std::to_string(random()) + "-"
                       + std::to_string(sequence.fetch_add(1)));
        std::error_code error;
        const bool created = std::filesystem::create_directory(path, error);

        if (error) {
          return fail(ErrorCode::Io, "Create staging directory: " + error.message());
        }

        if (created) {
          return StagedDirectory(std::move(ownedParent), std::move(path));
        }
      }

      return fail(ErrorCode::Unavailable, "Staging directory name attempts exhausted");
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Create staging directory: " + std::string(error.what()));
    }
  }

  auto StagedDirectory::files() const -> Result<FileSystem> {
    if (m_staging.empty()) {
      return fail(ErrorCode::InvalidState, "Staging directory was moved or published");
    }

    return FileSystem::open(m_staging);
  }

  auto StagedDirectory::publish(const std::filesystem::path& name) -> Result<FileSystem> {
    if (m_staging.empty()) {
      return fail(ErrorCode::InvalidState, "Staging directory was moved or published");
    }

    try {
      const auto utf = name.generic_u8string();
      const std::string text(utf.begin(), utf.end());

      if (name.empty() || name.has_root_path() || name.filename() != name || name == "."
          || name == ".." || text.find('\0') != std::string::npos
          || text.find('\\') != std::string::npos || !isValidUtf8(text)) {
        return fail(ErrorCode::InvalidArgument,
                    "Published directory name must be one UTF-8 relative component");
      }

      const auto destination = m_parent.root() / name;
      auto result = renameExclusive(m_staging, destination);

      if (!result) {
        return std::unexpected(result.error());
      }

      m_staging.clear();
      auto opened = FileSystem::open(destination);

      if (!opened) {
        auto error = opened.error();
        error.message = "Directory was published but opening failed: " + error.message;
        return std::unexpected(std::move(error));
      }

      return opened;
    } catch (const std::exception& error) {
      return fail(ErrorCode::External,
                  (m_staging.empty() ? "Directory was published; post-publication failure: "
                                     : "Publish directory: ")
                      + std::string(error.what()));
    }
  }
}  // namespace Dot::Core
