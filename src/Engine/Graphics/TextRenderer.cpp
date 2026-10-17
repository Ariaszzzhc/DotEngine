module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Dot.Graphics.TextRenderer;

import Dot.Core.Files;
import Dot.Core.Text;

namespace Dot::Graphics {
  struct TextRenderer::Impl {
    struct Entry {
      TextBox box;
      std::uint32_t width;
      std::uint32_t height;
      std::uint32_t viewportHeight;
      Texture texture;
      bool overflow;
    };

    Renderer* m_renderer;
    std::filesystem::path m_path;
    std::optional<TextFont> m_font;
    std::map<std::string, Entry> m_cache;
    TextRenderStats m_stats;
    std::thread::id m_thread{std::this_thread::get_id()};

    Impl(Renderer& renderer, std::filesystem::path font)
        : m_renderer(&renderer), m_path(std::move(font)) {
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Text renderer requires its owner thread");
      }

      return m_renderer->adapterInfo().transform([](const auto&) {});
    }
  };

  TextRenderer::TextRenderer(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
  }

  TextRenderer::TextRenderer(TextRenderer&&) noexcept = default;
  auto TextRenderer::operator=(TextRenderer&&) noexcept -> TextRenderer& = default;
  TextRenderer::~TextRenderer() = default;

  auto TextRenderer::create(Renderer& renderer, std::filesystem::path font)
      -> Core::Result<TextRenderer> {
    auto valid = renderer.adapterInfo();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (font.empty() || font.filename().empty()) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Text renderer requires an explicit absolute font path");
    }

    try {
      std::error_code error;
      font = std::filesystem::absolute(font, error);

      if (error) {
        return Core::fail(Core::ErrorCode::Io, "Resolve text font: " + error.message());
      }

      return TextRenderer(std::make_unique<Impl>(renderer, std::move(font)));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create text renderer: " + std::string(error.what()));
    }
  }

  auto TextRenderer::stats() const -> Core::Result<TextRenderStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Text renderer was moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      return m_impl->m_stats;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Snapshot text renderer: " + std::string(error.what()));
    }
  }

  auto TextRenderer::prepare(std::span<const TextLayer> labels, std::uint32_t width,
                             std::uint32_t height) -> Core::Result<std::vector<ScreenDraw>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Text renderer was moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (labels.size() > 32 || !width || !height || width > 8192 || height > 8192) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid text frame dimensions/count");
    }

    try {
      std::set<std::string> keys;

      for (const auto& label : labels) {
        valid = validateTextBox(label.value);

        if (!valid || label.key.empty() || label.key.size() > 64 || !Core::isValidUtf8(label.key)
            || label.key.find('\0') != std::string::npos || !keys.insert(label.key).second) {
          return valid ? Core::fail(Core::ErrorCode::InvalidArgument,
                                    "Invalid/duplicate text layer key")
                       : std::unexpected(valid.error());
        }
      }

      auto candidate = m_impl->m_cache;
      std::erase_if(candidate, [&](const auto& entry) {
        return std::ranges::none_of(labels, [&](const auto& label) {
          return label.key == entry.first && label.value.visible;
        });
      });
      std::uint64_t liveBytes{};

      for (const auto& [key, entry] : m_impl->m_cache) {
        liveBytes += static_cast<std::uint64_t>(entry.width) * entry.height * 4;
      }

      std::vector<ScreenDraw> result;
      TextRenderStats stats;
      stats.rasterizations = m_impl->m_stats.rasterizations;

      for (const auto& label : labels) {
        if (!label.value.visible) {
          continue;
        }

        const auto& box = label.value;
        const auto left
            = static_cast<std::uint32_t>(std::floor(box.position.x * static_cast<float>(width)));
        const auto top
            = static_cast<std::uint32_t>(std::floor(box.position.y * static_cast<float>(height)));
        const auto right = static_cast<std::uint32_t>(
            std::floor((box.position.x + box.size.x) * static_cast<float>(width)));
        const auto bottom = static_cast<std::uint32_t>(
            std::floor((box.position.y + box.size.y) * static_cast<float>(height)));
        const auto w = right - left;
        const auto h = bottom - top;
        const auto bytes = static_cast<std::uint64_t>(w) * h * 4;

        if (!w || !h) {
          candidate.erase(label.key);
          stats.overflow.push_back(label.key);
          continue;
        }

        if (bytes > 16 * 1024 * 1024) {
          return Core::fail(Core::ErrorCode::Unavailable, "Text box exceeds 16 MiB");
        }

        auto found = candidate.find(label.key);

        if (found == candidate.end() || found->second.box != box || found->second.width != w
            || found->second.height != h || found->second.viewportHeight != height) {
          if (m_impl->m_stats.rasterizations == std::numeric_limits<std::uint64_t>::max()) {
            return Core::fail(Core::ErrorCode::Unavailable, "Text rasterization counter exhausted");
          }

          if (liveBytes + bytes > 16 * 1024 * 1024) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Text cache replacement peak exceeds 16 MiB");
          }

          if (!m_impl->m_font) {
            auto files = Core::FileSystem::open(m_impl->m_path.parent_path());

            if (!files) {
              return std::unexpected(files.error());
            }

            auto data = files->readBytes(m_impl->m_path.filename(), 32 * 1024 * 1024);

            if (!data) {
              return std::unexpected(data.error());
            }

            auto font = TextFont::create(*data);

            if (!font) {
              return std::unexpected(font.error());
            }

            m_impl->m_font.emplace(std::move(*font));
          }

          auto raster = m_impl->m_font->rasterize(box, w, h, height);

          if (!raster) {
            return Core::fail(raster.error().code,
                              "Text layer " + label.key + ": " + raster.error().message);
          }

          ++m_impl->m_stats.rasterizations;
          stats.rasterizations = m_impl->m_stats.rasterizations;

          TextureOptions options;
          options.colorSpace = ColorSpace::Linear;
          options.filter = TextureFilter::Nearest;
          options.addressU = TextureAddress::Clamp;
          options.addressV = TextureAddress::Clamp;
          options.mipmaps = false;
          auto texture = m_impl->m_renderer->uploadTexture(raster->image, options);

          if (!texture) {
            return std::unexpected(texture.error());
          }

          found = candidate
                      .insert_or_assign(
                          label.key,
                          Impl::Entry{box, w, h, height, std::move(*texture), raster->overflow})
                      .first;
          liveBytes += bytes;
        }

        if (found->second.overflow) {
          stats.overflow.push_back(label.key);
        }

        stats.textureBytes += bytes;
        result.push_back({found->second.texture,
                          {static_cast<float>(left) / static_cast<float>(width),
                           static_cast<float>(top) / static_cast<float>(height)},
                          {static_cast<float>(w) / static_cast<float>(width),
                           static_cast<float>(h) / static_cast<float>(height)}});
      }

      m_impl->m_cache.swap(candidate);
      m_impl->m_stats = std::move(stats);
      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare text layers: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Graphics
