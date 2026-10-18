module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

module Dot.Animation.Controller;

import Dot.Animation.Layers;
import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Animation {
  namespace {
    auto validValue(const ParameterValue& value) -> bool {
      const auto* number = std::get_if<double>(&value);
      return !number || (std::isfinite(*number) && std::abs(*number) <= 1000000);
    }

    auto validClip(const std::string& clip) -> bool {
      return clip.size() <= 1024 && Core::isPortableRelativePath(clip)
             && clip.starts_with("assets/") && clip.ends_with(".dotanimation");
    }

    auto matches(const ParameterValue& actual, ConditionOp operation,
                 const ParameterValue& expected) -> bool {
      if (operation == ConditionOp::Equal) {
        return actual == expected;
      }

      if (operation == ConditionOp::NotEqual) {
        return actual != expected;
      }

      const auto a = std::get<double>(actual);
      const auto b = std::get<double>(expected);

      switch (operation) {
        case ConditionOp::Less:
          return a < b;
        case ConditionOp::LessEqual:
          return a <= b;
        case ConditionOp::Greater:
          return a > b;
        case ConditionOp::GreaterEqual:
          return a >= b;
        default:
          return false;
      }
    }
  }  // namespace

  struct Controller::Data {
    struct Condition {
      std::size_t parameter;
      ConditionOp operation;
      ParameterValue value;
    };

    struct Edge {
      std::size_t source;
      std::size_t target;
      std::vector<Condition> conditions;
    };

    ControllerDescription description;
    std::map<std::string, std::size_t, std::less<>> parameters;
    std::vector<Edge> edges;
    std::vector<std::vector<std::size_t>> outgoing;
    std::size_t initial{};
  };

  Controller::Controller(std::shared_ptr<const Data> data, std::vector<ParameterValue> values,
                         std::size_t initial)
      : m_data(std::move(data)),
        m_values(std::make_shared<const std::vector<ParameterValue>>(std::move(values))),
        m_state(initial) {
  }

  Controller::Controller(const Controller&) = default;

  auto Controller::operator=(const Controller& other) -> Controller& {
    if (this != &other) {
      Controller candidate(other);
      m_data.swap(candidate.m_data);
      m_values.swap(candidate.m_values);
      std::swap(m_state, candidate.m_state);
      std::swap(m_elapsed, candidate.m_elapsed);
      std::swap(m_last, candidate.m_last);
      std::swap(m_transitions, candidate.m_transitions);
    }

    return *this;
  }

  Controller::Controller(Controller&&) noexcept = default;

  auto Controller::operator=(Controller&& other) noexcept -> Controller& {
    if (this != &other) {
      m_data = std::move(other.m_data);
      m_values = std::move(other.m_values);
      m_state = other.m_state;
      m_elapsed = other.m_elapsed;
      m_last = other.m_last;
      m_transitions = other.m_transitions;
    }

    return *this;
  }

  Controller::~Controller() = default;

  auto Controller::create(ControllerDescription description) -> Core::Result<Controller> {
    if (description.name.empty() || description.name.size() > 256
        || description.name.find('\0') != std::string::npos || !Core::isValidUtf8(description.name)
        || description.parameters.size() > 64 || description.states.empty()
        || description.states.size() > 64 || description.transitions.size() > 512) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid animation controller capacities/name");
    }

    try {
      auto data = std::make_shared<Data>();
      std::vector<ParameterValue> values;
      std::map<std::string, std::size_t, std::less<>> states;
      std::set<std::string> transitions;

      for (const auto& parameter : description.parameters) {
        if (!validLayerName(parameter.name) || !validValue(parameter.value)
            || !data->parameters.emplace(parameter.name, values.size()).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid/duplicate controller parameter: " + parameter.name);
        }

        values.push_back(parameter.value);
      }

      for (const auto& state : description.states) {
        if (!validLayerName(state.name) || !states.emplace(state.name, states.size()).second
            || !std::isfinite(state.speed) || std::abs(state.speed) > 16) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid/duplicate controller state: " + state.name);
        }
      }

      for (const auto& state : description.states) {
        if (const auto* clip = std::get_if<std::string>(&state.motion)) {
          if (!validClip(*clip)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Invalid controller clip: " + state.name);
          }
        } else {
          const auto& blend = std::get<ControllerBlend1D>(state.motion);
          const auto parameter = data->parameters.find(blend.parameter);

          if (parameter == data->parameters.end()
              || !std::holds_alternative<double>(values[parameter->second])
              || blend.points.size() < 2 || blend.points.size() > 16) {
            return Core::fail(
                Core::ErrorCode::InvalidArgument,
                "1D controller motion requires numeric parameter and 2..16 points: " + state.name);
          }

          for (std::size_t i = 0; i < blend.points.size(); ++i) {
            const auto& point = blend.points[i];

            if (!validClip(point.clip) || !std::isfinite(point.threshold)
                || std::abs(point.threshold) > 1000000 || !std::isfinite(point.phaseOffset)
                || std::abs(point.phaseOffset) > 1
                || (i && blend.points[i - 1].threshold >= point.threshold)) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Invalid ordered controller blend point: " + state.name);
            }
          }
        }
      }

      const auto initial = states.find(description.initial);

      if (initial == states.end()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Controller initial state is missing");
      }

      data->initial = initial->second;
      data->outgoing.resize(states.size());

      for (const auto& transition : description.transitions) {
        const auto from = states.find(transition.from);
        const auto to = states.find(transition.to);

        if (!validLayerName(transition.name) || !transitions.insert(transition.name).second
            || from == states.end() || to == states.end() || transition.conditions.size() > 8
            || !std::isfinite(transition.fadeSeconds) || transition.fadeSeconds < 0
            || transition.fadeSeconds > 86400 || !std::isfinite(transition.minimumSeconds)
            || transition.minimumSeconds < 0 || transition.minimumSeconds > 86400
            || (transition.exitTime
                && (!std::isfinite(*transition.exitTime) || *transition.exitTime < 0
                    || *transition.exitTime > 1))) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid controller transition: " + transition.name);
        }

        Data::Edge edge{from->second, to->second, {}};

        for (const auto& condition : transition.conditions) {
          const auto parameter = data->parameters.find(condition.parameter);

          if (parameter == data->parameters.end() || !validValue(condition.value)
              || condition.value.index() != values[parameter->second].index()
              || condition.operation < ConditionOp::Equal
              || condition.operation > ConditionOp::GreaterEqual
              || (std::holds_alternative<bool>(condition.value)
                  && condition.operation != ConditionOp::Equal
                  && condition.operation != ConditionOp::NotEqual)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Invalid/ill-typed condition " + condition.parameter
                                  + " in controller transition " + transition.name);
          }

          edge.conditions.push_back({parameter->second, condition.operation, condition.value});
        }

        data->outgoing[from->second].push_back(data->edges.size());
        data->edges.push_back(std::move(edge));
      }

      data->description = std::move(description);
      const auto entry = data->initial;
      return Controller(std::move(data), std::move(values), entry);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create animation controller: " + std::string(error.what()));
    }
  }

  auto Controller::description() const -> Core::Result<const ControllerDescription*> {
    return m_data ? Core::Result<const ControllerDescription*>{&m_data->description}
                  : Core::fail(Core::ErrorCode::InvalidState,
                               "Animation controller is empty or moved");
  }

  auto Controller::state() const -> Core::Result<const ControllerState*> {
    return m_data ? Core::Result<const ControllerState*>{&m_data->description.states[m_state]}
                  : Core::fail(Core::ErrorCode::InvalidState,
                               "Animation controller is empty or moved");
  }

  auto Controller::parameter(std::string_view name) const -> Core::Result<ParameterValue> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation controller is empty or moved");
    }

    const auto found = m_data->parameters.find(name);
    return found != m_data->parameters.end()
               ? Core::Result<ParameterValue>{(*m_values)[found->second]}
               : Core::fail(Core::ErrorCode::NotFound,
                            "Controller parameter is missing: " + std::string(name));
  }

  auto Controller::snapshot() const -> Core::Result<ControllerSnapshot> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation controller is empty or moved");
    }

    try {
      ControllerSnapshot result{
          m_data->description.states[m_state].name, m_elapsed, {}, {}, m_transitions};

      for (std::size_t i = 0; i < m_values->size(); ++i) {
        result.parameters.push_back({m_data->description.parameters[i].name, (*m_values)[i]});
      }

      if (m_last) {
        result.lastTransition = m_data->description.transitions[*m_last].name;
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read animation controller: " + std::string(error.what()));
    }
  }

  auto Controller::transitionCount() const -> Core::Result<std::uint64_t> {
    return m_data ? Core::Result<std::uint64_t>{m_transitions}
                  : Core::fail(Core::ErrorCode::InvalidState,
                               "Animation controller is empty or moved");
  }

  auto Controller::lastTransition() const -> Core::Result<const ControllerTransition*> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation controller is empty or moved");
    }

    return m_last ? &m_data->description.transitions[*m_last] : nullptr;
  }

  auto Controller::withParameters(std::span<const ControllerParameter> parameters) const
      -> Core::Result<Controller> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation controller is empty or moved");
    }

    if (parameters.size() > 64) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Too many animation controller parameters");
    }

    try {
      std::array<bool, 64> assigned{};
      bool changed{};

      for (const auto& parameter : parameters) {
        const auto at = m_data->parameters.find(parameter.name);

        if (at == m_data->parameters.end() || assigned[at->second] || !validValue(parameter.value)
            || parameter.value.index() != (*m_values)[at->second].index()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid animation parameter assignment: " + parameter.name);
        }

        assigned[at->second] = true;
        changed = changed || parameter.value != (*m_values)[at->second];
      }

      auto candidate = *this;

      if (!changed) {
        return candidate;
      }

      auto values = std::make_shared<std::vector<ParameterValue>>(*m_values);

      for (const auto& parameter : parameters) {
        (*values)[m_data->parameters.at(parameter.name)] = parameter.value;
      }

      candidate.m_values = std::move(values);
      return candidate;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Set animation parameters: " + std::string(error.what()));
    }
  }

  auto Controller::advanced(double seconds, double clipTime, double duration) const
      -> Core::Result<Controller> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation controller is empty or moved");
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1 || !std::isfinite(clipTime)
        || !std::isfinite(duration) || duration < 0 || duration > 86400 || clipTime < 0
        || clipTime > duration || !std::isfinite(m_elapsed + seconds)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation controller clocks");
    }

    try {
      auto candidate = *this;

      if (seconds == 0) {
        return candidate;
      }

      const auto& state = m_data->description.states[m_state];
      const auto position = state.speed < 0 ? duration - clipTime : clipTime;
      const auto progress
          = duration == 0 ? 1 : (position + std::abs(state.speed) * seconds) / duration;

      for (const auto i : m_data->outgoing[m_state]) {
        const auto& edge = m_data->edges[i];
        const auto& transition = m_data->description.transitions[i];

        if (edge.source != m_state || m_elapsed < transition.minimumSeconds
            || (transition.exitTime && progress < *transition.exitTime)
            || !std::ranges::all_of(
                edge.conditions,
                [&](const auto& condition) {
                  return matches((*m_values)[condition.parameter], condition.operation,
                                 condition.value);
                })) {
          continue;
        }

        if (m_transitions == std::numeric_limits<std::uint64_t>::max()) {
          return Core::fail(Core::ErrorCode::Unavailable, "Animation transition counter exhausted");
        }

        candidate.m_state = edge.target;
        candidate.m_last = i;
        ++candidate.m_transitions;
        candidate.m_elapsed = seconds;
        return candidate;
      }

      candidate.m_elapsed += seconds;
      return candidate;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Advance animation controller: " + std::string(error.what()));
    }
  }

  auto Controller::reset() const -> Core::Result<Controller> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation controller is empty or moved");
    }

    try {
      std::vector<ParameterValue> values;

      for (const auto& parameter : m_data->description.parameters) {
        values.push_back(parameter.value);
      }

      return Controller(m_data, std::move(values), m_data->initial);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Reset animation controller: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation
