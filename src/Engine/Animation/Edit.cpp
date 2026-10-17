module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module Dot.Animation.Edit;

import Dot.Core.Result;

namespace Dot::Animation {
  namespace {
    auto component(double value) -> Core::Result<float> {
      if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Key adjustment overflows float");
      }

      return static_cast<float>(value);
    }

    /** Quaternion right multiplication is linear even for non-unit derivatives.
     * 四元数右乘对非单位导数同样线性，禁止按旋转值归一化切线。 */
    auto postRotate(Math::Vec4 value, Math::Quat rotation) -> Core::Result<Math::Vec4> {
      const std::array<double, 4> a{value.x, value.y, value.z, value.w};
      const std::array<double, 4> b{rotation.x, rotation.y, rotation.z, rotation.w};
      const std::array products{a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                                a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                                a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                                a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
      std::array<float, 4> result;

      for (std::size_t i = 0; i < result.size(); ++i) {
        auto checked = component(products[i]);

        if (!checked) {
          return std::unexpected(checked.error());
        }

        result[i] = *checked;
      }

      return Math::Vec4{result[0], result[1], result[2], result[3]};
    }

    auto transform(Key& key, const Track& track, const TransformKeys& edit) -> Core::Result<void> {
      if (track.path == Path::Rotation) {
        if (edit.rotation == Math::Quat{}) {
          return {};
        }

        for (auto* value : {&key.value, &key.inTangent, &key.outTangent}) {
          if (value != &key.value && track.interpolation != Interpolation::CubicSpline) {
            continue;
          }

          auto rotated = postRotate(*value, edit.rotation);

          if (!rotated) {
            return std::unexpected(rotated.error());
          }

          *value = *rotated;
        }

        return {};
      }

      const bool scale = track.path == Path::Scale;
      const std::array factors{edit.scale.x, edit.scale.y, edit.scale.z};
      const std::array offsets{edit.translation.x, edit.translation.y, edit.translation.z};

      for (auto* value : {&key.value, &key.inTangent, &key.outTangent}) {
        if (value != &key.value && (!scale || track.interpolation != Interpolation::CubicSpline)) {
          continue;
        }

        const std::array axes{&value->x, &value->y, &value->z};

        for (std::size_t i = 0; i < axes.size(); ++i) {
          if ((scale && factors[i] == 1) || (!scale && offsets[i] == 0)) {
            continue;
          }

          auto checked = component(scale ? static_cast<double>(*axes[i]) * factors[i]
                                         : static_cast<double>(*axes[i]) + offsets[i]);

          if (!checked) {
            return std::unexpected(checked.error());
          }

          *axes[i] = *checked;
        }
      }

      return {};
    }
  }  // namespace

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

