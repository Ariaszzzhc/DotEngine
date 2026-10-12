module;

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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
#  include <sys/stat.h>
#  include <unistd.h>
#endif

module Dot.Core.Files;

import Dot.Core.Result;

namespace Dot::Core::Detail {
  namespace Fs = std::filesystem;

  struct PendingFile {
    Fs::path path;
#if defined(_WIN32)
    HANDLE handle{INVALID_HANDLE_VALUE};
#else
    int handle{-1};
#endif

    PendingFile() = default;
    PendingFile(const PendingFile&) = delete;
    auto operator=(const PendingFile&) -> PendingFile& = delete;

    ~PendingFile() {
#if defined(_WIN32)
      if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
      }
#else
      if (handle >= 0) {
        close(handle);
      }
#endif
      if (!path.empty()) {
        std::error_code ignored;
        Fs::remove(path, ignored);
      }
    }
  };

  auto systemFailure(std::string_view operation) -> std::unexpected<Error> {
#if defined(_WIN32)
    return fail(ErrorCode::Io, std::format("{}: Win32 error {}", operation, GetLastError()));
#else
    const auto error = std::error_code(errno, std::generic_category());
    return fail(ErrorCode::Io, std::format("{}: {}", operation, error.message()));
#endif
  }

  auto inside(const Fs::path& root, const Fs::path& candidate) -> bool {
    auto part = candidate.begin();
    for (const auto& rootPart : root) {
      if (part == candidate.end()) {
        return false;
      }
#if defined(_WIN32)
      if (CompareStringOrdinal(rootPart.c_str(), -1, part->c_str(), -1, TRUE) != CSTR_EQUAL) {
        return false;
      }
#else
      if (rootPart != *part) {
        return false;
      }
#endif
      ++part;
    }
    return true;
  }
}  // namespace Dot::Core::Detail

namespace Dot::Core {
  auto FileSystem::open(const std::filesystem::path& root) -> Result<FileSystem> {
    if (root.empty()) {
      return fail(ErrorCode::InvalidArgument, "File root must not be empty");
    }
    std::error_code error;
    auto canonical = std::filesystem::canonical(root, error);
    if (error) {
      return fail(ErrorCode::Io, std::format("Cannot resolve file root: {}", error.message()));
    }
    if (!std::filesystem::is_directory(canonical, error) || error) {
      return fail(ErrorCode::InvalidArgument, "File root must be a directory");
    }
    return FileSystem(std::move(canonical));
  }

  auto FileSystem::root() const noexcept -> const std::filesystem::path& {
    return m_root;
  }

  auto FileSystem::resolve(const std::filesystem::path& relative) const
      -> Result<std::filesystem::path> {
    if (relative.empty() || relative.has_root_path()) {
      return fail(ErrorCode::InvalidArgument, "File path must be non-empty and relative");
    }
    std::error_code error;
    auto candidate = std::filesystem::weakly_canonical(m_root / relative, error);
    if (error) {
      return fail(ErrorCode::Io, std::format("Cannot resolve file path: {}", error.message()));
    }
    if (!Detail::inside(m_root, candidate)) {
      return fail(ErrorCode::InvalidArgument, "File path escapes the project root");
    }
    return candidate;
  }

  auto FileSystem::createDirectories(const std::filesystem::path& relative) const -> Result<void> {
    auto path = resolve(relative);
    if (!path) {
      return std::unexpected(path.error());
    }
    std::error_code error;
    std::filesystem::create_directories(*path, error);
    if (error) {
      return fail(ErrorCode::Io, std::format("Cannot create directories: {}", error.message()));
    }
    return {};
  }

  auto FileSystem::readBytes(const std::filesystem::path& relative, std::size_t maxBytes) const
      -> Result<std::vector<std::byte>> {
    auto path = resolve(relative);
    if (!path) {
      return std::unexpected(path.error());
    }
    std::error_code error;
    if (!std::filesystem::exists(*path, error)) {
      return fail(error ? ErrorCode::Io : ErrorCode::NotFound,
                  "File does not exist or is inaccessible");
    }
    if (!std::filesystem::is_regular_file(*path, error) || error) {
      return fail(ErrorCode::InvalidArgument, "Only regular files can be read");
    }
#if defined(_WIN32)
    Detail::PendingFile input;
    input.handle = CreateFileW(path->c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input.handle == INVALID_HANDLE_VALUE) {
      return Detail::systemFailure("Cannot open file for reading");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(input.handle, &size)) {
      return Detail::systemFailure("Cannot query file length");
    }
    if (size.QuadPart < 0 || static_cast<std::uintmax_t>(size.QuadPart) > maxBytes) {
      return fail(ErrorCode::InvalidArgument, "File exceeds the read limit or has invalid length");
    }
    std::vector<std::byte> data(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < data.size()) {
      const auto length = static_cast<DWORD>(std::min(
          data.size() - offset, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
      DWORD received = 0;
      if (!ReadFile(input.handle, data.data() + offset, length, &received, nullptr)
          || received == 0) {
        return Detail::systemFailure("Cannot read file");
      }
      offset += received;
    }
    LARGE_INTEGER finalSize{};
    if (!GetFileSizeEx(input.handle, &finalSize) || finalSize.QuadPart != size.QuadPart) {
      return fail(ErrorCode::Io, "File changed or failed during reading");
    }
#else
    std::ifstream stream(*path, std::ios::binary | std::ios::ate);
    if (!stream) {
      return fail(ErrorCode::Io, "Cannot open file for reading");
    }
    const auto size = stream.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > maxBytes
        || static_cast<std::uintmax_t>(size)
               > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
      return fail(ErrorCode::InvalidArgument, "File exceeds the read limit or has invalid length");
    }
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!stream) {
      return fail(ErrorCode::Io, "Cannot seek to the start of the file");
    }
    if (!data.empty()) {
      stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
      if (stream.gcount() != static_cast<std::streamsize>(data.size()) || !stream) {
        return fail(ErrorCode::Io, "File read failed or the file was truncated during reading");
      }
    }
    if (stream.peek() != std::char_traits<char>::eof() || stream.bad()) {
      return fail(ErrorCode::Io, "File changed or failed during reading");
    }
#endif
    return data;
  }

