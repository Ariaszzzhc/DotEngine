#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

import Dot.Editor.PreviewWorker;
import Dot.Core.Result;

namespace {
  using namespace Dot;
  using namespace Dot::Editor;
  using namespace std::chrono_literals;

  auto request(std::string slot, bool priority = false) -> PreviewRequest {
    return {std::move(slot), "image.png", 16, {}, priority};
  }

  auto image(const PreviewRequest& task) -> AssetPreviewImage {
    return {{task.size, task.size,
             std::vector<std::uint8_t>(std::size_t(task.size) * task.size * 4, 255)},
            {}};
  }

  template <typename Predicate>
  auto until(Predicate predicate) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + 5s;

    while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }

      std::this_thread::sleep_for(1ms);
    }

    return true;
  }
}  // namespace

TEST_CASE("preview_worker.nonblocking_submission_coalescing_priority_and_late_discard") {
  std::binary_semaphore entered{0}, release{0};
  auto worker = PreviewWorker::start([&]() -> Core::Result<PreviewExecutor> {
                  return PreviewExecutor{[&](const PreviewRequest& task,
                                             std::stop_token) -> Core::Result<AssetPreviewImage> {
                    if (task.slot == "active") {
                      entered.release();

                      if (!release.try_acquire_for(3s)) {
                        return Core::fail(Core::ErrorCode::External, "Test gate timeout");
                      }
                    }

                    return image(task);
                  }};
                }).value();
  auto active = worker.submit(request("active")).value();
  REQUIRE(entered.try_acquire_for(3s));
  REQUIRE(worker.submit(request("first")));
  auto superseded = worker.submit(request("material", true)).value();
  auto newest = worker.submit(request("material", true)).value();
  CHECK(newest > superseded);
  CHECK(worker.stats()->pending == 2);
  CHECK(worker.takeCompleted()->empty());
  REQUIRE(worker.cancel("active"));
  release.release();
  REQUIRE(until([&] { return worker.stats()->finished == 3; }));
  auto completed = worker.takeCompleted().value();
  REQUIRE(completed.size() == 2);
  CHECK(completed[0].ticket == newest);
  CHECK(completed[1].slot == "first");
  CHECK(completed[0].ticket != active);
  CHECK(completed[0].result);
  CHECK(worker.stats()->discarded == 2);
}

TEST_CASE("preview_worker.capacity_backpressure_invalidation_and_failures_are_explicit") {
  CHECK_FALSE(PreviewWorker::start({}));
  std::binary_semaphore entered{0}, release{0};
  auto worker = PreviewWorker::start([&]() -> Core::Result<PreviewExecutor> {
                  return PreviewExecutor{[&](const PreviewRequest& task,
                                             std::stop_token) -> Core::Result<AssetPreviewImage> {
                    if (task.slot == "block") {
                      entered.release();

                      if (!release.try_acquire_for(3s)) {
                        return Core::fail(Core::ErrorCode::External, "Test gate timeout");
                      }
                    }

                    if (task.slot == "throw") {
                      throw std::runtime_error("provider failure");
                    }

                    if (task.slot == "invalid") {
                      return AssetPreviewImage{};
                    }

                    return image(task);
                  }};
                }).value();
  REQUIRE(worker.submit(request("block")));
  REQUIRE(entered.try_acquire_for(3s));

  for (int i = 0; i < 64; ++i) {
    REQUIRE(worker.submit(request(std::to_string(i))));
  }

  CHECK_FALSE(worker.submit(request("overflow")));
  CHECK(worker.submit(request("0", true)));
  CHECK(worker.stats()->pending == 64);
  release.release();
  REQUIRE(until([&] { return worker.stats()->completed == 8; }));
  CHECK(worker.stats()->finished == 8);
  CHECK(worker.stats()->pending == 57);
  REQUIRE(worker.invalidate());
  CHECK(worker.takeCompleted()->empty());
  REQUIRE(worker.submit(request("throw")));
  REQUIRE(worker.submit(request("invalid")));
  REQUIRE(until([&] { return worker.stats()->completed == 2; }));
  auto failed = worker.takeCompleted().value();
  CHECK_FALSE(failed[0].result);
  CHECK(failed[0].result.error().message.find("provider failure") != std::string::npos);
  CHECK_FALSE(failed[1].result);
  CHECK_FALSE(worker.submit({"", "image.png", 16, {}, false}));
  CHECK_FALSE(worker.submit({"bad", "../image.png", 16, {}, false}));
  CHECK_FALSE(worker.submit({"bad", "image.png", 512, {}, false}));
}

