module;

#include <algorithm>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

module Dot.World.SceneResolver;

import Dot.Core.Result;
import Dot.Core.Paths;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.World.SceneSources;
import Dot.World.SceneLinks;

namespace Dot::World {
  namespace {
    auto sourcePath(std::string_view path) -> bool {
      return Core::isPortableRelativePath(path) && path.ends_with(".dotscene")
             && (path.starts_with("assets/") || path.starts_with("scenes/"))
             && path.find("/.dot-stage-") == std::string_view::npos;
    }

    auto validLimits(const SceneResolveLimits& limits) -> bool {
      const SceneResolveLimits maximum;
      return limits.depth > 0 && limits.depth <= maximum.depth && limits.sources > 0
             && limits.sources <= maximum.sources && limits.links > 0
             && limits.links <= maximum.links && limits.inputBytes > 0
             && limits.inputBytes <= maximum.inputBytes && limits.resolvedBytes > 0
             && limits.resolvedBytes <= maximum.resolvedBytes && limits.entities > 0
             && limits.entities <= maximum.entities;
    }

    struct CachedSource {
      SceneSourceSnapshot source;
      std::size_t height;
    };

    struct Resolver {
      const SceneSourceLoader& m_loader;
      const SceneResolveLimits& m_limits;
      std::string_view m_owner;
      SceneResolveStats m_stats;
      std::vector<std::string> m_stack;
      std::map<std::string, CachedSource, std::less<>> m_cache;

      auto chain() const -> std::string {
        std::string result;

        for (const auto& path : m_stack) {
          result += (result.empty() ? "" : " -> ") + path;
        }

        return result;
      }

      auto chargeEntities(std::size_t count) -> Core::Result<void> {
        if (count > m_limits.entities - m_stats.entities) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene source graph entity-work budget exceeded");
        }

        m_stats.entities += count;
        return {};
      }

      auto visit(std::string_view asset) -> Core::Result<CachedSource*> {
        if (!sourcePath(asset)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid portable scene source path: " + std::string(asset));
        }

        if (asset == m_owner || std::ranges::find(m_stack, asset) != m_stack.end()) {
          return Core::fail(
              Core::ErrorCode::InvalidState,
              "Scene source cycle or active-document reference: " + std::string(asset));
        }

        if (m_stack.size() >= m_limits.depth) {
          return Core::fail(Core::ErrorCode::Unavailable, "Scene source graph depth exceeded");
        }

        if (auto cached = m_cache.find(asset); cached != m_cache.end()) {
          if (cached->second.height > m_limits.depth - m_stack.size()) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Cached scene source exceeds placement depth: " + std::string(asset));
          }

          return &cached->second;
        }

        if (m_cache.size() + m_stack.size() >= m_limits.sources) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene source graph file budget exceeded");
        }

        m_stack.emplace_back(asset);
        const auto remaining = m_limits.inputBytes - m_stats.inputBytes;

        if (remaining == 0) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene source graph input budget exceeded");
        }

        auto text = m_loader(asset, remaining);

        if (!text) {
          return std::unexpected(text.error());
        }

        if (text->size() > remaining) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene source loader exceeded its input byte quota");
        }

        m_stats.inputBytes += text->size();
        ++m_stats.sources;
        auto scene = decodeScene(*text);

        if (!scene) {
          return std::unexpected(scene.error());
        }

        auto links = scene->sceneLinks().value();
        const auto entities = scene->entities().value();

        if (links.size() > m_limits.links - m_stats.links) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene source graph link budget exceeded");
        }

        m_stats.links += links.size();
        auto charged = chargeEntities(entities.size());

        if (!charged) {
          return std::unexpected(charged.error());
        }

        std::set<std::uint64_t> mapped;

        for (const auto& link : links) {
          for (const auto& pair : link.mapping) {
            mapped.insert(pair.target);
          }
        }

        SceneSourceNamespace original{{}, std::string(asset), *scene->nextEntityValue(), {}};
        std::vector<SceneSourceOrigin> locals;

        for (const auto& entity : entities) {
          original.entities.push_back(entity.id.value);

          if (!mapped.contains(entity.id.value)) {
            locals.push_back({entity.id.value, {entity.id.value}});
          }
        }

        std::ranges::sort(links, {}, &SceneLink::root);
        std::size_t height{1};

        for (const auto& link : links) {
          auto child = visit(link.asset);

          if (!child) {
            return std::unexpected(child.error());
          }

          height = std::max(height, (*child)->height + 1);
          const auto next
              = scene->nextEntityValue()->value_or(std::numeric_limits<std::uint64_t>::max());
          auto refreshed
              = prepareSceneLinkUpdate(*scene, (*child)->source, {scene->id(), link.root}, next);

          if (!refreshed) {
            return Core::fail(refreshed.error().code,
                              "Refresh nested instance " + std::to_string(link.root) + " from "
                                  + link.asset + ": " + refreshed.error().message);
          }

          charged = chargeEntities(*refreshed->scene.entityCount());

          if (!charged) {
            return std::unexpected(charged.error());
          }

          scene = std::move(refreshed->scene);
        }

        auto snapshot
            = prepareSceneSourceSnapshot(std::move(*scene), asset, locals, std::move(original));

        if (!snapshot) {
          return std::unexpected(snapshot.error());
        }

        auto encoded = encodeScene(snapshot->scene);

        if (!encoded) {
          return std::unexpected(encoded.error());
        }

        if (encoded->size() > m_limits.resolvedBytes - m_stats.resolvedBytes) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene source graph resolved-storage budget exceeded");
        }

        m_stats.resolvedBytes += encoded->size();
        auto [entry, inserted]
            = m_cache.emplace(std::string(asset), CachedSource{std::move(*snapshot), height});
        m_stack.pop_back();
        return &entry->second;
      }
    };
  }  // namespace

  auto resolveSceneSource(std::string_view asset, const SceneSourceLoader& loader,
                          const SceneResolveLimits& limits, std::string_view ownerAsset)
      -> Core::Result<ResolvedSceneSource> {
    if (!loader || !validLimits(limits) || (!ownerAsset.empty() && !sourcePath(ownerAsset))) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid scene source resolver inputs");
    }

    Resolver resolver{loader, limits, ownerAsset, {}, {}, {}};

    try {
      auto resolved = resolver.visit(asset);

      if (!resolved) {
        return Core::fail(resolved.error().code, "Resolve scene sources (" + resolver.chain()
                                                     + "): " + resolved.error().message);
      }

      return ResolvedSceneSource{std::move((*resolved)->source), resolver.m_stats};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Resolve scene sources (" + resolver.chain() + "): " + error.what());
    }
  }
}  // namespace Dot::World
