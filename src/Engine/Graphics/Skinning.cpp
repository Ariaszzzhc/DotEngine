module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <span>
#include <string>
#include <utility>

module Dot.Graphics.Skinning;

namespace Dot::Graphics {
  auto deformMesh(const MeshData& mesh, const Animation::Skin& skin,
                  std::span<const Math::Mat4> palette) -> Core::Result<MeshData> {
    auto valid = validateMesh(mesh);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    valid = Animation::validateSkinPalette(skin, palette);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    const auto* description = skin.description().value();

    if (description->vertices.size() != mesh.vertices.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Skin and mesh vertex counts differ");
    }

    try {
      auto output = mesh;

      for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex) {
        std::array<double, 16> mixed{};
        const auto& influence = description->vertices[vertex];

        for (std::size_t slot = 0; slot < 8; ++slot) {
          for (std::size_t element = 0; element < 16; ++element) {
            mixed[element] += static_cast<double>(influence.weights[slot])
                              * palette[influence.joints[slot]].data[element];
          }
        }

        double maximum{};

        for (const auto index : {0U, 1U, 2U, 4U, 5U, 6U, 8U, 9U, 10U}) {
          maximum = std::max(maximum, std::abs(mixed[index]));
        }

        const auto determinant = (mixed[0] * (mixed[5] * mixed[10] - mixed[9] * mixed[6])
                                  - mixed[4] * (mixed[1] * mixed[10] - mixed[9] * mixed[2])
                                  + mixed[8] * (mixed[1] * mixed[6] - mixed[5] * mixed[2]));

        if (maximum == 0 || std::abs(determinant / maximum / maximum / maximum) <= 1e-6) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Singular skin blend at vertex " + std::to_string(vertex));
        }

        Math::Mat4 matrix;

        for (std::size_t i = 0; i < 16; ++i) {
          matrix.data[i] = static_cast<float>(mixed[i]);
        }

        matrix.data[3] = matrix.data[7] = matrix.data[11] = 0;
        matrix.data[15] = 1;
        const auto& source = mesh.vertices[vertex];
        auto position = Math::transformPoint(matrix, source.position);

        if (!position) {
          return std::unexpected(position.error());
        }

        auto& result = output.vertices[vertex];
        result.position = *position;

        if (Math::length(source.normal) != 0) {
          auto inverse = Math::inverse(matrix);

          if (!inverse) {
            return std::unexpected(inverse.error());
          }

          Math::Mat4 transpose;

          for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t row = 0; row < 3; ++row) {
              transpose.data[column * 4 + row] = inverse->data[row * 4 + column];
            }
          }

          auto normal = Math::transformVector(transpose, source.normal);
          auto unit = normal ? Math::normalized(*normal) : std::unexpected(normal.error());

          if (!unit) {
            return std::unexpected(unit.error());
          }

          result.normal = *unit;
        }

        if (source.tangent.w != 0) {
          auto tangent = Math::transformVector(
              matrix, {source.tangent.x, source.tangent.y, source.tangent.z});

          if (!tangent) {
            return std::unexpected(tangent.error());
          }

          auto unit = Math::normalized(
              *tangent - result.normal * static_cast<float>(Math::dot(*tangent, result.normal)));

          if (!unit) {
            return std::unexpected(unit.error());
          }

          result.tangent
              = {unit->x, unit->y, unit->z, source.tangent.w * (determinant < 0 ? -1.F : 1.F)};
        }
      }

      valid = validateMesh(output);
      return valid ? Core::Result<MeshData>{std::move(output)} : std::unexpected(valid.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Deform mesh: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Graphics
