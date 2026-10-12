#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <future>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../src/Engine/Audio/FrameQueue.hpp"

namespace {
  using Dot::Audio::Detail::FrameQueue;
}

TEST_CASE("audio.queue_validates_channels_and_power_of_two_capacity") {
  CHECK_THROWS_AS((FrameQueue{0, 64}), std::invalid_argument);
  CHECK_THROWS_AS((FrameQueue{3, 64}), std::invalid_argument);
  CHECK_THROWS_AS((FrameQueue{1, 0}), std::invalid_argument);
  CHECK_THROWS_AS((FrameQueue{1, 32}), std::invalid_argument);
  CHECK_THROWS_AS((FrameQueue{1, 65}), std::invalid_argument);
  CHECK_THROWS_AS((FrameQueue{1, 524288}), std::invalid_argument);
  FrameQueue maximum{2, 262144};
  CHECK(maximum.available() == 262144);
}

TEST_CASE("audio.queue_copies_input_preserves_interleaving_and_zero_fills_underrun") {
  FrameQueue queue{2, 64};
  std::array<float, 6> source{1, -1, 0.5F, -0.5F, 0.25F, -0.25F};
  const auto expected = source;
  REQUIRE(queue.write(source));
  source.fill(99);
  CHECK(queue.queued() == 3);
  std::array<float, 12> output;
  output.fill(99);
  CHECK(queue.read(output.data(), 6) == 3);
  CHECK(std::equal(expected.begin(), expected.end(), output.begin()));
  CHECK(std::ranges::all_of(std::span{output}.subspan(6), [](float value) { return value == 0; }));
  CHECK(queue.available() == 64);
  CHECK(queue.read(output.data(), 6) == 0);
  CHECK(std::ranges::all_of(output, [](float value) { return value == 0; }));
}

TEST_CASE("audio.queue_full_and_invalid_writes_leave_published_data_unchanged") {
  FrameQueue queue{2, 64};
  std::array<float, 128> source;
  source.fill(0.5F);
  REQUIRE(queue.write(source));
  CHECK(queue.queued() == 64);
  CHECK(queue.available() == 0);
  CHECK_FALSE(queue.write(std::array{1.0F, 1.0F}));
  CHECK_FALSE(queue.write({}));
  CHECK_FALSE(queue.write(std::array{1.0F}));
  CHECK_FALSE(queue.write(std::vector<float>(130)));
  std::array<float, 128> output{};
  CHECK(queue.read(output.data(), 64) == 64);
  CHECK(output == source);
}

TEST_CASE("audio.queue_wraps_without_losing_order_and_quiescent_reset_discards_data") {
  FrameQueue queue{1, 64};
  std::array<float, 50> first{};
  std::array<float, 50> second{};

  for (std::size_t index = 0; index < first.size(); ++index) {
    first[index] = static_cast<float>(index);
    second[index] = static_cast<float>(index + 50);
  }

  REQUIRE(queue.write(first));
  std::array<float, 40> consumed{};
  REQUIRE(queue.read(consumed.data(), 40) == 40);
  REQUIRE(queue.write(second));
  std::array<float, 60> remainder{};
  REQUIRE(queue.read(remainder.data(), 60) == 60);

  for (std::size_t index = 0; index < remainder.size(); ++index) {
    CHECK(remainder[index] == static_cast<float>(index + 40));
  }

  REQUIRE(queue.write(first));
  queue.reset();
  CHECK(queue.queued() == 0);
  CHECK(queue.read(remainder.data(), 60) == 0);
  REQUIRE(queue.write(second));
  REQUIRE(queue.read(first.data(), 50) == 50);
  CHECK(first == second);
}

TEST_CASE("audio.queue_concurrent_spsc_delivers_complete_ordered_frames") {
  FrameQueue queue{2, 128};
  constexpr int total = 100000;
  std::atomic<bool> failed{};
  auto consumer = std::async(std::launch::async, [&] {
    int expected = 0;
    std::array<float, 22> output{};

    while (expected < total) {
      const auto read = queue.read(output.data(), 11);

      for (std::uint32_t frame = 0; frame < read; ++frame) {
        if (output[frame * 2] != static_cast<float>(expected)
            || output[frame * 2 + 1] != -static_cast<float>(expected)) {
          failed.store(true);
          return false;
        }

        ++expected;
      }

      if (read == 0) {
        std::this_thread::yield();
      }
    }

    return true;
  });

  for (int frame = 0; frame < total && !failed.load(); ++frame) {
    const std::array source{static_cast<float>(frame), -static_cast<float>(frame)};

    while (!queue.write(source)) {
      if (failed.load()) {
        break;
      }

      std::this_thread::yield();
    }
  }

  CHECK(consumer.get());
  CHECK(queue.queued() == 0);
}
