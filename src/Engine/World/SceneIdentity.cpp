module;

#include <algorithm>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

module Dot.World.SceneIdentity;

import Dot.Core.Result;
import Dot.Core.Paths;

namespace Dot::World {
  namespace {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    using Spaces = std::map<SceneSourceKey, const SceneSourceNamespace*>;

    auto sourcePath(std::string_view path) -> bool {
      return Core::isPortableRelativePath(path) && path.ends_with(".dotscene")
             && (path.starts_with("assets/") || path.starts_with("scenes/"))
             && path.find("/.dot-stage-") == std::string_view::npos;
    }

    auto validKey(const SceneSourceKey& key, std::size_t maximumSize) -> bool {
      return key.size() <= maximumSize
             && std::ranges::all_of(key, [](auto value) { return value != 0 && value != maximum; });
    }

    auto spaces(std::span<const SceneSourceNamespace> namespaces) -> Spaces {
      Spaces result;

      for (const auto& space : namespaces) {
        result.emplace(space.prefix, &space);
      }

      return result;
    }

    auto floor(const SceneSourceNamespace& space) -> std::uint64_t {
      return space.nextEntity.value_or(maximum);
    }
  }  // namespace

  auto validateSceneSourceIdentity(std::string_view asset,
                                   std::span<const SceneSourceOrigin> origins,
                                   std::span<const SceneSourceNamespace> namespaces)
      -> Core::Result<void> {
    try {
      if (!sourcePath(asset) || origins.size() > 10000 || namespaces.empty()
          || namespaces.size() > 10000) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid source identity path or capacity");
      }

      const auto indexed = spaces(namespaces);
      const auto root = indexed.find({});

      if (indexed.size() != namespaces.size() || root == indexed.end()
          || root->second->asset != asset) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Source identity requires one matching root namespace");
      }

      std::map<SceneSourceKey, std::set<std::uint64_t>> live;
      std::size_t observed{};

      for (const auto& space : namespaces) {
        observed += space.entities.size();

        if (!sourcePath(space.asset) || !validKey(space.prefix, 31) || observed > 100000
            || (space.nextEntity && (*space.nextEntity == 0 || *space.nextEntity == maximum))) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid source namespace: " + space.asset);
        }

        auto& values = live[space.prefix];

        for (auto value : space.entities) {
          if (value == 0 || value >= floor(space) || !values.insert(value).second) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Invalid source namespace live values: " + space.asset);
          }
        }
      }

      for (const auto& space : namespaces) {
        if (!space.prefix.empty()) {
          auto parent = space.prefix;
          const auto value = parent.back();
          parent.pop_back();
          const auto owner = indexed.find(parent);

          if (owner == indexed.end() || !live.at(parent).contains(value)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Missing namespace parent identity: " + space.asset);
          }
        }
      }

      std::set<std::uint64_t> entities;
      std::set<SceneSourceKey> keys;

      for (const auto& origin : origins) {
        if (origin.entity == 0 || origin.entity == maximum || origin.key.empty()
            || !validKey(origin.key, 32) || !entities.insert(origin.entity).second
            || !keys.insert(origin.key).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid or duplicated source node identity");
        }

        auto prefix = origin.key;
        const auto value = prefix.back();
        prefix.pop_back();
        const auto owner = indexed.find(prefix);

        if (owner == indexed.end() || !live.at(prefix).contains(value)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Source node is outside its live namespace");
        }
      }

      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Validate source identity: " + std::string(error.what()));
    }
  }

  auto validateSceneSourceUpdate(std::string_view asset,
                                 std::span<const SceneSourceOrigin> previous,
                                 std::span<const SceneSourceNamespace> oldNamespaces,
                                 std::span<const SceneSourceOrigin> incoming,
                                 std::span<const SceneSourceNamespace> newNamespaces)
      -> Core::Result<void> {
    auto valid = validateSceneSourceIdentity(asset, previous, oldNamespaces);

    if (!valid) {
      return valid;
    }

    valid = validateSceneSourceIdentity(asset, incoming, newNamespaces);

    if (!valid) {
      return valid;
    }

    try {
      const auto oldSpaces = spaces(oldNamespaces);

      for (const auto& space : newNamespaces) {
        const auto old = oldSpaces.find(space.prefix);

        if (old != oldSpaces.end()) {
          if (old->second->asset != space.asset || floor(space) < floor(*old->second)) {
            return Core::fail(
                Core::ErrorCode::InvalidState,
                "Source namespace asset changed or allocation regressed: " + space.asset);
          }

        } else if (!space.prefix.empty()) {
          auto parent = space.prefix;
          const auto value = parent.back();
          parent.pop_back();
          const auto owner = oldSpaces.find(parent);

          if (owner != oldSpaces.end() && value < floor(*owner->second)
              && std::ranges::find(owner->second->entities, value)
                     == owner->second->entities.end()) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Source reused a retired instance-root identity: " + space.asset);
          }
        }
      }

      for (const auto& origin : incoming) {
        auto parent = origin.key;
        const auto value = parent.back();
        parent.pop_back();
        const auto owner = oldSpaces.find(parent);

        if (owner != oldSpaces.end() && value < floor(*owner->second)
            && std::ranges::find(owner->second->entities, value) == owner->second->entities.end()) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Source reused a retired node identity: " + owner->second->asset);
        }
      }

      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Validate source update: " + std::string(error.what()));
    }
  }
}  // namespace Dot::World
