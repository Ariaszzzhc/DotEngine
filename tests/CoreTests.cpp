#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <memory_resource>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

import Dot.Core;
import Dot.Core.Application;

using namespace Dot::Core;
using namespace std::chrono_literals;

TEST_CASE("core.result.success") {
  Result<int> result{42};
  REQUIRE(result.has_value());
  CHECK(*result == 42);
}

TEST_CASE("core.application_profile_json_preserves_exact_metadata_and_optional_phases") {
  using Json = nlohmann::json;
  ApplicationProfiling profile;
  const auto empty = Json::parse(encodeApplicationProfiling(profile).value());
  CHECK(empty["session"] == "0");
  CHECK(empty["latest"].is_null());
  CHECK(empty["slowest"].is_null());
  CHECK(empty["frames"].empty());
  CHECK(empty["phasePeaks"].size() == 5);

  for (const auto& peak : empty["phasePeaks"]) {
    CHECK(peak.is_null());
  }

  profile.enabled = true;
  profile.session = std::numeric_limits<std::uint64_t>::max();
  profile.sampled = profile.session;
  ApplicationFrameTiming frame;
  frame.frame.index = profile.session;
  frame.frame.updates = std::numeric_limits<std::size_t>::max();
  frame.frame.elapsed = 1s;
  frame.frame.dropped = 1ms;
  frame.totalNanoseconds = profile.session;
  frame.eventsNanoseconds = 0;
  frame.renderNanoseconds = profile.session;
  frame.error = Error{ErrorCode::Io, "读取 \"asset\" failed", std::source_location::current()};
  profile.frames = {frame};
  profile.slowest = frame;
  profile.longestInterval = frame;
  profile.phasePeaks.events = frame;
  auto fixed = frame;
  fixed.fixedNanoseconds = 0;
  profile.phasePeaks.fixed = fixed;
  const auto full = Json::parse(encodeApplicationProfiling(profile).value());
  CHECK(full["session"] == "18446744073709551615");
  CHECK(full["latest"]["frame"] == full["session"]);
  CHECK(full["latest"]["updates"] == "18446744073709551615");
  CHECK(full["latest"]["eventsNanoseconds"] == "0");
  CHECK(full["latest"]["fixedNanoseconds"].is_null());
  CHECK(full["latest"]["elapsedNanoseconds"] == "1000000000");
  CHECK(full["latest"]["droppedNanoseconds"] == "1000000");
  CHECK(full["latest"]["renderNanoseconds"] == full["session"]);
  CHECK(full["latest"]["error"]["message"] == frame.error->message);
  CHECK(full["latest"] == full["slowest"]);
  CHECK(full["latest"] == full["longestInterval"]);
  CHECK(full["phasePeaks"]["events"] == full["latest"]);
  CHECK(full["phasePeaks"]["fixed"]["fixedNanoseconds"] == "0");
  CHECK(full["phasePeaks"]["fixed"]["error"]["message"] == frame.error->message);
  CHECK(full["phasePeaks"]["prepare"].is_null());
  CHECK(full["phasePeaks"]["update"].is_null());
  CHECK(full["phasePeaks"]["render"].is_null());
  const auto compact = Json::parse(encodeApplicationProfiling(profile, false).value());
  CHECK_FALSE(compact.contains("frames"));
  CHECK(compact["latest"] == full["frames"][0]);
  CHECK(compact["phasePeaks"] == full["phasePeaks"]);
}

TEST_CASE("core.result.failure_context") {
  Result<int> result = fail(ErrorCode::Io, "Cannot open scene");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().code == ErrorCode::Io);
  CHECK(result.error().message == "Cannot open scene");
  CHECK(std::string_view(result.error().source.file_name()).ends_with("CoreTests.cpp"));
  CHECK(result.error().source.line() > 0);
}

TEST_CASE("core.result.move_only") {
  Result<std::unique_ptr<int>> result{std::make_unique<int>(17)};
  auto moved = std::move(result);
  CHECK(**moved == 17);
}

TEST_CASE("core.result.monadic_propagation") {
  Result<int> result = fail(ErrorCode::NotFound, "Missing asset");
  bool called = false;
  const auto mapped = result.transform([&](int value) {
    called = true;
    return value + 1;
  });
  REQUIRE_FALSE(mapped.has_value());
  CHECK_FALSE(called);
  CHECK(mapped.error().message == "Missing asset");
}

TEST_CASE("core.platform.identification") {
#if defined(__APPLE__)
  CHECK(platform() == Platform::MacOS);
  CHECK(platformName() == "macOS");
#elif defined(_WIN32)
  CHECK(platform() == Platform::Windows);
  CHECK(platformName() == "Windows");
#else
  CHECK(platform() == Platform::Linux);
  CHECK(platformName() == "Linux");
#endif
}

