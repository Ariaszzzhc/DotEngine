module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module Dot.Physics.Mesh;

namespace Dot::Physics {
  namespace {
    auto triangleValid(Math::Vec3 a, Math::Vec3 b, Math::Vec3 c) -> bool {
      const std::array<double, 3> ab{static_cast<double>(b.x) - a.x, static_cast<double>(b.y) - a.y,
                                     static_cast<double>(b.z) - a.z};
      const std::array<double, 3> ac{static_cast<double>(c.x) - a.x, static_cast<double>(c.y) - a.y,
                                     static_cast<double>(c.z) - a.z};
      const auto x = ab[1] * ac[2] - ab[2] * ac[1];
      const auto y = ab[2] * ac[0] - ab[0] * ac[2];
      const auto z = ab[0] * ac[1] - ab[1] * ac[0];
      const auto ab2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
      const auto ac2 = ac[0] * ac[0] + ac[1] * ac[1] + ac[2] * ac[2];
      return x * x + y * y + z * z > 1e-12 * ab2 * ac2;
    }
  }  // namespace

  struct TriangleMesh::Data {
    MeshDescription description;
  };

  TriangleMesh::TriangleMesh(std::shared_ptr<const Data> data) noexcept : m_data(std::move(data)) {
  }

  TriangleMesh::TriangleMesh(const TriangleMesh&) = default;

  auto TriangleMesh::operator=(const TriangleMesh&) -> TriangleMesh& = default;

  TriangleMesh::TriangleMesh(TriangleMesh&&) noexcept = default;

  auto TriangleMesh::operator=(TriangleMesh&&) noexcept -> TriangleMesh& = default;

  TriangleMesh::~TriangleMesh() = default;

  auto TriangleMesh::create(MeshDescription description) -> Core::Result<TriangleMesh> {
    if (description.vertices.size() < 3 || description.vertices.size() > 262144
        || description.indices.empty() || description.indices.size() % 3
        || description.indices.size() / 3 > 262144) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid collision mesh counts");
    }

    for (const auto vertex : description.vertices) {
      if (!Math::isFinite(vertex) || std::abs(vertex.x) > 10000 || std::abs(vertex.y) > 10000
          || std::abs(vertex.z) > 10000) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid collision mesh vertex");
      }
    }

    for (std::size_t i = 0; i < description.indices.size(); i += 3) {
      const auto a = description.indices[i], b = description.indices[i + 1],
                 c = description.indices[i + 2];

      if (a >= description.vertices.size() || b >= description.vertices.size()
          || c >= description.vertices.size()
          || !triangleValid(description.vertices[a], description.vertices[b],
                            description.vertices[c])) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid/degenerate collision triangle " + std::to_string(i / 3));
      }
    }

    try {
      return TriangleMesh(std::make_shared<const Data>(Data{std::move(description)}));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create collision mesh: " + std::string(error.what()));
    }
  }

  auto TriangleMesh::view() const -> Core::Result<MeshView> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Collision mesh is empty or moved");
    }

    return MeshView{m_data->description.vertices, m_data->description.indices};
  }

  auto TriangleMesh::validateScale(Math::Vec3 scale) const -> Core::Result<void> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Collision mesh is empty or moved");
    }

    if (!Math::isFinite(scale) || std::abs(scale.x) < .0001F || std::abs(scale.y) < .0001F
        || std::abs(scale.z) < .0001F || std::abs(scale.x) > 10000 || std::abs(scale.y) > 10000
        || std::abs(scale.z) > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid collision mesh scale");
    }

    const auto point = [&](std::uint32_t index) {
      const auto vertex = m_data->description.vertices[index];
      return Math::Vec3{vertex.x * scale.x, vertex.y * scale.y, vertex.z * scale.z};
    };

    for (const auto& vertex : m_data->description.vertices) {
      if (std::abs(vertex.x * scale.x) > 1000000 || std::abs(vertex.y * scale.y) > 1000000
          || std::abs(vertex.z * scale.z) > 1000000) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Scaled collision mesh exceeds position limits");
      }
    }

    const auto& indices = m_data->description.indices;

    for (std::size_t i = 0; i < indices.size(); i += 3) {
      if (!triangleValid(point(indices[i]), point(indices[i + 1]), point(indices[i + 2]))) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Scaled collision triangle collapses: " + std::to_string(i / 3));
      }
    }

    return {};
  }

  auto TriangleMesh::bytes() const noexcept -> std::size_t {
    return m_data ? m_data->description.vertices.size() * 3 * sizeof(float)
                        + m_data->description.indices.size() * sizeof(std::uint32_t)
                  : 0;
  }

  auto TriangleMesh::triangles() const noexcept -> std::size_t {
    return m_data ? m_data->description.indices.size() / 3 : 0;
  }

  auto TriangleMesh::operator==(const TriangleMesh& other) const -> bool {
    return m_data == other.m_data
           || (m_data && other.m_data
               && m_data->description.vertices == other.m_data->description.vertices
               && m_data->description.indices == other.m_data->description.indices);
  }
}  // namespace Dot::Physics
