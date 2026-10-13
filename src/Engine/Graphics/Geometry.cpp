module;

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <string>

module Dot.Graphics.Geometry;

import Dot.Core.Result;
import Dot.Math;

namespace Dot::Graphics {
  auto validateMesh(const MeshData& mesh) -> Core::Result<void> {
    if (mesh.vertices.empty() || mesh.vertices.size() > 1'000'000
        || mesh.indices.size() > 3'000'000) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Mesh requires 1..1000000 vertices and at most 3000000 indices");
    }

    const auto count = mesh.indices.empty() ? mesh.vertices.size() : mesh.indices.size();

    if (count % 3 != 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Mesh geometry must form triangles");
    }

    for (const auto& vertex : mesh.vertices) {
      const auto color = vertex.color;
      const auto normalLength
          = std::hypot(static_cast<double>(vertex.normal.x), static_cast<double>(vertex.normal.y),
                       static_cast<double>(vertex.normal.z));

      if (!Math::isFinite(vertex.position) || !Math::isFinite(color) || color.x < 0 || color.x > 1
          || color.y < 0 || color.y > 1 || color.z < 0 || color.z > 1 || color.w < 0 || color.w > 1
          || !Math::isFinite(vertex.normal)
          || (normalLength != 0 && std::abs(normalLength - 1) > 1e-4)
          || !std::isfinite(vertex.texCoord.x) || !std::isfinite(vertex.texCoord.y)) {
        return Core::fail(
            Core::ErrorCode::InvalidArgument,
            "Mesh requires finite positions/UVs, colors in [0,1] and unit/absent normals");
      }
    }

    for (auto index : mesh.indices) {
      if (index >= mesh.vertices.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Mesh index exceeds its vertex array");
      }
    }

    return {};
  }

  auto meshBounds(const MeshData& mesh) -> Core::Result<Bounds> {
    auto valid = validateMesh(mesh);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    Bounds bounds{mesh.vertices.front().position, mesh.vertices.front().position};

    for (const auto& vertex : mesh.vertices) {
      bounds.minimum.x = std::min(bounds.minimum.x, vertex.position.x);
      bounds.minimum.y = std::min(bounds.minimum.y, vertex.position.y);
      bounds.minimum.z = std::min(bounds.minimum.z, vertex.position.z);
      bounds.maximum.x = std::max(bounds.maximum.x, vertex.position.x);
      bounds.maximum.y = std::max(bounds.maximum.y, vertex.position.y);
      bounds.maximum.z = std::max(bounds.maximum.z, vertex.position.z);
    }

    return bounds;
  }

  auto withFlatNormals(const MeshData& mesh) -> Core::Result<MeshData> {
    auto checked = validateMesh(mesh);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto count = mesh.indices.empty() ? mesh.vertices.size() : mesh.indices.size();

    if (count > 1'000'000) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "Flat-normal expansion exceeds one million vertices");
    }

    MeshData result;
    result.vertices.reserve(count);
    const auto vertex = [&](std::size_t index) -> const Vertex& {
      return mesh.vertices[mesh.indices.empty() ? index : mesh.indices[index]];
    };

    for (std::size_t face = 0; face < count; face += 3) {
      const auto& a = vertex(face);
      const auto& b = vertex(face + 1);
      const auto& c = vertex(face + 2);
      const std::array<double, 3> first{static_cast<double>(b.position.x) - a.position.x,
                                        static_cast<double>(b.position.y) - a.position.y,
                                        static_cast<double>(b.position.z) - a.position.z};
      const std::array<double, 3> second{static_cast<double>(c.position.x) - a.position.x,
                                         static_cast<double>(c.position.y) - a.position.y,
                                         static_cast<double>(c.position.z) - a.position.z};
      const std::array cross{first[1] * second[2] - first[2] * second[1],
                             first[2] * second[0] - first[0] * second[2],
                             first[0] * second[1] - first[1] * second[0]};
      const auto length = std::hypot(cross[0], cross[1], cross[2]);

      if (!std::isfinite(length) || length == 0) {
        return Core::fail(
            Core::ErrorCode::InvalidArgument,
            "Cannot generate a normal for degenerate triangle " + std::to_string(face / 3));
      }

      const Math::Vec3 normal{static_cast<float>(cross[0] / length),
                              static_cast<float>(cross[1] / length),
                              static_cast<float>(cross[2] / length)};

      for (const auto* source : {&a, &b, &c}) {
        auto output = *source;
        output.normal = normal;
        result.vertices.push_back(output);
      }
    }

    return result;
  }

  auto cube(Math::Vec3 size) -> Core::Result<MeshData> {
    if (!Math::isFinite(size) || size.x <= 0 || size.y <= 0 || size.z <= 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Cube size must be finite and positive");
    }

    const auto half = size * 0.5F;

    if (half.x == 0 || half.y == 0 || half.z == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Cube size underflows its vertex range");
    }

    MeshData mesh;
    mesh.vertices
        = {{{-half.x, -half.y, -half.z}}, {{half.x, -half.y, -half.z}}, {{half.x, half.y, -half.z}},
           {{-half.x, half.y, -half.z}},  {{-half.x, -half.y, half.z}}, {{half.x, -half.y, half.z}},
           {{half.x, half.y, half.z}},    {{-half.x, half.y, half.z}}};
    mesh.indices = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                    3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    return mesh;
  }

  auto quad(Math::Vec2 size) -> Core::Result<MeshData> {
    if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0 || size.y <= 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Quad size must be finite and positive");
    }

    const float halfX = size.x * 0.5F;
    const float halfY = size.y * 0.5F;

    if (halfX == 0 || halfY == 0) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Quad size underflows its vertex range");
    }

    MeshData mesh;
    mesh.vertices
        = {{{-halfX, -halfY, 0}}, {{halfX, -halfY, 0}}, {{halfX, halfY, 0}}, {{-halfX, halfY, 0}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
  }
}  // namespace Dot::Graphics
