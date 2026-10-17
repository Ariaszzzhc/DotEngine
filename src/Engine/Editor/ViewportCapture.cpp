module;

#include <cmath>
#include <expected>
#include <limits>
#include <utility>

module Dot.Editor.ViewportCapture;

import Dot.Core.Result;
import Dot.Graphics.Image;
import Dot.Math;

namespace Dot::Editor {
  ViewportCapture::ViewportCapture() = default;

  auto ViewportCapture::request(CaptureContext context, std::uint64_t after)
      -> Core::Result<std::uint64_t> {
    if (m_stats.state == CaptureState::Queued || m_stats.state == CaptureState::Ready
        || m_stats.id == std::numeric_limits<std::uint64_t>::max()
        || after == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Capture is pending, retained or exhausted");
    }

    if (!context.scene || !context.revision
        || (context.mode != ViewportMode::Editor && context.mode != ViewportMode::Game
            && context.mode != ViewportMode::Play)
        || (context.mode == ViewportMode::Play || context.animation) != (context.session != 0)
        || (context.transform && (context.mode != ViewportMode::Editor || context.animation))
        || (context.animation && context.mode == ViewportMode::Play)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid viewport capture context");
    }

    if ((context.hudRevision && context.mode != ViewportMode::Play)
        || context.hudRevision == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid capture HUD revision");
    }

    m_stats = {m_stats.id + 1, CaptureState::Queued, {context, after}, {}};
    return m_stats.id;
  }

  auto ViewportCapture::complete(std::uint64_t id, CaptureFrame frame, Graphics::Image image)
      -> Core::Result<void> {
    if (id != m_stats.id || m_stats.state != CaptureState::Queued
        || frame.serial <= m_stats.frame.serial || frame.context != m_stats.frame.context) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Capture frame is stale or belongs elsewhere");
    }

    auto valid = Graphics::validateImage(image);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (frame.width != image.width || frame.height != image.height
        || !Math::isFinite(frame.viewProjection)
        || (frame.context.mode != ViewportMode::Play && frame.playFrame)
        || frame.context.animation != frame.animation.has_value()
        || (frame.animation
            && (!frame.animation->owner || !frame.animation->generation
                || !std::isfinite(frame.animation->time) || frame.animation->time < 0
                || frame.animation->time > 86400))) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Capture metadata does not match pixels");
    }

    m_image = std::move(image);
    m_stats.frame = frame;
    m_stats.state = CaptureState::Ready;
    return {};
  }

  auto ViewportCapture::fail(std::uint64_t id, Core::Error error) -> Core::Result<void> {
    if (id != m_stats.id || m_stats.state != CaptureState::Queued) {
      return Core::fail(Core::ErrorCode::InvalidState, "No matching queued viewport capture");
    }

    m_stats.error = std::move(error);
    m_stats.state = CaptureState::Failed;
    m_image = {};
    return {};
  }

  auto ViewportCapture::discard(std::uint64_t id) -> Core::Result<void> {
    if (id != m_stats.id
        || (m_stats.state != CaptureState::Queued && m_stats.state != CaptureState::Ready)) {
      return Core::fail(Core::ErrorCode::InvalidState, "No matching retained viewport capture");
    }

    m_image = {};
    m_stats.state = CaptureState::Cancelled;
    return {};
  }

  auto ViewportCapture::image(std::uint64_t id) const -> Core::Result<const Graphics::Image*> {
    if (id != m_stats.id || m_stats.state != CaptureState::Ready) {
      return Core::fail(Core::ErrorCode::InvalidState, "No matching ready viewport capture");
    }

    return &m_image;
  }

  auto ViewportCapture::saved(std::uint64_t id) -> Core::Result<void> {
    auto ready = image(id);

    if (!ready) {
      return std::unexpected(ready.error());
    }

    m_image = {};
    m_stats.state = CaptureState::Saved;
    return {};
  }

  auto ViewportCapture::stats() const -> ViewportCaptureStats {
    return m_stats;
  }
}  // namespace Dot::Editor
