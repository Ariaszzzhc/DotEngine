#include <array>
#include <filesystem>
#include <print>

import Dot.Assets.Mesh;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Graphics.Geometry;
import Dot.Graphics.Renderer;
import Dot.Math;

namespace Dot::Runtime {
  /** Persist surface attributes, derive an owned normal visualization and verify
   * the vertex payload size and actual GPU rasterization.
   * 持久化表面属性，产生拥有的法线可视化，并验证顶点载荷大小及真实 GPU 光栅化。 */
  auto testSurfaceMesh(const std::filesystem::path& root) -> int {
    auto files = Core::FileSystem::open(root).value();
    auto owner = Core::StagedDirectory::create(files).value();
    auto geometry = Graphics::withFlatNormals(Graphics::cube().value()).value();

    for (auto& vertex : geometry.vertices) {
      vertex.texCoord = {vertex.position.x * 2, vertex.position.y * 2};
      vertex.color = {(vertex.normal.x + 1) * 0.5F, (vertex.normal.y + 1) * 0.5F,
                      (vertex.normal.z + 1) * 0.5F, 1};
    }

    auto saved = Assets::saveMesh(owner.files().value(), "surface.dotmesh", geometry);
    auto loaded = Assets::loadMesh(owner.files().value(), "surface.dotmesh");

    if (!saved || !loaded || loaded->data.vertices.size() != geometry.vertices.size()) {
      std::println(stderr, "Surface mesh persistence failed");
      return 1;
    }

    for (std::size_t i = 0; i < geometry.vertices.size(); ++i) {
      if (loaded->data.vertices[i].normal != geometry.vertices[i].normal
          || loaded->data.vertices[i].texCoord != geometry.vertices[i].texCoord) {
        std::println(stderr, "Surface mesh lost normal/UV attributes");
        return 1;
      }
    }

    auto renderer = Graphics::Renderer::create().value();
    auto mesh = renderer.upload(loaded->data).value();
    const auto camera = Math::multiply(Math::perspective(60, 1, 0.1F, 100).value(),
                                       Math::lookAt({2, 2, 4}, {}).value())
                            .value();
    const std::array draws{Graphics::MeshDraw{&mesh}};
    auto image = renderer.renderMeshes(draws, camera, 64, 64).value();
    const auto center = (32 * 64 + 32) * 4;

    if (image.pixels[center] == 0 || image.pixels[center + 1] == 0 || image.pixels[center + 2] == 0
        || renderer.resourceStats()->vertexBytes
               != geometry.vertices.size() * sizeof(Graphics::Vertex)) {
      std::println(stderr, "Surface mesh GPU payload/rasterization failed");
      return 1;
    }

    std::println("Surface mesh acceptance passed: normals/UVs, owned persistence and GPU payload");
    return 0;
  }
}  // namespace Dot::Runtime
