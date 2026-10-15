#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Animation.SceneSkin;
import Dot.Animation.SkinFiles;
import Dot.Assets.Image;
import Dot.Assets.Material;
import Dot.Assets.Mesh;
import Dot.Assets.ModelBundle;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.Resources;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.Graphics.Skinning;
import Dot.Project;
import Dot.Project.GamePackage;
import Dot.Project.GameSession;
import Dot.Script.Compiler;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace Dot::Runtime {
  /** Import external GLB skins through real native assets/session/render/export.
   * Compare independent CPU mesh pixels and reload after authoring removal.
   * 通过真实资产/会话/渲染/导出导入 GLB 蒙皮，对照独立 CPU 网格像素并删除源后重载。 */
  auto testModelSkinning(const std::filesystem::path& compilerPath,
                         const std::filesystem::path& output) -> int {
    const auto require = [](bool value, const char* message) {
      if (!value) {
        throw std::runtime_error(message);
      }
    };
    const auto clean = [&](const auto& report) {
      require(report.failures.empty() && !report.realmFaulted,
              "Native skin session script lifecycle must remain healthy");
    };
    auto files = Core::FileSystem::open(output).value();
    auto storage = Core::StagedDirectory::create(files).value();
    auto project = Project::Project::create(storage.files().value(), "game").value();
    const auto assets = project.files().value();
    auto resources = Editor::ResourceStore::open(project).value();
    const auto imported
        = resources
              .importModel(std::filesystem::path(DOT_MODEL_FIXTURES) / "skinned.glb",
                           "assets/character")
              .value();
    require(imported.skins == 1 && imported.skinnedMeshes == 1 && imported.animations == 1,
            "GLB skin import report must retain actual assets and animation");
    auto bundle = Assets::loadModelBundle(assets, imported.model).value();
    auto& scene = bundle.scene;

    const auto authoredEntities = scene.entities().value();

    Animation::ClipDescription counter{"Counter flex", {}};
    const auto negative = Math::rotationEulerDegrees({0, 0, -60}).value();

    for (const auto& track : bundle.animations[0].description().value()->tracks) {
      counter.tracks.push_back(
          {track.node,
           Animation::Path::Rotation,
           Animation::Interpolation::Linear,
           {{0, {0, 0, 0, 1}}, {2, {negative.x, negative.y, negative.z, negative.w}}}});
    }

    Animation::saveClip(assets, "assets/counter.dotanimation",
                        Animation::Clip::create(std::move(counter)).value())
        .value();
    assets
        .writeTextAtomic("scripts/skin.ts", R"ts(
import {animation, ScriptComponent} from 'dot:engine';

export class SkinController extends ScriptComponent {
  private phase = 0;

  onStart(): void { this.self.playAnimation(0, true); }

  onUpdate(): void {
    if (this.phase++ === 1) {
      this.self.seekAnimation(0.5);
      animation.crossFade(this.self.id, 1, 2);
    }

    const state = this.self.animationState;

    if (state.clip === 1 && state.transition &&
        (state.transition.durationSeconds !== 2 ||
         state.transition.elapsedSeconds !== state.timeSeconds)) {
      throw Error('exported native animation transition clock');
    }
  }

  onStop(): void { this.self.stopAnimation(); }
}
)ts")
        .value();

    for (const auto& entity : authoredEntities) {
      if (entity.animation) {
        auto player = *entity.animation;
        player.looping = false;
        player.clips.push_back("assets/counter.dotanimation");
        scene.setAnimationPlayer(entity.id, player).value();
        scene.setScript(entity.id, World::ScriptComponent{"skin.ts", "SkinController"}).value();
      }
    }

    const auto camera = scene.createEntity("Camera").value();
    scene
        .setCamera(camera, World::Camera{.projection = World::Projection::Orthographic,
                                         .orthographicHeight = 4,
                                         .farPlane = 20,
                                         .primary = true})
        .value();
    scene.setTransform(camera, {{0, 0, 5}}).value();
    const auto light = scene.createEntity("Key light").value();
    scene
        .setLight(light, World::Light{.intensity = 4,
                                      .shadows = Graphics::ShadowSettings{.resolution = 128,
                                                                          .cascades = 1,
                                                                          .distance = 15}})
        .value();
    scene.setOcclusion(Graphics::OcclusionSettings{}).value();
    Assets::MaterialDescription floorMaterial;
    floorMaterial.baseColor = {.2F, .25F, .3F, 1};
    Assets::saveMaterial(assets, "assets/floor.dotmaterial", floorMaterial).value();
    const auto floor = scene.createEntity("Floor").value();
    scene.setMeshRenderer(floor, World::MeshRenderer{}).value();
    scene.setTransform(floor, {{0, -1.15F, 0}, {}, {3, .2F, 2}}).value();
    scene.setMaterial(floor, World::MaterialBinding{"assets/floor.dotmaterial"}).value();
    project.saveStartup(scene).value();
    auto compiler = Script::TypeScriptCompiler::create(compilerPath).value();
    auto published
        = Project::GamePackage::publish(project, scene, compiler, storage.files().value(), "export")
              .value();
    auto renderer = Graphics::Renderer::create().value();
    const auto baseline = renderer.resourceStats().value();
    std::vector<std::uint8_t> initial;
    std::vector<std::uint8_t> exportedPose;

    {
      auto session = Project::GameSession::create(project, compiler).value();
      clean(session.start().value());
      auto playing = session.scene().value();
      auto entities = playing->entities().value();
      const auto entity = *std::ranges::find_if(
          entities, [](const auto& value) { return value.skin.has_value(); });
      const auto skin = Animation::loadSkin(assets, entity.skin->asset).value();
      const auto mesh = Assets::loadMesh(assets, entity.mesh->asset).value().data;
      auto binding = Animation::SceneSkin::bind(*playing, entity.id, skin).value();
      auto drawing = Graphics::SceneRenderer::create(renderer, assets).value();

      for (int frame = 0; frame <= 8; ++frame) {
        clean(session.update(frame == 0 ? 0 : .25).value().frame);
        const auto actual = drawing.render(*playing, 256, 256).value();
        const auto time = double(frame) * .25;
        const auto weight = time / 2;
        const auto degrees
            = frame == 0 ? 0 : (1 - weight) * std::min(.5 + time, 2.0) * 30 - weight * time * 30;
        const auto expectedRotation = Math::rotationEulerDegrees({0, 0, float(degrees)}).value();

        for (const auto& track : bundle.animations[0].description().value()->tracks) {
          const auto joint
              = std::ranges::find(bundle.bindings, track.node, &Animation::NodeBinding::node);
          require(joint != bundle.bindings.end(), "Expected imported animated joint");
          const auto rotation
              = playing->transform(playing->findEntity(joint->entity.value).value())->rotation;
          require(std::abs(rotation.z - expectedRotation.z) < .00001F
                      && std::abs(rotation.w - expectedRotation.w) < .00001F,
                  "Script-controlled native skin fade must match independent joint angles");
        }

        const auto palette = binding.palette(*playing).value();
        const auto reference = Graphics::deformMesh(mesh, skin, palette).value();
        Assets::saveMesh(assets, "assets/reference.dotmesh", reference).value();
        auto referenceScene = World::decodeScene(World::encodeScene(*playing).value()).value();
        const auto referenceOwner = referenceScene.findEntity(entity.id.value).value();
        referenceScene.setSkin(referenceOwner, std::nullopt).value();
        referenceScene
            .setMeshRenderer(referenceOwner, World::MeshRenderer{World::MeshKind::Asset,
                                                                 "assets/reference.dotmesh"})
            .value();
        auto referenceRenderer = Graphics::SceneRenderer::create(renderer, assets).value();
        const auto expected = referenceRenderer.render(referenceScene, 256, 256).value();
        unsigned difference{};

        for (std::size_t i = 0; i < actual.pixels.size(); ++i) {
          difference
              += static_cast<unsigned>(std::abs(int(actual.pixels[i]) - int(expected.pixels[i])));
        }

        require(difference <= 2048, "Imported GPU skin PBR/shadow/AO differs from CPU reference");

        if (frame == 0) {
          initial = actual.pixels;
          require(drawing.pick(*playing, 256, 256, {.5F, .5F}).value() == entity.id,
                  "Imported skin must remain pickable despite translated mesh node");
          const auto bounds = drawing.bounds(*playing, entity.id).value();
          require(std::abs(bounds.minimum.x + .3F) < .0001F,
                  "Imported mesh-node transform must cancel against its skin palette");
        } else if (frame == 2) {
          exportedPose = actual.pixels;
        } else if (frame == 8) {
          require(actual.pixels != initial, "Imported joint animation must deform real pixels");
        }

        files
            .writeAtomic("model-skin-" + std::to_string(frame) + ".png",
                         Assets::encodeImage(actual).value())
            .value();
      }

      clean(session.close().value());
    }

    const auto relocated = storage.files()->root() / "relocated";
    std::filesystem::rename(published.root(), relocated);
    auto package = Project::GamePackage::open(Core::FileSystem::open(relocated).value()).value();
    std::filesystem::remove_all(assets.root());
    std::filesystem::remove_all(relocated);

    {
      auto source = package.scene().value();
      auto session = Project::GameSession::createCompiled(package.files().value(),
                                                          package.description().value(), source,
                                                          package.program().value())
                         .value();
      clean(session.start().value());
      clean(session.update(0).value().frame);
      clean(session.update(.5).value().frame);
      auto drawing = Graphics::SceneRenderer::create(renderer, package.files().value()).value();
      const auto actual = drawing.render(*session.scene().value(), 256, 256).value();
      require(
          actual.pixels == exportedPose,
          "Relocated script-driven skin fade must replay without authoring/source/compiler files");
      files.writeAtomic("model-skin-export.png", Assets::encodeImage(actual).value()).value();
      clean(session.close().value());
    }

    const auto after = renderer.resourceStats().value();
    require(after.meshes == baseline.meshes && after.vertexBytes == baseline.vertexBytes
                && after.indexBytes == baseline.indexBytes && after.skins == baseline.skins
                && after.skinBytes == baseline.skinBytes && after.textures == baseline.textures,
            "Imported skin drawing scopes must release all owned geometry/skin resources");
    std::println(
        "Model skin acceptance passed: GLB import, native files/session, eight weights, "
        "nine independent-angle/CPU-compared script fade frames, picking/bounds and relocated "
        "export");
    return 0;
  }
}  // namespace Dot::Runtime
