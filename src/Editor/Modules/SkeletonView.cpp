module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <utility>

module Dot.Editor.SkeletonView;

import Dot.Animation.SkinFiles;
import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Graphics.Overlay;
import Dot.Math;

namespace Dot::Editor {
  SkeletonView::SkeletonView() : m_thread(std::this_thread::get_id()) {
  }

  auto SkeletonView::check() const -> Core::Result<void> {
    return m_thread == std::this_thread::get_id()
               ? Core::Result<void>{}
               : Core::fail(Core::ErrorCode::InvalidState, "Skeleton viewer requires owner thread");
  }

  auto SkeletonView::prepare(const World::Scene& scene, const Core::FileSystem& assets,
                             std::uint64_t entity) const
      -> Core::Result<std::pair<Cache, Animation::SkeletonPose>> {
    try {
      auto owner = scene.findEntity(entity);
      auto info = owner ? scene.entity(*owner)
                        : Core::Result<World::EntityInfo>{std::unexpected(owner.error())};

      if (!info) {
        return std::unexpected(info.error());
      }

      if (!info->mesh || !info->skin || !info->skin->enabled) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Choose an enabled mesh skin");
      }

      const auto& path = info->skin->asset;
      auto skin = m_cache && m_cache->root == assets.root() && m_cache->asset == path
                      ? Core::Result<Animation::Skin>{m_cache->skin}
                      : Animation::loadSkin(
                            assets, std::filesystem::path(std::u8string(path.begin(), path.end())));

      if (!skin) {
        return std::unexpected(skin.error());
      }

      auto pose = Animation::sampleSkeleton(scene, *owner, *skin);

      if (!pose) {
        return std::unexpected(pose.error());
      }

      return std::pair{Cache{assets.root(), path, std::move(*skin)}, std::move(*pose)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare skeleton view: " + std::string(error.what()));
    }
  }

  auto SkeletonView::set(const World::Scene& scene, const Core::FileSystem& assets,
                         std::optional<std::uint64_t> entity) -> Core::Result<void> {
    auto valid = check();

    if (!valid) {
      return valid;
    }

    if (entity) {
      auto prepared = prepare(scene, assets, *entity);

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      m_cache = std::move(prepared->first);
    } else {
      m_cache.reset();
    }

    m_entity = entity;
    m_selected.reset();
    m_frame.reset();
    m_error.reset();
    return {};
  }

  auto SkeletonView::select(const World::Scene& scene, const Core::FileSystem& assets,
                            std::optional<std::uint64_t> joint) -> Core::Result<void> {
    auto valid = check();

    if (!valid) {
      return valid;
    }

    if (!m_entity) {
      return Core::fail(Core::ErrorCode::InvalidState, "Enable a skeleton before selecting joints");
    }

    auto prepared = prepare(scene, assets, *m_entity);

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    if (joint && std::ranges::none_of(prepared->second.joints, [&](const auto& value) {
          return value.entity.value == *joint;
        })) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Joint does not belong to this skin");
    }

