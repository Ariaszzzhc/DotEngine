#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <string>
#include <utility>

import Dot.UI.Layout;

namespace {
  using namespace Dot::UI;

  auto menu() -> Description {
    Node root{"menu"};
    root.box.position = {.25F, .25F};
    root.box.size = {.5F, .5F};
    Node button{"return", "menu", Kind::Button};
    button.box.text = "Return / 返回";
    button.box.position = {.1F, .2F};
    button.box.size = {.8F, .2F};
    Node label{"title", "menu", Kind::Label};
    label.box.text = "Pause";
    label.box.position = {0, 0};
    label.box.size = {1, .2F};
    label.blocksPointer = false;
    return {"Pause menu", {button, root, label}};
  }
}  // namespace

TEST_CASE("ui.layout_parent_local_resolution_paint_order_and_owned_defaults") {
  const auto layout = Layout::create(menu()).value();
  auto resolved = layout.resolve().value();
  REQUIRE(resolved.size() == 3);
  CHECK(resolved[0].local.key == "menu");
  CHECK(resolved[1].local.key == "return");
  CHECK(resolved[2].local.key == "title");
  CHECK(resolved[1].box.position.x == doctest::Approx(.3));
  CHECK(resolved[1].box.position.y == doctest::Approx(.35));
  CHECK(resolved[1].box.size.x == doctest::Approx(.4));
  CHECK(resolved[1].box.size.y == doctest::Approx(.1));
  resolved[1].local.box.text = "Detached";
  CHECK(layout.resolve()->at(1).local.box.text == "Return / 返回");
  CHECK(layout.pick({.4F, .4F}).value() == "return");
  CHECK(layout.pick({.4F, .28F}).value() == "title");
  CHECK(layout.pick({.75F, .5F}).value() == std::nullopt);
  CHECK_FALSE(layout.pick({1, .5F}));
  CHECK_FALSE(layout.pick({std::numeric_limits<float>::quiet_NaN(), 0}));
  CHECK(Layout::create({"Empty canvas", {}})->resolve()->empty());
}

TEST_CASE("ui.layout_inherited_instance_updates_are_atomic_and_resettable") {
  const auto base = Layout::create(menu()).value();
  const std::array patches{NodeOverride{"menu", {}, false, false},
                           NodeOverride{"return", "Resume", true, true}};
  const auto changed = base.updated(patches).value();
  const auto values = changed.resolve().value();
  CHECK_FALSE(values[1].box.visible);
  CHECK_FALSE(values[1].enabled);
  CHECK(values[1].local.box.visible);
  CHECK(values[1].local.enabled);
  CHECK(values[1].box.text == "Resume");
  CHECK_FALSE(changed.pick({.4F, .4F}).value());
  CHECK(base.resolve()->at(1).box.visible);
  CHECK(changed.reset().value() == base);
  CHECK(changed.updated(patches).value() == changed);
  CHECK(changed.updated({}).value() == changed);
  CHECK(changed.description().value()->nodes[0].box.text == "Return / 返回");
  const std::array bad{NodeOverride{"return", "Changed"}, NodeOverride{"missing", {}, true}};
  CHECK_FALSE(base.updated(bad));
  const std::array duplicate{NodeOverride{"return"}, NodeOverride{"return"}};
  CHECK_FALSE(base.updated(duplicate));
  CHECK_FALSE(base.updated(std::array{NodeOverride{"menu", "Panel cannot have text"}}));
  CHECK_FALSE(base.updated(std::array{NodeOverride{"return", "bad\ttext"}}));
  CHECK(base == Layout::create(menu()).value());
  auto owner = changed;
  auto moved = std::move(owner);
  CHECK_FALSE(owner.resolve());
  CHECK_FALSE(owner.description());
  CHECK_FALSE(owner.reset());
  CHECK_FALSE(owner.updated({}));
  CHECK(moved == changed);
  CHECK(Layout{} == Layout{});
  CHECK_FALSE(Layout{} == base);
}

TEST_CASE("ui.layout_rejects_invalid_hierarchy_and_semantics") {
  for (const auto variant : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
    auto description = menu();

    switch (variant) {
      case 0:
        description.nodes[0].key = "menu";
        break;
      case 1:
        description.nodes[0].parent = "missing";
        break;
      case 2:
        description.nodes[0].parent = "title";
        break;
      case 3:
        description.nodes[1].parent = "menu";
        break;
      case 4:
        description.nodes[1].parent = "return";
        break;
      case 5:
        description.nodes[0].blocksPointer = false;
        break;
      case 6:
        description.nodes[1].box.text = "Invalid panel";
        break;
      case 7:
        description.nodes[0].kind = static_cast<Kind>(99);
        break;
      case 8:
        description.nodes[0].box.position.x = 1;
        break;
    }

    CHECK_FALSE(Layout::create(std::move(description)));
  }

  Description chain{"Depth", {}};

  for (int i = 0; i < 9; ++i) {
    Node node{std::to_string(i)};
    node.box.position = {0, 0};
    node.box.size = {1, 1};

    if (i) {
      node.parent = std::to_string(i - 1);
    }

    chain.nodes.push_back(node);
  }

  CHECK_FALSE(Layout::create(chain));
  chain.nodes.pop_back();
  CHECK(Layout::create(chain));
  chain.nodes[0].parent = "7";
  CHECK_FALSE(Layout::create(chain));
  CHECK_FALSE(Layout::create({"", {}}));
  CHECK_FALSE(Layout::create({std::string("UI\0name", 7), {}}));
  CHECK_FALSE(validKey("slash/key"));
  CHECK_FALSE(validKey(std::string(65, 'x')));
}

TEST_CASE("ui.layout_text_capacity_and_collapsed_rectangles_fail_explicitly") {
  Description description{"Budget", {}};

  for (int i = 0; i < 32; ++i) {
    Node node{std::to_string(i), {}, Kind::Label};
    node.box.text = std::string(1024, 'x');
    node.blocksPointer = false;
    description.nodes.push_back(node);
  }

  const auto full = Layout::create(description).value();
  description.nodes.push_back(Node{"extra"});
  CHECK_FALSE(Layout::create(description));
  description.nodes.pop_back();
  description.nodes[0].box.text = std::string(1023, 'x') + "é";
  CHECK_FALSE(Layout::create(description));
  CHECK_FALSE(full.updated(std::array{NodeOverride{"0", std::string(1023, 'x') + "é"}}));
  auto collapsed = menu();
  collapsed.nodes[0].box.position = {.5F, .5F};
  collapsed.nodes[0].box.size = {1e-20F, 1e-20F};
  CHECK_FALSE(Layout::create(collapsed));
}
