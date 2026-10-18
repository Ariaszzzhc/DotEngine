module;

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

module Dot.Navigation.Files;

import Dot.Core.Paths;
import Dot.Math;

namespace Dot::Navigation {
  namespace {
    constexpr std::size_t maxBytes = 64 * 1024 * 1024;
    constexpr std::array<std::byte, 8> magic{std::byte{'D'}, std::byte{'O'}, std::byte{'T'},
                                             std::byte{'N'}, std::byte{'A'}, std::byte{'V'},
                                             std::byte{0},   std::byte{1}};
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

    class Reader {
    public:
      explicit Reader(std::span<const std::byte> bytes) : m_bytes(bytes) {
      }

      auto integer(unsigned size) -> std::uint32_t {
        if (size > m_bytes.size() - m_offset) {
          throw std::invalid_argument("Truncated DotNav field");
        }

        std::uint32_t value{};

        for (unsigned index = 0; index < size; ++index) {
          value |= std::to_integer<std::uint32_t>(m_bytes[m_offset++]) << (index * 8);
        }

        return value;
      }

      auto scalar() -> float {
        return std::bit_cast<float>(integer(4));
      }

      auto vector() -> Math::Vec3 {
        return {scalar(), scalar(), scalar()};
      }

      auto count(std::uint32_t minimum, std::uint32_t maximum, std::size_t stride)
          -> std::uint32_t {
        const auto value = integer(4);

        if (value < minimum || value > maximum || value > (m_bytes.size() - m_offset) / stride) {
          throw std::invalid_argument("DotNav count exceeds capacity or remaining bytes");
        }

        return value;
      }

      auto complete() const -> bool {
        return m_offset == m_bytes.size();
      }

    private:
      std::span<const std::byte> m_bytes;
      std::size_t m_offset{8};
    };

    class Writer {
    public:
      std::vector<std::byte> m_bytes{magic.begin(), magic.end()};

      auto integer(std::uint32_t value, unsigned size) -> void {
        if (size > maxBytes - m_bytes.size()) {
          throw std::invalid_argument("DotNav exceeds 64 MiB");
        }

        for (unsigned index = 0; index < size; ++index) {
          m_bytes.push_back(static_cast<std::byte>((value >> (index * 8)) & 255));
        }
      }

      auto scalar(float value) -> void {
        integer(std::bit_cast<std::uint32_t>(value), 4);
      }

      auto vector(Math::Vec3 value) -> void {
        scalar(value.x);
        scalar(value.y);
        scalar(value.z);
      }
    };
  }  // namespace

  auto decodeSurface(std::span<const std::byte> bytes) -> Core::Result<Surface> {
    if (bytes.size() < magic.size() || bytes.size() > maxBytes
        || !std::equal(magic.begin(), magic.end(), bytes.begin())) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid DotNav header/length");
    }