    m_cache = std::move(prepared->first);
    m_selected = joint;
    m_frame.reset();
    m_error.reset();
    return {};
  }

  auto SkeletonView::bounds(const World::Scene& scene, const Core::FileSystem& assets,
                            std::optional<std::uint64_t> joint) -> Core::Result<Graphics::Bounds> {
    auto valid = check();

    if (!valid || !m_entity) {
      return valid ? Core::fail(Core::ErrorCode::InvalidState, "No enabled skeleton")
                   : std::unexpected(valid.error());
    }

    auto prepared = prepare(scene, assets, *m_entity);

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    const auto& joints = prepared->second.joints;
    auto first = joints.begin();

    if (joint) {
      first = std::ranges::find_if(joints,
                                   [&](const auto& value) { return value.entity.value == *joint; });

      if (first == joints.end()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Joint does not belong to this skin");
      }
    }

    Graphics::Bounds result{first->position, first->position};
    const auto include = [&](Math::Vec3 point) {
      result.minimum = {std::min(result.minimum.x, point.x), std::min(result.minimum.y, point.y),
                        std::min(result.minimum.z, point.z)};
      result.maximum = {std::max(result.maximum.x, point.x), std::max(result.maximum.y, point.y),
                        std::max(result.maximum.z, point.z)};
    };

    if (joint) {
      if (first->parent) {
        include(joints[*first->parent].position);
      }
    } else {
      for (const auto& value : joints) {
        include(value.position);
      }
    }

    const auto size = result.maximum - result.minimum;
    const auto padding = std::max(.03F, std::max({size.x, size.y, size.z}) * .05F);
    result.minimum = result.minimum - Math::Vec3{padding, padding, padding};
    result.maximum = result.maximum + Math::Vec3{padding, padding, padding};

    if (!Math::isFinite(result.minimum) || !Math::isFinite(result.maximum)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Skeleton focus bounds overflow");
    }

    return result;
  }

  auto SkeletonView::sample(const World::Scene& scene, const Core::FileSystem& assets)
      -> Core::Result<std::optional<Animation::SkeletonPose>> {
    auto valid = check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (!m_entity) {
      return std::nullopt;
    }

    try {
      auto prepared = prepare(scene, assets, *m_entity);

      if (!prepared) {
        m_frame.reset();
        m_error = prepared.error();
        return std::unexpected(prepared.error());
      }

      m_cache = std::move(prepared->first);
      return std::optional{std::move(prepared->second)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Sample skeleton view: " + std::string(error.what()));
    }
  }

  auto SkeletonView::render(const World::Scene& scene, const Core::FileSystem& assets,
                            const Math::Mat4& viewProjection, std::uint64_t serial)
      -> Core::Result<Graphics::ViewportOverlay> {
    auto pose = sample(scene, assets);
    return pose ? renderPose(std::move(*pose), viewProjection, serial)
                : Core::Result<Graphics::ViewportOverlay>{std::unexpected(pose.error())};
  }

  auto SkeletonView::renderPose(std::optional<Animation::SkeletonPose> pose,
                                const Math::Mat4& viewProjection, std::uint64_t serial)
      -> Core::Result<Graphics::ViewportOverlay> {
    auto valid = check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (!m_entity || !pose) {
      m_frame.reset();
      return Graphics::ViewportOverlay{};
    }

    auto render = [&]() -> Core::Result<Graphics::ViewportOverlay> {
      if (!serial) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Skeleton frame serial must be positive");
      }

      if (pose->owner.value != *m_entity || !pose->owner.scene || pose->joints.empty()
          || pose->joints.size() > 1024 || pose->asset.size() > 4096
          || !Core::isValidUtf8(pose->asset)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid skeleton snapshot owner/count");
      }

      std::set<std::uint64_t> ids;

      for (std::size_t i = 0; i < pose->joints.size(); ++i) {
        const auto& joint = pose->joints[i];

        if (!joint.entity.value || joint.entity.scene != pose->owner.scene
            || !ids.insert(joint.entity.value).second || !Math::isFinite(joint.position)
            || joint.name.size() > 4096 || !Core::isValidUtf8(joint.name)
            || (joint.parent
                && (*joint.parent >= pose->joints.size() || *joint.parent == i || !joint.parentLinks
                    || joint.parentLinks > 256))
            || (!joint.parent && joint.parentLinks)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid skeleton snapshot joint");
        }

        auto ancestor = joint.parent;
        std::size_t depth{};

        while (ancestor) {
          if (*ancestor >= pose->joints.size() || ++depth > 256) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Cyclic skeleton snapshot");
          }

          ancestor = pose->joints[*ancestor].parent;
        }
      }

      auto projector = Graphics::OverlayProjector::create(viewProjection);

      if (!projector) {
        return std::unexpected(projector.error());
      }

      SkeletonViewFrame frame{serial, std::move(*pose), {}, {}};
      frame.projected.reserve(frame.pose.joints.size());
      const bool selectedAlive
          = !m_selected || std::ranges::any_of(frame.pose.joints, [&](const auto& joint) {
              return joint.entity.value == *m_selected;
            });

      for (const auto& joint : frame.pose.joints) {
        auto point = projector->point(joint.position);

        if (!point) {
          return std::unexpected(point.error());
        }

        frame.projected.push_back(*point);
        const bool selected = m_selected && *m_selected == joint.entity.value;
        const Math::Vec4 color = selected ? Math::Vec4{1, .8F, .1F, 1} : Math::Vec4{.1F, 1, .9F, 1};

        if (*point) {
          frame.overlay.points.push_back({**point, color, selected ? 4.F : 2.5F});
        }

        if (joint.parent) {
          auto line = projector->segment(frame.pose.joints[*joint.parent].position, joint.position);

          if (!line) {
            return std::unexpected(line.error());
          }

          if (*line) {
            frame.overlay.lines.push_back({**line, color, selected ? 3.F : 1.5F});
          }
        }
      }

      auto overlay = frame.overlay;
      m_frame = std::move(frame);
      m_error.reset();

      if (!selectedAlive) {
        m_selected.reset();
      }

      return overlay;
    };

    try {
      auto result = render();

      if (!result) {
        m_frame.reset();
        m_error = result.error();
      }

      return result;
    } catch (const std::exception& error) {
      m_frame.reset();
      m_error = Core::fail(Core::ErrorCode::External,
                           "Render skeleton view: " + std::string(error.what()))
                    .error();
      return std::unexpected(*m_error);
    }
  }

  auto SkeletonView::invalidateAssets() -> Core::Result<void> {
    auto valid = check();

    if (!valid) {
      return valid;
    }

    m_cache.reset();
    m_frame.reset();
    m_error.reset();
    return {};
  }

  auto SkeletonView::stats() const -> Core::Result<SkeletonViewStats> {
    auto valid = check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      return SkeletonViewStats{m_entity, m_selected, m_frame, m_error};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Inspect skeleton view: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor
