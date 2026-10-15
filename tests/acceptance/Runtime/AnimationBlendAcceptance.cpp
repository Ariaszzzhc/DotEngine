#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Animation.SceneAnimator;
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
import Dot.World.SceneFiles;

namespace Dot::Runtime {
  /** Actual imported skin cross-fades versus independently authored joint angles
   * and CPU geometry. No script/editor transition support is implied by this case.
   * 实际导入蒙皮的过渡，对照独立关节角度与 CPU 几何；不代表脚本/编辑器已接入过渡。 */
  auto testAnimationBlend(const std::filesystem::path& output) -> int {
    using namespace Animation;
    const auto require = [](bool value, const char* message) {
      if (!value) {
        throw std::runtime_error(message);
      }
    };
    auto files = Core::FileSystem::open(output).value();
    auto storage = Core::StagedDirectory::create(files).value();
    auto project = Project::Project::create(storage.files().value(), "game").value();
    auto assets = project.files().value();
    auto resources = Editor::ResourceStore::open(project).value();
    auto imported = resources
                        .importModel(std::filesystem::path(DOT_MODEL_FIXTURES) / "skinned.glb",
                                     "assets/character")
                        .value();
    auto bundle = Assets::loadModelBundle(assets, imported.model).value();
    auto& scene = bundle.scene;
    const auto entities = scene.entities().value();
    const auto owner = *std::ranges::find_if(entities, [](const auto& e) { return bool(e.skin); });
    auto skin = loadSkin(assets, owner.skin->asset).value();
    auto mesh = Assets::loadMesh(assets, owner.mesh->asset).value().data;
    const auto source = bundle.animations[0];
    ClipDescription counter{"Counter flex", {}};
    ClipDescription hold{"Hold flex", {}};

    for (const auto& track : source.description().value()->tracks) {
      const auto negative = Math::rotationEulerDegrees({0, 0, -60}).value();
      const auto positive = Math::rotationEulerDegrees({0, 0, 60}).value();
      counter.tracks.push_back(
          {track.node,
           Path::Rotation,
           Interpolation::Linear,
           {{0, {0, 0, 0, 1}}, {2, {negative.x, negative.y, negative.z, negative.w}}}});
      hold.tracks.push_back({track.node,
                             Path::Rotation,
                             Interpolation::Step,
                             {{0, {positive.x, positive.y, positive.z, positive.w}}}});
    }

    saveClip(files, "blend-source.dotanimation", source).value();
    saveClip(files, "blend-target.dotanimation", Clip::create(std::move(counter)).value()).value();
    auto target = loadClip(files, "blend-target.dotanimation").value();
    auto held = Clip::create(std::move(hold)).value();
    const auto camera = scene.createEntity("Camera").value();
    scene
        .setCamera(camera, World::Camera{.projection = World::Projection::Orthographic,
                                         .orthographicHeight = 4,
                                         .primary = true})
        .value();
    scene.setTransform(camera, {{0, 0, 5}}).value();
    const auto light = scene.createEntity("Key light").value();
    scene
        .setLight(light, World::Light{.intensity = 2,
                                      .shadows = Graphics::ShadowSettings{.resolution = 128,
                                                                          .cascades = 1,
                                                                          .distance = 15}})
        .value();
    scene.setTransform(light, {{}, Math::rotationEulerDegrees({-30, -20, 0}).value()}).value();
    scene.setOcclusion(Graphics::OcclusionSettings{}).value();
    auto animator = SceneAnimator::bind(scene, source, bundle.bindings).value();
    animator.configure(1, false).value();
    auto renderer = Graphics::Renderer::create().value();
    const auto baseline = renderer.resourceStats().value();

    {
      auto drawing = Graphics::SceneRenderer::create(renderer, assets).value();
      const auto rest = drawing.render(scene, 256, 256).value();
      animator.seek(scene, .5).value();
      const auto initial = drawing.render(scene, 256, 256).value();
      animator.crossFade(scene, target, 2).value();
      require(drawing.render(scene, 256, 256)->pixels == initial.pixels,
              "Starting a fade must preserve the visible skin pose");
      const auto verify = [&](double degrees, const std::string& name) {
        auto reference = World::decodeScene(World::encodeScene(scene).value()).value();
        const auto rotation
            = Math::rotationEulerDegrees({0, 0, static_cast<float>(degrees)}).value();
        std::vector<World::TransformUpdate> updates;

        for (const auto& track : source.description().value()->tracks) {
          const auto binding = std::ranges::find(bundle.bindings, track.node, &NodeBinding::node);
          require(binding != bundle.bindings.end(), "Independent joint reference is missing");
          const auto entity = reference.findEntity(binding->entity.value).value();
          auto pose = reference.transform(entity).value();
          pose.rotation = rotation;
          updates.push_back({entity, pose});
        }

        reference.setTransforms(updates).value();
        const auto referenceOwner = reference.findEntity(owner.id.value).value();
        auto bound = SceneSkin::bind(reference, referenceOwner, skin).value();
        auto cpu = Graphics::deformMesh(mesh, skin, bound.palette(reference).value()).value();
        Assets::saveMesh(assets, "assets/reference.dotmesh", cpu).value();
        reference.setSkin(referenceOwner, {}).value();
        reference
            .setMeshRenderer(referenceOwner, World::MeshRenderer{World::MeshKind::Asset,
                                                                 "assets/reference.dotmesh"})
            .value();
        auto cpuDrawing = Graphics::SceneRenderer::create(renderer, assets).value();
        const auto expected = cpuDrawing.render(reference, 256, 256).value();
        const auto actual = drawing.render(scene, 256, 256).value();
        unsigned difference{};

        for (std::size_t i = 0; i < actual.pixels.size(); ++i) {
          difference
              += static_cast<unsigned>(std::abs(int(actual.pixels[i]) - int(expected.pixels[i])));
        }

        require(difference <= 2048, "Cross-faded skin differs from independent angle/CPU pixels");
        files.writeAtomic(name, Assets::encodeImage(actual).value()).value();
        require(drawing.stats()->skins == 1, "Cross-fade must reuse one actual scene GPU skin");
      };

      for (int frame = 0; frame <= 4; ++frame) {
        if (frame != 0) {
          animator.advance(scene, .25).value();
        }

        const auto time = double(frame) * .25;
        const auto weight = time / 2;
        verify((1 - weight) * (.5 + time) * 30 - weight * time * 30,
               "blend-" + std::to_string(frame) + ".png");
      }

      const auto interrupted = drawing.render(scene, 256, 256).value();
      animator.crossFade(scene, held, 1).value();
      require(drawing.render(scene, 256, 256)->pixels == interrupted.pixels,
              "Interrupted fade must preserve the visible composite skin pose");

      for (int frame = 0; frame <= 4; ++frame) {
        if (frame != 0) {
          animator.advance(scene, .25).value();
        }

        const auto weight = double(frame) * .25;
        verify(std::lerp(7.5, 60.0, weight), "blend-interrupt-" + std::to_string(frame) + ".png");
      }

      require(!animator.state()->transition && !animator.state()->playing,
              "Constant nonlooping destination must pause when the fade completes");
      require(drawing.render(scene, 256, 256)->pixels != initial.pixels,
              "Completed joint cross-fade must change visible geometry");
      animator.stop(scene).value();
      require(drawing.render(scene, 256, 256)->pixels == rest.pixels,
              "Stopping a cross-fade must restore every affected bind channel");
    }

    const auto after = renderer.resourceStats().value();
    require(after.meshes == baseline.meshes && after.vertexBytes == baseline.vertexBytes
                && after.indexBytes == baseline.indexBytes && after.skins == baseline.skins
                && after.skinBytes == baseline.skinBytes && after.targets == baseline.targets
                && after.textures == baseline.textures,
            "Cross-faded skin acceptance must release every drawing resource");
    std::println(
        "Animation blend acceptance passed: imported eight-joint skin, moving cross-fade, "
        "interruption, ten angle/CPU-compared GPU frames and bind/resource restoration");
    return 0;
  }
}  // namespace Dot::Runtime
