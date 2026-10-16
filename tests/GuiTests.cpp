#include <doctest/doctest.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
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
    auto result
        = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
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

TEST_CASE("graphics.gui_packaged_inter_sarasa_glyphs_own_deleted_font_source_and_survive_move") {
  const auto before = guiContexts();
  auto storage = Dot::Core::StagedDirectory::create(
                     Dot::Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                     .value();
  const auto source = std::filesystem::path(DOT_EDITOR_FONT);
  auto files = Dot::Core::FileSystem::open(source.parent_path()).value();
  const auto bytes = files.readBytes(source.filename()).value();
  REQUIRE(storage.files()->writeAtomic("editor.ttf", bytes));
  auto gpu = renderer();
  REQUIRE(gpu.enableGui({.font = storage.files()->root() / "editor.ttf", .fontSize = 15}));
  REQUIRE(std::filesystem::remove(storage.files()->root() / "editor.ttf"));
  auto moved = std::move(gpu);
  const auto text = []() -> Dot::Core::Result<void> {
    ImGui::GetBackgroundDrawList()->AddText({8, 8}, IM_COL32_WHITE, "Editor Wi 编辑 あ 한");
    auto* font = ImGui::GetFontBaked();

    for (const auto code : {0x57, 0x69, 0x7F16, 0x8F91, 0x3042, 0xD55C}) {
      const auto* glyph = font->FindGlyphNoFallback(static_cast<ImWchar>(code));
      REQUIRE(glyph);
      CHECK(glyph->Codepoint == code);
      CHECK(glyph->AdvanceX > 0);
    }

    return {};
  };
  auto image = moved.renderGui(text, 320, 64);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  image = moved.renderGui(text, 320, 64);
  REQUIRE(image);
  const auto background = pixel(*image, 0, 0);
  std::size_t changed{};

  for (unsigned y = 0; y < image->height; ++y) {
    for (unsigned x = 0; x < image->width; ++x) {
      changed += pixel(*image, x, y) != background;
    }
  }

  CHECK(changed > 32);
  REQUIRE(moved.closeGui());
  CHECK(guiContexts() == before);
}

TEST_CASE("graphics.gui_texture_pixels_owned_submission_foreign_errors_and_release") {
  auto gpu = renderer();
  REQUIRE(gpu.enableGui());
  std::optional<Texture> texture
      = gpu.uploadTexture(Image{1, 1, {128, 64, 255, 255}}, {.colorSpace = ColorSpace::Linear})
            .value();
  CHECK_FALSE(gpu.guiImage(*texture, 16, 16));
  auto foreign = renderer();
  auto other = foreign.uploadTexture(Image{1, 1, {255, 0, 0, 255}}).value();
  const auto draw = [&]() -> Dot::Core::Result<void> {
    CHECK_FALSE(gpu.guiImage(other, 16, 16));
    CHECK_FALSE(gpu.guiImage(Texture{}, 16, 16));
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({64, 64});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("texture", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    auto result = gpu.guiImage(*texture, 64, 64);
    texture.reset();
    ImGui::End();
    ImGui::PopStyleVar();
    return result;
  };
  auto image = gpu.renderGui(draw, 64, 64);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(pixel(*image, 32, 32) == std::array<std::uint8_t, 4>{128, 64, 255, 255});
  CHECK(gpu.resourceStats()->textures == 0);
  texture = gpu.uploadTexture(Image{1, 1, {255, 255, 255, 255}}).value();
  CHECK_FALSE(gpu.renderGui(
      [&]() -> Dot::Core::Result<void> {
        auto result = gpu.guiImage(*texture, -1, 16);
        texture.reset();
        return result;
      },
      64, 64));
  CHECK(gpu.resourceStats()->textures == 0);
}

TEST_CASE("graphics.gui_viewport_resolves_current_dpi_pixels_after_layout_and_retains_targets") {
  auto gpu = renderer();
  std::optional<RenderTarget> retained;
  int calls{};
  float width = 40.25F;
  const GuiViewportDraw viewport =
      [&](std::uint32_t w, std::uint32_t h) -> Dot::Core::Result<std::optional<GuiViewportFrame>> {
    ++calls;
    CHECK_FALSE(gpu.closeGui());
    CHECK_FALSE(gpu.renderGui(rect, 8, 8));
    auto target = gpu.createTarget(w, h);
    REQUIRE(target);
    REQUIRE(gpu.renderMeshesInto(*target, {}, {}, {1, 0, 0, 1}));
    retained = *target;
    return std::optional{GuiViewportFrame{*retained, {}}};
  };
  REQUIRE(gpu.enableGui());
  const GuiDraw draw = [&]() -> Dot::Core::Result<void> {
    CHECK_FALSE(gpu.createTarget(8, 8));
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({64, 48});
    ImGui::Begin("Deferred", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground
                     | ImGuiWindowFlags_NoSavedSettings);
    auto result = gpu.guiViewport(viewport, width, 24.25F);
    ImGui::End();
    return result;
  };
  auto image = gpu.renderGui(draw, 128, 96, 1.0 / 60, 2);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(calls == 1);
  REQUIRE(retained);
  CHECK(retained->info()->width == 81);
  CHECK(retained->info()->height == 49);
  CHECK(pixel(*image, 20, 20) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  width = 30.75F;
  image = gpu.renderGui(draw, 128, 96, 1.0 / 60, 2);
  REQUIRE(image);
  CHECK(calls == 2);
  CHECK(retained->info()->width == 62);
  CHECK(gpu.resourceStats()->targets == 1);
  CHECK(pixel(*image, 20, 20) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  retained.reset();
  CHECK(gpu.resourceStats()->targets == 0);
  REQUIRE(gpu.closeGui());
}

TEST_CASE(
    "graphics.gui_viewport_absence_failures_and_throw_recover_without_fake_frames_or_replay") {
  auto gpu = renderer();
  auto other = renderer();
  REQUIRE(gpu.enableGui());
  const auto frame = [&](GuiViewportDraw viewport) {
    return gpu.renderGui(
        [&]() -> Dot::Core::Result<void> {
          ImGui::SetNextWindowPos({0, 0});
          ImGui::SetNextWindowSize({64, 48});
          ImGui::Begin("Deferred", nullptr,
                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
          auto result = gpu.guiViewport(viewport, 24, 24);
          ImGui::End();
          return result;
        },
        64, 48);
  };
  auto foreign = other.createTarget(24, 24).value();
  REQUIRE(other.renderMeshesInto(foreign, {}, {}));
  CHECK_FALSE(frame([&](auto, auto) -> Dot::Core::Result<std::optional<GuiViewportFrame>> {
    return std::optional{GuiViewportFrame{foreign, {}}};
  }));
  auto empty = gpu.createTarget(24, 24).value();
  CHECK_FALSE(frame([&](auto, auto) -> Dot::Core::Result<std::optional<GuiViewportFrame>> {
    return std::optional{GuiViewportFrame{empty, {}}};
  }));
  empty = {};
  int calls{};
  auto failed = frame([&](auto w, auto h) -> Dot::Core::Result<std::optional<GuiViewportFrame>> {
    ++calls;
    auto target = gpu.createTarget(w, h).value();
    throw std::runtime_error("viewport draw fixture");
  });
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("viewport draw fixture") != std::string::npos);
  CHECK(calls == 1);
  CHECK(gpu.guiStats()->frames == 0);
  CHECK(gpu.resourceStats()->targets == 0);
  auto absent = frame([](auto, auto) -> Dot::Core::Result<std::optional<GuiViewportFrame>> {
    return std::nullopt;
  });
  REQUIRE_MESSAGE(absent, (absent ? "" : absent.error().message));
  CHECK(pixel(*absent, 20, 20) == pixel(*absent, 0, 0));
  CHECK(gpu.guiStats()->frames == 1);
  auto wrongSize = gpu.createTarget(8, 8).value();
  REQUIRE(gpu.renderMeshesInto(wrongSize, {}, {}));
  CHECK_FALSE(frame([&](auto, auto) -> Dot::Core::Result<std::optional<GuiViewportFrame>> {
    return std::optional{GuiViewportFrame{wrongSize, {}}};
  }));
  wrongSize = {};
  int omitted{};
  const GuiViewportDraw none
      = [&](auto, auto) -> Dot::Core::Result<std::optional<GuiViewportFrame>> {
    ++omitted;
    return std::nullopt;
  };
  REQUIRE(gpu.renderGui(
      [&]() -> Dot::Core::Result<void> {
        CHECK_FALSE(gpu.guiViewport({}, 1, 1));
        CHECK_FALSE(gpu.guiViewport(none, -1, 1));
        CHECK_FALSE(gpu.guiViewport(none, std::numeric_limits<float>::quiet_NaN(), 1));

        for (int i = 0; i < 16; ++i) {
          REQUIRE(gpu.guiViewport(none, 1, 1));
        }

        CHECK_FALSE(gpu.guiViewport(none, 1, 1));
        return {};
      },
      64, 48));
  CHECK(omitted == 16);
  CHECK(gpu.resourceStats()->targets == 0);
  REQUIRE(gpu.renderGui(rect, 64, 48));
  CHECK(calls == 1);
  CHECK(gpu.guiStats()->frames == 3);
}

TEST_CASE("graphics.target_real_frames_reuse_shared_ownership_and_validation_retention") {
  auto gpu = renderer();
  auto target = gpu.createTarget(65, 33).value();
  auto copy = target;
  CHECK(gpu.resourceStats()->targets == 1);
  CHECK(gpu.resourceStats()->targetBytes == 65 * 33 * 16);
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
        auto observed = gpu.guiStats();
        REQUIRE(observed);
        CHECK(observed->enabled);
        CHECK(observed->frames == 0);
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
    auto native
        = Renderer::create(nullptr, {.postProcess = {0, ToneMap::LinearClamp, ColorSpace::Linear}});
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

TEST_CASE(
    "graphics.gui_deferred_overlay_matches_dpi_clip_window_layer_and_preserves_scene_pixels") {
  auto gpu = renderer();
  REQUIRE(gpu.enableGui());
  std::optional<RenderTarget> retained;
  bool foreground{};
  bool large{};
  const GuiViewportDraw viewport
      = [&](auto w, auto h) -> Dot::Core::Result<std::optional<GuiViewportFrame>> {
    auto target = gpu.createTarget(w, h).value();
    REQUIRE(gpu.renderMeshesInto(target, {}, {}, {1, 0, 0, 1}));
    retained = target;
    ViewportOverlay overlay;
    overlay.lines.push_back({{{0, .25F}, {1, .25F}}, {0, 1, 1, 1}, 2});
    overlay.points.push_back({{.5F, .5F}, {0, 1, 0, 1}, 3});
    overlay.points.push_back({{1, 1}, {0, 1, 0, 1}, 3});
    if (large) {
      overlay.points.assign(4096, {{.5F, .5F}, {0, 1, 0, 1}, 3});
      overlay.lines.assign(4096, {{{0, .25F}, {1, .25F}}, {0, 1, 1, 1}, 2});
    }

    return std::optional{GuiViewportFrame{std::move(target), std::move(overlay)}};
  };
  const GuiDraw draw = [&]() -> Dot::Core::Result<void> {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 2);
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({64, 64});
    ImGui::Begin("Overlay canvas", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground
                     | ImGuiWindowFlags_NoSavedSettings);
    auto result = gpu.guiViewport(viewport, 32, 32);
    ImGui::End();
    ImGui::PopStyleVar(2);

    if (foreground) {
      ImGui::SetNextWindowPos({10, 10});
      ImGui::SetNextWindowSize({32, 32});
      ImGui::SetNextWindowFocus();
      ImGui::Begin("Overlay foreground", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground
                       | ImGuiWindowFlags_NoSavedSettings);
      ImGui::GetWindowDrawList()->AddRectFilled({10, 10}, {30, 30}, IM_COL32(0, 0, 255, 255));
      ImGui::End();
    }

    return result;
  };
  auto image = gpu.renderGui(draw, 128, 128, 1.0 / 60, 2);
  REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
  CHECK(pixel(*image, 36, 36) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
  CHECK(pixel(*image, 36, 20) == std::array<std::uint8_t, 4>{0, 255, 255, 255});
  CHECK(pixel(*image, 60, 36) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  CHECK(pixel(*image, 72, 60)[1] != 255);
  auto clean = gpu.readTarget(*retained).value();
  CHECK(pixel(clean, 32, 32) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
  foreground = true;
  image = gpu.renderGui(draw, 128, 128, 1.0 / 60, 2);
  REQUIRE(image);
  CHECK(pixel(*image, 36, 36) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
  CHECK(gpu.guiStats()->frames == 2);
  foreground = false;
  large = true;
  image = gpu.renderGui(draw, 128, 128, 1.0 / 60, 2);
  REQUIRE(image);
  CHECK(pixel(*image, 36, 36) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
  CHECK(gpu.guiStats()->vertices > 65535);
  CHECK(gpu.guiStats()->frames == 3);
  retained.reset();
  REQUIRE(gpu.closeGui());
  CHECK(gpu.resourceStats()->targets == 0);
}

TEST_CASE("graphics.gui_overlay_invalid_geometry_and_budget_errors_recover_without_replay") {
  auto gpu = renderer();
  GuiConfig config;
  config.maxVertices = 1000;
  config.maxIndices = 3000;
  REQUIRE(gpu.enableGui(config));
  int mode{};
  int calls{};
  const GuiViewportDraw viewport
      = [&](auto w, auto h) -> Dot::Core::Result<std::optional<GuiViewportFrame>> {
    ++calls;
    auto target = gpu.createTarget(w, h).value();
    REQUIRE(gpu.renderMeshesInto(target, {}, {}, {1, 0, 0, 1}));
    ViewportOverlay overlay;

    if (mode == 0) {
      overlay.points.push_back({{2, 0}});
    } else if (mode == 1) {
      overlay.points.assign(250, {{.5F, .5F}});
    }

    return std::optional{GuiViewportFrame{std::move(target), std::move(overlay)}};
  };
  const GuiDraw draw = [&]() -> Dot::Core::Result<void> {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({64, 64});
    ImGui::Begin("Overlay bounds", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    auto result = gpu.guiViewport(viewport, 24, 24);
    ImGui::End();
    return result;
  };
  auto invalid = gpu.renderGui(draw, 64, 64);
  REQUIRE_FALSE(invalid);
  CHECK(invalid.error().code == Dot::Core::ErrorCode::InvalidArgument);
  CHECK(calls == 1);
  CHECK(gpu.guiStats()->frames == 0);
  CHECK(gpu.resourceStats()->targets == 0);
  mode = 1;
  auto budget = gpu.renderGui(draw, 64, 64);
  REQUIRE_FALSE(budget);
  CHECK(budget.error().code == Dot::Core::ErrorCode::Unavailable);
  CHECK(calls == 2);
  CHECK(gpu.guiStats()->frames == 0);
  mode = 2;
  REQUIRE(gpu.renderGui(draw, 64, 64));
  CHECK(calls == 3);
  CHECK(gpu.guiStats()->frames == 1);
  REQUIRE(gpu.closeGui());
  CHECK(gpu.resourceStats()->targets == 0);
}
