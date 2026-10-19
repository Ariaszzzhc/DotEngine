module;

#include <array>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>

module Dot.Editor.AnimationClipboard;

import Dot.Core.Paths;
import Dot.Core.Result;

namespace Dot::Editor {
  namespace {
    auto validId(std::uint64_t id) -> bool {
      return id > 0 && id < std::numeric_limits<std::uint64_t>::max();
    }

    auto validateSource(const AnimationClipboardSource& source) -> Core::Result<void> {
      const auto& context = source.context;

      if (!validId(context.scene) || !validId(context.owner) || !validId(source.buffer)
          || !validId(source.revision) || source.path.size() > 1024
          || !source.path.starts_with("assets/") || !source.path.ends_with(".dotanimation")
          || !Core::isPortableRelativePath(source.path) || context.bindings.empty()
          || context.bindings.size() > 10000) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid animation clipboard provenance");
      }

      std::set<std::uint64_t> entities;

      for (const auto& [node, entity] : context.bindings) {
        if (node >= 10000 || !validId(entity) || !entities.insert(entity).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid clipboard node/entity binding");
        }
      }

      return {};
    }
  }  // namespace

  auto AnimationClipboardContext::operator==(const AnimationClipboardContext& other) const -> bool {
    return scene == other.scene && owner == other.owner && bindings == other.bindings;
  }

  auto AnimationClipboard::copy(const Animation::Clip& value,
                                std::span<const Animation::KeyReference> selection,
                                const AnimationClipboardSource& source)
      -> Core::Result<std::shared_ptr<const AnimationClipboardSnapshot>> {
    try {
      auto valid = validateSource(source);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      if (m_serial >= std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::InvalidState, "Animation clipboard identity exhausted");
      }

      auto keys = Animation::KeyGroup::create(value, selection);

      if (!keys) {
        return std::unexpected(keys.error());
      }

      for (const auto& item : keys->description().value()->keys) {
        if (!source.context.bindings.contains(item.node)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Copied animation node is not bound");
        }
      }

      auto next = std::make_shared<AnimationClipboardSnapshot>(
          AnimationClipboardSnapshot{m_serial + 1, source, std::move(*keys)});
      m_value = std::move(next);
      ++m_serial;
      return m_value;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Copy animation selection: " + std::string(error.what()));
    }
  }

  auto AnimationClipboard::snapshot() const noexcept
      -> std::shared_ptr<const AnimationClipboardSnapshot> {
    return m_value;
  }

  auto AnimationClipboard::read(std::uint64_t id) const
      -> Core::Result<std::shared_ptr<const AnimationClipboardSnapshot>> {
    if (!m_value || id != m_value->id) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation clipboard is empty or stale");
    }

    return m_value;
  }

  auto AnimationClipboard::clear(std::uint64_t id) -> Core::Result<void> {
    auto value = read(id);

    if (!value) {
      return std::unexpected(value.error());
    }

    m_value.reset();
    return {};
  }

  auto AnimationClipboard::compatible(const AnimationClipboardContext& context) const -> bool {
    return m_value && m_value->source.context == context;
  }

  auto AnimationClipboard::prepare(const Animation::Clip& destination,
                                   const AnimationClipboardContext& context, std::uint64_t id,
                                   float time, Animation::KeyCollision collision) const
      -> Core::Result<AnimationPaste> {
    auto copied = read(id);

    if (!copied) {
      return std::unexpected(copied.error());
    }

    if (!compatible(context)) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Copied keys belong to another scene/player binding");
    }

    try {
      const auto placed = (*copied)->keys.at(time);

      if (!placed) {
        return std::unexpected(placed.error());
      }

      const std::array<Animation::ClipEdit, 1> edits{
          Animation::PasteKeys{(*copied)->keys, time, collision}};
      auto value = Animation::editClip(destination, edits);

      if (!value) {
        return std::unexpected(value.error());
      }

      AnimationPaste result{std::move(*value), {}};
      result.selection.reserve(placed->size());

      for (const auto& item : *placed) {
        result.selection.push_back({item.node, item.path, item.key.time});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare animation paste: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor
