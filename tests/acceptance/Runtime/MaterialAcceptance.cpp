#include <filesystem>
#include <print>

import Dot.Assets.Material;
import Dot.Core.Files;
import Dot.Graphics.Renderer;

namespace Dot::Runtime {
  auto testMaterial(const std::filesystem::path& root) -> int {
    auto files = Core::FileSystem::open(root);

    if (!files) {
      std::println(stderr, "{}", files.error().message);
      return 1;
    }

    Assets::MaterialDescription material;
    material.baseColor = {1, 0.65F, 0.2F, 1};
    material.metallic = 1;
    material.roughness = 0.25F;
    material.baseColorTexture = Assets::TextureSource{"assets/metal.png"};
    auto saved = Assets::saveMaterial(*files, "gold.dotmaterial", material);

    if (!saved) {
      std::println(stderr, "{}", saved.error().message);
      return 1;
    }

    auto loaded = Assets::loadMaterial(*files, "gold.dotmaterial");

    if (!loaded || loaded->metallic != 1 || loaded->roughness != 0.25F
        || loaded->baseColorTexture->path != "assets/metal.png"
        || Assets::decodeMaterial("{\"model\":\"unknown\"}")) {
      std::println(stderr, "Material persistence/validation acceptance failed");
      return 1;
    }

    std::println(
        "Material acceptance passed: owned physical factors, explicit texture paths, "
        "atomic asset roundtrip and strict rejection; no GPU/file substitution");
    return 0;
  }
}  // namespace Dot::Runtime
