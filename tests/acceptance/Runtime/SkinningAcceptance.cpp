#include <array>
#include <filesystem>
#include <print>
#include <stdexcept>

import Dot.Animation.Skin;
import Dot.Assets.Mesh;
import Dot.Core.Files;
import Dot.Graphics.Geometry;
import Dot.Graphics.Skinning;
import Dot.Math;

namespace Dot::Runtime {
  /** Owned reference deformation artifacts; no renderer/window/device required.
   * 独立参考变形资源，不创建渲染器/窗口/设备。 */
  auto testSkinning(const std::filesystem::path& root) -> int {
    auto files = Core::FileSystem::open(root).value();
    auto mesh = Graphics::quad({2, 2}).value();
    Animation::SkinDescription description;
    description.inverseBindMatrices.resize(2);
    description.vertices.resize(mesh.vertices.size());

    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
      description.vertices[i].joints[0] = mesh.vertices[i].position.y > 0 ? 1U : 0U;
    }

    auto skin = Animation::Skin::create(std::move(description)).value();
    std::array palette{Math::Mat4{}, Math::compose({.75F, 0, 0}).value()};
    auto output = Graphics::deformMesh(mesh, skin, palette).value();

    if (output.vertices[2].position.x != mesh.vertices[2].position.x + .75F
        || output.vertices[0].position.x != mesh.vertices[0].position.x) {
      throw std::runtime_error("Skin reference does not follow the two joint groups");
    }

    Assets::saveMesh(files, "skin-bind.dotmesh", mesh).value();
    Assets::saveMesh(files, "skin-reference.dotmesh", output).value();
    std::println("Skinning reference acceptance passed: owned bind/posed geometry and two joints");
    return 0;
  }
}  // namespace Dot::Runtime
