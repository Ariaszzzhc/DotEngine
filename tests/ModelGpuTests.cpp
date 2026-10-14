#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <vector>

import Dot.Assets.Image;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.Resources;
import Dot.Project;
import Dot.Graphics.SceneRenderer;
import Dot.Graphics.Renderer;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Math;

TEST_CASE("model.gpu_imported_textured_hierarchy_matches_independent_native_reference") {
  using namespace Dot;
  auto storage = Core::StagedDirectory::create(
                     Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                     .value();
  auto project = Project::Project::create(storage.files().value(), "game").value();
  auto resources = Editor::ResourceStore::open(project).value();
  const auto source = Core::FileSystem::open(DOT_MODEL_FIXTURES).value();
  REQUIRE(resources.importModel(source.root() / "surface.glb", "assets/model"));
  auto scene = World::loadScene(project.files().value(), "assets/model/model.dotscene").value();
  auto camera = scene.createEntity("Camera").value();
  REQUIRE(scene.setCamera(camera, World::Camera{.projection = World::Projection::Orthographic,
                                                .orthographicHeight = 2.5F,
                                                .farPlane = 10,
                                                .primary = true}));
  REQUIRE(scene.setTransform(camera, {{0, 0, 3}}));
  auto light = scene.createEntity("Light").value();
  REQUIRE(scene.setLight(light, World::Light{.intensity = 3}));
  auto renderer = Graphics::Renderer::create().value();
  auto target = renderer.createTarget(65, 65).value();
  auto binding = Graphics::SceneRenderer::create(renderer, project.files().value()).value();
  REQUIRE(binding.renderInto(scene, target));
  const auto actual = renderer.readHdrTarget(target).value();
  auto geometry = Graphics::quad().value();
  for (auto& v : geometry.vertices) v.tangent = {1, 0, 0, -1};
  auto mesh = renderer.upload(geometry).value();
  Graphics::Material material;
  material.model = Graphics::SurfaceModel::MetallicRoughness;
  material.baseColor = {.8F, .7F, .6F, 1};
  material.metallic = .4F;
  material.roughness = .6F;
  material.normalScale = .75F;
  material.occlusionStrength = .8F;
  material.doubleSided = true;
  material.emissive = {.02F, .01F, 0};
  auto base = Assets::loadImage(source, "base.png").value();
  for (std::size_t alpha = 3; alpha < base.pixels.size(); alpha += 4) base.pixels[alpha] = 255;
  auto color = renderer.uploadTexture(base).value();
  auto packed = renderer
                    .uploadTexture(Assets::loadImage(source, "packed.png").value(),
                                   {.colorSpace = Graphics::ColorSpace::Linear})
                    .value();
  auto normal = renderer
                    .uploadTexture(Assets::loadImage(source, "normal.png").value(),
                                   {.colorSpace = Graphics::ColorSpace::Linear})
                    .value();
  material.baseColorTexture = color;
  material.emissiveTexture = color;
  material.metallicRoughnessTexture = packed;
  material.occlusionTexture = packed;
  material.normalTexture = normal;
  const std::array draws{Graphics::MeshDraw{
      &mesh, Math::compose({.2F, 0, 0}, {}, {-1, 1.5F, 1}).value(), {1, 1, 1, 1}, material}};
  REQUIRE(renderer.renderMeshesInto(target, draws, scene.viewProjection(camera, 1).value()));
  CHECK((actual.pixels == renderer.readHdrTarget(target)->pixels));
  auto pick = binding.pick(scene, 65, 65, {.5F, .5F});
  REQUIRE(pick);
  REQUIRE(*pick);
  CHECK(scene.entity(**pick)->name == "Primitive 0");
  REQUIRE(binding.clearCache());
  CHECK(binding.stats()->meshes == 0);
  CHECK(binding.stats()->textures == 0);
}