    try {
      Reader reader(bytes);
      SurfaceDescription description;
      auto& settings = description.settings;
      settings.cellSize = reader.scalar();
      settings.cellHeight = reader.scalar();
      settings.agentHeight = reader.scalar();
      settings.agentRadius = reader.scalar();
      settings.maxClimb = reader.scalar();
      settings.maxSlopeDegrees = reader.scalar();
      settings.tileCells = reader.integer(4);
      auto valid = validateSettings(settings);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      description.origin = reader.vector();

      for (unsigned index = 0; index < 32; ++index) {
        description.source.push_back(static_cast<char>(reader.integer(1)));
      }

      const auto tiles = reader.count(1, 256, 44);
      std::size_t coarseVertices{}, polygons{}, detailVertices{}, detailTriangles{};
      description.tiles.reserve(tiles);

      for (std::uint32_t index = 0; index < tiles; ++index) {
        Tile tile;
        tile.x = reader.integer(4);
        tile.z = reader.integer(4);
        tile.minimum = reader.vector();
        tile.maximum = reader.vector();
        const auto vertices = reader.count(3, 65534, 6);
        coarseVertices += vertices;

        if (coarseVertices > 1048576) {
          return Core::fail(Core::ErrorCode::Unavailable, "DotNav coarse vertex capacity exceeded");
        }

        tile.vertices.reserve(vertices);

        for (std::uint32_t vertex = 0; vertex < vertices; ++vertex) {
          tile.vertices.push_back({static_cast<std::uint16_t>(reader.integer(2)),
                                   static_cast<std::uint16_t>(reader.integer(2)),
                                   static_cast<std::uint16_t>(reader.integer(2))});
        }

        const auto count = reader.count(1, 16384, 16);
        polygons += count;

        if (polygons > 262144) {
          return Core::fail(Core::ErrorCode::Unavailable, "DotNav polygon capacity exceeded");
        }

        tile.polygons.reserve(count);

        for (std::uint32_t polygon = 0; polygon < count; ++polygon) {
          Polygon item;
          const auto corners = reader.count(3, 6, 2);

          for (std::uint32_t corner = 0; corner < corners; ++corner) {
            item.vertices.push_back(static_cast<std::uint16_t>(reader.integer(2)));
          }

          const auto detail = reader.count(corners, 255, 12);
          detailVertices += detail;

          if (detailVertices > 1048576) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "DotNav detail vertex capacity exceeded");
          }

          item.detailVertices.reserve(detail);

          for (std::uint32_t vertex = 0; vertex < detail; ++vertex) {
            item.detailVertices.push_back(reader.vector());
          }

          const auto triangles = reader.count(1, 255, 4);
          detailTriangles += triangles;

          if (detailTriangles > 1048576) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "DotNav detail triangle capacity exceeded");
          }

          item.detailTriangles.reserve(triangles);

          for (std::uint32_t triangle = 0; triangle < triangles; ++triangle) {
            item.detailTriangles.push_back({static_cast<std::uint8_t>(reader.integer(1)),
                                            static_cast<std::uint8_t>(reader.integer(1)),
                                            static_cast<std::uint8_t>(reader.integer(1)),
                                            static_cast<std::uint8_t>(reader.integer(1))});
          }

          tile.polygons.push_back(std::move(item));
        }

        description.tiles.push_back(std::move(tile));
      }

      if (!reader.complete()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unexpected trailing DotNav bytes");
      }

      return Surface::create(std::move(description));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Decode DotNav: " + std::string(error.what()));
    }
  }

  auto encodeSurface(const Surface& surface) -> Core::Result<std::vector<std::byte>> {
    auto source = surface.description();

    if (!source) {
      return std::unexpected(source.error());
    }

    try {
      Writer writer;
      const auto& description = **source;
      const auto& settings = description.settings;
      writer.scalar(settings.cellSize);
      writer.scalar(settings.cellHeight);
      writer.scalar(settings.agentHeight);
      writer.scalar(settings.agentRadius);
      writer.scalar(settings.maxClimb);
      writer.scalar(settings.maxSlopeDegrees);
      writer.integer(settings.tileCells, 4);
      writer.vector(description.origin);

      for (const auto value : description.source) {
        writer.integer(static_cast<unsigned char>(value), 1);
      }

      writer.integer(static_cast<std::uint32_t>(description.tiles.size()), 4);

      for (const auto& tile : description.tiles) {
        writer.integer(tile.x, 4);
        writer.integer(tile.z, 4);
        writer.vector(tile.minimum);
        writer.vector(tile.maximum);
        writer.integer(static_cast<std::uint32_t>(tile.vertices.size()), 4);

        for (const auto vertex : tile.vertices) {
          for (const auto value : vertex) {
            writer.integer(value, 2);
          }
        }

        writer.integer(static_cast<std::uint32_t>(tile.polygons.size()), 4);

        for (const auto& polygon : tile.polygons) {
          writer.integer(static_cast<std::uint32_t>(polygon.vertices.size()), 4);

          for (const auto corner : polygon.vertices) {
            writer.integer(corner, 2);
          }

          writer.integer(static_cast<std::uint32_t>(polygon.detailVertices.size()), 4);

          for (const auto vertex : polygon.detailVertices) {
            writer.vector(vertex);
          }

          writer.integer(static_cast<std::uint32_t>(polygon.detailTriangles.size()), 4);

          for (const auto triangle : polygon.detailTriangles) {
            for (const auto value : triangle) {
              writer.integer(value, 1);
            }
          }
        }
      }

      return std::move(writer.m_bytes);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Encode DotNav: " + std::string(error.what()));
    }
  }

  auto loadSurface(const Core::FileSystem& files, std::string_view source)
      -> Core::Result<Surface> {
    if (source.size() > 1024 || !Core::isPortableRelativePath(source)
        || !source.starts_with("assets/") || !source.ends_with(".dotnav")) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Navigation requires a portable assets/ .dotnav path");
    }

    try {
      const auto path = std::filesystem::path{std::u8string(source.begin(), source.end())};
      auto current = files.root();
      std::size_t depth{}, entries{};

      for (const auto& part : path) {
        if (++depth > 32) {
          return Core::fail(Core::ErrorCode::Unavailable, "Navigation path exceeds 32 components");
        }

        std::error_code error;
        std::filesystem::directory_iterator listing(current, error);

        if (error) {
          return Core::fail(Core::ErrorCode::Io, "Read navigation directory: " + error.message());
        }

        bool exact{};

        for (const auto& entry : listing) {
          if (++entries > 65536) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Navigation path lookup exceeds capacity");
          }

          if (entry.path().filename() == part) {
            exact = true;
            break;
          }
        }

        if (!exact) {
          return Core::fail(
              Core::ErrorCode::NotFound,
              "Navigation path missing or differently spelled: " + std::string(source));
        }

        current /= part;
        const auto status = std::filesystem::symlink_status(current, error);
        const bool final = current == files.root() / path;

        if (error || std::filesystem::is_symlink(status)
            || (final ? !std::filesystem::is_regular_file(status)
                      : !std::filesystem::is_directory(status))) {
          return Core::fail(
              Core::ErrorCode::Io,
              "Navigation path missing, symlink or special file: " + std::string(source));
        }
      }

      auto bytes = files.readBytes(path, maxBytes);

      if (!bytes) {
        return Core::fail(bytes.error().code, "Read navigation '" + std::string(source)
                                                  + "': " + bytes.error().message);
      }

      auto result = decodeSurface(*bytes);
      return result ? std::move(result)
                    : Core::fail(result.error().code, "Navigation '" + std::string(source)
                                                          + "': " + result.error().message);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Load navigation: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Navigation
