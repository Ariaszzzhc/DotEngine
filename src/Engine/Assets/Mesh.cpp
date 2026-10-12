module;

#include <tiny_obj_loader.h>

#include <array>
#include <charconv>
#include <cmath>
#include <expected>
#include <format>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_set>
#include <vector>

module Dot.Assets.Mesh;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Graphics.Geometry;
import Dot.Math;

namespace Dot::Assets {
  namespace {
    using Json = nlohmann::json;
    constexpr std::size_t maxFileBytes = 64 * 1024 * 1024;

    template <std::size_t N>
    auto components(const Json& array) -> Core::Result<std::array<float, N>> {
      if (!array.is_array() || array.size() != N) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid mesh vector dimensions");
      }

      std::array<float, N> values{};

      for (std::size_t index = 0; index < N; ++index) {
        if (!array[index].is_number()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Mesh vector requires numbers");
        }

        const auto value = array[index].get<double>();

        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Mesh vector exceeds float range");
        }

        values[index] = static_cast<float>(value);
      }

      return values;
    }

    auto decodeJson(std::string_view source) -> Core::Result<MeshAsset> {
      std::vector<std::unordered_set<std::string>> keys;
      auto root = Json::parse(
          source.begin(), source.end(), [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 64) {
              throw std::invalid_argument("Mesh JSON nesting exceeds 64");
            }

            if (event == Json::parse_event_t::object_start) {
              keys.emplace_back();
            } else if (event == Json::parse_event_t::object_end) {
              keys.pop_back();
            } else if (event == Json::parse_event_t::key
                       && !keys.back().insert(value.get<std::string>()).second) {
              throw std::invalid_argument("Duplicate mesh JSON field");
            }

            return true;
          });

      if (!root.is_object() || root.size() != 3 || !root.contains("version")
          || !root.contains("vertices") || !root.contains("indices")
          || !root["version"].is_number_integer() || root["version"] != 1) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid DotMesh version or fields");
      }

      const auto& vertices = root["vertices"];
      const auto& indices = root["indices"];

      if (!vertices.is_array() || vertices.empty() || vertices.size() > 1'000'000
          || !indices.is_array() || indices.size() > 3'000'000) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "DotMesh arrays exceed mesh limits");
      }

      MeshAsset asset;
      asset.data.vertices.reserve(vertices.size());
      asset.data.indices.reserve(indices.size());

      for (const auto& vertex : vertices) {
        if (!vertex.is_object() || vertex.size() != 2 || !vertex.contains("position")
            || !vertex.contains("color")) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid DotMesh vertex fields");
        }

        auto position = components<3>(vertex["position"]);
        auto color = components<4>(vertex["color"]);

        if (!position) {
          return std::unexpected(position.error());
        }

        if (!color) {
          return std::unexpected(color.error());
        }

        asset.data.vertices.push_back({{(*position)[0], (*position)[1], (*position)[2]},
                                       {(*color)[0], (*color)[1], (*color)[2], (*color)[3]}});
      }

      for (const auto& index : indices) {
        if (!index.is_number_integer()
            || (!index.is_number_unsigned() && index.get<std::int64_t>() < 0)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "DotMesh indices require unsigned integers");
        }

        const auto value = index.get<std::uint64_t>();

        if (value > std::numeric_limits<std::uint32_t>::max()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "DotMesh index exceeds uint32 range");
        }

        asset.data.indices.push_back(static_cast<std::uint32_t>(value));
      }

      auto valid = Graphics::validateMesh(asset.data);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      return asset;
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

          for (std::size_t index = 1; index < fields.size(); ++index) {
            auto value = objNumber(fields[index]);

            if (!value) {
              return std::unexpected(value.error());
            }

            if (command == "v" && count == 4 && index == 4 && *value != 1) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                "Weighted OBJ positions are unsupported");
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

      if (!attributes.normals.empty() || !attributes.texcoords.empty()) {
        asset.warnings
            += "OBJ normals and texture coordinates are not retained by colored meshes.\n";
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

          asset.data.indices.push_back(static_cast<std::uint32_t>(index.vertex_index));
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

  auto decodeMesh(std::string_view source, MeshFormat format) -> Core::Result<MeshAsset> {
    if (source.empty() || source.size() > maxFileBytes
        || source.find('\0') != std::string_view::npos) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Mesh source must be nonempty, at most 64 MiB, and contain no NUL bytes");
    }

    try {
      switch (format) {
        case MeshFormat::DotMesh:
          return decodeJson(source);
        case MeshFormat::Obj:
          return decodeObj(source);
        default:
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown mesh format");
      }
    } catch (const Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        std::format("Parse DotMesh: {}", error.what()));
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument, error.what());
    }
  }

  auto encodeMesh(const Graphics::MeshData& mesh) -> Core::Result<std::string> {
    auto checked = Graphics::validateMesh(mesh);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    Json vertices = Json::array();

    for (const auto& vertex : mesh.vertices) {
      vertices.push_back(
          {{"position", {vertex.position.x, vertex.position.y, vertex.position.z}},
           {"color", {vertex.color.x, vertex.color.y, vertex.color.z, vertex.color.w}}});
    }

    Json root{{"version", 1}, {"vertices", std::move(vertices)}, {"indices", mesh.indices}};
    auto encoded = root.dump(2) + '\n';

    if (encoded.size() > maxFileBytes) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Encoded DotMesh exceeds 64 MiB");
    }

    return encoded;
  }

  auto loadMesh(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<MeshAsset> {
    auto format = fileFormat(relative);

    if (!format) {
      return std::unexpected(format.error());
    }

    auto source = files.readText(relative, maxFileBytes);

    if (!source) {
      return std::unexpected(source.error());
    }

    return decodeMesh(*source, *format);
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

    return files.writeTextAtomic(relative, *source);
  }
}  // namespace Dot::Assets
