#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <vector>

import Dot.Animation.Clip;
import Dot.Animation.SceneAnimator;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.World.Scene;
import Dot.World.SceneFiles;

TEST_CASE("animation.gpu_hierarchy_samples_match_independent_native_pose_and_release_resources") {
  using namespace Dot;
  using namespace Dot::Animation;
  auto storage = Core::StagedDirectory::create(
                     Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                     .value();
  auto renderer = Graphics::Renderer::create().value();
  const auto baseline = renderer.resourceStats().value();

  {
    auto scene = World::Scene::create("Animated assembly").value();
    auto camera = scene.createEntity("Camera").value();
    REQUIRE(scene.setCamera(camera, World::Camera{.projection = World::Projection::Orthographic,
                                                  .orthographicHeight = 4,
                                                  .primary = true}));
    REQUIRE(scene.setTransform(camera, {{0, 0, 5}}));
    auto parent = scene.createEntity("Body").value();
    auto child = scene.createEntity("Panel").value();
    REQUIRE(
        scene.setMeshRenderer(parent, World::MeshRenderer{.asset = {}, .tint = {.2F, .6F, 1, 1}}));
    REQUIRE(
        scene.setMeshRenderer(child, World::MeshRenderer{.asset = {}, .tint = {1, .4F, .1F, 1}}));
    REQUIRE(scene.setTransform(parent, {{-1, 0, 0}}));
    REQUIRE(scene.setTransform(child, {{0, 1, 0}, {}, {.5F, .25F, .25F}}));
    REQUIRE(scene.setParent(child, parent));
    auto reference = World::decodeScene(World::encodeScene(scene).value()).value();
    REQUIRE(reference.setTransform(reference.findEntity(parent.value).value(), {{0, 0, 0}}));
    REQUIRE(
        reference.setTransform(reference.findEntity(child.value).value(),
                               {{0, 1, 0}, {0, 0, .70710678F, .70710678F}, {.75F, .375F, .25F}}));
    auto clip = Clip::create({"Mechanical motion",
                              {{0,
                                Path::Translation,
                                Interpolation::Linear,
                                {{0, {-1, 0, 0, 0}}, {2, {1, 0, 0, 0}}}},
                               {1,
                                Path::Rotation,
                                Interpolation::Step,
                                {{0, {0, 0, 0, 1}}, {1, {0, 0, .70710678F, .70710678F}}}},
                               {1,
                                Path::Scale,
                                Interpolation::Linear,
                                {{0, {.5F, .25F, .25F, 0}}, {2, {1, .5F, .25F, 0}}}}}})
                    .value();
    const std::array bindings{NodeBinding{0, parent}, NodeBinding{1, child}};
    auto animator = SceneAnimator::bind(scene, clip, bindings).value();
    auto drawing = Graphics::SceneRenderer::create(renderer, storage.files().value()).value();
    const auto initial = drawing.render(scene, 128, 128).value();
    const auto expected = drawing.render(reference, 128, 128).value();
    REQUIRE(animator.seek(scene, 1));
    const auto actual = drawing.render(scene, 128, 128).value();
    CHECK(actual.pixels == expected.pixels);
    CHECK(actual.pixels != initial.pixels);
    REQUIRE(animator.pause());
    REQUIRE(animator.advance(scene, 1));
    CHECK(drawing.render(scene, 128, 128)->pixels == actual.pixels);
    REQUIRE(animator.stop(scene));
    CHECK(drawing.render(scene, 128, 128)->pixels == initial.pixels);
    CHECK(scene.entity(camera)->transform.position.z == 5);
  }

  CHECK(renderer.resourceStats()->meshes == baseline.meshes);
  CHECK(renderer.resourceStats()->vertexBytes == baseline.vertexBytes);
  CHECK(renderer.resourceStats()->indexBytes == baseline.indexBytes);
  CHECK(renderer.resourceStats()->targets == baseline.targets);
  CHECK(renderer.resourceStats()->textures == baseline.textures);
}
