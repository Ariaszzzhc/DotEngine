#include <array>
#include <cstddef>
#include <cstdint>
#include <print>

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.Physics.Body;
import Dot.Physics.World;
import Dot.Physics.SceneSimulation;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;

namespace Dot::Runtime {
  namespace {
    auto pixel(const Graphics::Image& image, std::uint32_t x, std::uint32_t y)
        -> std::array<std::uint8_t, 4> {
      const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
      return {image.pixels.at(offset), image.pixels.at(offset + 1), image.pixels.at(offset + 2),
              image.pixels.at(offset + 3)};
    }
  }  // namespace

  auto testScenePhysics(bool render) -> int {
    auto scene = World::Scene::create("Falling Cube");

    if (!scene) {
      std::println(stderr, "{}", scene.error().message);
      return 1;
    }

    auto floor = scene->createEntity("Floor");
    auto body = scene->createEntity("Cube");
    auto camera = scene->createEntity("Camera");

    if (!floor || !body || !camera) {
      std::println(stderr, "Scene physics acceptance entity creation failed");
      return 1;
    }

    World::RigidBody fixed;
    fixed.motion = Physics::Motion::Static;
    fixed.shape = Physics::Shape::Box;
    auto moving = fixed;
    moving.motion = Physics::Motion::Dynamic;
    World::Camera view;
    view.primary = true;
    view.projection = World::Projection::Orthographic;
    view.orthographicHeight = 3;
    World::MeshRenderer mesh;
    mesh.tint = {0, 1, 0, 1};

    if (!scene->setTransform(*floor, {{0, -0.25F, 0}, {}, {6, 0.5F, 6}})
        || !scene->setTransform(*body, {{0, 1.5F, 0}, {}, {0.5F, 0.5F, 0.5F}})
        || !scene->setTransform(*camera, {{0, 0.75F, 5}}) || !scene->setRigidBody(*floor, fixed)
        || !scene->setRigidBody(*body, moving) || !scene->setCamera(*camera, view)
        || !scene->setMeshRenderer(*body, mesh)) {
      std::println(stderr, "Scene physics acceptance component setup failed");
      return 1;
    }

    auto simulation = Physics::SceneSimulation::create(*scene);

    if (!simulation) {
      std::println(stderr, "{}", simulation.error().message);
      return 1;
    }

    for (int frame = 0; frame < 180; ++frame) {
      auto step = simulation->step(*scene, 1.0 / 60);

      if (!step) {
        std::println(stderr, "{}", step.error().message);
        return 1;
      }
    }

    auto state = simulation->body(*body);
    auto ray = simulation->rayCast({0, 2, 0}, {0, -4, 0});
    auto contacts = simulation->drainContactEvents();
    bool began = false;

    if (contacts) {
      for (const auto& event : *contacts) {
        began = began
                || (event.phase == Physics::ContactPhase::Begin && event.first == *floor
                    && event.second == *body && event.geometry.has_value());
      }
    }

    if (!state || state->position.y < 0.20F || state->position.y > 0.28F || !ray
        || !ray->has_value() || (*ray)->entity != *body || !began) {
      std::println(stderr, "Scene physics acceptance resting pose/ray verification failed");
      return 1;
    }

    if (render) {
      auto renderer = Graphics::Renderer::create(
          nullptr,
          {.postProcess = {0, Graphics::ToneMap::LinearClamp, Graphics::ColorSpace::Linear}});

      if (!renderer) {
        std::println(stderr, "{}", renderer.error().message);
        return 1;
      }

      auto service = Graphics::SceneRenderer::create(*renderer);

      if (!service) {
        std::println(stderr, "{}", service.error().message);
        return 1;
      }

      auto image = service->render(*scene, 100, 100);

      if (!image || pixel(*image, 50, 25) != std::array<std::uint8_t, 4>{0, 0, 0, 255}
          || pixel(*image, 50, 67) != std::array<std::uint8_t, 4>{0, 255, 0, 255}) {
        std::println(stderr, "Scene physics acceptance pixel verification failed");
        return 1;
      }
    }

    auto removed = scene->destroyEntity(*body);

    if (!removed || !simulation->synchronize(*scene) || simulation->body(*body)
        || simulation->stats()->bodies != 1) {
      std::println(stderr, "Scene physics acceptance lifetime verification failed");
      return 1;
    }

    auto ended = simulation->drainContactEvents();

    if (!ended || ended->size() != 1 || ended->front().phase != Physics::ContactPhase::End
        || ended->front().endReason != Physics::ContactEndReason::Destroyed
        || ended->front().second != *body) {
      std::println(stderr, "Scene physics acceptance contact cleanup failed");
      return 1;
    }

    std::println("Scene physics acceptance passed: resting y={:.3f}; contact lifecycle; {}",
                 state->position.y, render ? "GPU pixels verified" : "synchronized body cleanup");
    return 0;
  }
}  // namespace Dot::Runtime
