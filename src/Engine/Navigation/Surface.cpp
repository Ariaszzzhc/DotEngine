module;

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>
#include <xxhash.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

module Dot.Navigation.Surface;

namespace Dot::Navigation {
  namespace {
    constexpr std::size_t maxGeometry = 262144;
    constexpr std::size_t maxRasterWork = 16 * 1024 * 1024;
    constexpr unsigned polygonSize = 6;

    auto validPoint(Math::Vec3 point) -> bool {
      return Math::isFinite(point) && std::abs(point.x) <= 1e6F && std::abs(point.y) <= 1e6F
             && std::abs(point.z) <= 1e6F;
    }

    auto components(Math::Vec3 point) -> std::array<float, 3> {
      return {point.x, point.y, point.z};
    }

    auto area(Math::Vec3 a, Math::Vec3 b, Math::Vec3 c) -> double {
      return (double(b.x) - a.x) * (double(c.z) - a.z) - (double(b.z) - a.z) * (double(c.x) - a.x);
    }

    auto point(const Tile& tile, const BuildSettings& settings, std::uint16_t index) -> Math::Vec3 {
      const auto v = tile.vertices[index];
      return {tile.minimum.x + float(v[0]) * settings.cellSize,
              tile.minimum.y + float(v[1]) * settings.cellHeight,
              tile.minimum.z + float(v[2]) * settings.cellSize};
    }

    class BuildContext final : public rcContext {
    public:
      std::string m_error;

    protected:
      auto doLog(rcLogCategory category, const char* message, int length) -> void override {
        if (category == RC_LOG_ERROR && m_error.empty() && length > 0) {
          m_error.assign(message, static_cast<std::size_t>(std::min(length, 512)));
        }
      }
    };

    auto failed(std::string stage, const BuildContext& context) -> Core::Result<Surface> {
      return Core::fail(Core::ErrorCode::External,
                        "Build navigation " + stage
                            + (context.m_error.empty() ? " failed" : ": " + context.m_error));
    }

    auto cancelled(std::stop_token stop) -> Core::Result<void> {
      return stop.stop_requested()
                 ? Core::fail(Core::ErrorCode::Unavailable, "Navigation build cancelled")
                 : Core::Result<void>{};
    }

    auto validateDescription(const SurfaceDescription& description, std::stop_token stop)
        -> Core::Result<void> {
      auto checked = validateSettings(description.settings);

      if (!checked) {
        return checked;
      }

      if (!validPoint(description.origin) || description.source.size() != 32
          || !std::ranges::all_of(
              description.source,
              [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })
          || description.tiles.empty() || description.tiles.size() > 256) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation source/tiles");
      }

      std::set<std::pair<std::uint32_t, std::uint32_t>> coordinates;
      std::size_t vertices{}, polygons{}, detailVertices{}, detailTriangles{};
      const auto& settings = description.settings;
      const auto width = float(settings.tileCells) * settings.cellSize;

      for (const auto& tile : description.tiles) {
        auto stopping = cancelled(stop);

        if (!stopping) {
          return stopping;
        }

        if (tile.x > 255 || tile.z > 255 || !coordinates.emplace(tile.x, tile.z).second
            || !validPoint(tile.minimum) || !validPoint(tile.maximum)
            || tile.minimum.x != description.origin.x + float(tile.x) * width
            || tile.minimum.z != description.origin.z + float(tile.z) * width
            || tile.maximum.x != tile.minimum.x + width || tile.maximum.z != tile.minimum.z + width
            || tile.minimum.y >= tile.maximum.y
            || (double(tile.maximum.y) - tile.minimum.y) / settings.cellHeight > RC_SPAN_MAX_HEIGHT
            || tile.vertices.size() < 3 || tile.vertices.size() >= 65535
            || tile.vertices.size() > 1048576 - vertices || tile.polygons.empty()
            || tile.polygons.size() > 16384 || tile.polygons.size() > maxGeometry - polygons) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid navigation tile bounds/counts");
        }

        vertices += tile.vertices.size();
        polygons += tile.polygons.size();

