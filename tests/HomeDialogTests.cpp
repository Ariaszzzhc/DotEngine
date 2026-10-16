#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <thread>

import Dot.Platform.Dialog;

TEST_CASE("home_dialog.copies_callback_selection_cancel_error_and_consumes_once") {
  using namespace Dot::Platform;
  DialogRequest selected;
  CHECK_FALSE(selected.take());
  std::string path = "/tmp/中文 project";
  const char* files[]{path.c_str(), nullptr};
  selected.complete(files, nullptr);
  path.assign("changed");
  auto result = selected.take();
  REQUIRE(result);
  CHECK(result->status == DialogStatus::Selected);
  CHECK(result->path == "/tmp/中文 project");
  CHECK_FALSE(selected.take());
  selected.complete(nullptr, "late duplicate");
  CHECK_FALSE(selected.take());
  DialogRequest cancelled;
  const char* empty[]{nullptr};
  cancelled.complete(empty, nullptr);
  CHECK(cancelled.take()->status == DialogStatus::Cancelled);
  DialogRequest failed;
  std::string error = "Portal unavailable";
  failed.complete(nullptr, error.c_str());
  error.clear();
  CHECK(failed.take()->error == "Portal unavailable");
  DialogRequest unnamed;
  unnamed.complete(nullptr, "");
  CHECK_FALSE(unnamed.take()->error.empty());
}

TEST_CASE("home_dialog.worker_reply_outlives_retired_home_owner_without_gui_borrow") {
  using namespace Dot::Platform;
  auto home = std::make_shared<DialogRequest>();
  std::weak_ptr<DialogRequest> weak = home;
  auto callbackTicket = std::make_shared<std::shared_ptr<DialogRequest>>(home);
  home.reset();
  CHECK_FALSE(weak.expired());
  std::thread worker([callbackTicket] {
    const char* files[]{"/tmp/selected", nullptr};
    (*callbackTicket)->complete(files, nullptr);
  });
  worker.join();
  auto remaining = weak.lock();
  REQUIRE(remaining);
  CHECK(remaining->take()->path == "/tmp/selected");
  remaining.reset();
  callbackTicket.reset();
  CHECK(weak.expired());
}

TEST_CASE("path_dialog.rejects_empty_malformed_oversized_and_multiple_selections") {
  using namespace Dot::Platform;
  const std::string oversized(16385, 'a');
  const char malformed[]{static_cast<char>(0xC0), static_cast<char>(0x80), 0};

  for (const char* path : {"", malformed, oversized.c_str()}) {
    DialogRequest request;
    const char* paths[]{path, nullptr};
    request.complete(paths, nullptr);
    auto reply = request.take();
    REQUIRE(reply);
    CHECK(reply->status == DialogStatus::Failed);
    CHECK(reply->path.empty());
    CHECK_FALSE(reply->error.empty());
  }

  DialogRequest multiple;
  const char* paths[]{"/tmp/first", "/tmp/second", nullptr};
  multiple.complete(paths, nullptr);
  CHECK(multiple.take()->status == DialogStatus::Failed);
}
