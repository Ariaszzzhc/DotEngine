module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

module Dot.Editor.AnimationTimeline;

namespace Dot::Editor {
  namespace {
    auto validRange(double from, double to) -> bool {
      return std::isfinite(from) && std::isfinite(to) && from >= 0 && to <= 86400 && from <= to;
    }

    using KeyRange = std::pair<std::vector<Animation::Key>::const_iterator,
                               std::vector<Animation::Key>::const_iterator>;

    auto range(const Animation::Track& track, double from, double to) -> KeyRange {
      return std::pair{std::ranges::lower_bound(track.keys, from, {}, &Animation::Key::time),
                       std::ranges::upper_bound(track.keys, to, {}, &Animation::Key::time)};
    }
  }  // namespace

  AnimationTimeline::AnimationTimeline(Animation::Clip clip, std::vector<std::size_t> tracks)
      : m_clip(std::move(clip)), m_tracks(std::move(tracks)) {
  }

  AnimationTimeline::AnimationTimeline(AnimationTimeline&&) noexcept = default;

  auto AnimationTimeline::operator=(AnimationTimeline&&) noexcept -> AnimationTimeline& = default;

  AnimationTimeline::~AnimationTimeline() = default;

  auto AnimationTimeline::create(Animation::Clip clip) -> Core::Result<AnimationTimeline> {
    auto description = clip.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    try {
      std::vector<std::size_t> tracks((*description)->tracks.size());
      std::iota(tracks.begin(), tracks.end(), std::size_t{});
      std::ranges::sort(tracks, [&](auto a, auto b) {
        const auto& left = (*description)->tracks[a];
        const auto& right = (*description)->tracks[b];
        return std::tie(left.node, left.path) < std::tie(right.node, right.path);
      });
      return AnimationTimeline{std::move(clip), std::move(tracks)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Index animation timeline: " + std::string(error.what()));
    }
  }

  auto AnimationTimeline::rows() const -> Core::Result<std::size_t> {
    auto value = m_clip.description();
    return value ? Core::Result<std::size_t>{m_tracks.size()} : std::unexpected(value.error());
  }

  auto AnimationTimeline::row(std::uint32_t node, Animation::Path path) const
      -> Core::Result<std::size_t> {
    auto value = m_clip.description();

    if (!value) {
      return std::unexpected(value.error());
    }

    const auto found
        = std::ranges::lower_bound(m_tracks, std::pair{node, path}, {}, [&](auto index) {
            const auto& track = (*value)->tracks[index];
            return std::pair{track.node, track.path};
          });

    if (found == m_tracks.end() || (*value)->tracks[*found].node != node
        || (*value)->tracks[*found].path != path) {
      return Core::fail(Core::ErrorCode::NotFound, "Animation timeline track not found");
    }

    return static_cast<std::size_t>(found - m_tracks.begin());
  }

  auto AnimationTimeline::page(double from, double to, std::size_t offset, std::size_t count,
                               std::size_t columns) const -> Core::Result<TimelinePage> {
    auto value = m_clip.description();

    if (!value) {
      return std::unexpected(value.error());
    }

    if (!validRange(from, to) || to - from < 1e-6 || offset > m_tracks.size() || count == 0
        || count > 32 || columns == 0 || columns > 128) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation timeline page range");
    }

    try {
      TimelinePage result{m_tracks.size(), offset, {}};
      result.rows.reserve(std::min(count, m_tracks.size() - offset));

      for (std::size_t i = offset; i < m_tracks.size() && i - offset < count; ++i) {
        const auto& track = (*value)->tracks[m_tracks[i]];
        const auto [first, last] = range(track, from, to);
        const auto visible = static_cast<std::size_t>(last - first);
        TimelineRow row{i, track.node, track.path, track.keys.size(), visible, {}, {}};

        if (visible <= 256) {
          row.times.reserve(visible);

          for (auto key = first; key != last; ++key) {
            row.times.push_back(key->time);
          }
        } else {
          row.bins.reserve(columns);
          auto begin = first;

          for (std::size_t column = 0; column < columns; ++column) {
            const auto end
                = column + 1 == columns
                      ? last
                      : std::lower_bound(begin, last,
                                         from
                                             + (to - from) * static_cast<double>(column + 1)
                                                   / static_cast<double>(columns),
                                         [](const auto& key, double at) { return key.time < at; });

            if (begin != end) {
              row.bins.push_back(
                  {begin->time, (end - 1)->time, static_cast<std::size_t>(end - begin)});
            }

            begin = end;
          }
        }

        result.rows.push_back(std::move(row));
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read animation timeline: " + std::string(error.what()));
    }
  }

  auto AnimationTimeline::select(double from, double to, std::size_t first, std::size_t last) const
      -> Core::Result<std::vector<Animation::KeyReference>> {
    auto value = m_clip.description();

    if (!value) {
      return std::unexpected(value.error());
    }

    if (!validRange(from, to) || first > last || last >= m_tracks.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid animation timeline selection range");
    }

    try {
      std::size_t count{};

      for (auto i = first; i <= last; ++i) {
        const auto [begin, end] = range((*value)->tracks[m_tracks[i]], from, to);
        const auto size = static_cast<std::size_t>(end - begin);

        if (size > Animation::MaxSelectedKeys - count) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Timeline selection exceeds 16384 keys; narrow the row/time range");
        }

        count += size;
      }

      std::vector<Animation::KeyReference> result;
      result.reserve(count);

      for (auto i = first; i <= last; ++i) {
        const auto& track = (*value)->tracks[m_tracks[i]];
        const auto [begin, end] = range(track, from, to);

        for (auto key = begin; key != end; ++key) {
          result.push_back({track.node, track.path, key->time});
        }
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Select animation timeline: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor
