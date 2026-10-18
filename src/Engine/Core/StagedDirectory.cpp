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

    auto renameError(const std::filesystem::path& from, const std::filesystem::path& to) noexcept
        -> int {
#if defined(_WIN32)
      return MoveFileExW(from.c_str(), to.c_str(), 0) ? 0 : static_cast<int>(GetLastError());
#elif defined(__APPLE__)
      return renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0 ? 0 : errno;
#else
      return renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0 ? 0
                                                                                            : errno;
#endif
    }

    auto renameMessage(int error) -> std::string {
#if defined(_WIN32)
      return "Win32 error " + std::to_string(error);
#else
      return std::error_code(error, std::generic_category()).message();
#endif
    }

    auto renameExclusive(const std::filesystem::path& from, const std::filesystem::path& to)
        -> Result<void> {
      const auto error = renameError(from, to);
      return error == 0 ? Result<void>{}
                        : fail(ErrorCode::Io, "Publish directory: " + renameMessage(error));
    }

    auto replacementName(const std::filesystem::path& name) -> bool {
      const auto utf = name.generic_u8string();
      const std::string text(utf.begin(), utf.end());
      return !name.empty() && !name.has_root_path() && name.filename() == name && name != "."
             && name != ".." && text.find('\0') == std::string::npos
             && text.find('\\') == std::string::npos && isValidUtf8(text);
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

  auto StagedDirectory::replace(const std::filesystem::path& name) -> Result<StagedDirectory> {
    return replace(m_parent, name);
  }

  auto StagedDirectory::replace(const FileSystem& destinationParent,
                                const std::filesystem::path& name) -> Result<StagedDirectory> {
    if (m_staging.empty() || destinationParent.root().empty()) {
      return fail(ErrorCode::InvalidState, "Replacement staging was moved or published");
    }

    try {
      if (!replacementName(name)) {
        return fail(ErrorCode::InvalidArgument,
                    "Replacement name must be one UTF-8 relative component");
      }

      const auto destination = destinationParent.root() / name;
      const auto sourceStatus = std::filesystem::symlink_status(m_staging);
      const auto targetStatus = std::filesystem::symlink_status(destination);

      if (destination == m_staging || std::filesystem::is_symlink(sourceStatus)
          || !std::filesystem::is_directory(sourceStatus)
          || std::filesystem::is_symlink(targetStatus)
          || !std::filesystem::is_directory(targetStatus)) {
        return fail(ErrorCode::InvalidArgument,
                    "Replacement requires two distinct regular directories");
      }

      auto previous = create(m_parent);

      if (!previous) {
        return std::unexpected(previous.error());
      }

      const auto bytes = previous->m_staging.generic_u8string();
      const std::string recovery(bytes.begin(), bytes.end());
      std::error_code error;
      const bool removed = std::filesystem::remove(previous->m_staging, error);

      if (error || !removed) {
        return fail(ErrorCode::Io, "Prepare replacement backup: " + error.message());
      }

      const auto reserved = renameError(destination, previous->m_staging);

      if (reserved != 0) {
        previous->m_staging.clear();
        return fail(ErrorCode::Io, "Reserve previous directory: " + renameMessage(reserved));
      }

      /** No throwing operation may destroy ownership of the original between renames.
       * 两次改名之间不执行可能抛出并导致原目录被析构删除的操作。 */
      const auto published = renameError(m_staging, destination);

      if (published != 0) {
        const auto restored = renameError(previous->m_staging, destination);
        previous->m_staging.clear();

        if (restored == 0) {
          return fail(ErrorCode::Io, "Replacement failed; original directory restored: "
                                         + renameMessage(published));
        }

        return fail(ErrorCode::Io,
                    "Replacement and rollback failed; original directory retained at '" + recovery
                        + "': " + renameMessage(restored));
      }

      m_staging.clear();
      return std::move(*previous);
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Replace staged directory: " + std::string(error.what()));
    }
  }

}  // namespace Dot::Core
