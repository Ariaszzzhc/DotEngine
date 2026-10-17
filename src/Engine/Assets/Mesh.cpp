module;

#include <tiny_obj_loader.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

module Dot.Assets.Mesh;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Graphics.Geometry;
import Dot.Math;

namespace Dot::Assets {
  namespace {
    constexpr std::size_t maxFileBytes = 64 * 1024 * 1024;

    constexpr std::array<std::byte, 8> magic{std::byte{'D'}, std::byte{'O'}, std::byte{'T'},
                                             std::byte{'M'}, std::byte{'E'}, std::byte{'S'},
                                             std::byte{'H'}, std::byte{0}};
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

    auto decodeBinary(std::span<const std::byte> source) -> Core::Result<MeshAsset> {
      if (source.size() < 16 || !std::equal(magic.begin(), magic.end(), source.begin())) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid DotMesh header");
      }

      std::size_t offset = 8;
      const auto word = [&]() {
        std::uint32_t value{};

        for (unsigned shift = 0; shift < 32; shift += 8) {
          value |= std::to_integer<std::uint32_t>(source[offset++]) << shift;
        }

        return value;
      };
      const auto vertices = word();
      const auto indices = word();
      const auto expected = 16ULL + 64ULL * vertices + 4ULL * indices;

      if (vertices == 0 || vertices > 1'000'000 || indices > 3'000'000
          || expected != source.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "DotMesh counts/length do not match");
      }

      MeshAsset asset;
      asset.data.vertices.resize(vertices);
      asset.data.indices.resize(indices);

      for (auto& vertex : asset.data.vertices) {
        std::array<float, 16> values;

        for (auto& value : values) {
          value = std::bit_cast<float>(word());
        }

        vertex.position = {values[0], values[1], values[2]};
        vertex.color = {values[3], values[4], values[5], values[6]};
        vertex.normal = {values[7], values[8], values[9]};
        vertex.texCoord = {values[10], values[11]};
        vertex.tangent = {values[12], values[13], values[14], values[15]};
      }

      for (auto& index : asset.data.indices) {
        index = word();
      }

      auto valid = Graphics::validateMesh(asset.data);
      return valid ? Core::Result<MeshAsset>{std::move(asset)}
                   : Core::Result<MeshAsset>{std::unexpected(valid.error())};
    }

    auto tokens(std::string_view line) -> std::vector<std::string_view> {
      line = line.substr(0, line.find('#'));
      std::vector<std::string_view> result;

      while (!line.empty()) {
        const auto start = line.find_first_not_of(" \t\r");

        if (start == std::string_view::npos) {
          break;
        }

        line.remove_prefix(start);
        const auto end = line.find_first_of(" \t\r");
        result.push_back(line.substr(0, end));

        if (end == std::string_view::npos) {
          break;
        }

        line.remove_prefix(end);
      }

      return result;
    }

    auto objNumber(std::string_view token) -> Core::Result<float> {
      if (token.starts_with('+')) {
        token.remove_prefix(1);
      }

      float value{};
      const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);

      if (token.empty() || parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()
          || !std::isfinite(value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Malformed OBJ number");
      }

      return value;
    }

    /** TinyObj permits numeric fallbacks; preflight prevents malformed authored
     * data from becoming a valid-looking zero/default value.
     * TinyObj 允许数值回退；预校验防止错误数据被替换为看似有效的零值或默认值。
     */
    auto preflightObj(std::string_view source) -> Core::Result<bool> {
      std::size_t vertexCount = 0;
      std::size_t triangleIndices = 0;
      std::size_t lineNumber = 0;
      bool metadata = false;

      while (!source.empty()) {
        ++lineNumber;
        const auto end = source.find('\n');
        auto line = source.substr(0, end);
        source = end == std::string_view::npos ? std::string_view{} : source.substr(end + 1);

        if (line.size() > 1024 * 1024) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "OBJ line exceeds 1 MiB");
        }

        const auto fields = tokens(line);

        if (fields.empty()) {
          continue;
        }

        const auto command = fields[0];

        if (command == "v" || command == "vn" || command == "vt") {
          const auto count = fields.size() - 1;

          if ((command == "v" && count != 3 && count != 4 && count != 6)
              || (command == "vn" && count != 3) || (command == "vt" && (count < 1 || count > 3))) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              std::format("Invalid OBJ vector on line {}", lineNumber));
          }

          std::array<float, 3> vector{};

          for (std::size_t index = 1; index < fields.size(); ++index) {
            auto value = objNumber(fields[index]);

            if (!value) {
              return std::unexpected(value.error());
            }

            if (index <= 3) {
              vector[index - 1] = *value;
            }

            if (command == "v" && count == 6 && index >= 4 && (*value < 0 || *value > 1)) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "OBJ vertex colors must be in [0,1]");
            }

            if (command == "vt" && count == 3 && index == 3 && *value != 0) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                "OBJ volumetric texture coordinates are unsupported");
            }

            if (command == "v" && count == 4 && index == 4 && *value != 1) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                "Weighted OBJ positions are unsupported");
            }
          }

          if (command == "vn") {
            auto normal = Math::normalized(Math::Vec3{vector[0], vector[1], vector[2]});

            if (!normal) {
              return Core::fail(normal.error().code, "OBJ normal: " + normal.error().message);
            }
          }

          if (command == "v" && ++vertexCount > 1'000'000) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "OBJ exceeds vertex limit");
          }
        } else if (command == "f") {
          if (fields.size() < 4 || fields.size() > 65) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "OBJ faces require 3..64 vertices");
          }

          triangleIndices += (fields.size() - 3) * 3;

          if (triangleIndices > 3'000'000) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "OBJ exceeds triangle index limit");
          }

          for (std::size_t index = 1; index < fields.size(); ++index) {
            auto field = fields[index];
            std::size_t part = 0;

            while (true) {
              const auto slash = field.find('/');
              auto number = field.substr(0, slash);

              if (number.empty()) {
                if (part != 1 || slash == std::string_view::npos) {
                  return Core::fail(Core::ErrorCode::InvalidArgument, "Malformed OBJ face tuple");
                }
              } else {
                if (number.starts_with('+')) {
                  number.remove_prefix(1);
                }

                int value{};
                auto parsed = std::from_chars(number.data(), number.data() + number.size(), value);

                if (number.empty() || parsed.ec != std::errc{}
                    || parsed.ptr != number.data() + number.size() || value == 0) {
                  return Core::fail(Core::ErrorCode::InvalidArgument, "Malformed OBJ face index");
                }
              }

              if (++part > 3) {
                return Core::fail(Core::ErrorCode::InvalidArgument, "Malformed OBJ face tuple");
              }

              if (slash == std::string_view::npos) {
                break;
              }

              field.remove_prefix(slash + 1);
            }
          }
        } else if (command == "s") {
          if (fields.size() != 2) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Malformed OBJ smoothing group");
          }

          if (fields[1] != "off" && fields[1] != "on") {
            int group{};
            auto parsed
                = std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), group);

            if (parsed.ec != std::errc{} || parsed.ptr != fields[1].data() + fields[1].size()
                || group < 0) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Malformed OBJ smoothing group");
            }
          }

          metadata = true;
        } else if (command == "o" || command == "g") {
          metadata = true;
        } else {
          return Core::fail(
              Core::ErrorCode::Unavailable,
              std::format("Unsupported OBJ command '{}' on line {}", command, lineNumber));
        }
      }

      return metadata;
    }

    auto decodeObj(std::string_view source) -> Core::Result<MeshAsset> {
      auto checked = preflightObj(source);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      tinyobj::ObjReader reader;

      if (!reader.ParseFromString(std::string(source), "")) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Parse OBJ: " + reader.Error());
      }

      const auto& attributes = reader.GetAttrib();
      const auto count = attributes.vertices.size() / 3;
      MeshAsset asset;
      asset.warnings = reader.Warning();

      if (*checked) {
        asset.warnings
            += "OBJ object/group names and smoothing groups are not retained by colored meshes.\n";
      }

      asset.data.vertices.reserve(count);

      for (std::size_t vertex = 0; vertex < count; ++vertex) {
        const auto offset = vertex * 3;
        asset.data.vertices.push_back(
            {{attributes.vertices[offset], attributes.vertices[offset + 1],
              attributes.vertices[offset + 2]},
             {attributes.colors[offset], attributes.colors[offset + 1],
              attributes.colors[offset + 2], 1}});
      }

      auto positions = std::move(asset.data.vertices);
      asset.data.vertices.clear();
      std::map<std::tuple<int, int, int>, std::uint32_t> corners;

      for (const auto& shape : reader.GetShapes()) {
        if (!shape.lines.indices.empty() || !shape.points.indices.empty()) {
          return Core::fail(Core::ErrorCode::Unavailable, "OBJ lines and points are unsupported");
        }

        for (auto faceSize : shape.mesh.num_face_vertices) {
          if (faceSize != 3) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "OBJ triangulation failed");
          }
        }

        for (const auto& index : shape.mesh.indices) {
          if (index.vertex_index < 0 || static_cast<std::size_t>(index.vertex_index) >= count
              || (index.normal_index >= 0
                  && static_cast<std::size_t>(index.normal_index) >= attributes.normals.size() / 3)
              || (index.texcoord_index >= 0
                  && static_cast<std::size_t>(index.texcoord_index)
                         >= attributes.texcoords.size() / 2)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "OBJ attribute index exceeds its array");
          }

          std::uint32_t mapped{};
          const auto key = std::tuple{index.vertex_index, index.normal_index, index.texcoord_index};
          const auto existing = corners.find(key);

          if (existing != corners.end()) {
            mapped = existing->second;
          } else {
            if (asset.data.vertices.size() >= 1'000'000) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                "OBJ surface seams exceed one million vertices");
            }

            auto vertex = positions[static_cast<std::size_t>(index.vertex_index)];

            if (index.normal_index >= 0) {
              const auto offset = static_cast<std::size_t>(index.normal_index) * 3;
              auto normal = Math::normalized(Math::Vec3{attributes.normals[offset],
                                                        attributes.normals[offset + 1],
                                                        attributes.normals[offset + 2]});

              if (!normal) {
                return Core::fail(normal.error().code, "OBJ normal: " + normal.error().message);
              }

              vertex.normal = *normal;
            }

            if (index.texcoord_index >= 0) {
              const auto offset = static_cast<std::size_t>(index.texcoord_index) * 2;
              vertex.texCoord = {attributes.texcoords[offset], attributes.texcoords[offset + 1]};
            }

            mapped = static_cast<std::uint32_t>(asset.data.vertices.size());
            corners.emplace(key, mapped);
            asset.data.vertices.push_back(vertex);
          }

          asset.data.indices.push_back(mapped);
        }
      }

      auto valid = Graphics::validateMesh(asset.data);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      return asset;
    }

    auto fileFormat(const std::filesystem::path& path) -> Core::Result<MeshFormat> {
      try {
        auto extension = path.extension().generic_u8string();

        for (auto& character : extension) {
          if (character >= u8'A' && character <= u8'Z') {
            character = static_cast<char8_t>(character + (u8'a' - u8'A'));
          }
        }

        if (extension == u8".dotmesh") {
          return MeshFormat::DotMesh;
        }

        if (extension == u8".obj") {
          return MeshFormat::Obj;
        }

        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Mesh extension must be .dotmesh or .obj");
      } catch (const std::filesystem::filesystem_error& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          std::format("Decode mesh path: {}", error.what()));
      }
    }
  }  // namespace

  auto decodeMesh(std::span<const std::byte> source, MeshFormat format) -> Core::Result<MeshAsset> {
    if (source.empty() || source.size() > maxFileBytes) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Mesh source must be nonempty and at most 64 MiB");
    }

    try {
      switch (format) {
        case MeshFormat::DotMesh:
          return decodeBinary(source);
        case MeshFormat::Obj: {
          const std::string_view text(reinterpret_cast<const char*>(source.data()), source.size());

          if (text.find('\0') != std::string_view::npos) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "OBJ source contains NUL bytes");
          }

          return decodeObj(text);
        }
        default:
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown mesh format");
      }
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument, error.what());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Decode mesh: " + std::string(error.what()));
    }
  }

  auto encodeMesh(const Graphics::MeshData& mesh) -> Core::Result<std::vector<std::byte>> {
    auto checked = Graphics::validateMesh(mesh);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto size = 16ULL + 64ULL * mesh.vertices.size() + 4ULL * mesh.indices.size();

    if (size > maxFileBytes) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Encoded DotMesh exceeds 64 MiB");
    }

    try {
      std::vector<std::byte> bytes(magic.begin(), magic.end());
      bytes.reserve(static_cast<std::size_t>(size));
      const auto word = [&](std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
          bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
        }
      };
      word(static_cast<std::uint32_t>(mesh.vertices.size()));
      word(static_cast<std::uint32_t>(mesh.indices.size()));

      for (const auto& vertex : mesh.vertices) {
        const std::array values{
            vertex.position.x, vertex.position.y, vertex.position.z, vertex.color.x,
            vertex.color.y,    vertex.color.z,    vertex.color.w,    vertex.normal.x,
            vertex.normal.y,   vertex.normal.z,   vertex.texCoord.x, vertex.texCoord.y,
            vertex.tangent.x,  vertex.tangent.y,  vertex.tangent.z,  vertex.tangent.w};

        for (auto value : values) {
          word(std::bit_cast<std::uint32_t>(value));
        }
      }

      for (auto index : mesh.indices) {
        word(index);
      }

      return bytes;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Encode mesh: " + std::string(error.what()));
    }
  }

  auto loadMesh(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<MeshAsset> {
    auto format = fileFormat(relative);

    if (!format) {
      return std::unexpected(format.error());
    }

    auto source = files.readBytes(relative, maxFileBytes);

    if (!source) {
      return std::unexpected(source.error());
    }

    auto result = decodeMesh(*source, *format);

    if (!result) {
      const auto utf = relative.generic_u8string();
      return Core::fail(result.error().code, "Mesh '" + std::string(utf.begin(), utf.end())
                                                 + "': " + result.error().message);
    }

    return result;
  }

  auto saveMesh(const Core::FileSystem& files, const std::filesystem::path& relative,
                const Graphics::MeshData& mesh) -> Core::Result<void> {
    auto format = fileFormat(relative);

    if (!format) {
      return std::unexpected(format.error());
    }

    if (*format != MeshFormat::DotMesh) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Mesh saving requires .dotmesh extension");
    }

    auto source = encodeMesh(mesh);

    if (!source) {
      return std::unexpected(source.error());
    }

    return files.writeAtomic(relative, *source);
  }
}  // namespace Dot::Assets
