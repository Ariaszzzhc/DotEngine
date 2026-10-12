#include <doctest/doctest.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

import Dot.Core.Events;

using Dot::Core::Signal;

TEST_CASE("events.dispatch_order_and_payload") {
  Signal<int> signal;
  std::vector<int> received;
  auto first = signal.subscribe([&](int event) { received.push_back(event); });
  auto second = signal.subscribe([&](int event) { received.push_back(event * 2); });
  CHECK(signal.publish(7) == 2);
  CHECK(received == std::vector<int>{7, 14});
  CHECK(signal.listeners() == 2);
}

TEST_CASE("events.scoped_disconnect") {
  Signal<int> signal;
  int count = 0;
  {
    auto connection = signal.subscribe([&](int) { ++count; });
    CHECK(connection.connected());
    CHECK(signal.publish(1) == 1);
  }
  CHECK(signal.publish(1) == 0);
  CHECK(signal.listeners() == 0);
  CHECK(count == 1);
}

TEST_CASE("events.disconnect_is_idempotent") {
  Signal<int> signal;
  auto connection = signal.subscribe([](int) {});
  connection.disconnect();
  connection.disconnect();
  CHECK_FALSE(connection.connected());
  CHECK(signal.publish(0) == 0);
}

TEST_CASE("events.disconnect_during_dispatch") {
  Signal<int> signal;
  Signal<int>::Connection second;
  auto first = signal.subscribe([&](int) { second.disconnect(); });
  second = signal.subscribe([](int) { FAIL("Disconnected callback ran"); });
  CHECK(signal.publish(0) == 1);
}

TEST_CASE("events.self_disconnect") {
  Signal<int> signal;
  Signal<int>::Connection connection;
  connection = signal.subscribe([&](int) { connection.disconnect(); });
  CHECK(signal.publish(0) == 1);
  CHECK(signal.publish(0) == 0);
}

TEST_CASE("events.subscribe_during_dispatch") {
  Signal<int> signal;
  Signal<int>::Connection added;
  int calls = 0;
  auto first = signal.subscribe([&](int) {
    if (!added.connected()) {
      added = signal.subscribe([&](int) { ++calls; });
    }
  });
  CHECK(signal.publish(0) == 1);
  CHECK(calls == 0);
  CHECK(signal.publish(0) == 2);
  CHECK(calls == 1);
}

TEST_CASE("events.connection_move_releases_old_subscription") {
  Signal<int> signal;
  auto first = signal.subscribe([](int) {});
  auto second = signal.subscribe([](int) {});
  first = std::move(second);
  CHECK_FALSE(second.connected());
  CHECK(first.connected());
  CHECK(signal.publish(0) == 1);
  auto moved = std::move(first);
  CHECK_FALSE(first.connected());
  CHECK(moved.connected());
}

TEST_CASE("events.owner_lifetime_and_move") {
  Signal<int>::Connection connection;
  {
    Signal<int> signal;
    connection = signal.subscribe([](int) {});
    Signal<int> moved(std::move(signal));
    CHECK(connection.connected());
    CHECK(moved.publish(0) == 1);
    CHECK(signal.publish(0) == 0);
    auto fresh = signal.subscribe([](int) {});
    CHECK(signal.publish(0) == 1);
  }
  CHECK_FALSE(connection.connected());
}

TEST_CASE("events.nested_dispatch") {
  Signal<int> signal;
  std::vector<int> received;
  auto connection = signal.subscribe([&](int value) {
    received.push_back(value);
    if (value == 1) {
      signal.publish(2);
    }
  });
  CHECK(signal.publish(1) == 1);
  CHECK(received == std::vector<int>{1, 2});
}

TEST_CASE("events.exception_preserves_signal") {
  Signal<int> signal;
  auto connection = signal.subscribe([](int) { throw std::runtime_error("Callback failed"); });
  CHECK_THROWS_AS(signal.publish(0), std::runtime_error);
  connection.disconnect();
  auto replacement = signal.subscribe([](int) {});
  CHECK(signal.publish(0) == 1);
}

TEST_CASE("events.destroy_owner_during_dispatch") {
  auto signal = std::make_unique<Signal<int>>();
  auto connection = signal->subscribe([&](int) { signal.reset(); });
  CHECK(signal->publish(0) == 1);
  CHECK_FALSE(connection.connected());
}

TEST_CASE("events.reject_empty_callback") {
  Signal<int> signal;
  CHECK_THROWS_AS(static_cast<void>(signal.subscribe({})), std::invalid_argument);
  CHECK(signal.listeners() == 0);
}
