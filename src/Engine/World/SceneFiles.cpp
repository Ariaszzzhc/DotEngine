module;

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <unordered_map>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.World.SceneFiles;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;
import Dot.Math;

namespace Dot::World {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    constexpr std::size_t maxFileBytes = 64 * 1024 * 1024;

    auto vector3(const Json& value) -> Math::Vec3 {
      const auto result = Codec::numbers<3>(value);
      return {result[0], result[1], result[2]};
    }

    auto vector4(const Json& value) -> Math::Vec4 {
      const auto result = Codec::numbers<4>(value);
      return {result[0], result[1], result[2], result[3]};
    }

    auto transform(const Json& value) -> Transform {
      Codec::requireFields(value, {"position", "rotation", "scale"});
      const auto rotation = Codec::numbers<4>(value["rotation"]);
      return {vector3(value["position"]),
              {rotation[0], rotation[1], rotation[2], rotation[3]},
              vector3(value["scale"])};
    }

    auto camera(const Json& value) -> Camera {
      Codec::requireFields(value, {"projection", "verticalFovDegrees", "orthographicHeight",
                                   "nearPlane", "farPlane", "primary"});
      Camera result;
      const auto projection = value["projection"].get<std::string>();

      if (projection == "perspective") {
        result.projection = Projection::Perspective;
      } else if (projection == "orthographic") {
        result.projection = Projection::Orthographic;
      } else {
        throw std::invalid_argument("Unknown camera projection");
      }

      result.verticalFovDegrees = Codec::number(value["verticalFovDegrees"]);
      result.orthographicHeight = Codec::number(value["orthographicHeight"]);
      result.nearPlane = Codec::number(value["nearPlane"]);
      result.farPlane = Codec::number(value["farPlane"]);
      result.primary = value["primary"].get<bool>();
      return result;
    }

    auto mesh(const Json& value) -> MeshRenderer {
      Codec::requireFields(value, {"kind", "asset", "tint", "visible"});
      MeshRenderer result;
      const auto kind = value["kind"].get<std::string>();

      if (kind == "cube") {
        result.kind = MeshKind::Cube;
      } else if (kind == "quad") {
        result.kind = MeshKind::Quad;
      } else if (kind == "asset") {
        result.kind = MeshKind::Asset;
      } else {
        throw std::invalid_argument("Unknown mesh kind");
      }

      result.asset = value["asset"].get<std::string>();
      result.tint = vector4(value["tint"]);
      result.visible = value["visible"].get<bool>();
      return result;
    }

    auto validateExtension(const std::filesystem::path& path) -> Core::Result<void> {
      try {
        auto extension = path.extension().generic_u8string();

        for (auto& character : extension) {
          if (character >= u8'A' && character <= u8'Z') {
            character = static_cast<char8_t>(character + (u8'a' - u8'A'));
          }
        }

        if (extension != u8".dotscene") {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Scene extension must be .dotscene");
        }
      } catch (const std::filesystem::filesystem_error& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          std::format("Decode scene path: {}", error.what()));
      }

