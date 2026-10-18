#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>
#include <variant>

import Dot.UI.Files;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.UiDocument;
import Dot.Editor.UiSession;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Project;

namespace {
  using namespace Dot;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Project::Project project
        = Project::Project::create(storage.files().value(), "UiResource").value();
    Core::FileSystem files = project.files().value();
    UI::Layout graph = UI::Layout::create({"Menu", {UI::Node{"menu"}}}).value();
    std::string path{"assets/motion.dotui"};
    std::string original = "\n" + UI::encodeLayout(graph).value() + "\n";

    Fixture() {
      files.writeTextAtomic(path, original).value();
    }

    auto renamed(std::string name) const -> UI::Layout {
      auto spec = *graph.description().value();
      spec.name = std::move(name);
      return UI::Layout::create(std::move(spec)).value();
    }
  };
}  // namespace

TEST_CASE("ui_document.history_noops_exact_undo_save_and_owned_lifetime") {
  Fixture f;
  auto doc = Editor::UiDocument::open(f.project, f.path).value();
  REQUIRE(doc.replace(f.renamed("Walking"), 1));
  CHECK(doc.snapshot()->revision == 2);
  REQUIRE(doc.undo(2));
  CHECK_FALSE(doc.snapshot()->dirty);
  CHECK(doc.snapshot()->canRedo);
  auto runtime = f.graph.updated(std::array{UI::NodeOverride{"menu", {}, false, false}}).value();
  REQUIRE(doc.replace(runtime, 3));
  CHECK(doc.snapshot()->revision == 3);
  CHECK(doc.snapshot()->canRedo);
  CHECK(doc.snapshot()->value.resolve()->front().box.visible);
  REQUIRE(doc.redo(3));
  REQUIRE(doc.save(4));
  CHECK_FALSE(doc.snapshot()->dirty);
  CHECK(doc.snapshot()->revision == 5);
  CHECK(UI::loadLayout(f.files, f.path)->description().value()->name == "Walking");
  REQUIRE(doc.undo(5));
  REQUIRE(doc.save(6));
  CHECK(f.files.readText(f.path).value() == f.original);
  const auto owned = doc.snapshot().value();
  auto moved = std::move(doc);
  CHECK_FALSE(doc.snapshot());
  CHECK_FALSE(doc.replace(f.graph, 1));
  bool foreignRejected{};
  std::thread caller([&] { foreignRejected = !moved.snapshot() && !moved.save(7); });
  caller.join();
  CHECK(foreignRejected);
  CHECK(owned.value.description().value()->name == "Menu");
  CHECK_FALSE(moved.historyValue(false, 7));
}

TEST_CASE("ui_document.conflicts_budgets_and_stale_edits_preserve_draft") {
  Fixture f;
  CHECK_FALSE(Editor::UiDocument::open(f.project, "scripts/a.dotui"));
  CHECK_FALSE(Editor::UiDocument::open(f.project, f.path, {1, 1024}));
  CHECK_FALSE(Editor::UiDocument::open(f.project, f.path, {2, 1}));
  auto doc = Editor::UiDocument::open(f.project, f.path, {2, 4096}).value();
  REQUIRE(doc.replace(f.renamed("Second"), 1));
  CHECK_FALSE(doc.replace(f.renamed("Stale"), 1));
  CHECK_FALSE(doc.replace({}, 2));
  CHECK(doc.snapshot()->revision == 2);
  REQUIRE(f.files.writeTextAtomic(f.path, "external edit"));
  CHECK_FALSE(doc.save(2));
  CHECK(doc.snapshot()->dirty);
  CHECK(f.files.readText(f.path).value() == "external edit");
  REQUIRE(doc.replace(f.renamed("Third"), 2));
  CHECK(doc.snapshot()->historyStates == 2);
  REQUIRE(doc.undo(3));
  CHECK(doc.snapshot()->value.description().value()->name == "Second");
  CHECK_FALSE(doc.undo(4));
  CHECK(doc.snapshot()->canRedo);
}

