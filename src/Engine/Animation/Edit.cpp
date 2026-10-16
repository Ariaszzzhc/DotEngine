module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

module Dot.Animation.Edit;

import Dot.Core.Result;

namespace Dot::Animation {
  auto editClip(const Clip& clip, std::span<const ClipEdit> edits) -> Core::Result<Clip> {
    auto source = clip.description();

    if (!source) {
      return std::unexpected(source.error());
    }

    if (edits.size() > 256) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Animation edit batch exceeds 256");
    }

    if (edits.empty()) {
      return clip;
    }

    try {
      auto candidate = **source;

      for (std::size_t index = 0; index < edits.size(); ++index) {
        auto applied = std::visit(
            [&](const auto& edit) -> Core::Result<void> {
              using T = std::decay_t<decltype(edit)>;

              if constexpr (std::is_same_v<T, RenameClip>) {
                candidate.name = edit.name;
                return {};
              } else {
                auto track = std::ranges::find_if(candidate.tracks, [&](const auto& value) {
                  return value.node == edit.node && value.path == edit.path;
                });

                if constexpr (std::is_same_v<T, PutKey>) {
                  if (!std::isfinite(edit.key.time) || edit.key.time < 0 || edit.key.time > 86400) {
                    return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid key time");
                  }

                  if (track == candidate.tracks.end()) {
                    candidate.tracks.push_back(
                        {edit.node, edit.path, edit.interpolation, {edit.key}});
                  } else {
                    if (track->interpolation != edit.interpolation) {
                      return Core::fail(Core::ErrorCode::InvalidArgument,
                                        "Key interpolation differs from track");
                    }

                    auto key = std::ranges::lower_bound(track->keys, edit.key.time, {}, &Key::time);

                    if (key != track->keys.end() && key->time == edit.key.time) {
                      *key = edit.key;
                    } else {
                      track->keys.insert(key, edit.key);
                    }
                  }
                } else {
                  if (track == candidate.tracks.end()) {
                    return Core::fail(Core::ErrorCode::NotFound, "Animation track does not exist");
                  }

                  if constexpr (std::is_same_v<T, RemoveTrack>) {
                    candidate.tracks.erase(track);
                  } else if constexpr (std::is_same_v<T, UpdateKey>) {
                    if (!std::isfinite(edit.time) || edit.time < 0 || edit.time > 86400
                        || !std::isfinite(edit.key.time) || edit.key.time < 0
                        || edit.key.time > 86400) {
                      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid key time");
                    }

                    auto key = std::ranges::lower_bound(track->keys, edit.time, {}, &Key::time);

                    if (key == track->keys.end() || key->time != edit.time) {
                      return Core::fail(Core::ErrorCode::NotFound, "Animation key does not exist");
                    }

                    const auto destination
                        = std::ranges::lower_bound(track->keys, edit.key.time, {}, &Key::time);

                    if (edit.key.time != edit.time && destination != track->keys.end()
                        && destination->time == edit.key.time) {
                      return Core::fail(Core::ErrorCode::InvalidArgument,
                                        "Animation destination key time is occupied");
                    }

                    track->keys.erase(key);
                    key = std::ranges::lower_bound(track->keys, edit.key.time, {}, &Key::time);
                    track->keys.insert(key, edit.key);
                  } else if constexpr (std::is_same_v<T, RemoveKey>) {
                    if (!std::isfinite(edit.time) || edit.time < 0 || edit.time > 86400) {
                      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid key time");
                    }

                    auto key = std::ranges::lower_bound(track->keys, edit.time, {}, &Key::time);

                    if (key == track->keys.end() || key->time != edit.time) {
                      return Core::fail(Core::ErrorCode::NotFound, "Animation key does not exist");
                    }

                    track->keys.erase(key);

                    if (track->keys.empty()) {
                      candidate.tracks.erase(track);
                    }
                  } else if (track->interpolation != edit.interpolation) {
                    track->interpolation = edit.interpolation;

                    for (auto& key : track->keys) {
                      key.inTangent = {};
                      key.outTangent = {};
                    }
                  }
                }

                return {};
              }
            },
            edits[index]);

        if (!applied) {
          return Core::fail(applied.error().code, "Animation edit " + std::to_string(index) + ": "
                                                      + applied.error().message);
        }
      }

      return Clip::create(std::move(candidate));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Edit animation: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Animation