  auto FileSystem::readText(const std::filesystem::path& relative, std::size_t maxBytes) const
      -> Result<std::string> {
    return readBytes(relative, maxBytes).transform([](const auto& data) {
      return data.empty() ? std::string{}
                          : std::string(reinterpret_cast<const char*>(data.data()), data.size());
    });
  }

  auto FileSystem::writeAtomic(const std::filesystem::path& relative,
                               std::span<const std::byte> data) const -> Result<void> {
    auto target = resolve(relative);
    if (!target) {
      return std::unexpected(target.error());
    }
    Detail::PendingFile pending;
#if defined(_WIN32)
    static std::atomic<std::uint64_t> sequence{0};
    for (int attempt = 0; attempt < 64; ++attempt) {
      pending.path = *target;
      pending.path += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                      + std::to_wstring(sequence.fetch_add(1));
      pending.handle = CreateFileW(pending.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
      if (pending.handle != INVALID_HANDLE_VALUE) {
        break;
      }
      const auto error = GetLastError();
      pending.path.clear();
      if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
        SetLastError(error);
        return Detail::systemFailure("Cannot create temporary file");
      }
    }
    if (pending.handle == INVALID_HANDLE_VALUE) {
      return fail(ErrorCode::Io, "Cannot reserve a unique temporary filename");
    }
    std::size_t offset = 0;
    while (offset < data.size()) {
      const auto length = static_cast<DWORD>(std::min(
          data.size() - offset, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
      DWORD written = 0;
      if (!WriteFile(pending.handle, data.data() + offset, length, &written, nullptr)
          || written == 0) {
        return Detail::systemFailure("Cannot write temporary file");
      }
      offset += written;
    }
    if (!FlushFileBuffers(pending.handle)) {
      return Detail::systemFailure("Cannot flush temporary file");
    }
    const auto handle = std::exchange(pending.handle, INVALID_HANDLE_VALUE);
    if (!CloseHandle(handle)) {
      return Detail::systemFailure("Cannot close temporary file");
    }
    if (!MoveFileExW(pending.path.c_str(), target->c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      return Detail::systemFailure("Cannot replace destination file");
    }
#else
    auto pattern = (target->parent_path() / ".dot-save-XXXXXX").string();
    pending.handle = mkstemp(pattern.data());
    if (pending.handle < 0) {
      return Detail::systemFailure("Cannot create temporary file");
    }
    pending.path = pattern;
    struct stat previous{};
    if (stat(target->c_str(), &previous) == 0) {
      if (fchmod(pending.handle, previous.st_mode & 0777) != 0) {
        return Detail::systemFailure("Cannot preserve destination permissions");
      }
    } else if (errno != ENOENT) {
      return Detail::systemFailure("Cannot inspect destination permissions");
    }
    std::size_t offset = 0;
    while (offset < data.size()) {
      const auto length = std::min(data.size() - offset, static_cast<std::size_t>(SSIZE_MAX));
      const auto written = write(pending.handle, data.data() + offset, length);
      if (written < 0 && errno == EINTR) {
        continue;
      }
      if (written <= 0) {
        return Detail::systemFailure("Cannot write temporary file");
      }
      offset += static_cast<std::size_t>(written);
    }
    if (fsync(pending.handle) != 0) {
      return Detail::systemFailure("Cannot flush temporary file");
    }
    const auto handle = std::exchange(pending.handle, -1);
    if (close(handle) != 0) {
      return Detail::systemFailure("Cannot close temporary file");
    }
    if (rename(pending.path.c_str(), target->c_str()) != 0) {
      return Detail::systemFailure("Cannot replace destination file");
    }
#endif
    pending.path.clear();
    return {};
  }

  auto FileSystem::writeTextAtomic(const std::filesystem::path& relative,
                                   std::string_view text) const -> Result<void> {
    return writeAtomic(relative, std::as_bytes(std::span(text.data(), text.size())));
  }

  FileSystem::FileSystem(std::filesystem::path root) : m_root(std::move(root)) {
  }

}  // namespace Dot::Core
