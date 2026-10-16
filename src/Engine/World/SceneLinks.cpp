module;

#include <algorithm>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.World.SceneLinks;

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneContent;
import Dot.World.SceneFiles;

namespace Dot::World {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    using Values = std::map<std::uint64_t, std::uint64_t>;
    using Nodes = std::map<std::uint64_t, Json>;

    auto baseline(const Scene& source) -> Core::Result<std::string> {
      auto encoded = encodeScene(source);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      auto data = Codec::parseJson(*encoded);
      data.erase("links");
      data["environment"] = nullptr;
      data["occlusion"] = nullptr;
      return data.dump(2) + '\n';
    }

    auto nodes(const Json& source, const Values& mapping, std::uint64_t root) -> Nodes {
      Nodes result;

      for (auto entity : source["entities"]) {
        const auto id = Codec::id(entity["id"]);
        entity["id"] = std::to_string(mapping.at(id));
        entity["parent"] = std::to_string(
            entity["parent"].is_null() ? root : mapping.at(Codec::id(entity["parent"])));

        for (const auto* field : {"camera", "audioListener"}) {
          if (!entity[field].is_null()) {
            entity[field]["primary"] = false;
          }
        }

        if (!entity["skin"].is_null()) {
          for (auto& joint : entity["skin"]["joints"]) {
            joint = std::to_string(mapping.at(Codec::id(joint)));
          }
        }

        if (!entity["animation"].is_null()) {
          for (auto& target : entity["animation"]["targets"]) {
            target["entity"] = std::to_string(mapping.at(Codec::id(target["entity"])));
          }
        }

        result.emplace(id, std::move(entity));
      }

      return result;
    }

    auto member(const Json& object, const std::string& key) -> std::optional<Json> {
      const auto found = object.find(key);
      return found == object.end() ? std::optional<Json>{} : std::optional<Json>{*found};
    }

    /** Missing and null are distinct: deleting a property is an override too.
     * 缺失与 null 不同，删除属性也是覆盖。 */
    auto merge(const std::optional<Json>& old, const std::optional<Json>& local,
               const std::optional<Json>& incoming) -> std::optional<Json> {
      if (local == old) {
        return incoming;
      }

      if (!old || !local || !incoming || !old->is_object() || !local->is_object()
          || !incoming->is_object()) {
        return local;
      }

      std::set<std::string> keys;

      for (const auto* value : {&*old, &*local, &*incoming}) {
        for (const auto& item : value->items()) {
          keys.insert(item.key());
        }
      }

      Json result = Json::object();

      for (const auto& key : keys) {
        auto value = merge(member(*old, key), member(*local, key), member(*incoming, key));

        if (value) {
          result[key] = std::move(*value);
        }
      }

      return result;
    }
  }  // namespace

  auto prepareLinkedScene(const Scene& target, const Scene& source, std::string_view asset,
                          std::uint64_t firstValue, const Transform& placement,
                          std::optional<EntityId> parent) -> Core::Result<SceneContentResult> {
    try {
      auto result = prepareSceneInstance(target, source, firstValue, placement, parent);
      auto text = baseline(source);

      if (!result || !text) {
        return std::unexpected(!result ? result.error() : text.error());
      }

      SceneLink link{result->root.value, std::string(asset), std::move(*text), {}};

      for (const auto& mapping : result->entities) {
        link.mapping.push_back({mapping.source.value, mapping.target.value});
      }

      auto links = result->scene.sceneLinks().value();
      links.push_back(std::move(link));
      auto changed = result->scene.setSceneLinks(links);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      auto bounded = encodeScene(result->scene);
      return bounded ? std::move(result)
                     : Core::Result<SceneContentResult>{std::unexpected(bounded.error())};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Link scene: " + std::string(error.what()));
    }
  }

  auto prepareSceneLinkUpdate(const Scene& target, const Scene& source, EntityId root,
                              std::uint64_t firstValue, bool revert)
      -> Core::Result<SceneLinkUpdate> {
    try {
      auto owner = target.entity(root);
      auto links = target.sceneLinks();
      auto text = baseline(source);
      auto encoded = encodeScene(target);
      auto floor = target.nextEntityValue();

      if (!owner || !links || !text || !encoded || !floor) {
        return std::unexpected(!owner     ? owner.error()
                               : !links   ? links.error()
                               : !text    ? text.error()
                               : !encoded ? encoded.error()
                                          : floor.error());
      }

      auto found = std::ranges::find(*links, root.value, &SceneLink::root);

      if (found == links->end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Entity is not a linked scene root");
      }

      const auto maximum = std::numeric_limits<std::uint64_t>::max();
      const auto minimum = floor->value_or(maximum);

      if (firstValue < minimum || firstValue == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Refresh requires the reserved allocation floor");
      }

      Values oldMapping, mapping;

      for (const auto& pair : found->mapping) {
        oldMapping.emplace(pair.source, pair.target);
      }

      const auto incoming = Codec::parseJson(*text);
      const auto original = Codec::parseJson(found->baseline);
      const auto sourceFloor = [](const Json& value) {
        return value["nextEntity"].is_null() ? std::numeric_limits<std::uint64_t>::max()
                                             : Codec::id(value["nextEntity"]);
      };

      if (sourceFloor(incoming) < sourceFloor(original)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Linked source allocation regressed");
      }

      auto next = firstValue;
      std::vector<std::uint64_t> created;

      for (const auto& entity : incoming["entities"]) {
        const auto id = Codec::id(entity["id"]);
        const auto old = oldMapping.find(id);

        if (old != oldMapping.end()) {
          mapping.emplace(id, old->second);
        } else {
          if (id < sourceFloor(original)) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Linked source reused a retired node value");
          }

          if (next == maximum) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Linked scene entity allocation is exhausted");
          }

          mapping.emplace(id, next);
          created.push_back(next++);
        }
      }

      const auto previous = nodes(original, oldMapping, root.value);
      const auto current = nodes(incoming, mapping, root.value);
      auto document = Codec::parseJson(*encoded);
      Nodes local;

      for (const auto& entity : document["entities"]) {
        local.emplace(Codec::id(entity["id"]), entity);
      }

      for (const auto& [id, old] : previous) {
        const auto value = oldMapping.at(id);
        const auto entry = local.find(value);

        if (!current.contains(id)) {
          if (entry != local.end() && !revert && entry->second != old) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Source removed an overridden node: " + std::to_string(id));
          }

          local.erase(value);
        }
      }

      for (const auto& [id, value] : current) {
        const auto old = previous.find(id);
        const auto entry = local.find(mapping.at(id));

        if (old == previous.end() || revert) {
          if (entry == local.end() && old != previous.end()) {
            created.push_back(mapping.at(id));
          }

          local[mapping.at(id)] = value;
        } else if (entry != local.end()) {
          entry->second = *merge(std::optional<Json>{old->second},
                                 std::optional<Json>{entry->second}, std::optional<Json>{value});
        }
      }

      document["entities"] = Json::array();

      for (auto& [id, value] : local) {
        document["entities"].push_back(std::move(value));
      }

      found->baseline = std::move(*text);
      found->mapping.clear();

      for (const auto& [sourceValue, targetValue] : mapping) {
        found->mapping.push_back({sourceValue, targetValue});
      }

      document.erase("links");
      document["nextEntity"] = next == maximum ? Json(nullptr) : Json(std::to_string(next));
      auto prepared = decodeScene(document.dump());

      if (!prepared) {
        return Core::fail(prepared.error().code,
                          "Linked scene refresh: " + prepared.error().message);
      }

      auto changed = prepared->setSceneLinks(*links);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      auto bounded = encodeScene(*prepared);

      if (!bounded) {
        return std::unexpected(bounded.error());
      }

      const auto handle = EntityId{prepared->id(), root.value};
      return SceneLinkUpdate{std::move(*prepared), handle, std::move(created)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Refresh linked scene: " + std::string(error.what()));
    }
  }
}  // namespace Dot::World
