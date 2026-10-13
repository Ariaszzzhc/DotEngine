#include <doctest/doctest.h>
#include <imgui.h>

#include <array>
#include <expected>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.World.Scene;
import Dot.Platform.Events;
import Dot.Math;

namespace {
  using namespace Dot::Graphics;

  auto renderer() -> Renderer {
    auto result = Renderer::create();
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }

  auto pixel(const Image& image, unsigned x, unsigned y) -> std::array<std::uint8_t, 4> {
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels.at(offset), image.pixels.at(offset + 1), image.pixels.at(offset + 2),
            image.pixels.at(offset + 3)};
  }

  auto rect() -> Dot::Core::Result<void> {
    ImGui::GetBackgroundDrawList()->AddRectFilled({8, 8}, {40, 40}, IM_COL32(255, 0, 0, 255));
    return {};
  }
}  // namespace

TEST_CASE("graphics.target_real_frames_reuse_shared_ownership_and_validation_retention") {
  auto gpu = renderer();
  auto target = gpu.createTarget(65, 33).value();
  auto copy = target;
  CHECK(gpu.resourceStats()->targets == 1);
  CHECK(gpu.resourceStats()->targetBytes == 65 * 33 * 8);
  CHECK_FALSE(gpu.readTarget(target));
  REQUIRE(gpu.renderMeshesInto(target, {}, {}, {1, 0, 0, 1}));
  auto red = gpu.readTarget(copy).value();
  CHECK(pixel(red, 32, 16) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  REQUIRE(gpu.renderMeshesInto(copy, {}, {}, {0, 0, 1, 1}));
  CHECK(target.info()->frames == 2);
  CHECK(pixel(gpu.readTarget(target).value(), 32, 16)
        == std::array<std::uint8_t, 4>{0, 0, 255, 255});
  CHECK_FALSE(gpu.renderMeshesInto(target, {}, {}, {-1, 0, 0, 1}));
  CHECK(target.info()->frames == 2);
  CHECK(pixel(gpu.readTarget(target).value(), 32, 16)
        == std::array<std::uint8_t, 4>{0, 0, 255, 255});
  target = {};
  CHECK(gpu.resourceStats()->targets == 1);
  copy = {};
  CHECK(gpu.resourceStats()->targets == 0);
  CHECK(gpu.resourceStats()->targetBytes == 0);
}

TEST_CASE("graphics.target_scene_pixels_match_readback_and_reject_foreign_owners_threads") {
  auto gpu = renderer();
  auto other = renderer();
  auto target = gpu.createTarget(64, 64).value();
  auto scene = Dot::World::Scene::create().value();
  auto camera = scene.createEntity("Camera").value();
  Dot::World::Camera view;
  view.primary = true;
  REQUIRE(scene.setCamera(camera, view));
  REQUIRE(scene.setTransform(camera, {{0, 0, 4}}));
  auto cube = scene.createEntity("Cube").value();
  REQUIRE(scene.setMeshRenderer(cube, Dot::World::MeshRenderer{}));
  auto drawing = SceneRenderer::create(gpu).value();
  REQUIRE(drawing.renderInto(scene, target));
  CHECK(gpu.readTarget(target)->pixels == drawing.render(scene, 64, 64)->pixels);
  CHECK_FALSE(other.renderMeshesInto(target, {}, {}));
  CHECK_FALSE(other.readTarget(target));
  CHECK_FALSE(gpu.createTarget(0, 1));
  CHECK_FALSE(gpu.createTarget(1, 4097));
  bool rejected = false;
  std::thread worker([&] {
    rejected = !target.info() && !gpu.readTarget(target) && !drawing.renderInto(scene, target);
  });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(gpu);
  CHECK_FALSE(gpu.readTarget(target));
  CHECK(moved.readTarget(target));
}

TEST_CASE("graphics.target_can_outlive_renderer_only_for_resource_release") {
  RenderTarget retained;

  {
    auto gpu = renderer();
    retained = gpu.createTarget(1, 1).value();
    REQUIRE(gpu.renderMeshesInto(retained, {}, {}));
  }

  CHECK_FALSE(retained.info());
  retained = {};
}

TEST_CASE("graphics.target_count_budget_releases_for_retry") {
  auto gpu = renderer();
  std::vector<RenderTarget> targets;

  for (unsigned index = 0; index < 128; ++index) {
    auto target = gpu.createTarget(1, 1);
    REQUIRE(target);
    targets.push_back(std::move(*target));
  }

  CHECK(gpu.resourceStats()->targets == 128);
  CHECK_FALSE(gpu.createTarget(1, 1));
  targets.pop_back();
  CHECK(gpu.createTarget(1, 1));
  targets.clear();
  CHECK(gpu.resourceStats()->targets == 0);
}

TEST_CASE("graphics.gui_real_pixels_dpi_and_context_restoration_lifetime") {
  const auto baseline = guiContexts();
  auto* foreign = ImGui::CreateContext();

  {
    auto gpu = renderer();
    REQUIRE(gpu.enableGui());
    CHECK(ImGui::GetCurrentContext() == foreign);
    CHECK(guiContexts() == baseline + 1);
    auto image = gpu.renderGui(rect, 96, 96).value();
    CHECK(pixel(image, 16, 16) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    CHECK(pixel(image, 0, 0) != std::array<std::uint8_t, 4>{255, 0, 0, 255});
    CHECK(ImGui::GetCurrentContext() == foreign);
    image = gpu.renderGui(rect, 96, 96, 1.0 / 60, 2).value();
    CHECK(pixel(image, 64, 64) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    CHECK(gpu.guiStats()->frames == 2);
    CHECK(gpu.guiStats()->vertices > 0);
    REQUIRE(gpu.closeGui());
    REQUIRE(gpu.closeGui());
    CHECK(guiContexts() == baseline);
    REQUIRE(gpu.enableGui());
  }

  CHECK(guiContexts() == baseline);
  CHECK(ImGui::GetCurrentContext() == foreign);
  ImGui::DestroyContext(foreign);
}

TEST_CASE("graphics.gui_callback_failures_unbalanced_stacks_reentry_and_recovery") {
  auto gpu = renderer();
  REQUIRE(gpu.enableGui());
  auto failed = gpu.renderGui(
      []() -> Dot::Core::Result<void> {
        ImGui::Begin("Unfinished");
        return Dot::Core::fail(Dot::Core::ErrorCode::External, "widget failure");
      },
      96, 96);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message == "widget failure");
  CHECK(gpu.guiStats()->frames == 0);
  CHECK_FALSE(gpu.renderGui(
      []() -> Dot::Core::Result<void> {
        ImGui::Begin("Unfinished");
        return {};
      },
      96, 96));
  CHECK_FALSE(gpu.renderGui(
      []() -> Dot::Core::Result<void> { throw std::runtime_error("widget exception"); }, 96, 96));
  CHECK_FALSE(gpu.renderGui([]() -> Dot::Core::Result<void> { throw 7; }, 96, 96));
  REQUIRE(gpu.renderGui(
      [&]() -> Dot::Core::Result<void> {
        CHECK_FALSE(gpu.closeGui());
        CHECK_FALSE(gpu.renderGui(rect, 96, 96));
        CHECK_FALSE(gpu.createTarget(1, 1));
        return rect();
      },
      96, 96));
  CHECK(gpu.guiStats()->frames == 1);
}

TEST_CASE("graphics.gui_direct_target_image_retains_content_and_frame_ownership") {
  auto gpu = renderer();
  REQUIRE(gpu.enableGui());
  auto target = gpu.createTarget(16, 16).value();
  REQUIRE(gpu.renderMeshesInto(target, {}, {}, {0, 0, 1, 1}));
  CHECK_FALSE(gpu.guiImage(target, 32, 32));
  auto draw = [&]() -> Dot::Core::Result<void> {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({96, 96});
    ImGui::Begin("Image", nullptr, ImGuiWindowFlags_NoDecoration);
    ImGui::SetCursorScreenPos({8, 8});
    auto result = gpu.guiImage(target, 64, 64);
    target = {};
    ImGui::End();
    return result;
  };
  auto image = gpu.renderGui(draw, 96, 96).value();
  CHECK(pixel(image, 32, 32) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
  CHECK(gpu.resourceStats()->targets == 0);
}

TEST_CASE("graphics.gui_input_real_button_keyboard_text_modifiers_and_focus") {
  using namespace Dot::Platform;
  auto gpu = renderer();
  REQUIRE(gpu.enableGui());
  bool clicked = false;
  auto draw = [&]() -> Dot::Core::Result<void> {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({160, 100});
    ImGui::Begin("Input", nullptr, ImGuiWindowFlags_NoDecoration);
    ImGui::SetCursorScreenPos({16, 16});
    clicked = ImGui::Button("Move", {80, 24});
    ImGui::End();
    return {};
  };
  REQUIRE(gpu.renderGui(draw, 160, 100));
  REQUIRE(gpu.guiEvent({0, 0, PointerMoved{32, 24, 0, 0}}));
  REQUIRE(gpu.renderGui(draw, 160, 100));
  REQUIRE(gpu.guiEvent({0, 0, ButtonChanged{MouseButton::Left, true}}));
  REQUIRE(gpu.renderGui(draw, 160, 100));
  REQUIRE(gpu.guiEvent({0, 0, ButtonChanged{MouseButton::Left, false}}));
  REQUIRE(gpu.renderGui(draw, 160, 100));
  CHECK(clicked);
  REQUIRE(gpu.guiEvent({0, 0, KeyChanged{Key::LeftControl, true, false}}));
  REQUIRE(gpu.renderGui(
      []() -> Dot::Core::Result<void> {
        CHECK((ImGui::GetIO().ConfigMacOSXBehaviors ? ImGui::GetIO().KeySuper
                                                    : ImGui::GetIO().KeyCtrl));
        return {};
      },
      96, 96));
  REQUIRE(gpu.guiEvent({0, 0, Focus{false}}));
  REQUIRE(gpu.renderGui(
      []() -> Dot::Core::Result<void> {
        CHECK_FALSE(ImGui::GetIO().KeyCtrl);
        CHECK_FALSE(ImGui::GetIO().KeySuper);
        return {};
      },
      96, 96));
  CHECK_FALSE(gpu.guiEvent({0, 0, Text{"\xc0\x80"}}));
  CHECK_FALSE(gpu.guiEvent({0, 0, Wheel{std::numeric_limits<float>::infinity(), 0}}));
}

TEST_CASE("graphics.gui_validation_budget_thread_moves_and_disabled_context") {
  auto gpu = renderer();
  CHECK_FALSE(gpu.renderGui(rect, 96, 96));
  CHECK_FALSE(gpu.guiEvent({0, 0, Dot::Platform::Focus{true}}));
  GuiConfig config;
  config.fontSize = 0;
  CHECK_FALSE(gpu.enableGui(config));
  auto storage = Dot::Core::StagedDirectory::create(
                     Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                     .value();
  REQUIRE(storage.files()->writeTextAtomic("invalid.ttf", std::string(64, 'x')));
  config.font = storage.files()->root() / "invalid.ttf";
  const auto contexts = guiContexts();
  CHECK_FALSE(gpu.enableGui(config));
  CHECK(guiContexts() == contexts);
  config = {};
  config.font = std::filesystem::temp_directory_path() / "dot-nonexistent-font.ttf";
  CHECK_FALSE(gpu.enableGui(config));
  config = {};
  config.maxVertices = 1;
  REQUIRE(gpu.enableGui(config));
  CHECK_FALSE(gpu.renderGui(rect, 96, 96));
  CHECK(gpu.guiStats()->frames == 0);
  REQUIRE(gpu.closeGui());
  REQUIRE(gpu.enableGui());
  bool rejected = false;
  std::thread worker(
      [&] { rejected = !gpu.closeGui() && !gpu.guiStats() && !gpu.renderGui(rect, 96, 96); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(gpu);
  CHECK_FALSE(gpu.closeGui());
  CHECK_FALSE(moved.renderGui(rect, 0, 96));
  CHECK_FALSE(moved.renderGui(rect, 96, 96, -1));
  CHECK_FALSE(moved.renderGui(rect, 96, 96, 0, 0));
  REQUIRE(moved.renderGui(rect, 96, 96));
}

TEST_CASE("graphics.gui_text_input_preserves_utf8_and_rejects_worker_context_creation") {
  using namespace Dot::Platform;
  auto gpu = renderer();
  REQUIRE(gpu.enableGui());
  std::array<char, 64> buffer{};
  int frame = 0;
  auto draw = [&]() -> Dot::Core::Result<void> {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({200, 100});
    ImGui::Begin("Text", nullptr, ImGuiWindowFlags_NoDecoration);

    if (frame++ < 2) {
      ImGui::SetKeyboardFocusHere();
    }

    ImGui::InputText("Name", buffer.data(), buffer.size());
    ImGui::End();
    return {};
  };
  REQUIRE(gpu.renderGui(draw, 200, 100));
  REQUIRE(gpu.renderGui(draw, 200, 100));
  REQUIRE(gpu.guiEvent({0, 0, Text{"Hello 中文"}}));
  REQUIRE(gpu.renderGui(draw, 200, 100));
  CHECK(std::string(buffer.data()) == "Hello 中文");
  bool rejected = false;
  std::thread worker([&] {
    auto native = Renderer::create();
    rejected = !native || !native->enableGui();
  });
  worker.join();
  CHECK(rejected);
}

TEST_CASE("graphics.gui_pending_input_budget_is_explicit_and_recovers_after_frame") {
  auto gpu = renderer();
  REQUIRE(gpu.enableGui());
  Dot::Platform::Event text{0, 0, Dot::Platform::Text{std::string(65536, 'a')}};
  REQUIRE(gpu.guiEvent(text));
  auto rejected = gpu.guiEvent(text);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().code == Dot::Core::ErrorCode::Unavailable);
  REQUIRE(gpu.renderGui(rect, 96, 96));
  REQUIRE(gpu.guiEvent(text));
}

TEST_CASE("graphics.gui_navigation_keys_reach_exact_widget_keys") {
  auto gpu = renderer();
  REQUIRE(gpu.enableGui());
  constexpr std::array mapping{std::pair{Dot::Platform::Key::Delete, ImGuiKey_Delete},
                               std::pair{Dot::Platform::Key::Insert, ImGuiKey_Insert},
                               std::pair{Dot::Platform::Key::Home, ImGuiKey_Home},
                               std::pair{Dot::Platform::Key::End, ImGuiKey_End},
                               std::pair{Dot::Platform::Key::PageUp, ImGuiKey_PageUp},
                               std::pair{Dot::Platform::Key::PageDown, ImGuiKey_PageDown},
                               std::pair{Dot::Platform::Key::KeypadEnter, ImGuiKey_KeypadEnter}};

  for (const auto& [native, gui] : mapping) {
    REQUIRE(gpu.guiEvent({0, 0, Dot::Platform::KeyChanged{native, true, false}}));
  }

  REQUIRE(gpu.renderGui(
      [&]() -> Dot::Core::Result<void> {
        for (const auto& [native, gui] : mapping) {
          CHECK(ImGui::IsKeyDown(gui));
        }

        return {};
      },
      96, 96));
}