      return {};
    }

    struct Record {
      std::uint64_t id;
      std::string name;
      std::optional<std::uint64_t> parent;
      Transform transform;
      std::optional<Camera> camera;
      std::optional<MeshRenderer> mesh;
    };

    auto parsedScene(const Json& root) -> Core::Result<Scene> {
      Codec::requireFields(root, {"version", "name", "entities"});

      if (!root["version"].is_number_integer() || root["version"] != 1
          || !root["entities"].is_array() || root["entities"].size() > 10000) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid scene version or entity array");
      }

      std::vector<Record> records;
      records.reserve(root["entities"].size());
      std::unordered_set<std::uint64_t> ids;

      for (const auto& value : root["entities"]) {
        Codec::requireFields(value, {"id", "name", "parent", "transform"}, {"camera", "mesh"});
        Record record;
        record.id = Codec::id(value["id"]);
        record.name = value["name"].get<std::string>();
        record.transform = transform(value["transform"]);

        if (!ids.insert(record.id).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate scene entity ID");
        }

        if (!value["parent"].is_null()) {
          record.parent = Codec::id(value["parent"]);
        }

        if (value.contains("camera") && !value["camera"].is_null()) {
          record.camera = camera(value["camera"]);
        }

        if (value.contains("mesh") && !value["mesh"].is_null()) {
          record.mesh = mesh(value["mesh"]);
        }

        records.push_back(std::move(record));
      }

      for (const auto& record : records) {
        if (record.parent && !ids.contains(*record.parent)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene parent refers to a missing entity");
        }
      }

      std::ranges::sort(records, {}, &Record::id);
      auto scene = Scene::create(root["name"].get<std::string>());

      if (!scene) {
        return std::unexpected(scene.error());
      }

      std::unordered_map<std::uint64_t, EntityId> handles;
      std::vector<TransformUpdate> transforms;
      std::vector<ParentUpdate> parents;
      transforms.reserve(records.size());
      parents.reserve(records.size());

      for (const auto& record : records) {
        auto entity = scene->createEntityWithId(record.name, record.id);

        if (!entity) {
          return std::unexpected(entity.error());
        }

        handles.emplace(record.id, *entity);
        transforms.push_back({*entity, record.transform});
      }

      auto changed = scene->setTransforms(transforms);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      for (const auto& record : records) {
        parents.push_back({handles.at(record.id), record.parent
                                                      ? std::optional{handles.at(*record.parent)}
                                                      : std::nullopt});
      }

      changed = scene->setParents(parents);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      for (const auto& record : records) {
        const auto entity = handles.at(record.id);
        changed = scene->setCamera(entity, record.camera);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = scene->setMeshRenderer(entity, record.mesh);

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }

      return std::move(*scene);
    }

    auto cameraJson(const Camera& value) -> Json {
      return {{"projection",
               value.projection == Projection::Perspective ? "perspective" : "orthographic"},
              {"verticalFovDegrees", value.verticalFovDegrees},
              {"orthographicHeight", value.orthographicHeight},
              {"nearPlane", value.nearPlane},
              {"farPlane", value.farPlane},
              {"primary", value.primary}};
    }

    auto meshJson(const MeshRenderer& value) -> Json {
      const auto kind = value.kind == MeshKind::Cube   ? "cube"
                        : value.kind == MeshKind::Quad ? "quad"
                                                       : "asset";
      return {{"kind", kind},
              {"asset", value.asset},
              {"visible", value.visible},
              {"tint", {value.tint.x, value.tint.y, value.tint.z, value.tint.w}}};
    }
  }  // namespace

  auto encodeScene(const Scene& scene) -> Core::Result<std::string> {
    auto name = scene.name();
    auto entities = scene.entities();

    if (!name) {
      return std::unexpected(name.error());
    }

    if (!entities) {
      return std::unexpected(entities.error());
    }

    Json records = Json::array();

    for (const auto& entity : *entities) {
      const auto& transform = entity.transform;
      records.push_back(
          {{"id", std::to_string(entity.id.value)},
           {"name", entity.name},
           {"parent", entity.parent ? Json(std::to_string(entity.parent->value)) : Json(nullptr)},
           {"transform",
            {{"position", {transform.position.x, transform.position.y, transform.position.z}},
             {"rotation",
              {transform.rotation.x, transform.rotation.y, transform.rotation.z,
               transform.rotation.w}},
             {"scale", {transform.scale.x, transform.scale.y, transform.scale.z}}}},
           {"camera", entity.camera ? cameraJson(*entity.camera) : Json(nullptr)},
           {"mesh", entity.mesh ? meshJson(*entity.mesh) : Json(nullptr)}});
    }

    auto encoded
        = Json{{"version", 1}, {"name", *name}, {"entities", std::move(records)}}.dump(2) + '\n';

    if (encoded.size() > maxFileBytes) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Encoded scene exceeds 64 MiB");
    }

    return encoded;
  }

  auto decodeScene(std::string_view source) -> Core::Result<Scene> {
    if (source.empty() || source.size() > maxFileBytes
        || source.find('\0') != std::string_view::npos) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Scene source must be nonempty, at most 64 MiB, and contain no NUL bytes");
    }

    try {
      return parsedScene(Codec::parseJson(source));
    } catch (const Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        std::format("Decode scene JSON: {}", error.what()));
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        std::format("Decode scene: {}", error.what()));
    }
  }

  auto loadScene(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<Scene> {
    auto checked = validateExtension(relative);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto source = files.readText(relative, maxFileBytes);

    if (!source) {
      return std::unexpected(source.error());
    }

    return decodeScene(*source);
  }

  auto saveScene(const Core::FileSystem& files, const std::filesystem::path& relative,
                 const Scene& scene) -> Core::Result<void> {
    auto checked = validateExtension(relative);

    if (!checked) {
      return checked;
    }

    auto source = encodeScene(scene);

    if (!source) {
      return std::unexpected(source.error());
    }

    return files.writeTextAtomic(relative, *source);
  }
}  // namespace Dot::World