TEST_CASE("preview_worker.provider_thread_reset_stop_join_move_and_owner_contract") {
  std::thread::id created, destroyed;
  const auto main = std::this_thread::get_id();
  std::atomic<int> factories{};

  struct Lifetime {
    std::thread::id& destroyed;

    ~Lifetime() {
      destroyed = std::this_thread::get_id();
    }
  };

  std::binary_semaphore entered{0};
  auto worker = PreviewWorker::start([&]() -> Core::Result<PreviewExecutor> {
                  ++factories;
                  created = std::this_thread::get_id();
                  auto lifetime = std::make_shared<Lifetime>(destroyed);
                  return PreviewExecutor{
                      [&, lifetime](const PreviewRequest& task,
                                    std::stop_token stop) -> Core::Result<AssetPreviewImage> {
                        if (task.slot == "active") {
                          entered.release();

                          while (!stop.stop_requested()) {
                            std::this_thread::sleep_for(1ms);
                          }
                        }

                        return image(task);
                      }};
                }).value();
  REQUIRE(worker.submit(request("first")));
  REQUIRE(until([&] { return worker.stats()->completed == 1; }));
  REQUIRE(worker.invalidate(true));
  REQUIRE(worker.submit(request("active")));
  REQUIRE(entered.try_acquire_for(3s));
  bool rejected{};
  std::jthread foreign([&] { rejected = !worker.submit(request("foreign")) && !worker.stop(); });
  foreign.join();
  CHECK(rejected);
  auto moved = std::move(worker);
  CHECK_FALSE(worker.stats());
  REQUIRE(moved.stop());
  CHECK(created != main);
  CHECK(destroyed == created);
  CHECK(factories == 2);
  CHECK(moved.stats()->stopped);
  CHECK_FALSE(moved.stats()->ready);
  CHECK(moved.stats()->pending == 0);
  CHECK_FALSE(moved.submit(request("late")));
  REQUIRE(moved.stop());
}

TEST_CASE("preview_worker.initialization_failure_is_cached_until_explicit_reset") {
  std::atomic<int> attempts{};
  auto worker = PreviewWorker::start([&]() -> Core::Result<PreviewExecutor> {
                  if (++attempts == 1) {
                    return Core::fail(Core::ErrorCode::Io, "studio source missing");
                  }

                  return PreviewExecutor{[](const PreviewRequest& task,
                                            std::stop_token) -> Core::Result<AssetPreviewImage> {
                    return image(task);
                  }};
                }).value();

  REQUIRE(worker.setPaused(true));
  REQUIRE(worker.submit(request("paused")));
  CHECK(worker.stats()->pending == 1);
  CHECK(worker.stats()->started == 0);
  REQUIRE(worker.invalidate());
  REQUIRE(worker.setPaused(false));

  for (const auto* slot : {"first", "second"}) {
    REQUIRE(worker.submit(request(slot)));
    REQUIRE(until([&] { return worker.stats()->completed == 1; }));
    auto result = worker.takeCompleted().value();
    CHECK_FALSE(result[0].result);
    CHECK(result[0].result.error().message == "studio source missing");
  }

  CHECK(attempts == 1);
  CHECK_FALSE(worker.stats()->ready);
  REQUIRE(worker.invalidate(true));
  REQUIRE(worker.submit(request("recovered")));
  REQUIRE(until([&] { return worker.stats()->completed == 1; }));
  CHECK(worker.takeCompleted()->at(0).result);
  CHECK(attempts == 2);
  CHECK(worker.stats()->ready);
  CHECK(worker.stats()->initializationError.empty());
}