              if constexpr (std::is_same_v<T, TransformKeys>) {
                const std::array<double, 4> q{edit.rotation.x, edit.rotation.y, edit.rotation.z,
                                              edit.rotation.w};
                const auto length
                    = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

                if (edit.keys.empty() || edit.keys.size() > MaxSelectedKeys
                    || !Math::isFinite(edit.translation) || !Math::isFinite(edit.scale)
                    || !Math::isFinite(edit.rotation) || std::abs(length - 1) > 1e-4
                    || edit.scale.x == 0 || edit.scale.y == 0 || edit.scale.z == 0) {
                  return Core::fail(Core::ErrorCode::InvalidArgument,
                                    "Invalid key adjustment count/offset/unit rotation/scale");
                }

                std::map<std::pair<std::uint32_t, Path>, std::size_t> channels;

                for (std::size_t i = 0; i < candidate.tracks.size(); ++i) {
                  channels.emplace(std::pair{candidate.tracks[i].node, candidate.tracks[i].path},
                                   i);
                }

                std::set<std::pair<std::size_t, float>> selected;

                for (const auto& item : edit.keys) {
                  if (!std::isfinite(item.time) || item.time < 0 || item.time > 86400) {
                    return Core::fail(Core::ErrorCode::InvalidArgument,
                                      "Invalid adjustment key time");
                  }

                  const auto channel = channels.find({item.node, item.path});

                  if (channel == channels.end()) {
                    return Core::fail(Core::ErrorCode::NotFound, "Adjustment track does not exist");
                  }

                  auto& track = candidate.tracks[channel->second];
                  auto key = std::ranges::lower_bound(track.keys, item.time, {}, &Key::time);

                  if (key == track.keys.end() || key->time != item.time) {
                    return Core::fail(Core::ErrorCode::NotFound, "Adjustment key does not exist");
                  }

                  if (!selected.emplace(channel->second, item.time).second) {
                    return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate adjustment key");
                  }

                  auto changed = transform(*key, track, edit);

                  if (!changed) {
                    return changed;
                  }
                }

                return {};
              } else if constexpr (std::is_same_v<T, PasteKeys>) {
                if (edit.collision != KeyCollision::Reject
                    && edit.collision != KeyCollision::Replace) {
                  return Core::fail(Core::ErrorCode::InvalidArgument,
                                    "Invalid paste collision policy");
                }

                auto placed = edit.group.at(edit.time);

                if (!placed) {
                  return std::unexpected(placed.error());
                }

                std::map<std::pair<std::uint32_t, Path>, std::size_t> channels;

                for (std::size_t i = 0; i < candidate.tracks.size(); ++i) {
                  channels.emplace(std::pair{candidate.tracks[i].node, candidate.tracks[i].path},
                                   i);
                }

                std::map<std::size_t, std::vector<Key>> additions;

                for (const auto& item : *placed) {
                  const auto identity = std::pair{item.node, item.path};
                  auto channel = channels.find(identity);

                  if (channel == channels.end()) {
                    const auto index = candidate.tracks.size();
                    candidate.tracks.push_back({item.node, item.path, item.interpolation, {}});
                    channel = channels.emplace(identity, index).first;
                  }

                  if (candidate.tracks[channel->second].interpolation != item.interpolation) {
                    return Core::fail(Core::ErrorCode::InvalidArgument,
                                      "Paste interpolation differs from target channel");
                  }

                  additions[channel->second].push_back(item.key);
                }

                for (auto& [channel, keys] : additions) {
                  std::ranges::sort(keys, {}, &Key::time);
                  auto& track = candidate.tracks[channel];
                  std::vector<Key> merged;
                  merged.reserve(track.keys.size() + keys.size());
                  auto old = track.keys.begin();
                  auto added = keys.begin();

                  while (old != track.keys.end() || added != keys.end()) {
                    if (added == keys.end()
                        || (old != track.keys.end() && old->time < added->time)) {
                      merged.push_back(*old++);
                    } else if (old == track.keys.end() || added->time < old->time) {
                      merged.push_back(*added++);
                    } else {
                      if (edit.collision == KeyCollision::Reject) {
                        return Core::fail(Core::ErrorCode::InvalidArgument,
                                          "Paste destination key time is occupied");
                      }

                      merged.push_back(*added++);
                      ++old;
                    }
                  }

                  track.keys = std::move(merged);
                }

                return {};
              } else if constexpr (std::is_same_v<T, RetimeKeys>) {
                if (edit.keys.empty() || edit.keys.size() > MaxSelectedKeys) {
                  return Core::fail(Core::ErrorCode::InvalidArgument,
                                    "Retime requires 1..16384 source keys");
                }

                std::map<std::pair<std::uint32_t, Path>, std::size_t> channels;

                for (std::size_t i = 0; i < candidate.tracks.size(); ++i) {
                  channels.emplace(std::pair{candidate.tracks[i].node, candidate.tracks[i].path},
                                   i);
                }

                std::map<std::size_t, std::map<float, float>> changes;

                for (const auto& item : edit.keys) {
                  if (!std::isfinite(item.time) || item.time < 0 || item.time > 86400
                      || !std::isfinite(item.target) || item.target < 0 || item.target > 86400) {
                    return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid retime key time");
                  }

                  const auto channel = channels.find({item.node, item.path});

                  if (channel == channels.end()) {
                    return Core::fail(Core::ErrorCode::NotFound, "Retime track does not exist");
                  }

                  const auto& keys = candidate.tracks[channel->second].keys;
                  const auto key = std::ranges::lower_bound(keys, item.time, {}, &Key::time);

                  if (key == keys.end() || key->time != item.time) {
                    return Core::fail(Core::ErrorCode::NotFound,
                                      "Retime source key does not exist");
                  }

                  if (!changes[channel->second].emplace(item.time, item.target).second) {
                    return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate retime source");
                  }
                }

                for (const auto& [channel, times] : changes) {
                  auto& keys = candidate.tracks[channel].keys;

                  for (auto& key : keys) {
                    const auto change = times.find(key.time);

                    if (change != times.end() && change->second != key.time) {
                      key.time = change->second;
                    }
                  }

                  std::ranges::sort(keys, {}, &Key::time);

                  if (std::ranges::adjacent_find(keys, {}, &Key::time) != keys.end()) {
                    return Core::fail(Core::ErrorCode::InvalidArgument,
                                      "Retime destination key time is occupied");
                  }
                }

                return {};
              } else if constexpr (std::is_same_v<T, RenameClip>) {
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
