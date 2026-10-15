#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <thread>

#include "../src/Engine/Editor/HomeDialog.hpp"

TEST_CASE("home_dialog.copies_callback_selection_cancel_error_and_consumes_once") {
  using namespace Dot::Editor::Detail;
  FolderRequest selected;
  CHECK_FALSE(selected.take());
  std::string path = "/tmp/中文 project";
  const char* files[]{path.c_str(), nullptr};
  selected.complete(files, nullptr);
  path.assign("changed");
  auto result = selected.take();
  REQUIRE(result);
  CHECK(result->status == FolderStatus::Selected);
  CHECK(result->path == "/tmp/中文 project");
  CHECK_FALSE(selected.take());
  selected.complete(nullptr, "late duplicate");
  CHECK_FALSE(selected.take());
  FolderRequest cancelled;
  const char* empty[]{nullptr};
  cancelled.complete(empty, nullptr);
  CHECK(cancelled.take()->status == FolderStatus::Cancelled);
  FolderRequest failed;
  std::string error = "Portal unavailable";
  failed.complete(nullptr, error.c_str());
  error.clear();
  CHECK(failed.take()->error == "Portal unavailable");
  FolderRequest unnamed;
  unnamed.complete(nullptr, "");
  CHECK_FALSE(unnamed.take()->error.empty());
}

TEST_CASE("home_dialog.worker_reply_outlives_retired_home_owner_without_gui_borrow") {
  using namespace Dot::Editor::Detail;
  auto home = std::make_shared<FolderRequest>();
  std::weak_ptr<FolderRequest> weak = home;
  auto callbackTicket = std::make_shared<std::shared_ptr<FolderRequest>>(home);
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
