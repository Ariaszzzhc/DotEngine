module;

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

module Dot.Assets.ImageExport;

import Dot.Assets.Image;
import Dot.Core.Files;
import Dot.Core.Paths;
import Dot.Core.Result;
import Dot.Graphics.Image;

namespace Dot::Assets {
  namespace {
    namespace Fs = std::filesystem;
    using Baseline = std::optional<std::vector<std::byte>>;

    auto folded(std::string value) -> std::string {
      for (auto& letter : value) {
        if (letter >= 'A' && letter <= 'Z') {
          letter = static_cast<char>(letter - 'A' + 'a');
        }
      }

      return value;
    }

    auto utf(const Fs::path& path) -> std::string {
      const auto value = path.generic_u8string();
      return {value.begin(), value.end()};
    }

    auto baseline(const Core::FileSystem& files, const Fs::path& name) -> Core::Result<Baseline> {
      const auto spelling = utf(name);
      const auto key = folded(spelling);
      std::size_t scanned{};

      for (const auto& entry : Fs::directory_iterator(files.root())) {
        if (++scanned > 65536) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "PNG parent directory exceeds entry limit");
        }

        const auto actual = utf(entry.path().filename());

        if (folded(actual) == key && actual != spelling) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "PNG destination requires exact filename spelling");
        }
      }

      std::error_code error;
      const auto status = Fs::symlink_status(files.root() / name, error);

      if (status.type() == Fs::file_type::not_found) {
        return Baseline{};
      }

      if (error || !Fs::is_regular_file(status) || Fs::is_symlink(status)) {
        return Core::fail(Core::ErrorCode::Io,
                          "PNG destination is inaccessible or not a regular file");
      }

      constexpr auto writable
          = Fs::perms::owner_write | Fs::perms::group_write | Fs::perms::others_write;

      if ((status.permissions() & writable) == Fs::perms::none) {
        return Core::fail(Core::ErrorCode::Io, "PNG destination is read-only");
      }

      auto bytes = files.readBytes(name, 64 * 1024 * 1024);
      return bytes ? Core::Result<Baseline>{Baseline{std::move(*bytes)}}
                   : std::unexpected(bytes.error());
    }
  }  // namespace

  struct ImageExport::Impl {
    struct Encoding {
      std::mutex mutex;
      std::optional<Core::Result<std::vector<std::byte>>> result;
    };

    Core::FileSystem files;
    Fs::path filename;
    Baseline original;
    std::string absolute;
    std::thread::id thread{std::this_thread::get_id()};
    bool written{};
    bool started{};
    bool cancelled{};
    std::shared_ptr<Encoding> encoding;
    std::jthread worker;
    std::optional<Core::Error> error;
    std::optional<ImageExportReport> report;

    Impl(Core::FileSystem parent, Fs::path name, Baseline before)
        : files(std::move(parent)),
          filename(std::move(name)),
          original(std::move(before)),
          absolute(utf(files.root() / filename)) {
    }

    auto unchanged() const -> Core::Result<void> {
      auto current = baseline(files, filename);

      if (!current) {
        return std::unexpected(current.error());
      }

      if (*current != original) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "PNG destination changed after preparation");
      }

      return {};
    }

    auto commit(std::span<const std::byte> encoded) -> Core::Result<ImageExportReport> {
      auto current = unchanged();

      if (!current) {
        return std::unexpected(current.error());
      }

      ImageExportReport result{absolute, encoded.size()};
      auto saved = files.writeAtomic(filename, encoded);

      if (!saved) {
        return std::unexpected(saved.error());
      }

      written = true;
      original.reset();
      return result;
    }
  };

  ImageExport::ImageExport(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  ImageExport::ImageExport(ImageExport&&) noexcept = default;

  auto ImageExport::operator=(ImageExport&&) noexcept -> ImageExport& = default;

  ImageExport::~ImageExport() = default;

  auto ImageExport::prepare(const Core::FileSystem& parent, std::string_view filename,
                            bool overwrite) -> Core::Result<ImageExport> {
    try {
      if (!Core::isPortableRelativePath(filename) || filename.find('/') != std::string_view::npos
          || filename.size() <= 4 || !folded(std::string(filename)).ends_with(".png")) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "PNG export requires a portable .png filename");
      }

      const auto name = Fs::path(std::u8string(filename.begin(), filename.end()));
      auto original = baseline(parent, name);

      if (!original) {
        return std::unexpected(original.error());
      }

      if (*original && !overwrite) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "PNG destination exists; explicit overwrite is required");
      }

      return ImageExport(std::make_unique<Impl>(parent, name, std::move(*original)));
    } catch (const Fs::filesystem_error& error) {
      return Core::fail(Core::ErrorCode::Io,
                        "Prepare PNG destination: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare PNG export: " + std::string(error.what()));
    }
  }

  auto ImageExport::write(const Graphics::Image& image) -> Core::Result<ImageExportReport> {
    if (!m_impl || m_impl->thread != std::this_thread::get_id() || m_impl->written
        || m_impl->started) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "PNG export requires its unused plan on the creating thread");
    }

    try {
      auto before = m_impl->unchanged();

      if (!before) {
        return std::unexpected(before.error());
      }

      auto encoded = encodeImage(image);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      return m_impl->commit(*encoded);
    } catch (const Fs::filesystem_error& error) {
      return Core::fail(Core::ErrorCode::Io, "Write PNG destination: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Write PNG export: " + std::string(error.what()));
    }
  }

  auto ImageExport::begin(Graphics::Image image) -> Core::Result<void> {
    if (!m_impl || m_impl->thread != std::this_thread::get_id() || m_impl->written
        || m_impl->started) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "PNG encoding requires its unused plan on the creating thread");
    }

    try {
      auto valid = Graphics::validateImage(image);

      if (!valid) {
        return valid;
      }

      valid = m_impl->unchanged();

      if (!valid) {
        return valid;
      }

      auto state = std::make_shared<Impl::Encoding>();
      m_impl->worker = std::jthread([state, image = std::move(image)](std::stop_token stop) {
        auto encode = [&]() -> Core::Result<std::vector<std::byte>> {
          try {
            return stop.stop_requested()
                       ? Core::fail(Core::ErrorCode::Unavailable, "PNG encoding cancelled")
                       : encodeImage(image);
          } catch (const std::exception& error) {
            return Core::fail(Core::ErrorCode::External,
                              "Encode PNG: " + std::string(error.what()));
          } catch (...) {
            return Core::fail(Core::ErrorCode::External, "Unknown PNG encoding failure");
          }
        };
        auto encoded = encode();
        std::lock_guard lock(state->mutex);
        state->result.emplace(std::move(encoded));
      });
      m_impl->encoding = std::move(state);
      m_impl->started = true;
      return {};
    } catch (const std::filesystem::filesystem_error& error) {
      return Core::fail(Core::ErrorCode::Io, "Begin PNG destination: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Begin PNG export: " + std::string(error.what()));
    }
  }

  auto ImageExport::poll() -> Core::Result<std::optional<ImageExportReport>> {
    if (!m_impl || m_impl->thread != std::this_thread::get_id() || !m_impl->started) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "PNG poll requires a started plan on the creating thread");
    }

    if (m_impl->error) {
      return std::unexpected(*m_impl->error);
    }

    if (m_impl->report) {
      return m_impl->report;
    }

    std::optional<Core::Result<std::vector<std::byte>>> encoded;

    {
      std::lock_guard lock(m_impl->encoding->mutex);
      encoded = std::move(m_impl->encoding->result);
    }

    if (!encoded) {
      return std::nullopt;
    }

    m_impl->worker = {};
    m_impl->encoding.reset();

    try {
      auto result = m_impl->cancelled
                        ? Core::fail(Core::ErrorCode::Unavailable, "PNG export cancelled")
                    : *encoded ? m_impl->commit(**encoded)
                               : Core::Result<ImageExportReport>{std::unexpected(encoded->error())};

      if (!result) {
        m_impl->error = result.error();
        return std::unexpected(*m_impl->error);
      }

      m_impl->report = std::move(*result);
      return m_impl->report;
    } catch (const std::filesystem::filesystem_error& error) {
      m_impl->error
          = Core::fail(Core::ErrorCode::Io, "Commit PNG destination: " + std::string(error.what()))
                .error();
    } catch (const std::exception& error) {
      m_impl->error
          = Core::fail(Core::ErrorCode::External, "Commit PNG: " + std::string(error.what()))
                .error();
    }

    return std::unexpected(*m_impl->error);
  }

  auto ImageExport::cancel() -> Core::Result<void> {
    if (!m_impl || m_impl->thread != std::this_thread::get_id() || !m_impl->started
        || m_impl->written || m_impl->error) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "PNG cancellation requires a pending plan on the creating thread");
    }

    m_impl->cancelled = true;
    m_impl->worker.request_stop();
    return {};
  }
}  // namespace Dot::Assets