        for (const auto vertex : tile.vertices) {
          if (vertex[0] > settings.tileCells || vertex[2] > settings.tileCells
              || vertex[1] > RC_SPAN_MAX_HEIGHT
              || tile.minimum.y + float(vertex[1]) * settings.cellHeight > tile.maximum.y) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation cell vertex");
          }
        }

        for (const auto& polygon : tile.polygons) {
          const auto size = polygon.vertices.size();

          if (size < 3 || size > polygonSize || polygon.detailVertices.size() < size
              || polygon.detailVertices.size() > 255 || polygon.detailTriangles.empty()
              || polygon.detailTriangles.size() > 255
              || polygon.detailVertices.size() > 1048576 - detailVertices
              || polygon.detailTriangles.size() > 1048576 - detailTriangles) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Invalid navigation polygon counts");
          }

          detailVertices += polygon.detailVertices.size();
          detailTriangles += polygon.detailTriangles.size();
          std::set<std::uint16_t> unique;
          double totalArea{};

          for (std::size_t i = 0; i < size; ++i) {
            const auto index = polygon.vertices[i];

            if (index >= tile.vertices.size() || !unique.insert(index).second) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid polygon vertex index");
            }
          }

          for (std::size_t i = 0; i < size; ++i) {
            const auto a = point(tile, settings, polygon.vertices[i]);
            const auto b = point(tile, settings, polygon.vertices[(i + 1) % size]);
            const auto c = point(tile, settings, polygon.vertices[(i + 2) % size]);
            totalArea += double(a.x) * b.z - double(a.z) * b.x;

            if (a.x == b.x && a.z == b.z) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Collapsed navigation polygon edge");
            }

            if (area(a, b, c) > 1e-7) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Navigation polygon is not convex clockwise");
            }
          }

          if (totalArea >= -1e-8) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Degenerate navigation polygon");
          }

          for (std::size_t i = 0; i < polygon.detailVertices.size(); ++i) {
            const auto p = polygon.detailVertices[i];

            if (!validPoint(p) || p.y < tile.minimum.y || p.y > tile.maximum.y) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid detail vertex");
            }

            for (std::size_t edge = 0; edge < size; ++edge) {
              if (area(point(tile, settings, polygon.vertices[edge]),
                       point(tile, settings, polygon.vertices[(edge + 1) % size]), p)
                  > double(settings.cellSize) * settings.cellSize * .01) {
                return Core::fail(Core::ErrorCode::InvalidArgument,
                                  "Detail vertex leaves its polygon");
              }
            }

            if (i < size) {
              const auto coarse = point(tile, settings, polygon.vertices[i]);
              const auto tolerance
                  = std::max(settings.cellSize * .01F,
                             std::abs(coarse.x) * 2e-7F + std::abs(coarse.z) * 2e-7F);

              if (std::abs(p.x - coarse.x) > tolerance || std::abs(p.z - coarse.z) > tolerance) {
                return Core::fail(Core::ErrorCode::InvalidArgument,
                                  "Detail/coarse boundary mismatch");
              }
            }
          }

          for (const auto triangle : polygon.detailTriangles) {
            if (triangle[0] >= polygon.detailVertices.size()
                || triangle[1] >= polygon.detailVertices.size()
                || triangle[2] >= polygon.detailVertices.size() || (triangle[3] & 0xc0U)
                || area(polygon.detailVertices[triangle[0]], polygon.detailVertices[triangle[1]],
                        polygon.detailVertices[triangle[2]])
                       >= -1e-10) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid detail triangle");
            }
          }
        }
      }

      return {};
    }

    auto nativeTile(const Tile& tile, const BuildSettings& settings)
        -> Core::Result<std::pair<unsigned char*, int>> {
      std::vector<unsigned short> vertices;
      vertices.reserve(tile.vertices.size() * 3);

      for (const auto vertex : tile.vertices) {
        vertices.insert(vertices.end(), vertex.begin(), vertex.end());
      }

      std::vector<unsigned short> polygons(tile.polygons.size() * polygonSize * 2,
                                           RC_MESH_NULL_IDX);
      std::vector<unsigned short> flags(tile.polygons.size(), 1);
      std::vector<unsigned char> areas(tile.polygons.size(), 0);
      std::vector<unsigned int> meshes;
      std::vector<float> details;
      std::vector<unsigned char> triangles;

      struct Edge {
        std::size_t polygon{}, slot{};
        std::uint16_t from{}, to{};
      };

      std::map<std::pair<std::uint16_t, std::uint16_t>, Edge> edges;
      std::set<std::pair<std::uint16_t, std::uint16_t>> paired;

      for (std::size_t i = 0; i < tile.polygons.size(); ++i) {
        const auto& polygon = tile.polygons[i];
        const auto offset = i * polygonSize * 2;

        for (std::size_t j = 0; j < polygon.vertices.size(); ++j) {
          const auto from = polygon.vertices[j],
                     to = polygon.vertices[(j + 1) % polygon.vertices.size()];
          polygons[offset + j] = from;
          const auto pair = std::minmax(from, to);
          const std::pair<std::uint16_t, std::uint16_t> key{pair.first, pair.second};

          if (paired.contains(key)) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Nonmanifold navigation edge");
          }

          const auto existing = edges.find(key);

          if (existing != edges.end()) {
            const auto previous = existing->second;

            if (previous.from != to || previous.to != from) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Navigation edge winding conflict");
            }

            polygons[offset + polygonSize + j] = static_cast<unsigned short>(previous.polygon);
            polygons[previous.polygon * polygonSize * 2 + polygonSize + previous.slot]
                = static_cast<unsigned short>(i);
            edges.erase(existing);
            paired.insert(key);
          } else {
            edges.emplace(key, Edge{i, j, from, to});
          }
        }

        meshes.insert(meshes.end(), {static_cast<unsigned>(details.size() / 3),
                                     static_cast<unsigned>(polygon.detailVertices.size()),
                                     static_cast<unsigned>(triangles.size() / 4),
                                     static_cast<unsigned>(polygon.detailTriangles.size())});

        for (const auto vertex : polygon.detailVertices) {
          const auto values = components(vertex);
          details.insert(details.end(), values.begin(), values.end());
        }

        for (const auto triangle : polygon.detailTriangles) {
          triangles.insert(triangles.end(), triangle.begin(), triangle.end());
        }
      }

      /** Reconstruct topology and cardinal portals from engine descriptions; no saved vendor
       * neighbor bits or native struct layouts enter the file format.
       * 从引擎描述重建拓扑及四向入口，不将第三方邻接位或原生结构布局写入格式。 */
      for (const auto& [key, edge] : edges) {
        const auto a = tile.vertices[edge.from], b = tile.vertices[edge.to];
        unsigned short direction = 15;

        if (a[0] == 0 && b[0] == 0) {
          direction = 0;
        } else if (a[2] == settings.tileCells && b[2] == settings.tileCells) {
          direction = 1;
        } else if (a[0] == settings.tileCells && b[0] == settings.tileCells) {
          direction = 2;
        } else if (a[2] == 0 && b[2] == 0) {
          direction = 3;
        }

        polygons[edge.polygon * polygonSize * 2 + polygonSize + edge.slot]
            = static_cast<unsigned short>(0x8000U | direction);
      }

      dtNavMeshCreateParams parameters{};
      parameters.verts = vertices.data();
      parameters.vertCount = static_cast<int>(vertices.size() / 3);
      parameters.polys = polygons.data();
      parameters.polyFlags = flags.data();
      parameters.polyAreas = areas.data();
      parameters.polyCount = static_cast<int>(tile.polygons.size());
      parameters.nvp = polygonSize;
      parameters.detailMeshes = meshes.data();
      parameters.detailVerts = details.data();
      parameters.detailVertsCount = static_cast<int>(details.size() / 3);
      parameters.detailTris = triangles.data();
      parameters.detailTriCount = static_cast<int>(triangles.size() / 4);
      const auto low = components(tile.minimum), high = components(tile.maximum);
      std::ranges::copy(low, parameters.bmin);
      std::ranges::copy(high, parameters.bmax);
      parameters.tileX = static_cast<int>(tile.x);
      parameters.tileY = static_cast<int>(tile.z);
      parameters.walkableHeight = settings.agentHeight;
      parameters.walkableRadius = settings.agentRadius;
      parameters.walkableClimb = settings.maxClimb;
      parameters.cs = settings.cellSize;
      parameters.ch = settings.cellHeight;
      parameters.buildBvTree = true;
      unsigned char* bytes{};
      int size{};

      if (!dtCreateNavMeshData(&parameters, &bytes, &size) || !bytes || size <= 0) {
        dtFree(bytes);
        return Core::fail(Core::ErrorCode::External, "Create Detour navigation tile failed");
      }

      return std::pair{bytes, size};
    }
  }  // namespace

  struct Surface::Data {
    SurfaceDescription description;
    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> native{nullptr, dtFreeNavMesh};
  };

  Surface::Surface(std::shared_ptr<const Data> data) noexcept : m_data(std::move(data)) {
  }

  auto validateSettings(const BuildSettings& s) -> Core::Result<void> {
    const auto range = [](float value, float low, float high) {
      return std::isfinite(value) && value >= low && value <= high;
    };

    if (!range(s.cellSize, .02F, 1) || !range(s.cellHeight, .01F, .5F)
        || !range(s.agentHeight, .2F, 10) || !range(s.agentRadius, .05F, 5)
        || !range(s.maxClimb, 0, 2) || s.maxClimb >= s.agentHeight
        || !range(s.maxSlopeDegrees, 0, 80) || s.tileCells < 32 || s.tileCells > 256
        || !std::has_single_bit(s.tileCells)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation build settings");
    }

    return {};
  }

  auto geometryIdentity(const InputGeometry& geometry) -> Core::Result<std::string> {
    if (geometry.vertices.size() < 3 || geometry.vertices.size() > maxGeometry
        || geometry.indices.empty() || geometry.indices.size() % 3
        || geometry.indices.size() / 3 > maxGeometry) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation input counts");
    }

    try {
      std::vector<unsigned char> bytes;
      bytes.reserve(16 + geometry.vertices.size() * 12 + geometry.indices.size() * 4);
      const auto integer = [&](std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
          bytes.push_back(static_cast<unsigned char>(value >> shift));
        }
      };
      integer(static_cast<std::uint32_t>(geometry.vertices.size()));
      integer(static_cast<std::uint32_t>(geometry.indices.size()));

      for (const auto v : geometry.vertices) {
        if (!validPoint(v)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation input vertex");
        }

        for (const float value : components(v)) {
          integer(std::bit_cast<std::uint32_t>(value == 0 ? 0.F : value));
        }
      }

      for (std::size_t i = 0; i < geometry.indices.size(); i += 3) {
        const auto a = geometry.indices[i], b = geometry.indices[i + 1],
                   c = geometry.indices[i + 2];

        if (a >= geometry.vertices.size() || b >= geometry.vertices.size()
            || c >= geometry.vertices.size()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation triangle index");
        }

        const auto p = geometry.vertices[a], q = geometry.vertices[b], r = geometry.vertices[c];
        const std::array<double, 3> ab{double(q.x) - p.x, double(q.y) - p.y, double(q.z) - p.z};
        const std::array<double, 3> ac{double(r.x) - p.x, double(r.y) - p.y, double(r.z) - p.z};
        const auto x = ab[1] * ac[2] - ab[2] * ac[1];
        const auto y = ab[2] * ac[0] - ab[0] * ac[2];
        const auto z = ab[0] * ac[1] - ab[1] * ac[0];
        const auto ab2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
        const auto ac2 = ac[0] * ac[0] + ac[1] * ac[1] + ac[2] * ac[2];

        if (x * x + y * y + z * z <= 1e-12 * ab2 * ac2) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Degenerate navigation input triangle");
        }

        integer(a);
        integer(b);
        integer(c);
      }

      const auto hash = XXH3_128bits(bytes.data(), bytes.size());
      constexpr auto digits = "0123456789abcdef";
      std::string result;
      result.reserve(32);

      for (const auto half : {hash.high64, hash.low64}) {
        for (int shift = 60; shift >= 0; shift -= 4) {
          result.push_back(digits[(half >> shift) & 15U]);
        }
      }

      return result;
    } catch (const std::exception& e) {
      return Core::fail(Core::ErrorCode::External,
                        "Identify navigation input: " + std::string(e.what()));
    }
  }

  auto Surface::create(SurfaceDescription description, std::stop_token stop)
      -> Core::Result<Surface> {
    try {
      auto checked = validateDescription(description, stop);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      auto data = std::make_shared<Data>();
      data->description = std::move(description);
      data->native.reset(dtAllocNavMesh());

      if (!data->native) {
        return Core::fail(Core::ErrorCode::External, "Allocate Detour navigation mesh failed");
      }

      dtNavMeshParams parameters{};
      const auto origin = components(data->description.origin);
      std::ranges::copy(origin, parameters.orig);
      parameters.tileWidth
          = float(data->description.settings.tileCells) * data->description.settings.cellSize;
      parameters.tileHeight = parameters.tileWidth;
      parameters.maxTiles = static_cast<int>(std::bit_ceil(data->description.tiles.size()));
      std::size_t largest{};

      for (const auto& tile : data->description.tiles) {
        largest = std::max(largest, tile.polygons.size());
      }

      parameters.maxPolys = static_cast<int>(std::bit_ceil(largest));

      if (dtStatusFailed(data->native->init(&parameters))) {
        return Core::fail(Core::ErrorCode::External, "Initialize Detour navigation mesh failed");
      }

      for (const auto& tile : data->description.tiles) {
        checked = cancelled(stop);

        if (!checked) {
          return std::unexpected(checked.error());
        }

        auto bytes = nativeTile(tile, data->description.settings);

        if (!bytes) {
          return std::unexpected(bytes.error());
        }

        std::unique_ptr<unsigned char, decltype(&dtFree)> owned(bytes->first, dtFree);
        const auto status
            = data->native->addTile(owned.get(), bytes->second, DT_TILE_FREE_DATA, 0, nullptr);

        if (dtStatusFailed(status)) {
          return Core::fail(Core::ErrorCode::External, "Publish Detour navigation tile failed");
        }

        owned.release();
      }

      return Surface(std::move(data));
    } catch (const std::exception& e) {
      return Core::fail(Core::ErrorCode::External,
                        "Create navigation surface: " + std::string(e.what()));
    }
  }

  auto Surface::description() const -> Core::Result<const SurfaceDescription*> {
    return m_data
               ? Core::Result<const SurfaceDescription*>{&m_data->description}
               : Core::fail(Core::ErrorCode::InvalidState, "Navigation surface is empty or moved");
  }

  auto Surface::stats() const -> Core::Result<SurfaceStats> {
    auto value = description();

    if (!value) {
      return std::unexpected(value.error());
    }

    SurfaceStats result;
    result.tiles = (*value)->tiles.size();

    for (const auto& tile : (*value)->tiles) {
      result.vertices += tile.vertices.size();
      result.polygons += tile.polygons.size();

      for (const auto& polygon : tile.polygons) {
        result.detailTriangles += polygon.detailTriangles.size();
      }
    }

    return result;
  }

  auto Surface::route(const RouteQuery& request) const -> Core::Result<Route> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation surface is empty or moved");
    }

    if (!validPoint(request.start) || !validPoint(request.goal) || !validPoint(request.searchExtent)
        || request.searchExtent.x <= 0 || request.searchExtent.y <= 0 || request.searchExtent.z <= 0
        || request.searchExtent.x > 10 || request.searchExtent.y > 10 || request.searchExtent.z > 10
        || request.maxPoints < 2 || request.maxPoints > 512) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid navigation route query");
    }

    try {
      std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query(dtAllocNavMeshQuery(),
                                                                           dtFreeNavMeshQuery);

      if (!query || dtStatusFailed(query->init(m_data->native.get(), 8192))) {
        return Core::fail(Core::ErrorCode::External, "Allocate navigation query failed");
      }

      const auto start = components(request.start), goal = components(request.goal),
                 extent = components(request.searchExtent);
      dtQueryFilter filter;
      filter.setIncludeFlags(1);
      dtPolyRef first{}, last{};
      std::array<float, 3> a{}, b{};
      const auto firstStatus
          = query->findNearestPoly(start.data(), extent.data(), &filter, &first, a.data());
      const auto lastStatus
          = query->findNearestPoly(goal.data(), extent.data(), &filter, &last, b.data());

      if (dtStatusFailed(firstStatus) || dtStatusFailed(lastStatus) || !first || !last) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Navigation endpoint has no surface within projection extent");
      }

      std::array<dtPolyRef, 8192> corridor{};
      int count{};
      const auto status = query->findPath(first, last, a.data(), b.data(), &filter, corridor.data(),
                                          &count, static_cast<int>(corridor.size()));

      if (dtStatusFailed(status)
          || dtStatusDetail(status, DT_BUFFER_TOO_SMALL | DT_OUT_OF_NODES | DT_PARTIAL_RESULT)
          || count <= 0 || corridor[static_cast<std::size_t>(count - 1)] != last) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Navigation goal disconnected or route search capacity exhausted");
      }

      std::vector<float> points(std::size_t(request.maxPoints) * 3);
      int corners{};
      const auto straight = query->findStraightPath(a.data(), b.data(), corridor.data(), count,
                                                    points.data(), nullptr, nullptr, &corners,
                                                    static_cast<int>(request.maxPoints));

      if (dtStatusFailed(straight)
          || dtStatusDetail(straight, DT_BUFFER_TOO_SMALL | DT_PARTIAL_RESULT) || corners < 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "Navigation corner capacity exhausted");
      }

      Route result{{a[0], a[1], a[2]}, {b[0], b[1], b[2]}, {}};
      result.points.reserve(static_cast<std::size_t>(corners));

      for (int i = 0; i < corners; ++i) {
        const auto offset = static_cast<std::size_t>(i) * 3;
        result.points.push_back({points[offset], points[offset + 1], points[offset + 2]});
      }

      return result;
    } catch (const std::exception& e) {
      return Core::fail(Core::ErrorCode::External,
                        "Query navigation route: " + std::string(e.what()));
    }
  }

  auto Surface::build(const InputGeometry& geometry, const BuildSettings& settings,
                      std::stop_token stop) -> Core::Result<Surface> {
    auto checked = validateSettings(settings);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    checked = cancelled(stop);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto identity = geometryIdentity(geometry);

    if (!identity) {
      return std::unexpected(identity.error());
    }

    try {
      std::vector<float> vertices;
      vertices.reserve(geometry.vertices.size() * 3);

      for (const auto vertex : geometry.vertices) {
        const auto values = components(vertex);
        vertices.insert(vertices.end(), values.begin(), values.end());
      }

      std::array<float, 3> minimum{}, maximum{};
      rcCalcBounds(vertices.data(), static_cast<int>(geometry.vertices.size()), minimum.data(),
                   maximum.data());
      minimum[1] -= settings.cellHeight * 2;
      maximum[1] += settings.agentHeight + settings.cellHeight * 2;
      const auto tileWidth = float(settings.tileCells) * settings.cellSize;
      const double columns
          = std::max(1.0, std::ceil((double(maximum[0]) - minimum[0]) / tileWidth));
      const double rows = std::max(1.0, std::ceil((double(maximum[2]) - minimum[2]) / tileWidth));

      if (columns > 256 || rows > 256 || columns * rows > 256
          || (double(maximum[1]) - minimum[1]) / settings.cellHeight > RC_SPAN_MAX_HEIGHT) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Navigation tile/vertical span budget exceeded");
      }

      SurfaceDescription description{settings, {minimum[0], minimum[1], minimum[2]}, *identity, {}};
      std::size_t rasterWork{}, polygonCount{}, detailVertexCount{}, detailTriangleCount{};

      for (unsigned z = 0; z < static_cast<unsigned>(rows); ++z) {
        for (unsigned x = 0; x < static_cast<unsigned>(columns); ++x) {
          checked = cancelled(stop);

          if (!checked) {
            return std::unexpected(checked.error());
          }

          BuildContext context;
          rcConfig configuration{};
          configuration.cs = settings.cellSize;
          configuration.ch = settings.cellHeight;
          configuration.walkableSlopeAngle = settings.maxSlopeDegrees;
          configuration.walkableHeight
              = static_cast<int>(std::ceil(settings.agentHeight / settings.cellHeight));
          configuration.walkableClimb
              = static_cast<int>(std::floor(settings.maxClimb / settings.cellHeight));
          configuration.walkableRadius
              = static_cast<int>(std::ceil(settings.agentRadius / settings.cellSize));
          configuration.borderSize = configuration.walkableRadius + 3;
          configuration.tileSize = static_cast<int>(settings.tileCells);
          configuration.width = configuration.tileSize + configuration.borderSize * 2;
          configuration.height = configuration.width;
          configuration.maxEdgeLen = configuration.tileSize;
          configuration.maxSimplificationError = .1F;
          configuration.minRegionArea = 0;
          configuration.mergeRegionArea = 0;
          configuration.maxVertsPerPoly = polygonSize;
          configuration.detailSampleDist = settings.cellSize * 6;
          configuration.detailSampleMaxError = settings.cellHeight;
          const Math::Vec3 base{description.origin.x + float(x) * tileWidth, minimum[1],
                                description.origin.z + float(z) * tileWidth};
          const float padding = float(configuration.borderSize) * settings.cellSize;
          const std::array low{base.x - padding, minimum[1], base.z - padding};
          const std::array high{base.x + tileWidth + padding, maximum[1],
                                base.z + tileWidth + padding};
          std::ranges::copy(low, configuration.bmin);
          std::ranges::copy(high, configuration.bmax);

          if (std::size_t(configuration.width) * std::size_t(configuration.height) > 262144) {
            return Core::fail(Core::ErrorCode::Unavailable, "Navigation tile cell budget exceeded");
          }

          std::vector<int> triangles;

          for (std::size_t i = 0; i < geometry.indices.size(); i += 3) {
            const auto a = geometry.vertices[geometry.indices[i]],
                       b = geometry.vertices[geometry.indices[i + 1]],
                       c = geometry.vertices[geometry.indices[i + 2]];
            const auto lx = std::max(low[0], std::min({a.x, b.x, c.x}));
            const auto hx = std::min(high[0], std::max({a.x, b.x, c.x}));
            const auto lz = std::max(low[2], std::min({a.z, b.z, c.z}));
            const auto hz = std::min(high[2], std::max({a.z, b.z, c.z}));

            if (lx > hx || lz > hz) {
              continue;
            }

            const auto work
                = static_cast<std::size_t>(std::ceil((double(hx) - lx) / settings.cellSize) + 1)
                  * static_cast<std::size_t>(std::ceil((double(hz) - lz) / settings.cellSize) + 1);

            if (work > maxRasterWork - rasterWork) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                "Navigation rasterization work budget exceeded");
            }

            rasterWork += work;
            triangles.push_back(static_cast<int>(geometry.indices[i]));
            triangles.push_back(static_cast<int>(geometry.indices[i + 1]));
            triangles.push_back(static_cast<int>(geometry.indices[i + 2]));
          }

          if (triangles.empty()) {
            continue;
          }

          std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> field(rcAllocHeightfield(),
                                                                             rcFreeHeightField);

          if (!field
              || !rcCreateHeightfield(&context, *field, configuration.width, configuration.height,
                                      configuration.bmin, configuration.bmax, configuration.cs,
                                      configuration.ch)) {
            return failed("heightfield", context);
          }

          std::vector<unsigned char> areas(triangles.size() / 3, RC_NULL_AREA);
          rcMarkWalkableTriangles(&context, configuration.walkableSlopeAngle, vertices.data(),
                                  static_cast<int>(geometry.vertices.size()), triangles.data(),
                                  static_cast<int>(areas.size()), areas.data());

          if (!rcRasterizeTriangles(&context, vertices.data(),
                                    static_cast<int>(geometry.vertices.size()), triangles.data(),
                                    areas.data(), static_cast<int>(areas.size()), *field,
                                    configuration.walkableClimb)) {
            return failed("rasterization", context);
          }

          checked = cancelled(stop);

          if (!checked) {
            return std::unexpected(checked.error());
          }

          rcFilterLowHangingWalkableObstacles(&context, configuration.walkableClimb, *field);
          rcFilterLedgeSpans(&context, configuration.walkableHeight, configuration.walkableClimb,
                             *field);
          rcFilterWalkableLowHeightSpans(&context, configuration.walkableHeight, *field);
          std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> compact(
              rcAllocCompactHeightfield(), rcFreeCompactHeightfield);

          if (!compact
              || !rcBuildCompactHeightfield(&context, configuration.walkableHeight,
                                            configuration.walkableClimb, *field, *compact)) {
            return failed("compact heightfield", context);
          }

          field.reset();

          if (!rcErodeWalkableArea(&context, configuration.walkableRadius, *compact)) {
            return failed("agent erosion", context);
          }

          checked = cancelled(stop);

          if (!checked) {
            return std::unexpected(checked.error());
          }

          if (!rcBuildDistanceField(&context, *compact)
              || !rcBuildRegions(&context, *compact, configuration.borderSize, 0, 0)) {
            return failed("regions", context);
          }

          std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> contours(rcAllocContourSet(),
                                                                              rcFreeContourSet);

          if (!contours
              || !rcBuildContours(&context, *compact, configuration.maxSimplificationError,
                                  configuration.maxEdgeLen, *contours,
                                  RC_CONTOUR_TESS_WALL_EDGES)) {
            return failed("contours", context);
          }

          if (!contours->nconts) {
            continue;
          }

          std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> mesh(rcAllocPolyMesh(),
                                                                      rcFreePolyMesh);

          if (!mesh || !rcBuildPolyMesh(&context, *contours, polygonSize, *mesh)) {
            return failed("polygons", context);
          }

          contours.reset();

          if (!mesh->npolys) {
            continue;
          }

          if (mesh->nverts >= 65535 || mesh->npolys > 16384
              || std::size_t(mesh->npolys) > maxGeometry - polygonCount) {
            return Core::fail(Core::ErrorCode::Unavailable, "Navigation polygon budget exceeded");
          }

          checked = cancelled(stop);

          if (!checked) {
            return std::unexpected(checked.error());
          }

          std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)> detail(
              rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);

          if (!detail
              || !rcBuildPolyMeshDetail(&context, *mesh, *compact, configuration.detailSampleDist,
                                        configuration.detailSampleMaxError, *detail)) {
            return failed("detail heights", context);
          }

          if (std::size_t(detail->nverts) > 1048576 - detailVertexCount
              || std::size_t(detail->ntris) > 1048576 - detailTriangleCount) {
            return Core::fail(Core::ErrorCode::Unavailable, "Navigation detail budget exceeded");
          }

          polygonCount += static_cast<std::size_t>(mesh->npolys);
          detailVertexCount += static_cast<std::size_t>(detail->nverts);
          detailTriangleCount += static_cast<std::size_t>(detail->ntris);
          Tile tile{x, z, base, {base.x + tileWidth, maximum[1], base.z + tileWidth}, {}, {}};
          tile.vertices.reserve(static_cast<std::size_t>(mesh->nverts));

          for (int i = 0; i < mesh->nverts; ++i) {
            tile.vertices.push_back(
                {mesh->verts[i * 3], mesh->verts[i * 3 + 1], mesh->verts[i * 3 + 2]});
          }

          for (int i = 0; i < mesh->npolys; ++i) {
            Polygon polygon;
            const auto* indices = mesh->polys + static_cast<std::size_t>(i) * polygonSize * 2;

            for (unsigned j = 0; j < polygonSize && indices[j] != RC_MESH_NULL_IDX; ++j) {
              polygon.vertices.push_back(indices[j]);
            }

            const auto* submesh = detail->meshes + i * 4;

            for (unsigned j = 0; j < submesh[1]; ++j) {
              const auto offset = (submesh[0] + j) * 3;
              polygon.detailVertices.push_back(
                  {detail->verts[offset], detail->verts[offset + 1], detail->verts[offset + 2]});
            }

            for (unsigned j = 0; j < submesh[3]; ++j) {
              const auto* triangle = detail->tris + (submesh[2] + j) * 4;
              polygon.detailTriangles.push_back(
                  {triangle[0], triangle[1], triangle[2], triangle[3]});
            }

            tile.polygons.push_back(std::move(polygon));
          }

          description.tiles.push_back(std::move(tile));
        }
      }

      checked = cancelled(stop);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (description.tiles.empty()) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "No walkable navigation surface for these agent dimensions");
      }

      return create(std::move(description), stop);
    } catch (const std::exception& e) {
      return Core::fail(Core::ErrorCode::External,
                        "Build navigation surface: " + std::string(e.what()));
    }
  }
}  // namespace Dot::Navigation
