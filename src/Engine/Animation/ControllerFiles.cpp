module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Animation.ControllerFiles;

import Dot.Animation.Layers;
import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Animation {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    constexpr std::size_t maxBytes = 1024 * 1024;
    constexpr std::array operations{"eq", "ne", "lt", "le", "gt", "ge"};

    auto parameterValue(const Json& json) -> ParameterValue {
      if (json.is_boolean()) {
        return json.get<bool>();
      }

      if (!json.is_number()) {
        throw std::invalid_argument("Controller parameters require booleans or numbers");
      }

      const auto value = json.get<double>();

      if (!std::isfinite(value) || std::abs(value) > 1000000) {
        throw std::invalid_argument("Controller numbers must be finite within +/-1000000");
      }

      return value;
    }

    auto parameterJson(const ParameterValue& value) -> Json {
      return std::visit([](auto scalar) { return Json(scalar); }, value);
    }

    auto checkedPath(const Core::FileSystem& files, const std::filesystem::path& path, bool saving)
        -> Core::Result<void> {
      const auto utf = path.generic_u8string();
      const std::string text(utf.begin(), utf.end());

      if (text.size() > 1024 || !Core::isPortableRelativePath(text) || !text.starts_with("assets/")
          || !text.ends_with(".dotcontroller")) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Controller requires a portable assets/*.dotcontroller path");
      }

      auto current = files.root();

      for (const auto& part : path) {
        current /= part;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        const bool final = current == files.root() / path;

        if (status.type() == std::filesystem::file_type::not_found && final && saving) {
          return {};
        }

        if (error || std::filesystem::is_symlink(status)
            || (final ? !std::filesystem::is_regular_file(status)
                      : !std::filesystem::is_directory(status))) {
          return Core::fail(
              Core::ErrorCode::Io,
              "Controller path '" + text + "' is missing, inaccessible, a symlink or special file");
        }
      }

      return {};
    }
  }  // namespace

  auto decodeControllerForm(std::string_view source) -> Core::Result<ControllerDescription> {
    if (source.empty() || source.size() > maxBytes || source.find('\0') != std::string_view::npos
        || !Core::isValidUtf8(source)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid animation controller JSON bytes");
    }

    try {
      const auto root = Codec::parseJson(source, 16);
      Codec::requireFields(root, {"name", "initial", "parameters", "states", "transitions"});

      if (!root["parameters"].is_array() || root["parameters"].size() > 64
          || !root["states"].is_array() || root["states"].size() > 64
          || !root["transitions"].is_array() || root["transitions"].size() > 512) {
        throw std::invalid_argument("Invalid controller array types/capacities");
      }

      ControllerDescription description{
          root["name"].get<std::string>(), root["initial"].get<std::string>(), {}, {}, {}};

      for (const auto& item : root["parameters"]) {
        Codec::requireFields(item, {"name", "value"});
        description.parameters.push_back(
            {item["name"].get<std::string>(), parameterValue(item["value"])});
      }

      for (const auto& item : root["states"]) {
        Codec::requireFields(item, {"name", "motion", "speed", "looping"});

        if (!item["speed"].is_number()) {
          throw std::invalid_argument("Controller state speed requires a number");
        }

        const auto& motion = item["motion"];
        const auto kind = motion.at("kind").get<std::string>();
        ControllerMotion selected;

        if (kind == "clip") {
          Codec::requireFields(motion, {"kind", "clip"});
          selected = motion["clip"].get<std::string>();
        } else if (kind == "blend1d") {
          Codec::requireFields(motion, {"kind", "parameter", "points"});

          if (!motion["points"].is_array() || motion["points"].size() > 16) {
            throw std::invalid_argument("1D controller motion requires <=16 points");
          }

          ControllerBlend1D blend{motion["parameter"].get<std::string>(), {}};

          for (const auto& point : motion["points"]) {
            Codec::requireFields(point, {"threshold", "clip", "phaseOffset"});

            if (!point["threshold"].is_number() || !point["phaseOffset"].is_number()) {
              throw std::invalid_argument("1D motion threshold/phase offset require numbers");
            }

            blend.points.push_back({point["threshold"].get<double>(),
                                    point["clip"].get<std::string>(),
                                    point["phaseOffset"].get<double>()});
          }

          selected = std::move(blend);
        } else {
          throw std::invalid_argument("Unknown controller motion kind");
        }

        description.states.push_back({item["name"].get<std::string>(), std::move(selected),
                                      item["speed"].get<double>(), item["looping"].get<bool>()});
      }

      for (const auto& item : root["transitions"]) {
        Codec::requireFields(item, {"name", "from", "to", "conditions", "fadeSeconds",
                                    "minimumSeconds", "exitTime"});

        if (!item["conditions"].is_array() || item["conditions"].size() > 8
            || !item["fadeSeconds"].is_number() || !item["minimumSeconds"].is_number()
            || (!item["exitTime"].is_null() && !item["exitTime"].is_number())) {
          throw std::invalid_argument("Invalid controller transition types/capacities");
        }

        ControllerTransition transition{item["name"].get<std::string>(),
                                        item["from"].get<std::string>(),
                                        item["to"].get<std::string>(),
                                        {},
                                        item["fadeSeconds"].get<double>(),
                                        item["minimumSeconds"].get<double>(),
                                        {}};

        if (!item["exitTime"].is_null()) {
          transition.exitTime = item["exitTime"].get<double>();
        }

        for (const auto& condition : item["conditions"]) {
          Codec::requireFields(condition, {"parameter", "operation", "value"});
          const auto op = condition["operation"].get<std::string>();
          const auto found = std::find(operations.begin(), operations.end(), op);

          if (found == operations.end()) {
            throw std::invalid_argument("Unknown animation controller condition operation");
          }

          transition.conditions.push_back({condition["parameter"].get<std::string>(),
                                           static_cast<ConditionOp>(found - operations.begin()),
                                           parameterValue(condition["value"])});
        }

        description.transitions.push_back(std::move(transition));
      }

      return description;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Decode animation controller: " + std::string(error.what()));
    }
  }

  auto decodeController(std::string_view source) -> Core::Result<Controller> {
    auto form = decodeControllerForm(source);
    return form ? Controller::create(std::move(*form)) : std::unexpected(form.error());
  }

  auto encodeController(const Controller& controller) -> Core::Result<std::string> {
    auto spec = controller.description();

    if (!spec) {
      return std::unexpected(spec.error());
    }

    try {
      Json parameters = Json::array(), states = Json::array(), transitions = Json::array();

      for (const auto& parameter : (*spec)->parameters) {
        parameters.push_back({{"name", parameter.name}, {"value", parameterJson(parameter.value)}});
      }

      for (const auto& state : (*spec)->states) {
        Json motion;

        if (const auto* clip = std::get_if<std::string>(&state.motion)) {
          motion = {{"kind", "clip"}, {"clip", *clip}};
        } else {
          const auto& blend = std::get<ControllerBlend1D>(state.motion);
          Json points = Json::array();

          for (const auto& point : blend.points) {
            points.push_back({{"threshold", point.threshold},
                              {"clip", point.clip},
                              {"phaseOffset", point.phaseOffset}});
          }

          motion = {
              {"kind", "blend1d"}, {"parameter", blend.parameter}, {"points", std::move(points)}};
        }

        states.push_back({{"name", state.name},
                          {"motion", std::move(motion)},
                          {"speed", state.speed},
                          {"looping", state.looping}});
      }

      for (const auto& transition : (*spec)->transitions) {
        Json conditions = Json::array();

        for (const auto& condition : transition.conditions) {
          conditions.push_back(
              {{"parameter", condition.parameter},
               {"operation", operations.at(static_cast<std::size_t>(condition.operation))},
               {"value", parameterJson(condition.value)}});
        }

        transitions.push_back(
            {{"name", transition.name},
             {"from", transition.from},
             {"to", transition.to},
             {"conditions", std::move(conditions)},
             {"fadeSeconds", transition.fadeSeconds},
             {"minimumSeconds", transition.minimumSeconds},
             {"exitTime", transition.exitTime ? Json(*transition.exitTime) : Json(nullptr)}});
      }

      auto text = Json{{"name", (*spec)->name},
                       {"initial", (*spec)->initial},
                       {"parameters", std::move(parameters)},
                       {"states", std::move(states)},
                       {"transitions", std::move(transitions)}}
                      .dump(2)
                  + '\n';

      if (text.size() > maxBytes) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Encoded animation controller exceeds 1 MiB");
      }

      return text;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Encode animation controller: " + std::string(error.what()));
    }
  }

  auto loadController(const Core::FileSystem& files, const std::filesystem::path& path)
      -> Core::Result<Controller> {
    auto checked = checkedPath(files, path, false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto text = files.readText(path, maxBytes);
    auto decoded
        = text ? decodeController(*text) : Core::Result<Controller>{std::unexpected(text.error())};

    if (!decoded) {
      const auto utf = path.generic_u8string();
      const std::string name(utf.begin(), utf.end());
      return Core::fail(decoded.error().code,
                        "Load controller '" + name + "': " + decoded.error().message);
    }

    return decoded;
  }

  auto saveController(const Core::FileSystem& files, const std::filesystem::path& path,
                      const Controller& controller) -> Core::Result<void> {
    auto text = encodeController(controller);

    if (!text) {
      return std::unexpected(text.error());
    }

    auto checked = checkedPath(files, path, true);
    return checked ? files.writeTextAtomic(path, *text) : std::unexpected(checked.error());
  }

  auto decodeControllerParameters(std::string_view source)
      -> Core::Result<std::vector<ControllerParameter>> {
    if (source.empty() || source.size() > 16 * 1024 || source.find('\0') != std::string_view::npos
        || !Core::isValidUtf8(source)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation parameter JSON bytes");
    }

    try {
      const auto root = Codec::parseJson(source, 4);

      if (!root.is_object() || root.size() > 64) {
        throw std::invalid_argument("Animation parameters require an object with <=64 names");
      }

      std::vector<ControllerParameter> result;

      for (const auto& [name, value] : root.items()) {
        if (!validLayerName(name)) {
          throw std::invalid_argument("Invalid animation parameter name");
        }

        result.push_back({name, parameterValue(value)});
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Decode animation parameters: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation
