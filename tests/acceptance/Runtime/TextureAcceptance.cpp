#include <array>
#include <filesystem>
#include <print>

import Dot.Assets.Image;
import Dot.Core.Files;
import Dot.Graphics.Renderer;
import Dot.Math;

namespace Dot::Runtime {
  /** Decode real fixture files, sample owned textures through the material path,
   * retain HDR radiance and verify quiescent release counters.
   * 解码真实测试文件，经材质路径采样拥有的纹理，保留 HDR 亮度并验证静止释放计数。 */
  auto testTextures(const std::filesystem::path& root) -> int {
    auto files = Core::FileSystem::open(root).value();
    auto image = Assets::loadImage(files, "surface.png");
    auto hdr = Assets::loadHdrImage(files, "radiance.hdr");

    if (!image || !hdr) {
      std::println(stderr, "Texture decode: {}",
                   !image ? image.error().message : hdr.error().message);
      return 1;
    }

    if (hdr->pixels[0] != 8) {
      std::println(stderr, "HDR radiance was lost");
      return 1;
    }

    auto renderer
        = Graphics::Renderer::create(nullptr, {.postProcess = {0, Graphics::ToneMap::LinearClamp,
                                                               Graphics::ColorSpace::Linear}})
              .value();

    {
      Graphics::TextureOptions options{Graphics::ColorSpace::Srgb,
                                       Graphics::TextureFilter::Nearest};
      auto texture = renderer.uploadTexture(*image, options).value();
      auto geometry = Graphics::quad({2, 2}).value();

      for (auto& vertex : geometry.vertices) {
        vertex.texCoord = {0.5F, 0.25F};
      }

      auto mesh = renderer.upload(geometry).value();
      const std::array draws{Graphics::MeshDraw{
          &mesh, {}, {1, 1, 1, 1}, Graphics::Material{{1, 0.5F, 1, 1}, texture}}};
      auto target = renderer.createTarget(32, 32).value();
      renderer.renderMeshesInto(target, draws, {}).value();
      auto rendered = renderer.readTarget(target).value();
      const auto offset = (16 * 32 + 16) * 4;

      if (rendered.pixels[offset] != 0 || rendered.pixels[offset + 1] != 128
          || rendered.pixels[offset + 2] != 0 || rendered.pixels[offset + 3] != 128
          || renderer.resourceStats()->textures != 1) {
        std::println(stderr, "Actual material/texture sampling failed");
        return 1;
      }
    }

    const auto resources = renderer.resourceStats().value();

    if (resources.textures != 0 || resources.textureBytes != 0 || resources.meshes != 0
        || resources.targets != 0) {
      std::println(stderr, "Texture acceptance leaked owned resources");
      return 1;
    }

    std::println("Texture acceptance passed: decoded PNG/HDR, GPU material sampling and release");
    return 0;
  }
}  // namespace Dot::Runtime