TEST_CASE("ui_resource.strict_codec_defaults_and_guarded_regular_files") {
  using Json = nlohmann::json;
  Fixture f;
  auto text = UI::encodeLayout(f.graph).value();
  CHECK(UI::decodeLayout(text).value() == f.graph);
  const auto runtime = f.graph.updated(std::array{UI::NodeOverride{"menu", {}, false}}).value();
  CHECK(UI::encodeLayout(runtime).value() == text);

  for (const auto& source : {std::string(R"({"name":"a","name":"b","nodes":[]})"),
                             text + std::string("\0bad", 4), std::string("[]")}) {
    CHECK_FALSE(UI::decodeLayout(source));
  }

  for (const auto& key : {"enabled", "blocksPointer", "parent", "kind", "box"}) {
    auto invalid = Json::parse(text);
    invalid["nodes"][0].erase(key);
    CHECK_FALSE(UI::decodeLayoutForm(invalid.dump()));
  }

  auto incomplete = Json::parse(text);
  incomplete["nodes"][0]["parent"] = "missing";
  CHECK(UI::decodeLayoutForm(incomplete.dump()));
  CHECK_FALSE(UI::decodeLayout(incomplete.dump()));
  incomplete = Json::parse(text);
  incomplete["nodes"][0]["box"]["visible"] = 1;
  CHECK_FALSE(UI::decodeLayoutForm(incomplete.dump()));
  CHECK_FALSE(UI::saveLayout(f.files, "../menu.dotui", f.graph));
  CHECK_FALSE(UI::saveLayout(f.files, "assets/missing/menu.dotui", f.graph));
  CHECK_FALSE(UI::loadLayout(f.files, "scripts/menu.dotui"));
  const auto original = f.files.readText(f.path).value();
  CHECK_FALSE(UI::saveLayout(f.files, f.path, {}));
  CHECK(f.files.readText(f.path).value() == original);
  const std::filesystem::path unicode{u8"assets/界面.dotui"};
  REQUIRE(UI::saveLayout(f.files, unicode, f.graph));
  CHECK(UI::loadLayout(f.files, unicode).value() == f.graph);
  std::error_code error;
  std::filesystem::create_symlink(f.files.root() / f.path, f.files.root() / "assets/link.dotui",
                                  error);
  REQUIRE_FALSE(error);
  CHECK_FALSE(UI::loadLayout(f.files, "assets/link.dotui"));
  CHECK_FALSE(UI::saveLayout(f.files, "assets/link.dotui", f.graph));
  std::filesystem::create_directory_symlink(f.files.root() / "assets",
                                            f.files.root() / "assets/linked", error);
  REQUIRE_FALSE(error);
  CHECK_FALSE(UI::loadLayout(f.files, "assets/linked/motion.dotui"));
  CHECK_FALSE(UI::saveLayout(f.files, "assets/linked/new.dotui", f.graph));
  auto scene = World::Scene::create().value();
  REQUIRE(scene.setUi(f.path));
  CHECK(World::decodeScene(World::encodeScene(scene).value())->ui().value() == f.path);
  CHECK_FALSE(scene.setUi("assets/../menu.dotui"));
  CHECK(scene.ui().value() == f.path);
  REQUIRE(scene.setUi({}));
  CHECK_FALSE(scene.ui().value());
}

TEST_CASE("ui_session_forms_gestures_history_and_preview_defaults_share_native_contracts") {
  using Json = nlohmann::json;
  Fixture f;
  auto session = Editor::UiSession::open(f.project, f.path, 5).value();
  const auto status = [&] { return Json::parse(session.snapshot().value()); };
  const auto operation = [&](std::string name, Json arguments = Json::object()) {
    arguments["command"] = name;
    arguments["buffer"] = "5";
    arguments["revision"] = status()["revision"];
    return session.perform(arguments.dump());
  };
  CHECK_FALSE(session.pending().value());
  auto form = status()["form"];
  form["nodes"][0]["parent"] = "missing";
  REQUIRE(operation("stage-ui", {{"value", form}}));
  CHECK(session.pending().value());
  CHECK_FALSE(operation("apply-ui"));
  CHECK(session.generation().value() == 1);
  CHECK_FALSE(operation("save-ui"));
  REQUIRE(operation("reset-ui-form"));
  REQUIRE(operation("begin-ui-rect", {{"key", "menu"}}));
  const auto gesture = status()["gesture"];
  CHECK_FALSE(operation("save-ui"));
  CHECK_FALSE(operation("close-ui", {{"discard", true}}));
  const Json rectangle{{"position", {{"x", .2}, {"y", .2}}}, {"size", {{"x", .5}, {"y", .5}}}};
  REQUIRE(operation("update-ui-rect", {{"gesture", gesture}, {"value", rectangle}}));
  CHECK(status()["revision"] == "1");
  CHECK(status()["historyStates"] == 1);
  const auto preview = status();
  auto invalid = rectangle;
  invalid["size"]["x"] = 2;
  CHECK_FALSE(operation("update-ui-rect", {{"gesture", gesture}, {"value", invalid}}));
  CHECK(status() == preview);
  REQUIRE(operation("finish-ui-rect", {{"gesture", gesture}, {"commit", true}}));
  CHECK(status()["revision"] == "2");
  CHECK(status()["historyStates"] == 2);
  REQUIRE(operation("undo-ui"));
  CHECK(status()["value"] == Json::parse(UI::encodeLayout(f.graph).value()));
  CHECK(status()["canRedo"] == true);
  const auto unchanged = status()["generation"];
  REQUIRE(operation("apply-ui"));
  CHECK(status()["generation"] == unchanged);
  CHECK(status()["canRedo"] == true);
  REQUIRE(operation("redo-ui"));
  REQUIRE(operation("save-ui"));
  CHECK_FALSE(session.pending().value());
  CHECK_FALSE(session.perform(R"({"command":"inspect-ui","buffer":"4","revision":"5"})"));
  REQUIRE(operation("begin-ui-rect", {{"key", "menu"}}));
  const auto second = status()["gesture"];
  auto changed = rectangle;
  changed["position"]["x"] = .3;
  REQUIRE(operation("update-ui-rect", {{"gesture", second}, {"value", changed}}));
  REQUIRE(operation("finish-ui-rect", {{"gesture", second}, {"commit", false}}));
  CHECK(status()["form"] == status()["value"]);
  CHECK_FALSE(session.pending().value());
  CHECK_FALSE(operation("update-ui-rect", {{"gesture", second}, {"value", changed}}));
  const auto old = status()["generation"];
  REQUIRE(operation("ui-preview-options", {{"showHidden", true}}));
  CHECK_FALSE(operation("pick-ui-node", {{"generation", old}, {"uv", {{"x", .3}, {"y", .3}}}}));
  REQUIRE(operation("pick-ui-node",
                    {{"generation", status()["generation"]}, {"uv", {{"x", .3}, {"y", .3}}}}));
  CHECK(status()["selected"] == "menu");
  CHECK_FALSE(operation("close-ui", {{"discard", 1}}));
  REQUIRE(operation("close-ui", {{"discard", false}}));
  bool rejected{};
  std::thread foreign([&] {
    rejected
        = !session.snapshot() && !session.pending() && !session.generation() && !session.draws();
  });
  foreign.join();
  CHECK(rejected);
  auto moved = std::move(session);
  CHECK_FALSE(session.snapshot());
  CHECK_FALSE(session.pending());
  CHECK(moved.snapshot());
}

