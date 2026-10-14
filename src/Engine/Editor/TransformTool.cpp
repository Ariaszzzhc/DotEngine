module;

#include <cmath>
#include <expected>
#include <memory>
#include <thread>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.Editor.TransformTool;

import Dot.World.SceneFiles;

namespace Dot::Editor {
  namespace {
    auto parameter(const Math::Mat4& inverse, Math::Vec3 origin, Math::Vec3 axis, Math::Vec2 uv)
        -> Core::Result<double> {
      if (!std::isfinite(uv.x) || !std::isfinite(uv.y) || uv.x < 0 || uv.x > 1 || uv.y < 0
          || uv.y > 1) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Axis drag UV must be within [0,1]");
      }

      const auto near = Math::transformPoint(inverse, {2 * uv.x - 1, 1 - 2 * uv.y, 0});
      const auto far = Math::transformPoint(inverse, {2 * uv.x - 1, 1 - 2 * uv.y, 1});

      if (!near || !far) {
        return std::unexpected(!near ? near.error() : far.error());
      }

      const auto direction = Math::normalized(*far - *near);

      if (!direction) {
        return std::unexpected(direction.error());
      }

      const double b = Math::dot(axis, *direction);
      const double denominator = 1 - b * b;

      if (denominator < 1e-4) {
        return Core::fail(Core::ErrorCode::Unavailable, "Axis is parallel to the viewport ray");
      }

