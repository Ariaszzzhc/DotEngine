module;

#include <cmath>
#include <expected>
#include <memory>
#include <numbers>
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

  namespace {
    auto ringAngle(const Math::Mat4& clipToFrame, int axis, Math::Vec2 uv) -> Core::Result<double> {
      if (axis < 0 || axis > 2 || !std::isfinite(uv.x) || !std::isfinite(uv.y) || uv.x < 0
          || uv.x > 1 || uv.y < 0 || uv.y > 1) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid rotation axis/UV");
      }

      auto near = Math::transformPoint(clipToFrame, {2 * uv.x - 1, 1 - 2 * uv.y, 0});
      auto far = Math::transformPoint(clipToFrame, {2 * uv.x - 1, 1 - 2 * uv.y, 1});

      if (!near || !far) {
        return std::unexpected(!near ? near.error() : far.error());
      }

      const double a[3]{near->x, near->y, near->z};
      const double b[3]{far->x - near->x, far->y - near->y, far->z - near->z};
      const double magnitude = Math::length(*far - *near);

      if (magnitude == 0 || std::abs(b[axis]) / magnitude < 1e-4) {
        return Core::fail(Core::ErrorCode::Unavailable, "Rotation plane is parallel to the ray");
      }

      const double t = -a[axis] / b[axis];

      if (!std::isfinite(t) || t < 0 || t > 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "Rotation plane is outside the clip range");
      }

      const int first = (axis + 1) % 3;
      const int second = (axis + 2) % 3;
      const double x = a[first] + t * b[first];
      const double y = a[second] + t * b[second];

      if (std::hypot(x, y) < 1e-6) {
        return Core::fail(Core::ErrorCode::Unavailable, "Rotation sample is at the pivot");
      }

      return std::atan2(y, x);
    }
  }  // namespace

  auto RotationDrag::begin(const Math::Mat4& viewProjection, const Math::Mat4& frame, int axis,
                           Math::Vec2 uv) -> Core::Result<RotationDrag> {
    if (!Math::isFinite(frame) || std::abs(frame.data[3]) > 1e-6F || std::abs(frame.data[7]) > 1e-6F
        || std::abs(frame.data[11]) > 1e-6F || std::abs(frame.data[15] - 1) > 1e-6F) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Rotation operation frame must be affine");
    }

    auto inverseView = Math::inverse(viewProjection);
    auto inverseFrame = Math::inverse(frame);

    if (!inverseView || !inverseFrame) {
      return std::unexpected(!inverseView ? inverseView.error() : inverseFrame.error());
    }

    auto matrix = Math::multiply(*inverseFrame, *inverseView);

    if (!matrix) {
      return std::unexpected(matrix.error());
    }

    auto angle = ringAngle(*matrix, axis, uv);

    if (!angle) {
      return std::unexpected(angle.error());
    }

    RotationDrag result;
    result.m_clipToFrame = *matrix;
    result.m_axis = axis;
    result.m_previous = *angle;
    return result;
  }

  auto RotationDrag::degrees(Math::Vec2 uv, float snap) -> Core::Result<float> {
    if (!std::isfinite(snap) || snap < 0 || snap > 360) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Rotation snap must be within 0..360 degrees");
    }

    auto angle = ringAngle(m_clipToFrame, m_axis, uv);

    if (!angle) {
      return std::unexpected(angle.error());
    }

    const double total = m_total + std::remainder(*angle - m_previous, 2 * std::numbers::pi);
    double degrees = total * 180 / std::numbers::pi;

    if (snap > 0) {
      degrees = std::round(degrees / snap) * snap;
    }

    if (!std::isfinite(degrees) || std::abs(degrees) > 36000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Rotation exceeds 100 turns");
    }

    m_previous = *angle;
    m_total = total;
    return static_cast<float>(degrees);
  }

  auto transformFrame(const World::Scene& scene, World::EntityId entity, bool local)
      -> Core::Result<Math::Mat4> {
    auto info = scene.entity(entity);

    if (!info) {
      return std::unexpected(info.error());
    }

    if (!local) {
      auto world = scene.worldTransform(entity);
      return world ? Math::compose({world->data[12], world->data[13], world->data[14]})
                   : std::unexpected(world.error());
    }

    auto pose = Math::compose(info->transform.position, info->transform.rotation);

    if (!pose || !info->parent) {
      return pose;
    }

    auto parent = scene.worldTransform(*info->parent);
    return parent ? Math::multiply(*parent, *pose) : std::unexpected(parent.error());
  }

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

  auto TransformGesture::rotate(int axis, float degrees, bool local) -> Core::Result<void> {
    auto draft = scene();

    if (!draft) {
      return std::unexpected(draft.error());
    }

    if (axis < 0 || axis > 2 || !std::isfinite(degrees) || std::abs(degrees) > 36000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid gesture rotation");
    }

    if (degrees == 0) {
      return update(m_impl->initialWorld);
    }

    auto delta = Math::rotationEulerDegrees(
        {axis == 0 ? degrees : 0, axis == 1 ? degrees : 0, axis == 2 ? degrees : 0});
    auto rotation = Math::compose({}, *delta);

    if (local) {
      auto original = Math::compose({}, m_impl->original.rotation);
      auto rotated = Math::multiply(*original, *rotation);
      auto parts = rotated ? Math::decomposeTrs(*rotated) : std::unexpected(rotated.error());

      if (!parts) {
        return std::unexpected(parts.error());
      }

      auto next = m_impl->original;
      next.rotation = parts->rotation;
      return m_impl->draft.setTransform(m_impl->handle, next);
    }

    auto rotated = Math::multiply(*rotation, m_impl->initialWorld);

    if (!rotated) {
      return std::unexpected(rotated.error());
    }

    for (std::size_t column = 12; column < 15; ++column) {
      rotated->data[column] = m_impl->initialWorld.data[column];
    }

    return update(*rotated);
  }

  auto TransformGesture::scale(int axis, float factor, bool local) -> Core::Result<void> {
    auto draft = scene();

    if (!draft) {
      return std::unexpected(draft.error());
    }

    if (axis < 0 || axis > 2 || !std::isfinite(factor) || factor < 0.001F || factor > 1000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Scale factor must be within .001..1000");
    }

    if (factor == 1) {
      return update(m_impl->initialWorld);
    }

    if (local) {
      auto next = m_impl->original;

      if (axis == 0) {
        next.scale.x *= factor;
      } else if (axis == 1) {
        next.scale.y *= factor;
      } else {
        next.scale.z *= factor;
      }

      return m_impl->draft.setTransform(m_impl->handle, next);
    }

    auto delta = Math::compose(
        {}, {}, {axis == 0 ? factor : 1, axis == 1 ? factor : 1, axis == 2 ? factor : 1});
    auto scaled = Math::multiply(*delta, m_impl->initialWorld);

    if (!scaled) {
      return std::unexpected(scaled.error());
    }

    for (std::size_t column = 12; column < 15; ++column) {
      scaled->data[column] = m_impl->initialWorld.data[column];
    }

    return update(*scaled);
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
