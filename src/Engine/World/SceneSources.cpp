module;

#include <exception>
#include <expected>
#include <map>
#include <set>
#include <string>
#include <utility>

module Dot.World.SceneSources;

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.World.SceneIdentity;

namespace Dot::World {
  auto validateSceneSourceSnapshot(const SceneSourceSnapshot& source) -> Core::Result<void> {
    auto entities = source.scene.entities();

    if (!entities) {
      return std::unexpected(entities.error());
    }

    auto valid = validateSceneSourceIdentity(source.asset, source.origins, source.namespaces);

    if (!valid) {
      return valid;
    }

    if (entities->size() != source.origins.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Source origins do not cover materialized entities");
    }

    for (const auto& origin : source.origins) {
      if (!source.scene.findEntity(origin.entity)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Source origin names a missing materialized entity");
      }
    }

    return {};
  }

  auto prepareSceneSourceSnapshot(Scene scene, std::string_view asset,
                                  std::span<const SceneSourceOrigin> localOrigins,
                                  SceneSourceNamespace localNamespace)
      -> Core::Result<SceneSourceSnapshot> {
    try {
      auto entities = scene.entities();
      auto links = scene.sceneLinks();

      if (!entities || !links) {
        return std::unexpected(!entities ? entities.error() : links.error());
      }

      if (!localNamespace.prefix.empty() || localNamespace.asset != asset) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Working source requires its original root namespace");
      }

      SceneSourceSnapshot snapshot{
          std::move(scene), std::string(asset), {}, {std::move(localNamespace)}};

      auto valid = validateSceneSourceIdentity(asset, localOrigins, snapshot.namespaces);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      std::map<std::uint64_t, SceneSourceKey> locals, mapped;

      for (const auto& origin : localOrigins) {
        if (origin.key.size() != 1) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Local source identities must be singleton keys");
        }

        locals.emplace(origin.entity, origin.key);
      }

      for (const auto& link : *links) {
        const auto root = locals.find(link.root);

        if (root == locals.end()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Source instance root requires a local identity");
        }

        for (const auto& pair : link.mapping) {
          auto key = root->second;
          key.insert(key.end(), pair.key.begin(), pair.key.end());
          mapped.emplace(pair.target, std::move(key));
        }

        for (const auto& space : link.namespaces) {
          auto prefix = root->second;
          prefix.insert(prefix.end(), space.prefix.begin(), space.prefix.end());
          snapshot.namespaces.push_back(
              {std::move(prefix), space.asset, space.nextEntity, space.entities});
        }
      }

      std::size_t localCount{};

      for (const auto& entity : *entities) {
        if (const auto found = mapped.find(entity.id.value); found != mapped.end()) {
          snapshot.origins.push_back({entity.id.value, found->second});
        } else if (const auto local = locals.find(entity.id.value); local != locals.end()) {
          snapshot.origins.push_back({entity.id.value, local->second});
          ++localCount;
        } else {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Missing local source identity");
        }
      }

      if (localCount != locals.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Local origins include missing or linked nodes");
      }

      valid = validateSceneSourceSnapshot(snapshot);
      return valid ? Core::Result<SceneSourceSnapshot>{std::move(snapshot)}
                   : Core::Result<SceneSourceSnapshot>{std::unexpected(valid.error())};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare scene source: " + std::string(error.what()));
    }
  }

  auto captureSceneSource(const Scene& scene, std::string_view asset)
      -> Core::Result<SceneSourceSnapshot> {
    try {
      auto links = scene.sceneLinks();
      auto entities = scene.entities();
      auto next = scene.nextEntityValue();
      auto text = encodeScene(scene);

      if (!links || !entities || !next || !text) {
        return std::unexpected(!links      ? links.error()
                               : !entities ? entities.error()
                               : !next     ? next.error()
                                           : text.error());
      }

      std::set<std::uint64_t> mapped;

      for (const auto& link : *links) {
        for (const auto& pair : link.mapping) {
          mapped.insert(pair.target);
        }
      }

      std::vector<SceneSourceOrigin> locals;
      SceneSourceNamespace rootNamespace{{}, std::string(asset), *next, {}};

      for (const auto& entity : *entities) {
        rootNamespace.entities.push_back(entity.id.value);

        if (!mapped.contains(entity.id.value)) {
          locals.push_back({entity.id.value, {entity.id.value}});
        }
      }

      auto copy = decodeScene(*text);
      return copy ? prepareSceneSourceSnapshot(std::move(*copy), asset, locals,
                                               std::move(rootNamespace))
                  : Core::Result<SceneSourceSnapshot>{std::unexpected(copy.error())};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Capture scene source: " + std::string(error.what()));
    }
  }
}  // namespace Dot::World