      const auto offset = origin - *near;
      return (b * Math::dot(*direction, offset) - Math::dot(axis, offset)) / denominator;
    }
  }  // namespace

  auto AxisDrag::begin(const Math::Mat4& viewProjection, Math::Vec3 origin, Math::Vec3 axis,
                       Math::Vec2 uv) -> Core::Result<AxisDrag> {
    if (!Math::isFinite(origin)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid axis origin");
    }

    auto inverse = Math::inverse(viewProjection);
    auto direction = Math::normalized(axis);

    if (!inverse || !direction) {
      return std::unexpected(!inverse ? inverse.error() : direction.error());
    }

    auto start = parameter(*inverse, origin, *direction, uv);

    if (!start) {
      return std::unexpected(start.error());
    }

    AxisDrag result;
    result.m_inverse = *inverse;
    result.m_origin = origin;
    result.m_axis = *direction;
    result.m_start = *start;
    return result;
  }

  auto AxisDrag::delta(Math::Vec2 uv, float snap) const -> Core::Result<Math::Vec3> {
    if (!std::isfinite(snap) || snap < 0 || snap > 1e6) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid axis snap distance");
    }

    auto current = parameter(m_inverse, m_origin, m_axis, uv);

    if (!current) {
      return std::unexpected(current.error());
    }

    double amount = *current - m_start;

    if (snap > 0) {
      amount = std::round(amount / snap) * snap;
    }

    if (!std::isfinite(amount) || std::abs(amount) > 1e6) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Axis movement exceeds world limits");
    }

    return m_axis * static_cast<float>(amount);
  }

  struct TransformGesture::Impl {
    const Document* owner;
    std::uint64_t revision;
    std::uint64_t scope;
    std::uint64_t entity;
    World::Scene draft;
    World::EntityId handle;
    Math::Mat4 parentInverse;
    Math::Mat4 initialWorld;
    World::Transform original;
    std::thread::id thread{std::this_thread::get_id()};

    auto check() const -> Core::Result<void> {
      if (thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Transform gesture requires owner thread");
      }

      return {};
    }
  };

  TransformGesture::TransformGesture(std::unique_ptr<Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  TransformGesture::TransformGesture(TransformGesture&&) noexcept = default;
  auto TransformGesture::operator=(TransformGesture&&) noexcept -> TransformGesture& = default;
  TransformGesture::~TransformGesture() = default;

  auto TransformGesture::begin(const Document& document, std::uint64_t revision,
                               std::uint64_t entity) -> Core::Result<TransformGesture> {
    auto stats = document.stats();

    if (!stats) {
      return std::unexpected(stats.error());
    }

    if (stats->revision != revision) {
      return Core::fail(Core::ErrorCode::InvalidState, "Transform gesture has stale revision");
    }

    auto source = document.scene();

    if (!source) {
      return std::unexpected(source.error());
    }

    auto handle = (*source)->findEntity(entity);

    if (!handle) {
      return std::unexpected(handle.error());
    }

    auto info = (*source)->entity(*handle);

    if (!info) {
      return std::unexpected(info.error());
    }

    auto world = (*source)->worldTransform(*handle);
    Core::Result<Math::Mat4> parentInverse = Math::Mat4{};

    if (info->parent) {
      auto parent = (*source)->worldTransform(*info->parent);
      parentInverse = parent ? Math::inverse(*parent) : std::unexpected(parent.error());
    }

    if (!world || !parentInverse) {
      return std::unexpected(!world ? world.error() : parentInverse.error());
    }

    auto encoded = World::encodeScene(**source);

    if (!encoded) {
      return std::unexpected(encoded.error());
    }

    auto draft = World::decodeScene(*encoded);

    if (!draft) {
      return std::unexpected(draft.error());
    }

    auto draftHandle = draft->findEntity(entity).value();
    return TransformGesture(std::make_unique<Impl>(&document, revision, stats->scene, entity,
                                                   std::move(*draft), draftHandle, *parentInverse,
                                                   *world, info->transform));
  }

  auto TransformGesture::update(const Math::Mat4& world) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Transform gesture is moved or committed");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (world == m_impl->initialWorld) {
      return m_impl->draft.setTransform(m_impl->handle, m_impl->original);
    }

    auto local = Math::multiply(m_impl->parentInverse, world);
    auto parts = local ? Math::decomposeTrs(*local) : std::unexpected(local.error());

    if (!parts) {
      return Core::fail(parts.error().code, "Transform cannot be represented as parent-local TRS: "
                                                + parts.error().message);
    }

    return m_impl->draft.setTransform(m_impl->handle,
                                      {parts->position, parts->rotation, parts->scale});
  }

  auto TransformGesture::scene() const -> Core::Result<const World::Scene*> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Transform gesture is moved or committed");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<const World::Scene*>(&m_impl->draft)
                   : std::unexpected(checked.error());
  }

  auto TransformGesture::world() const -> Core::Result<Math::Mat4> {
    auto draft = scene();
    return draft ? (*draft)->worldTransform(m_impl->handle) : std::unexpected(draft.error());
  }

  auto TransformGesture::entity() const noexcept -> std::uint64_t {
    return m_impl ? m_impl->entity : 0;
  }

  auto TransformGesture::revision() const noexcept -> std::uint64_t {
    return m_impl ? m_impl->revision : 0;
  }

  auto TransformGesture::commit(Document& document) -> Core::Result<EditReport> {
    auto draft = scene();

    if (!draft) {
      return std::unexpected(draft.error());
    }

    auto stats = document.stats();

    if (!stats) {
      return std::unexpected(stats.error());
    }

    if (&document != m_impl->owner || stats->revision != m_impl->revision
        || stats->scene != m_impl->scope) {
      return Core::fail(Core::ErrorCode::InvalidState, "Transform gesture document conflict");
    }

    const auto transform = (*draft)->entity(m_impl->handle)->transform;
    using Json = Serialization::Detail::Json;
    auto result = document.apply(
        Json::array(
            {{{"op", "transform"},
              {"entity", std::to_string(m_impl->entity)},
              {"value",
               {{"position", {transform.position.x, transform.position.y, transform.position.z}},
                {"rotation",
                 {transform.rotation.x, transform.rotation.y, transform.rotation.z,
                  transform.rotation.w}},
                {"scale", {transform.scale.x, transform.scale.y, transform.scale.z}}}}}})
            .dump(),
        m_impl->revision);

    if (result) {
      m_impl.reset();
    }

    return result;
  }
}  // namespace Dot::Editor
