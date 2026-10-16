module;

#include <algorithm>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.World.SceneContent;

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Math;

namespace Dot::World {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    using ValueMap = std::map<std::uint64_t, std::uint64_t>;

    auto encoded(const Scene& scene) -> Core::Result<Json> {
      auto text = encodeScene(scene);
      return text ? Core::Result<Json>{Codec::parseJson(*text)}
                  : Core::Result<Json>{std::unexpected(text.error())};
    }

    auto rootScene(std::string_view name, const Transform& placement = {}) -> Core::Result<Json> {
      auto scene = Scene::create(name);

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto root = scene->createEntity(name);
      auto changed = root ? scene->setTransform(*root, placement)
                          : Core::Result<void>{std::unexpected(root.error())};
      return changed ? encoded(*scene) : Core::Result<Json>{std::unexpected(changed.error())};
    }

    auto remapReferences(Json& entity, const ValueMap& values) -> Core::Result<void> {
      auto replace = [&](Json& value) -> Core::Result<void> {
        const auto source = Codec::id(value);
        const auto found = values.find(source);

        if (found == values.end()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene content references an entity outside the selected subtrees: "
                                + std::to_string(source));
        }

        value = std::to_string(found->second);
        return {};
      };

      if (!entity["skin"].is_null()) {
        for (auto& joint : entity["skin"]["joints"]) {
          auto result = replace(joint);

          if (!result) {
            return result;
          }
        }
      }

      if (!entity["animation"].is_null()) {
        for (auto& target : entity["animation"]["targets"]) {
          auto result = replace(target["entity"]);

          if (!result) {
            return result;
          }
        }
      }