TEST_CASE("core.logger.filter_and_format") {
  std::vector<std::string> messages;
  Logger logger([&](const LogRecord& record) { messages.emplace_back(record.message); });
  logger.log(LogLevel::Debug, "Ignored {}", 1);
  logger.log(LogLevel::Info, "Entity {}", 7);
  logger.setLevel(LogLevel::Error);
  logger.write(LogLevel::Warn, "Ignored warning");
  logger.write(LogLevel::Error, "Error detail");
  REQUIRE(messages.size() == 2);
  CHECK(messages[0] == "Entity 7");
  CHECK(messages[1] == "Error detail");
}

TEST_CASE("core.logger.source_and_time") {
  bool received = false;
  Logger logger([&](const LogRecord& record) {
    received = record.level == LogLevel::Warn
               && std::string_view(record.source.file_name()).ends_with("CoreTests.cpp")
               && record.time.time_since_epoch().count() > 0;
  });
  logger.write(LogLevel::Warn, "Check location");
  CHECK(received);
  CHECK(logger.failures() == 0);
}

TEST_CASE("core.logger.sink_failure") {
  Logger logger([](const LogRecord&) { throw std::runtime_error("Sink failed"); });
  logger.write(LogLevel::Error, "Must survive");
  CHECK(logger.failures() == 1);
  logger.setSink({});
  logger.write(LogLevel::Error, "Discarded");
  CHECK(logger.failures() == 1);
}

TEST_CASE("core.logger.formatted_source") {
  std::source_location source;
  Logger logger([&](const LogRecord& record) { source = record.source; });
  const auto expectedLine = std::source_location::current().line() + 1;
  logger.log(LogLevel::Info, "Entity {}", 7);
  CHECK(source.line() == expectedLine);
  CHECK(std::string_view(source.file_name()).ends_with("CoreTests.cpp"));
}

TEST_CASE("core.logger.off") {
  std::size_t calls = 0;
  Logger logger([&](const LogRecord&) { ++calls; });
  logger.write(LogLevel::Off, "Never a record");
  logger.setLevel(LogLevel::Off);
  logger.write(LogLevel::Fatal, "Disabled");
  CHECK(calls == 0);
}

TEST_CASE("core.logger.concurrent") {
  std::atomic<std::size_t> calls{0};
  Logger logger([&](const LogRecord&) { calls.fetch_add(1); });
  {
    std::vector<std::jthread> workers;

    for (int i = 0; i < 4; ++i) {
      workers.emplace_back([&] {
        for (int entry = 0; entry < 1000; ++entry) {
          logger.log(LogLevel::Info, "Record {}", entry);
        }
      });
    }
  }

  CHECK(calls.load() == 4000);
  CHECK(logger.failures() == 0);
}

TEST_CASE("core.logger.reentrant_sink") {
  Logger logger;
  logger.setSink([&](const LogRecord&) { logger.setSink({}); });
  logger.write(LogLevel::Info, "May replace own sink");
  CHECK(logger.failures() == 0);
}

TEST_CASE("core.clock.monotonic") {
  Clock clock;
  const auto first = clock.elapsed();
  const auto second = clock.elapsed();
  CHECK(first.count() >= 0);
  CHECK(second >= first);
  clock.reset();
  CHECK(clock.elapsed().count() >= 0);
}

TEST_CASE("core.stepper.fraction_and_carry") {
  FrameStepper stepper(10ms);
  auto frame = stepper.advance(25ms);
  CHECK(frame.updates == 2);
  CHECK(frame.interpolation == 0.5);
  CHECK(frame.dropped == 0ns);
  frame = stepper.advance(5ms);
  CHECK(frame.updates == 1);
  CHECK(frame.interpolation == 0.0);
  CHECK(frame.dropped == 0ns);
}

TEST_CASE("core.stepper.budget") {
  FrameStepper stepper(10ms, 3);
  auto frame = stepper.advance(105ms);
  CHECK(frame.updates == 3);
  CHECK(frame.dropped == 70ms);
  CHECK(frame.interpolation == 0.5);
  frame = stepper.advance(5ms);
  CHECK(frame.updates == 1);
  CHECK(frame.dropped == 0ns);
}

TEST_CASE("core.stepper.invalid_configuration") {
  CHECK_THROWS_AS(FrameStepper(0ns), std::invalid_argument);
  CHECK_THROWS_AS(FrameStepper(-1ns), std::invalid_argument);
  CHECK_THROWS_AS(FrameStepper(10ms, 0), std::invalid_argument);
}