TEST_CASE("ui_session_structural_operations_share_reference_and_order_rules") {
  using Json = nlohmann::json;
  Fixture f;
  auto session = Editor::UiSession::open(f.project, f.path, 7).value();
  const auto status = [&] { return Json::parse(session.snapshot().value()); };
  const auto operation = [&](std::string name, Json arguments = Json::object()) {
    arguments["command"] = name;
    arguments["buffer"] = "7";
    arguments["revision"] = status()["revision"];
    return session.perform(arguments.dump());
  };
  REQUIRE(operation("add-ui-node", {{"kind", "label"}, {"parent", "menu"}}));
  CHECK(status()["selected"] == "node-1");
  REQUIRE(operation("rename-ui-node", {{"key", "node-1"}, {"name", "caption"}}));
  CHECK(status()["selected"] == "caption");
  REQUIRE(operation("add-ui-node", {{"kind", "button"}, {"parent", "menu"}}));
  REQUIRE(operation("reorder-ui-node", {{"key", "node-1"}, {"direction", "earlier"}}));
  CHECK(status()["form"]["nodes"][1]["key"] == "node-1");
  const auto boundary = status();
  REQUIRE(operation("reorder-ui-node", {{"key", "node-1"}, {"direction", "earlier"}}));
  CHECK(status() == boundary);
  REQUIRE(operation("rename-ui-node", {{"key", "menu"}, {"name", "root"}}));
  CHECK(status()["form"]["nodes"][1]["parent"] == "root");
  CHECK(status()["form"]["nodes"][2]["parent"] == "root");
  const auto valid = status();
  CHECK_FALSE(operation("rename-ui-node", {{"key", "caption"}, {"name", "root"}}));
  CHECK_FALSE(operation("reparent-ui-node", {{"key", "root"}, {"parent", "caption"}}));
  CHECK_FALSE(operation("reparent-ui-node", {{"key", "root"}, {"parent", "root"}}));
  CHECK_FALSE(operation("set-ui-node-kind", {{"key", "root"}, {"kind", "button"}}));
  CHECK_FALSE(operation("reorder-ui-node", {{"key", "caption"}, {"direction", "front"}}));
  CHECK_FALSE(operation("add-ui-node", {{"kind", "unknown"}, {"parent", nullptr}}));
  CHECK(status() == valid);
  REQUIRE(operation("reparent-ui-node", {{"key", "caption"}, {"parent", nullptr}}));
  CHECK(status()["form"]["nodes"][2]["parent"].is_null());
  REQUIRE(operation("set-ui-node-kind", {{"key", "caption"}, {"kind", "panel"}}));
  CHECK(status()["form"]["nodes"][2]["box"]["text"] == "");
  REQUIRE(operation("set-ui-node-kind", {{"key", "caption"}, {"kind", "button"}}));
  CHECK(status()["form"]["nodes"][2]["blocksPointer"] == true);
  REQUIRE(operation("select-ui-node", {{"key", "node-1"}}));
  REQUIRE(operation("remove-ui-node", {{"key", "root"}}));
  CHECK(status()["selected"].is_null());
  CHECK(status()["form"]["nodes"].size() == 1);
  CHECK(status()["revision"] == "1");
  REQUIRE(operation("apply-ui"));
  CHECK(status()["revision"] == "2");
  CHECK(status()["preview"]["nodes"][0]["key"] == "caption");
  REQUIRE(operation("undo-ui"));
  CHECK(status()["form"]["nodes"][0]["key"] == "menu");
}