      return {};
    }

    auto finish(Json document, const ValueMap& values, std::uint64_t sourceScope,
                std::uint64_t rootValue) -> Core::Result<SceneContentResult> {
      auto scene = decodeScene(document.dump());

      if (!scene) {
        return std::unexpected(scene.error());
      }

      std::vector<ContentEntityMapping> mapping;
      mapping.reserve(values.size());

      for (const auto& [source, target] : values) {
        mapping.push_back({{sourceScope, source}, {scene->id(), target}});
      }

      const auto root = scene->findEntity(rootValue).value();
      return SceneContentResult{std::move(*scene), root, std::move(mapping)};
    }
  }  // namespace

  auto extractSceneContent(const Scene& source, std::span<const EntityId> selected,
                           std::string_view name) -> Core::Result<SceneContentResult> {
    try {
      auto entities = source.entities();

      if (!entities) {
        return std::unexpected(entities.error());
      }

      if (selected.empty() || selected.size() > 10000) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Scene content requires 1..10000 selected entities");
      }

      std::map<std::uint64_t, const EntityInfo*> live;
      std::map<std::uint64_t, std::vector<std::uint64_t>> children;

      for (const auto& entity : *entities) {
        live.emplace(entity.id.value, &entity);

        if (entity.parent) {
          children[entity.parent->value].push_back(entity.id.value);
        }
      }

      std::set<std::uint64_t> requested, included;
      std::vector<std::uint64_t> pending;

      for (const auto id : selected) {
        if (id.scene != source.id() || !live.contains(id.value)
            || !requested.insert(id.value).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene content selection is foreign, stale or duplicated");
        }

        pending.push_back(id.value);
      }

      while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();

        if (included.insert(id).second) {
          const auto& descendants = children[id];
          pending.insert(pending.end(), descendants.begin(), descendants.end());
        }
      }

      if (included.size() >= 10000) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Scene content needs a free container slot");
      }

      auto output = rootScene(name);
      auto input = encoded(source);
      auto active = source.worldTransform(selected.back());

      if (!output || !input || !active) {
        return std::unexpected(!output ? output.error() : !input ? input.error() : active.error());
      }

      ValueMap values;
      std::uint64_t next = 2;

      for (const auto id : included) {
        values.emplace(id, next++);
      }

      for (auto entity : (*input)["entities"]) {
        const auto id = Codec::id(entity["id"]);

        if (!included.contains(id)) {
          continue;
        }

        auto remapped = remapReferences(entity, values);

        if (!remapped) {
          return std::unexpected(remapped.error());
        }

        const auto parent = live.at(id)->parent;
        entity["id"] = std::to_string(values.at(id));

        if (parent && included.contains(parent->value)) {
          entity["parent"] = std::to_string(values.at(parent->value));
        } else {
          auto world = source.worldTransform(live.at(id)->id);

          if (!world) {
            return std::unexpected(world.error());
          }

          world->data[12] -= active->data[12];
          world->data[13] -= active->data[13];
          world->data[14] -= active->data[14];
          auto pose = Math::decomposeTrs(*world);

          if (!pose) {
            return Core::fail(pose.error().code, "Scene content cannot flatten root world pose: "
                                                     + pose.error().message);
          }

          entity["parent"] = "1";
          entity["transform"]
              = {{"position", {pose->position.x, pose->position.y, pose->position.z}},
                 {"rotation",
                  {pose->rotation.x, pose->rotation.y, pose->rotation.z, pose->rotation.w}},
                 {"scale", {pose->scale.x, pose->scale.y, pose->scale.z}}};
        }

        (*output)["entities"].push_back(std::move(entity));
      }

      return finish(std::move(*output), values, source.id(), 1);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Extract scene content: " + std::string(error.what()));
    }
  }

  auto prepareSceneInstance(const Scene& target, const Scene& content, std::uint64_t firstValue,
                            const Transform& placement, std::optional<EntityId> parent)
      -> Core::Result<SceneContentResult> {
    try {
      auto input = encoded(content);
      auto output = encoded(target);

      if (!input || !output) {
        return std::unexpected(!input ? input.error() : output.error());
      }

      if (parent) {
        auto owner = target.entity(*parent);

        if (!owner) {
          return std::unexpected(owner.error());
        }
      }

      const auto count = (*input)["entities"].size();
      const auto existing = (*output)["entities"].size();
      const auto maximum = std::numeric_limits<std::uint64_t>::max();

      if (count == 0 || firstValue == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Scene instance requires nonempty content and a positive first value");
      }

      if (count + 1 > 10000 - existing || count + 1 > maximum - firstValue) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Scene instance exceeds entity/identity capacity");
      }

      for (const auto& entity : (*output)["entities"]) {
        if (Codec::id(entity["id"]) >= firstValue) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene instance requires a monotonic first entity value");
        }
      }

      auto container = rootScene((*input)["name"].get<std::string>(), placement);

      if (!container) {
        return std::unexpected(container.error());
      }

      auto root = (*container)["entities"][0];
      root["id"] = std::to_string(firstValue);
      root["parent"] = parent ? Json(std::to_string(parent->value)) : Json(nullptr);
      (*output)["entities"].push_back(std::move(root));
      ValueMap values;
      auto next = firstValue + 1;

      for (const auto& entity : (*input)["entities"]) {
        values.emplace(Codec::id(entity["id"]), next++);
      }

      for (auto entity : (*input)["entities"]) {
        const auto id = Codec::id(entity["id"]);
        auto remapped = remapReferences(entity, values);

        if (!remapped) {
          return std::unexpected(remapped.error());
        }

        entity["id"] = std::to_string(values.at(id));
        entity["parent"] = entity["parent"].is_null()
                               ? std::to_string(firstValue)
                               : std::to_string(values.at(Codec::id(entity["parent"])));

        for (const auto* component : {"camera", "audioListener"}) {
          if (!entity[component].is_null()) {
            entity[component]["primary"] = false;
          }
        }

        (*output)["entities"].push_back(std::move(entity));
      }

      return finish(std::move(*output), values, content.id(), firstValue);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare scene instance: " + std::string(error.what()));
    }
  }
}  // namespace Dot::World
