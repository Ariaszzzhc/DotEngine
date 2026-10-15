#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <print>
#include <stdexcept>
#include <vector>

import Dot.Animation.Clip;
import Dot.Animation.SceneAnimator;
import Dot.Animation.SceneSkin;
import Dot.Animation.Skin;
import Dot.Animation.SkinFiles;
import Dot.Assets.Image;
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Core.Files;
import Dot.Graphics.Geometry;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.Graphics.Skinning;
import Dot.Math;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace Dot::Runtime {
  /** Real node hierarchy drives GPU skinning; compare owned CPU reference vertices
   * and pixels, save nine frames, and prove failed updates retain prior geometry.
   * 真实节点层级驱动 GPU 蒙皮，对照独立 CPU 顶点/像素，保存九帧并验证失败保留旧几何。 */
  auto testSkinning(const std::filesystem::path& root) -> int {
    auto files = Core::FileSystem::open(root).value();
    auto renderer = Graphics::Renderer::create().value();
    const auto baseline = renderer.resourceStats().value();

    {
      auto scene = World::Scene::create("Skinning hierarchy").value();
      auto first = scene.createEntity("Root joint").value();
      auto second = scene.createEntity("Tip joint").value();
      auto entity = scene.createEntity("Skin mesh node").value();
      scene.setTransform(first, {{0, -1, 0}}).value();
      scene.setTransform(second, {{0, 1, 0}}).value();
      scene.setParent(second, first).value();
      scene.setTransform(entity, {{.2F, .1F, 0}}).value();
      const auto world = scene.worldTransform(entity).value();
      Graphics::MeshData mesh;

      for (int row = 0; row <= 4; ++row) {
        const auto y = static_cast<float>(row) * .5F - 1;

        for (const float x : {-.3F, .3F}) {
          mesh.vertices.push_back({{x, y, 0},
                                   {1, 1, 1, 1},
                                   {0, 0, 1},
                                   {x < 0 ? 0.F : 1.F, 1 - static_cast<float>(row) * .25F},
                                   {1, 0, 0, 1}});
        }
      }

      for (std::uint32_t row = 0; row < 4; ++row) {
        const auto i = row * 2;
        mesh.indices.insert(mesh.indices.end(), {i, i + 1, i + 3, i, i + 3, i + 2});
      }

      Animation::SkinDescription description;
      description.inverseBindMatrices
          = {Math::inverse(scene.worldTransform(first).value()).value(),
             Math::inverse(scene.worldTransform(second).value()).value()};
      description.vertices.resize(mesh.vertices.size());

      for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto fraction = (mesh.vertices[i].position.y + 1) * .5F;
        description.vertices[i].joints[1] = 1;
        description.vertices[i].weights = {1 - fraction, fraction};
      }

      auto skin = Animation::Skin::create(std::move(description)).value();
      files.createDirectories("assets").value();
      Animation::saveSkin(files, "assets/strip.dotskin", skin).value();
      Assets::saveMesh(files, "assets/strip.dotmesh", mesh).value();
      scene
          .setMeshRenderer(entity,
                           World::MeshRenderer{World::MeshKind::Asset, "assets/strip.dotmesh"})
          .value();
      scene.setSkin(entity, World::SkinBinding{"assets/strip.dotskin", {first.value, second.value}})
          .value();
      World::saveScene(files, "skin.dotscene", scene).value();
      scene = World::loadScene(files, "skin.dotscene").value();
      first.scene = scene.id();
      second.scene = scene.id();
      entity.scene = scene.id();
      skin = Animation::loadSkin(files, "assets/strip.dotskin").value();
      auto bound = Animation::SceneSkin::bind(scene, entity, skin).value();
      auto clip = Animation::Clip::create(
                      {"Bend",
                       {{1,
                         Animation::Path::Rotation,
                         Animation::Interpolation::Linear,
                         {{0, {0, 0, 0, 1}}, {2, {0, 0, .573576436F, .819152044F}}}}}})
                      .value();
      const std::array bindings{Animation::NodeBinding{1, second}};
      auto animator = Animation::SceneAnimator::bind(scene, clip, bindings).value();
      auto uploaded = renderer.upload(mesh).value();
      auto controller = renderer.createSkin(uploaded, skin).value();
      auto view = controller.mesh().value();
      auto floor = renderer.upload(Graphics::cube({3, .15F, 2}).value()).value();
      Graphics::Lighting lighting;
      lighting.viewPosition = {0, 0, 5};
      lighting.viewDirection = Math::Vec3{0, 0, -1};
      const auto lightRotation = Math::rotationEulerDegrees({-35, 35, 0}).value();
      const auto lightDirection
          = Math::transformVector(Math::compose({}, lightRotation).value(), {0, 0, -1}).value();
      lighting.lights = {
          {.direction = lightDirection,
           .intensity = 4,
           .shadows = Graphics::ShadowSettings{.resolution = 128, .cascades = 1, .distance = 15}}};
      renderer.setLighting(lighting).value();
      renderer.setOcclusion(Graphics::OcclusionSettings{}).value();
      const auto camera = Math::multiply(Math::orthographic(-2, 2, -2, 2, .1F, 15).value(),
                                         Math::lookAt(lighting.viewPosition, {}).value())
                              .value();
      Graphics::Material material;
      material.model = Graphics::SurfaceModel::MetallicRoughness;
      material.baseColor = {.8F, .35F, .12F, 1};
      material.metallic = .65F;
      material.roughness = .35F;
      Assets::MaterialDescription authored;
      authored.baseColor = material.baseColor;
      authored.metallic = material.metallic;
      authored.roughness = material.roughness;
      Assets::saveMaterial(files, "assets/copper.dotmaterial", authored).value();
      scene.setMaterial(entity, World::MaterialBinding{"assets/copper.dotmaterial"}).value();
      const auto floorEntity = scene.createEntity("Floor").value();
      scene.setMeshRenderer(floorEntity, World::MeshRenderer{}).value();
      scene.setTransform(floorEntity, {{0, -1.2F, .1F}, {}, {3, .15F, 2}}).value();
      scene.setMaterial(floorEntity, World::MaterialBinding{"assets/copper.dotmaterial"}).value();
      const auto cameraEntity = scene.createEntity("Camera").value();
      scene
          .setCamera(cameraEntity, World::Camera{.projection = World::Projection::Orthographic,
                                                 .orthographicHeight = 4,
                                                 .nearPlane = .1F,
                                                 .farPlane = 15,
                                                 .primary = true})
          .value();
      scene.setTransform(cameraEntity, {lighting.viewPosition}).value();
      const auto lightEntity = scene.createEntity("Key light").value();
      scene
          .setLight(lightEntity,
                    World::Light{.intensity = 4, .shadows = lighting.lights[0].shadows})
          .value();
      scene.setTransform(lightEntity, {{}, lightRotation}).value();
      scene.setOcclusion(Graphics::OcclusionSettings{}).value();
      World::saveScene(files, "skin.dotscene", scene).value();
      auto sceneRenderer = Graphics::SceneRenderer::create(renderer, files).value();
      std::array draws{Graphics::MeshDraw{&view, world, {1, 1, 1, 1}, material},
                       Graphics::MeshDraw{
                           &floor, Math::compose({0, -1.2F, .1F}).value(), {1, 1, 1, 1}, material}};
      Assets::saveMesh(files, "skin-bind.dotmesh", mesh).value();
      std::vector<std::uint8_t> initial;

      for (int frame = 0; frame <= 8; ++frame) {
        animator.seek(scene, static_cast<double>(frame) * .25).value();
        const std::array worlds{scene.worldTransform(first).value(),
                                scene.worldTransform(second).value()};
        const auto palette = bound.palette(scene).value();

        if (palette != skin.palette(world, worlds).value()) {
          throw std::runtime_error("Reloaded scene binding changed ordered skin palette");
        }
        const std::array updates{Graphics::SkinUpdate{&controller, palette}};
        renderer.updateSkins(updates).value();
        auto reference = Graphics::deformMesh(mesh, skin, palette).value();
        auto actualVertices = renderer.readSkin(controller).value();

        for (std::size_t i = 0; i < actualVertices.size(); ++i) {
          if (Math::length(actualVertices[i].position - reference.vertices[i].position) > .00001
              || Math::length(actualVertices[i].normal - reference.vertices[i].normal) > .00001) {
            throw std::runtime_error(
                "GPU skin vertices/normals differ from independent CPU reference");
          }
        }

        draws[0].mesh = &view;
        const auto actual = renderer.renderMeshes(draws, camera, 256, 256).value();
        auto expectedMesh = renderer.upload(reference).value();
        draws[0].mesh = &expectedMesh;
        const auto expected = renderer.renderMeshes(draws, camera, 256, 256).value();
        unsigned difference{};

        for (std::size_t i = 0; i < actual.pixels.size(); ++i) {
          difference
              += static_cast<unsigned>(std::abs(int(actual.pixels[i]) - int(expected.pixels[i])));
        }

        if (difference > 2048) {
          throw std::runtime_error("GPU skin physical/shadow/AO pixels differ from reference");
        }

        const auto automatic = sceneRenderer.render(scene, 256, 256).value();
        unsigned automaticDifference{};

        for (std::size_t i = 0; i < automatic.pixels.size(); ++i) {
          automaticDifference += static_cast<unsigned>(
              std::abs(int(automatic.pixels[i]) - int(expected.pixels[i])));
        }

        if (automaticDifference > 2048) {
          throw std::runtime_error("Automatic scene skin/shadow/AO differs from CPU reference");
        }

        if (frame == 8) {
          files.writeAtomic("skin-scene.png", Assets::encodeImage(automatic).value()).value();
        }

        if (frame == 0) {
          initial = actual.pixels;
        } else if (frame == 8 && actual.pixels == initial) {
          throw std::runtime_error("GPU skin did not change rendered pixels");
        }

        files
            .writeAtomic("skin-" + std::to_string(frame) + ".png",
                         Assets::encodeImage(actual).value())
            .value();
        Assets::saveMesh(files, "skin-reference.dotmesh", reference).value();
        reference.vertices = std::move(actualVertices);
        Assets::saveMesh(files, "skin-gpu.dotmesh", reference).value();
      }

      const auto before = renderer.readSkin(controller).value();
      const auto updateCount = controller.info()->updates;
      const std::array invalid{Math::Mat4{}, Math::compose({}, {}, {-1, 1, 1}).value()};
      const std::array bad{Graphics::SkinUpdate{&controller, invalid}};

      if (renderer.updateSkins(bad) || controller.info()->updates != updateCount
          || std::memcmp(renderer.readSkin(controller)->data(), before.data(),
                         before.size() * sizeof(Graphics::Vertex))
                 != 0) {
        throw std::runtime_error("Invalid GPU deformation did not preserve last complete pose");
      }
    }

    const auto after = renderer.resourceStats().value();

    if (after.meshes != baseline.meshes || after.vertexBytes != baseline.vertexBytes
        || after.indexBytes != baseline.indexBytes || after.skins != baseline.skins
        || after.skinBytes != baseline.skinBytes) {
      throw std::runtime_error("GPU skin resources survived their owning scope");
    }

    std::println(
        "Skinning acceptance passed: saved skin/scene, native hierarchy, automatic scene GPU "
        "deformation, nine "
        "reference-compared "
        "frames and owned release");
    return 0;
  }
}  // namespace Dot::Runtime
