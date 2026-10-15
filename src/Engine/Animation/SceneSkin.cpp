module;

#include <exception>
#include <expected>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Dot.Animation.SceneSkin;

namespace Dot::Animation {
  struct SceneSkin::Impl {
    Skin skin;
    World::EntityId owner;
    World::SkinBinding properties;
    std::thread::id thread{std::this_thread::get_id()};

    auto check() const -> Core::Result<void> {
      return thread == std::this_thread::get_id()
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState, "Scene skin requires owner thread");
    }
  };

  SceneSkin::SceneSkin(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
  }

  SceneSkin::SceneSkin(SceneSkin&&) noexcept = default;

  auto SceneSkin::operator=(SceneSkin&&) noexcept -> SceneSkin& = default;

  SceneSkin::~SceneSkin() = default;

  auto SceneSkin::bind(const World::Scene& scene, World::EntityId owner, Skin skin)
      -> Core::Result<SceneSkin> {
    auto entity = scene.entity(owner);

    if (!entity) {
      return std::unexpected(entity.error());
    }

    auto data = skin.description();

    if (!data) {
      return std::unexpected(data.error());
    }

    if (!entity->skin || !entity->skin->enabled
        || entity->skin->joints.size() != (*data)->inverseBindMatrices.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Enabled scene skin and exact joint count are required");
    }

    try {
      auto impl = std::make_unique<Impl>();
      impl->skin = std::move(skin);
      impl->owner = owner;
      impl->properties = std::move(*entity->skin);
      SceneSkin result{std::move(impl)};
      auto palette = result.palette(scene);
      return palette ? Core::Result<SceneSkin>{std::move(result)}
                     : std::unexpected(palette.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Bind scene skin: " + std::string(error.what()));
    }
  }

  auto SceneSkin::skin() const -> Core::Result<Skin> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene skin was moved");
    }

    auto valid = m_impl->check();
    return valid ? Core::Result<Skin>{m_impl->skin} : std::unexpected(valid.error());
  }

  auto SceneSkin::palette(const World::Scene& scene) const
      -> Core::Result<std::vector<Math::Mat4>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene skin was moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto entity = scene.entity(m_impl->owner);

    if (!entity) {
      return std::unexpected(entity.error());
    }

    if (!entity->skin || *entity->skin != m_impl->properties) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene skin binding changed");
    }

    auto mesh = scene.worldTransform(m_impl->owner);

    if (!mesh) {
      return std::unexpected(mesh.error());
    }

    try {
      std::vector<Math::Mat4> joints;
      joints.reserve(m_impl->properties.joints.size());

      for (const auto value : m_impl->properties.joints) {
        auto world = scene.worldTransform({m_impl->owner.scene, value});

        if (!world) {
          return Core::fail(world.error().code,
                            "Skin joint " + std::to_string(value) + ": " + world.error().message);
        }

        joints.push_back(*world);
      }

      return m_impl->skin.palette(*mesh, joints);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Scene skin palette: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation
