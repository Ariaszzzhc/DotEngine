#include <doctest/doctest.h>

#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

import Dot.Assets.Material;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Graphics.Renderer;
import Dot.Math;

using namespace Dot::Assets;
using namespace Dot::Graphics;

TEST_CASE("material.authoring_roundtrip_preserves_factors_slots_and_sampler_options") {
  MaterialDescription source;
  source.baseColor = {0.2F, 0.4F, 0.8F, 0.5F};
  source.emissive = {8, 2, 0};
  source.metallic = 1;
  source.roughness = 0.2F;
  source.normalScale = 3;
  source.doubleSided = true;
  source.baseColorTexture = TextureSource{"assets/颜色.png"};
  TextureOptions data;
  data.colorSpace = ColorSpace::Linear;
  data.addressU = TextureAddress::Mirror;
  data.addressV = TextureAddress::Clamp;
  data.maxAnisotropy = 8;
  source.metallicRoughnessTexture = TextureSource{"assets/packed.jpg", data};
  source.normalTexture = TextureSource{"assets/normal.png", data};
  source.emissiveTexture = TextureSource{"assets/灯.jpeg"};
  auto encoded = encodeMaterial(source);
  REQUIRE(encoded);
  auto decoded = decodeMaterial(*encoded);
  REQUIRE(decoded);
  CHECK(decoded->model == SurfaceModel::MetallicRoughness);
  CHECK(decoded->baseColor == source.baseColor);
  CHECK(decoded->emissive == source.emissive);
  CHECK(decoded->metallic == 1);
  CHECK(decoded->roughness == 0.2F);
  CHECK(decoded->normalScale == 3);
  CHECK(decoded->doubleSided);
  CHECK(decoded->baseColorTexture->path == source.baseColorTexture->path);
  CHECK(decoded->normalTexture->sampling.addressU == TextureAddress::Mirror);
  CHECK(decoded->normalTexture->sampling.addressV == TextureAddress::Clamp);
  CHECK(decoded->normalTexture->sampling.maxAnisotropy == 8);
  auto reencoded = encodeMaterial(*decoded);
  REQUIRE(reencoded);
  CHECK(*reencoded == *encoded);
  source.model = SurfaceModel::Unlit;
  source.normalTexture.reset();
  source.metallicRoughnessTexture.reset();
  CHECK(decodeMaterial(encodeMaterial(source).value())->model == SurfaceModel::Unlit);
}

TEST_CASE("material.factor_path_and_sampling_boundaries_reject_invalid_authoring") {
  MaterialDescription source;
  source.metallic = -0.01F;
  CHECK_FALSE(validateMaterial(source));
  source.metallic = 0;
  source.roughness = 1.01F;
  CHECK_FALSE(validateMaterial(source));
  source.roughness = 0;
  source.emissive = {65503, 0, 0};
  CHECK(validateMaterial(source));
  source.emissive.x = 65504;
  CHECK_FALSE(validateMaterial(source));
  source.emissive = {};
  source.baseColor.x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(validateMaterial(source));
  source.baseColor.x = 1;
  source.normalScale = 8;
  CHECK(validateMaterial(source));
  source.normalScale = 8.01F;
  CHECK_FALSE(validateMaterial(source));
  source.normalScale = 1;

  for (const auto* path : {"../color.png", "/color.png", "assets/CON.png", "assets/x.hdr",
                           "assets/x.png/../y.png", "assets\\x.png", "assets/x.png ", ""}) {
    source.baseColorTexture = TextureSource{path};
    CHECK_FALSE(validateMaterial(source));
  }

  source.baseColorTexture = TextureSource{"assets/x.png"};
  source.baseColorTexture->sampling.filter = TextureFilter::Nearest;
  source.baseColorTexture->sampling.maxAnisotropy = 2;
  CHECK_FALSE(validateMaterial(source));
  source.baseColorTexture->sampling.maxAnisotropy = 1;
  CHECK(validateMaterial(source));
  source.normalTexture = TextureSource{"assets/x.png"};
  CHECK_FALSE(validateMaterial(source));
  source.normalTexture->sampling.colorSpace = ColorSpace::Linear;
  CHECK(validateMaterial(source));
  source.model = SurfaceModel::Unlit;
  CHECK_FALSE(validateMaterial(source));
  source.model = static_cast<SurfaceModel>(99);
  CHECK_FALSE(encodeMaterial(source));
}

TEST_CASE("material.strict_structure_types_duplicates_depth_and_size_fail") {
  const auto source = encodeMaterial({}).value();
  auto json = nlohmann::json::parse(source);
  json["model"] = "unknown";
  CHECK_FALSE(decodeMaterial(json.dump()));
  json = nlohmann::json::parse(source);
  json["version"] = 2;
  CHECK_FALSE(decodeMaterial(json.dump()));
  json = nlohmann::json::parse(source);
  json.erase("roughness");
  CHECK_FALSE(decodeMaterial(json.dump()));
  json = nlohmann::json::parse(source);
  json["metallic"] = "0";
  CHECK_FALSE(decodeMaterial(json.dump()));
  json = nlohmann::json::parse(source);
  json["doubleSided"] = 1;
  CHECK_FALSE(decodeMaterial(json.dump()));
  CHECK_FALSE(decodeMaterial("{\"model\":\"unlit\",\"model\":\"metallicRoughness\"}"));
  CHECK_FALSE(decodeMaterial(std::string(1024 * 1024 + 1, ' ')));
  CHECK_FALSE(decodeMaterial(std::string(30, '[') + "0" + std::string(30, ']')));
  MaterialDescription textured;
  textured.baseColorTexture = TextureSource{"assets/x.png"};
  json = nlohmann::json::parse(encodeMaterial(textured).value());
  json["baseColorTexture"]["sampling"]["maxAnisotropy"] = 1.5;
  CHECK_FALSE(decodeMaterial(json.dump()));
  json["baseColorTexture"]["sampling"]["maxAnisotropy"] = 1;
  json["baseColorTexture"]["sampling"]["addressU"] = "unknown";
  CHECK_FALSE(decodeMaterial(json.dump()));
}

TEST_CASE("material.files_are_atomic_rooted_and_preserve_previous_on_failure") {
  auto parent = Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value();
  auto storage = Dot::Core::StagedDirectory::create(parent).value();
  auto files = storage.files().value();
  MaterialDescription source;
  source.baseColorTexture = TextureSource{"assets/not-loaded.png"};
  REQUIRE(saveMaterial(files, "steel.dotmaterial", source));
  CHECK(loadMaterial(files, "steel.dotmaterial")->baseColorTexture->path
        == "assets/not-loaded.png");
  const auto before = files.readText("steel.dotmaterial").value();
  source.roughness = -1;
  CHECK_FALSE(saveMaterial(files, "steel.dotmaterial", source));
  CHECK(files.readText("steel.dotmaterial").value() == before);
  CHECK_FALSE(loadMaterial(files, "missing.dotmaterial"));
  CHECK_FALSE(loadMaterial(files, "../steel.dotmaterial"));
  CHECK_FALSE(loadMaterial(files, "steel.json"));
  REQUIRE(files.writeTextAtomic("broken.dotmaterial", "{}"));
  auto broken = loadMaterial(files, "broken.dotmaterial");
  REQUIRE_FALSE(broken);
  CHECK(broken.error().message.find("broken.dotmaterial") != std::string::npos);
  std::filesystem::create_symlink("steel.dotmaterial", files.root() / "linked.dotmaterial");
  CHECK_FALSE(loadMaterial(files, "linked.dotmaterial"));
  CHECK_FALSE(saveMaterial(files, "linked.dotmaterial", {}));
}
