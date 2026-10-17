module;

#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

module Dot.Core.Application;

import Dot.Core.Result;

namespace Dot::Core {
  auto encodeApplicationProfiling(const ApplicationProfiling& profile, bool history)
      -> Result<std::string> {
    try {
      using Json = nlohmann::json;
      const auto optional = [](const std::optional<std::uint64_t>& value) {
        return value ? Json(std::to_string(*value)) : Json(nullptr);
      };
      const auto sample = [&](const ApplicationFrameTiming& value) {
        Json error = nullptr;

        if (value.error) {
          constexpr std::array codes{"InvalidArgument", "NotFound",     "Io",
                                     "Unavailable",     "InvalidState", "External"};
          error = {{"code", codes.at(static_cast<std::size_t>(value.error->code))},
                   {"message", value.error->message}};
        }

        return Json{{"frame", std::to_string(value.frame.index)},
                    {"running", value.frame.running},
                    {"paused", value.frame.paused},
                    {"updates", std::to_string(value.frame.updates)},
                    {"elapsedNanoseconds", std::to_string(value.frame.elapsed.count())},
                    {"droppedNanoseconds", std::to_string(value.frame.dropped.count())},
                    {"totalNanoseconds", std::to_string(value.totalNanoseconds)},
                    {"eventsNanoseconds", optional(value.eventsNanoseconds)},
                    {"prepareNanoseconds", optional(value.prepareNanoseconds)},
                    {"fixedNanoseconds", optional(value.fixedNanoseconds)},
                    {"updateNanoseconds", optional(value.updateNanoseconds)},
                    {"renderNanoseconds", optional(value.renderNanoseconds)},
                    {"error", std::move(error)}};
      };
      Json result{
          {"enabled", profile.enabled},
          {"session", std::to_string(profile.session)},
          {"sampled", std::to_string(profile.sampled)},
          {"retained", profile.frames.size()},
          {"latest", profile.frames.empty() ? Json(nullptr) : sample(profile.frames.back())},
          {"slowest", profile.slowest ? sample(*profile.slowest) : Json(nullptr)},
          {"longestInterval",
           profile.longestInterval ? sample(*profile.longestInterval) : Json(nullptr)},
          {"phasePeaks", Json::object()}};
      const std::array phaseMembers{std::pair{"events", &ApplicationPhasePeaks::events},
                                    std::pair{"prepare", &ApplicationPhasePeaks::prepare},
                                    std::pair{"fixed", &ApplicationPhasePeaks::fixed},
                                    std::pair{"update", &ApplicationPhasePeaks::update},
                                    std::pair{"render", &ApplicationPhasePeaks::render}};

      for (const auto& [name, member] : phaseMembers) {
        const auto& peak = profile.phasePeaks.*member;
        result["phasePeaks"][name] = peak ? sample(*peak) : Json(nullptr);
      }

      if (history) {
        result["frames"] = Json::array();

        for (const auto& frame : profile.frames) {
          result["frames"].push_back(sample(frame));
        }
      }

      return result.dump();
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Encode application profile: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Core
