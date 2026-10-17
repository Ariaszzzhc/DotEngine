#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.Editor.HierarchyTree;

namespace {
  using Dot::Editor::HierarchyItem;
  using Dot::Editor::HierarchyTree;
}  // namespace

TEST_CASE("hierarchy_tree.ordered_forward_links_filter_ancestors_and_immutable_lifetime") {
  std::vector<HierarchyItem> items{{9, 7, "Bolt"},
                                   {7, {}, "Maintenance"},
                                   {11, 7, "灯 Light"},
                                   {20, {}, "Other"},
                                   {12, 9, "Fastener"}};
  auto tree = HierarchyTree::create(items).value();
  items[0].name = "changed source";
  CHECK(tree.nodes()[0].item.name == "Bolt");
  CHECK(std::vector(tree.roots().begin(), tree.roots().end()) == std::vector<std::size_t>{1, 3});
  CHECK(tree.nodes()[1].children == std::vector<std::size_t>{0, 2});
  CHECK(tree.nodes()[0].children == std::vector<std::size_t>{4});
  CHECK(tree.nodes()[0].key == "9");
  CHECK(tree.filter("BOLT").value() == std::vector<bool>{true, true, false, false, false});
  CHECK(tree.filter("fast").value() == std::vector<bool>{true, true, false, false, true});
  CHECK(tree.filter("灯").value() == std::vector<bool>{false, true, true, false, false});
  CHECK(tree.filter("maintenance").value() == std::vector<bool>{false, true, false, false, false});
  CHECK(tree.filter("absent").value() == std::vector<bool>(5, false));
  CHECK(tree.filter("").value() == std::vector<bool>(5, true));
  auto shared = tree;
  CHECK(shared.nodes().data() == tree.nodes().data());
  auto moved = std::move(tree);
  CHECK(tree.nodes().empty());
  CHECK(tree.roots().empty());
  CHECK(tree.filter("a")->empty());
  CHECK(moved.nodes().data() == shared.nodes().data());
  bool independent{};
  std::thread reader([&] {
    independent
        = shared.filter("FAST").value() == std::vector<bool>{true, true, false, false, true};
  });
  reader.join();
  CHECK(independent);
}

TEST_CASE("hierarchy_tree.invalid_graph_text_and_queries_fail_without_partial_projection") {
  CHECK_FALSE(HierarchyTree::create({{0, {}, "Name"}}));
  CHECK_FALSE(HierarchyTree::create({{1, {}, "Name"}, {1, {}, "Duplicate"}}));
  CHECK_FALSE(HierarchyTree::create({{1, 2, "Missing"}}));
  CHECK_FALSE(HierarchyTree::create({{1, 1, "Self"}}));
  CHECK_FALSE(HierarchyTree::create({{1, {}, "Root"}, {2, 3, "Cycle"}, {3, 2, "Cycle"}}));

  for (auto name :
       {std::string{}, std::string(257, 'a'), std::string("a\0b", 3), std::string("\xff", 1)}) {
    CHECK_FALSE(HierarchyTree::create({{1, {}, std::move(name)}}));
  }

  const auto tree = HierarchyTree::create({{1, {}, std::string(256, 'a')}}).value();
  CHECK_FALSE(tree.filter(std::string(129, 'a')));
  CHECK_FALSE(tree.filter(std::string("a\0b", 3)));
  CHECK_FALSE(tree.filter(std::string("\xff", 1)));
  CHECK(tree.filter(std::string(128, 'A')).value() == std::vector<bool>{true});
  CHECK(HierarchyTree::create({})->roots().empty());
}

TEST_CASE("hierarchy_tree.depth_node_capacity_and_full_width_ids_keep_boundary_rows") {
  std::vector<HierarchyItem> chain;

  for (std::uint64_t i = 1; i <= 256; ++i) {
    chain.push_back(
        {i, i == 1 ? std::nullopt : std::optional{i - 1}, i == 256 ? "Needle" : "Ancestor"});
  }

  auto tree = HierarchyTree::create(chain).value();
  CHECK(tree.filter("needle").value() == std::vector<bool>(256, true));
  chain.push_back({257, 256, "Too deep"});
  CHECK_FALSE(HierarchyTree::create(std::move(chain)));
  std::vector<HierarchyItem> wide;

  for (std::uint64_t i = 1; i <= 10000; ++i) {
    wide.push_back({i, {}, i == 10000 ? "Last" : "Sibling"});
  }

  tree = HierarchyTree::create(wide).value();
  REQUIRE(tree.roots().size() == 10000);
  CHECK(tree.roots().back() == 9999);
  auto flags = tree.filter("last").value();
  CHECK(std::count(flags.begin(), flags.end(), true) == 1);
  CHECK(flags.back());
  wide.push_back({10001, {}, "Over capacity"});
  CHECK_FALSE(HierarchyTree::create(std::move(wide)));
  auto large
      = HierarchyTree::create({{std::numeric_limits<std::uint64_t>::max(), {}, "Full ID"}}).value();
  CHECK(large.nodes()[0].key == "18446744073709551615");
}