TEST_CASE("core.stepper.invalid_duration_is_transactional") {
  FrameStepper stepper(10ms);
  static_cast<void>(stepper.advance(5ms));
  CHECK_THROWS_AS(static_cast<void>(stepper.advance(-1ns)), std::invalid_argument);
  CHECK(stepper.advance(5ms).updates == 1);
}

TEST_CASE("core.stepper.reset") {
  FrameStepper stepper(10ms);
  static_cast<void>(stepper.advance(5ms));
  stepper.reset();
  const auto frame = stepper.advance(0ns);
  CHECK(frame.updates == 0);
  CHECK(frame.interpolation == 0.0);
  CHECK(stepper.fixedStep() == 10ms);
}

TEST_CASE("core.stepper.extreme_duration") {
  FrameStepper stepper(10ns, 3);
  static_cast<void>(stepper.advance(9ns));
  const auto frame = stepper.advance(std::chrono::nanoseconds::max());
  CHECK(frame.updates == 3);
  CHECK(frame.interpolation >= 0);
  CHECK(frame.interpolation < 1);
  CHECK(frame.dropped.count() > 0);
}

TEST_CASE("core.stepper.partition_invariance") {
  FrameStepper whole(10ms, 100);
  FrameStepper split(10ms, 100);
  const auto expected = whole.advance(97ms);
  std::size_t updates = 0;
  FrameStep last{};

  for (int i = 0; i < 97; ++i) {
    last = split.advance(1ms);
    updates += last.updates;
  }

  CHECK(updates == expected.updates);
  CHECK(last.interpolation == expected.interpolation);
}

TEST_CASE("core.stepper.interpolation_stays_below_one") {
  FrameStepper stepper(std::chrono::nanoseconds::max());
  const auto frame = stepper.advance(std::chrono::nanoseconds::max() - 1ns);
  CHECK(frame.updates == 0);
  CHECK(frame.interpolation >= 0);
  CHECK(frame.interpolation < 1);
}

TEST_CASE("core.stepper.one_nanosecond_limit") {
  FrameStepper stepper(1ns, 1);
  const auto frame = stepper.advance(std::chrono::nanoseconds::max());
  CHECK(frame.updates == 1);
  CHECK(frame.interpolation == 0);
  CHECK(frame.dropped == std::chrono::nanoseconds::max() - 1ns);
}

TEST_CASE("core.memory.lifetime_and_peak") {
  TrackingResource memory;
  auto* first = memory.allocate(128);
  auto* second = memory.allocate(64);
  CHECK(memory.stats().liveBytes == 192);
  CHECK(memory.stats().liveAllocations == 2);
  memory.deallocate(first, 128);
  memory.deallocate(second, 64);
  CHECK(memory.stats().liveBytes == 0);
  CHECK(memory.stats().liveAllocations == 0);
  CHECK(memory.stats().peakBytes == 192);
  CHECK(memory.stats().allocations == 2);
}

TEST_CASE("core.memory.alignment") {
  TrackingResource memory;
  auto* allocation = memory.allocate(128, 64);
  CHECK(reinterpret_cast<std::uintptr_t>(allocation) % 64 == 0);
  memory.deallocate(allocation, 128, 64);
}

TEST_CASE("core.memory.container_release") {
  TrackingResource memory;
  {
    std::pmr::vector<int> values(&memory);

    for (int i = 0; i < 1000; ++i) {
      values.push_back(i);
    }

    CHECK(memory.stats().liveBytes >= 1000 * sizeof(int));
  }

  CHECK(memory.stats().liveBytes == 0);
  CHECK(memory.stats().liveAllocations == 0);
}

TEST_CASE("core.memory.upstream_failure") {
  TrackingResource memory(std::pmr::null_memory_resource());
  CHECK_THROWS_AS(static_cast<void>(memory.allocate(1)), std::bad_alloc);
  CHECK(memory.stats().allocations == 0);
  CHECK(memory.stats().liveBytes == 0);
  CHECK_THROWS_AS(TrackingResource(nullptr), std::invalid_argument);
}

TEST_CASE("core.memory.resource_identity") {
  TrackingResource first;
  TrackingResource second;
  CHECK(first.is_equal(first));
  CHECK_FALSE(first.is_equal(second));
}

TEST_CASE("core.memory.concurrent") {
  TrackingResource memory;
  {
    std::vector<std::jthread> workers;

    for (int i = 0; i < 4; ++i) {
      workers.emplace_back([&] {
        for (int allocation = 0; allocation < 1000; ++allocation) {
          auto* pointer = memory.allocate(64);
          memory.deallocate(pointer, 64);
        }
      });
    }
  }

  CHECK(memory.stats().liveBytes == 0);
  CHECK(memory.stats().liveAllocations == 0);
  CHECK(memory.stats().allocations == 4000);
  CHECK(memory.stats().peakBytes >= 64);
}
