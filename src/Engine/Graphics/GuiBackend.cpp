module;

#include <SDL3/SDL.h>
#include <ft2build.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>
#include <imgui_internal.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

module Dot.Graphics.Renderer;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Core.Text;
import Dot.Platform.Events;

#include "GuiBackend.hpp"

namespace Dot::Graphics {
  namespace {
    std::atomic<std::uint64_t> contexts{};
  }

  auto guiContexts() noexcept -> std::uint64_t {
    return contexts.load();
  }
}  // namespace Dot::Graphics

namespace Dot::Graphics::Detail {
  namespace {
    struct ContextScope {
      ImGuiContext* previous{ImGui::GetCurrentContext()};

      explicit ContextScope(ImGuiContext* current) {
        ImGui::SetCurrentContext(current);
      }

      ~ContextScope() {
        ImGui::SetCurrentContext(previous);
      }
    };

    struct FontReader {
      FT_Library library{};
      FT_Face face{};

      ~FontReader() {
        if (face) {
          FT_Done_Face(face);
        }

        if (library) {
          FT_Done_FreeType(library);
        }
      }
    };

    struct KeyMapping {
      SDL_Scancode native;
      ImGuiKey gui;
    };

    auto key(Platform::Key value) -> KeyMapping {
      using Key = Platform::Key;
      const auto index = static_cast<int>(value);

      if (value >= Key::A && value <= Key::Z) {
        const auto offset = index - static_cast<int>(Key::A);
        return {static_cast<SDL_Scancode>(SDL_SCANCODE_A + offset),
                static_cast<ImGuiKey>(ImGuiKey_A + offset)};
      }

      if (value >= Key::Digit1 && value <= Key::Digit9) {
        const auto offset = index - static_cast<int>(Key::Digit1);
        return {static_cast<SDL_Scancode>(SDL_SCANCODE_1 + offset),
                static_cast<ImGuiKey>(ImGuiKey_1 + offset)};
      }

      if (value >= Key::F1 && value <= Key::F12) {
        const auto offset = index - static_cast<int>(Key::F1);
        return {static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + offset),
                static_cast<ImGuiKey>(ImGuiKey_F1 + offset)};
      }

      switch (value) {
        case Key::Digit0:
          return {SDL_SCANCODE_0, ImGuiKey_0};
        case Key::Enter:
          return {SDL_SCANCODE_RETURN, ImGuiKey_Enter};
        case Key::Escape:
          return {SDL_SCANCODE_ESCAPE, ImGuiKey_Escape};
        case Key::Backspace:
          return {SDL_SCANCODE_BACKSPACE, ImGuiKey_Backspace};
        case Key::Tab:
          return {SDL_SCANCODE_TAB, ImGuiKey_Tab};
        case Key::Space:
          return {SDL_SCANCODE_SPACE, ImGuiKey_Space};
        case Key::Left:
          return {SDL_SCANCODE_LEFT, ImGuiKey_LeftArrow};
        case Key::Right:
          return {SDL_SCANCODE_RIGHT, ImGuiKey_RightArrow};
        case Key::Up:
          return {SDL_SCANCODE_UP, ImGuiKey_UpArrow};
        case Key::Down:
          return {SDL_SCANCODE_DOWN, ImGuiKey_DownArrow};
        case Key::LeftControl:
          return {SDL_SCANCODE_LCTRL, ImGuiKey_LeftCtrl};
        case Key::RightControl:
          return {SDL_SCANCODE_RCTRL, ImGuiKey_RightCtrl};
        case Key::LeftShift:
          return {SDL_SCANCODE_LSHIFT, ImGuiKey_LeftShift};
        case Key::RightShift:
          return {SDL_SCANCODE_RSHIFT, ImGuiKey_RightShift};
        case Key::LeftAlt:
          return {SDL_SCANCODE_LALT, ImGuiKey_LeftAlt};
        case Key::RightAlt:
          return {SDL_SCANCODE_RALT, ImGuiKey_RightAlt};
        case Key::LeftSuper:
          return {SDL_SCANCODE_LGUI, ImGuiKey_LeftSuper};
        case Key::RightSuper:
          return {SDL_SCANCODE_RGUI, ImGuiKey_RightSuper};
        case Key::Delete:
          return {SDL_SCANCODE_DELETE, ImGuiKey_Delete};
        case Key::Insert:
          return {SDL_SCANCODE_INSERT, ImGuiKey_Insert};
        case Key::Home:
          return {SDL_SCANCODE_HOME, ImGuiKey_Home};
        case Key::End:
          return {SDL_SCANCODE_END, ImGuiKey_End};
        case Key::PageUp:
          return {SDL_SCANCODE_PAGEUP, ImGuiKey_PageUp};
        case Key::PageDown:
          return {SDL_SCANCODE_PAGEDOWN, ImGuiKey_PageDown};
        case Key::KeypadEnter:
          return {SDL_SCANCODE_KP_ENTER, ImGuiKey_KeypadEnter};
        default:
          return {SDL_SCANCODE_UNKNOWN, ImGuiKey_None};
      }
    }

    auto button(Platform::MouseButton value) -> int {
      switch (value) {
        case Platform::MouseButton::Left:
          return 0;
        case Platform::MouseButton::Right:
          return 1;
        case Platform::MouseButton::Middle:
          return 2;
        case Platform::MouseButton::Back:
          return 3;
        case Platform::MouseButton::Forward:
          return 4;
        default:
          return -1;
      }
    }
  }  // namespace

  struct GuiBackend::Impl {
    ImGuiContext* m_context{};
    SDL_Window* m_window{};
    GuiConfig m_config;
    GuiStats m_stats;
    bool m_platform{};
    bool m_renderer{};
    bool m_building{};
    bool m_tracked{};
    std::array<bool, static_cast<std::size_t>(Platform::Key::Count)> m_keys{};
    std::string m_font;
    /** Stable opaque layout tokens; never submitted as GPU texture views.
     * 所属帧解析为真实视图或移除绘制命令，稳定布局令牌不作为 GPU 视图提交。 */
    std::array<std::uint8_t, 16> m_imageTags{};

    struct ImageRegion {
      ImDrawList* source{};
      ImVec2 origin{};
      ImVec2 size{};
      ImVec2 clipMin{};
      ImVec2 clipMax{};
      float alpha{1};
    };

    std::array<ImageRegion, 16> m_regions{};
    std::vector<std::unique_ptr<ImDrawList>> m_overlays;
    std::size_t m_imageCount{};
    std::string m_error;
    bool m_validationError{};

    ~Impl() {
      if (!m_context) {
        return;
      }

      auto* previous = ImGui::GetCurrentContext();
      ImGui::SetCurrentContext(m_context);
      m_overlays.clear();

      if (m_renderer) {
        ImGui_ImplWGPU_Shutdown();
      }

      if (m_platform) {
        ImGui_ImplSDL3_Shutdown();
      }

      ImGui::DestroyContext(m_context);
      ImGui::SetCurrentContext(previous == m_context ? nullptr : previous);

      if (m_tracked) {
        --contexts;
      }
    }

    auto modifiers() -> SDL_Keymod {
      using Key = Platform::Key;
      const auto held = [&](Key value) { return m_keys[static_cast<std::size_t>(value)]; };
      const bool control = held(Key::LeftControl) || held(Key::RightControl);
      const bool shift = held(Key::LeftShift) || held(Key::RightShift);
      const bool alt = held(Key::LeftAlt) || held(Key::RightAlt);
      const bool super = held(Key::LeftSuper) || held(Key::RightSuper);
      auto& io = ImGui::GetIO();
      io.AddKeyEvent(ImGuiMod_Ctrl, control);
      io.AddKeyEvent(ImGuiMod_Shift, shift);
      io.AddKeyEvent(ImGuiMod_Alt, alt);
      io.AddKeyEvent(ImGuiMod_Super, super);
      return static_cast<SDL_Keymod>((control ? SDL_KMOD_CTRL : 0) | (shift ? SDL_KMOD_SHIFT : 0)
                                     | (alt ? SDL_KMOD_ALT : 0) | (super ? SDL_KMOD_GUI : 0));
    }
  };

  GuiBackend::GuiBackend(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  GuiBackend::~GuiBackend() = default;

  auto GuiBackend::create(WGPUDevice device, WGPUTextureFormat format, SDL_Window* window,
                          const GuiConfig& config) -> Core::Result<std::unique_ptr<GuiBackend>> {
    if (!std::isfinite(config.fontSize) || config.fontSize < 8 || config.fontSize > 64
        || config.maxVertices == 0 || config.maxVertices > 2'000'000 || config.maxIndices == 0
        || config.maxIndices > 6'000'000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid GUI font/geometry limits");
    }

    try {
      auto impl = std::make_unique<Impl>();
      impl->m_config = config;
      impl->m_window = window;

      if (!config.font.empty()) {
        std::error_code error;
        auto absolute = std::filesystem::absolute(config.font, error);

        if (error) {
          return Core::fail(Core::ErrorCode::Io, "Resolve GUI font: " + error.message());
        }

        auto files = Core::FileSystem::open(absolute.parent_path());

        if (!files) {
          return std::unexpected(files.error());
        }

        auto bytes = files->readBytes(absolute.filename(), 64 * 1024 * 1024);

        if (!bytes) {
          return std::unexpected(bytes.error());
        }

        if (bytes->size() < 12) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "GUI font is truncated");
        }

        impl->m_font.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        FontReader reader;

        if (FT_Init_FreeType(&reader.library) != 0) {
          return Core::fail(Core::ErrorCode::Unavailable, "Initialize GUI font reader failed");
        }

        if (FT_New_Memory_Face(reader.library,
                               reinterpret_cast<const FT_Byte*>(impl->m_font.data()),
                               static_cast<FT_Long>(impl->m_font.size()), 0, &reader.face)
                != 0
            || !FT_IS_SCALABLE(reader.face)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "GUI font is not a loadable scalable font");
        }
      }

      auto* previous = ImGui::GetCurrentContext();
      impl->m_context = ImGui::CreateContext();
      ImGui::SetCurrentContext(previous);

      if (!impl->m_context) {
        return Core::fail(Core::ErrorCode::Unavailable, "Create GUI context failed");
      }

      ++contexts;
      impl->m_tracked = true;

      ContextScope context{impl->m_context};
      auto& io = ImGui::GetIO();
      io.IniFilename = nullptr;
      io.LogFilename = nullptr;
      io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      io.ConfigErrorRecoveryEnableAssert = false;
      io.ConfigErrorRecoveryEnableDebugLog = false;
      io.ConfigErrorRecoveryEnableTooltip = false;
      impl->m_context->ErrorCallbackUserData = impl.get();
      impl->m_context->ErrorCallback = [](ImGuiContext*, void* user, const char* message) noexcept {
        static_cast<Impl*>(user)->m_validationError = true;
        try {
          static_cast<Impl*>(user)->m_error = message;
        } catch (...) {
        }
      };
      ImGui::StyleColorsDark();

      if (!impl->m_font.empty()) {
        ImFontConfig fontConfig;
        fontConfig.FontDataOwnedByAtlas = false;

        if (!io.Fonts->AddFontFromMemoryTTF(impl->m_font.data(),
                                            static_cast<int>(impl->m_font.size()), config.fontSize,
                                            &fontConfig)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "GUI font could not be loaded");
        }
      } else {
        ImFontConfig fontConfig;
        fontConfig.SizePixels = config.fontSize;
        io.Fonts->AddFontDefault(&fontConfig);
      }

      if (window) {
        if (!ImGui_ImplSDL3_InitForOther(window)) {
          return Core::fail(Core::ErrorCode::External, "Initialize GUI SDL3 backend failed");
        }

        impl->m_platform = true;
      }

      ImGui_ImplWGPU_InitInfo gpu;
      gpu.Device = device;
      gpu.RenderTargetFormat = format;

      if (!ImGui_ImplWGPU_Init(&gpu)) {
        return Core::fail(Core::ErrorCode::External, "Initialize GUI WebGPU backend failed");
      }

      impl->m_renderer = true;
      impl->m_stats.enabled = true;
      return std::unique_ptr<GuiBackend>(new GuiBackend(std::move(impl)));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create GUI backend: " + std::string(error.what()));
    }
  }

  auto GuiBackend::event(const Platform::Event& event) -> Core::Result<bool> {
    if (m_impl->m_building) {
      return Core::fail(Core::ErrorCode::InvalidState, "GUI event cannot reenter drawing");
    }

    if (m_impl->m_window && event.windowId != SDL_GetWindowID(m_impl->m_window)) {
      return false;
    }

    ContextScope context{m_impl->m_context};
    auto& io = ImGui::GetIO();
    constexpr int maxPendingEvents = 131072;

    if (m_impl->m_context->InputEventsQueue.Size > maxPendingEvents - 8) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "GUI pending input budget exhausted; process a frame before more input");
    }

    SDL_Event native{};
    native.common.timestamp = event.timestamp;
    bool handled = true;

    if (const auto* keyEvent = std::get_if<Platform::KeyChanged>(&event.data)) {
      auto mapped = key(keyEvent->key);

      if (mapped.gui == ImGuiKey_None) {
        return false;
      }

      m_impl->m_keys[static_cast<std::size_t>(keyEvent->key)] = keyEvent->down;
      native.key.type = keyEvent->down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
      native.key.windowID = event.windowId;
      native.key.scancode = mapped.native;
      native.key.mod = m_impl->modifiers();
      native.key.key
          = m_impl->m_platform ? SDL_GetKeyFromScancode(mapped.native, native.key.mod, true) : 0;
      native.key.down = keyEvent->down;
      native.key.repeat = keyEvent->repeat;

      if (!m_impl->m_platform) {
        io.AddKeyEvent(mapped.gui, keyEvent->down);
      }
    } else if (const auto* pointer = std::get_if<Platform::PointerMoved>(&event.data)) {
      if (!std::isfinite(pointer->x) || !std::isfinite(pointer->y)
          || !std::isfinite(pointer->deltaX) || !std::isfinite(pointer->deltaY)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "GUI pointer requires finite coordinates");
      }

      native.motion.type = SDL_EVENT_MOUSE_MOTION;
      native.motion.windowID = event.windowId;
      native.motion.x = pointer->x;
      native.motion.y = pointer->y;
      native.motion.xrel = pointer->deltaX;
      native.motion.yrel = pointer->deltaY;

      if (!m_impl->m_platform) {
        io.AddMousePosEvent(pointer->x, pointer->y);
      }
    } else if (const auto* buttonEvent = std::get_if<Platform::ButtonChanged>(&event.data)) {
      const auto index = button(buttonEvent->button);

      if (index < 0) {
        return false;
      }

      if (buttonEvent->position) {
        const auto& point = *buttonEvent->position;

        if (!std::isfinite(point[0]) || !std::isfinite(point[1])) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "GUI button requires finite coordinates");
        }

        io.AddMousePosEvent(point[0], point[1]);
      }

      constexpr std::array<Uint8, 5> buttons{SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE,
                                             SDL_BUTTON_X1, SDL_BUTTON_X2};
      native.button.type
          = buttonEvent->down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
      native.button.windowID = event.windowId;
      native.button.button = buttons[static_cast<std::size_t>(index)];
      native.button.down = buttonEvent->down;

      if (buttonEvent->position) {
        native.button.x = (*buttonEvent->position)[0];
        native.button.y = (*buttonEvent->position)[1];
      }

      if (!m_impl->m_platform) {
        io.AddMouseButtonEvent(index, buttonEvent->down);
      }
    } else if (const auto* wheel = std::get_if<Platform::Wheel>(&event.data)) {
      if (!std::isfinite(wheel->x) || !std::isfinite(wheel->y)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "GUI wheel requires finite coordinates");
      }

      native.wheel.type = SDL_EVENT_MOUSE_WHEEL;
      native.wheel.windowID = event.windowId;
      native.wheel.x = wheel->x;
      native.wheel.y = wheel->y;

      if (!m_impl->m_platform) {
        io.AddMouseWheelEvent(wheel->x, wheel->y);
      }
    } else if (const auto* text = std::get_if<Platform::Text>(&event.data)) {
      if (text->value.size() > 64 * 1024 || text->value.find('\0') != std::string::npos
          || !Core::isValidUtf8(text->value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "GUI text requires UTF-8 without NUL, at most 64 KiB");
      }

      auto characters = Core::utf8CodePoints(text->value);

      if (!characters
          || *characters + static_cast<std::size_t>(m_impl->m_context->InputEventsQueue.Size) + 8
                 > static_cast<std::size_t>(maxPendingEvents)) {
        return Core::fail(Core::ErrorCode::Unavailable, "GUI text exceeds pending input budget");
      }

      native.text.type = SDL_EVENT_TEXT_INPUT;
      native.text.windowID = event.windowId;
      native.text.text = text->value.c_str();

      if (!m_impl->m_platform) {
        io.AddInputCharactersUTF8(text->value.c_str());
      }
    } else if (const auto* focus = std::get_if<Platform::Focus>(&event.data)) {
      native.window.type
          = focus->focused ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
      native.window.windowID = event.windowId;

      if (!focus->focused) {
        m_impl->m_keys.fill(false);
        static_cast<void>(m_impl->modifiers());
      }

      if (!m_impl->m_platform) {
        io.AddFocusEvent(focus->focused);
      }
    } else {
      handled = false;
    }

    if (handled && m_impl->m_platform) {
      return ImGui_ImplSDL3_ProcessEvent(&native);
    }

    return handled;
  }

  auto GuiBackend::build(const GuiDraw& draw, std::uint32_t width, std::uint32_t height,
                         double seconds, float scaleX, float scaleY) -> Core::Result<void> {
    if (!draw || m_impl->m_building || !std::isfinite(seconds) || seconds < 0 || seconds > 1
        || !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX < 0.25F || scaleY < 0.25F
        || scaleX > 8 || scaleY > 8) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid GUI draw/delta/scale");
    }

    if (m_impl->m_stats.frames == std::numeric_limits<std::uint64_t>::max() - 1
        || m_impl->m_stats.input.frame == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::Unavailable, "GUI frame index exhausted");
    }

    ContextScope context{m_impl->m_context};
    m_impl->m_building = true;
    m_impl->m_error.clear();
    m_impl->m_validationError = false;
    bool started = false;
    ImGuiErrorRecoveryState recovery;

    try {
      if (auto* previous = ImGui::GetDrawData()) {
        previous->Clear();
      }

      m_impl->m_overlays.clear();
      ImGui_ImplWGPU_NewFrame();
      m_impl->m_imageCount = 0;

      if (m_impl->m_platform) {
        ImGui_ImplSDL3_NewFrame();
      }

      auto& io = ImGui::GetIO();
      io.DisplaySize = {static_cast<float>(width) / scaleX, static_cast<float>(height) / scaleY};
      io.DisplayFramebufferScale = {scaleX, scaleY};
      io.DeltaTime = static_cast<float>(std::max(seconds, 0.000001));
      ImGui::NewFrame();
      started = true;
      auto& input = m_impl->m_stats.input;
      ++input.frame;
      input.logicalSize = {io.DisplaySize.x, io.DisplaySize.y};
      input.framebufferScale = {io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y};
      input.pointer = ImGui::IsMousePosValid(&io.MousePos)
                          ? std::optional{Math::Vec2{io.MousePos.x, io.MousePos.y}}
                          : std::nullopt;
      input.buttons = 0;

      for (std::uint32_t button = 0; button < 5; ++button) {
        input.buttons |= io.MouseDown[button] ? (1U << button) : 0;
      }

      ImGui::ErrorRecoveryStoreState(&recovery);
      auto result = draw();

      if (!result) {
        ImGui::ErrorRecoveryTryToRecoverState(&recovery);
        ImGui::EndFrame();
        m_impl->m_building = false;
        return result;
      }

      ImGui::ErrorRecoveryTryToRecoverState(&recovery);

      if (m_impl->m_validationError) {
        ImGui::EndFrame();
        m_impl->m_building = false;
        return Core::fail(Core::ErrorCode::InvalidState,
                          "GUI callback validation failed: " + m_impl->m_error);
      }

      ImGui::Render();
      started = false;

      if (m_impl->m_validationError) {
        m_impl->m_building = false;
        return Core::fail(Core::ErrorCode::InvalidState,
                          "GUI frame validation failed: " + m_impl->m_error);
      }

      auto* data = ImGui::GetDrawData();
      m_impl->m_building = false;

      if (!data || data->TotalVtxCount < 0 || data->TotalIdxCount < 0
          || static_cast<std::uint32_t>(data->TotalVtxCount) > m_impl->m_config.maxVertices
          || static_cast<std::uint32_t>(data->TotalIdxCount) > m_impl->m_config.maxIndices) {
        return Core::fail(Core::ErrorCode::Unavailable, "GUI geometry exceeded configured budget");
      }

      m_impl->m_stats.vertices = static_cast<std::uint32_t>(data->TotalVtxCount);
      m_impl->m_stats.indices = static_cast<std::uint32_t>(data->TotalIdxCount);
      m_impl->m_stats.mouse = io.WantCaptureMouse;
      m_impl->m_stats.keyboard = io.WantCaptureKeyboard;
      m_impl->m_stats.text = io.WantTextInput;
      return {};
    } catch (const std::exception& error) {
      if (started) {
        ImGui::ErrorRecoveryTryToRecoverState(&recovery);
        ImGui::EndFrame();
      }

      m_impl->m_building = false;
      return Core::fail(Core::ErrorCode::External, "Build GUI frame: " + std::string(error.what()));
    } catch (...) {
      if (started) {
        ImGui::ErrorRecoveryTryToRecoverState(&recovery);
        ImGui::EndFrame();
      }

      m_impl->m_building = false;
      return Core::fail(Core::ErrorCode::External, "GUI callback threw a nonstandard exception");
    }
  }

  auto GuiBackend::encode(WGPURenderPassEncoder pass) -> void {
    ContextScope context{m_impl->m_context};
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
  }

  auto GuiBackend::image(WGPUTextureView view, float width, float height) -> Core::Result<void> {
    if (!m_impl->m_building || !std::isfinite(width) || !std::isfinite(height) || width <= 0
        || height <= 0 || width > 32768 || height > 32768) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "GUI image requires an active frame and finite size in (0,32768]");
    }

    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(view)), {width, height});
    return {};
  }

  auto GuiBackend::reserveImage(float width, float height, std::uint32_t maximum)
      -> Core::Result<GuiImageSlot> {
    if (!m_impl->m_building || !std::isfinite(width) || !std::isfinite(height) || width <= 0
        || height <= 0 || m_impl->m_imageCount >= 16) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid deferred GUI viewport size/count");
    }

    const auto scale = ImGui::GetIO().DisplayFramebufferScale;
    const auto pixelsX = std::ceil(width * scale.x);
    const auto pixelsY = std::ceil(height * scale.y);

    if (!std::isfinite(pixelsX) || !std::isfinite(pixelsY) || pixelsX < 1 || pixelsY < 1
        || pixelsX > static_cast<float>(std::min(4096U, maximum))
        || pixelsY > static_cast<float>(std::min(4096U, maximum))) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Deferred GUI viewport exceeds physical pixel limits");
    }

    const auto index = m_impl->m_imageCount;
    const auto token
        = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(&m_impl->m_imageTags[index]));
    ImGui::Image(token, {width, height});
    auto* list = ImGui::GetWindowDrawList();
    const auto border = ImGui::GetStyle().ImageBorderSize;
    const auto item = ImGui::GetItemRectMin();
    const ImVec2 origin{item.x + border, item.y + border};
    const auto clipMin = list->GetClipRectMin();
    const auto clipMax = list->GetClipRectMax();
    auto& region = m_impl->m_regions[index];
    region = {list,
              origin,
              {width, height},
              {std::max(origin.x, clipMin.x), std::max(origin.y, clipMin.y)},
              {std::min(origin.x + width, clipMax.x), std::min(origin.y + height, clipMax.y)},
              ImGui::GetStyle().Alpha};

    if (!ImGui::IsItemVisible()) {
      region.clipMax = region.clipMin;
    }

    ++m_impl->m_imageCount;
    return GuiImageSlot{index, static_cast<std::uint32_t>(pixelsX),
                        static_cast<std::uint32_t>(pixelsY)};
  }

  auto GuiBackend::resolveImage(std::size_t index, WGPUTextureView view) -> Core::Result<void> {
    if (m_impl->m_building || index >= m_impl->m_imageCount) {
      return Core::fail(Core::ErrorCode::InvalidState, "Invalid deferred GUI viewport resolution");
    }

    ContextScope context{m_impl->m_context};
    const auto token
        = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(&m_impl->m_imageTags[index]));
    auto* data = ImGui::GetDrawData();

    for (auto* list : data->CmdLists) {
      for (int commandIndex = list->CmdBuffer.Size; commandIndex-- > 0;) {
        auto& command = list->CmdBuffer[commandIndex];

        if (!command.TexRef._TexData && command.TexRef._TexID == token) {
          if (view) {
            command.TexRef = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(view));
          } else {
            list->CmdBuffer.erase(list->CmdBuffer.Data + commandIndex);
          }
        }
      }
    }

    return {};
  }

  auto GuiBackend::resolveOverlay(std::size_t index, const ViewportOverlay& overlay)
      -> Core::Result<void> {
    if (m_impl->m_building || index >= m_impl->m_imageCount) {
      return Core::fail(Core::ErrorCode::InvalidState, "Invalid viewport overlay resolution");
    }

    auto valid = validateOverlay(overlay);

    if (!valid) {
      return valid;
    }

    const auto& region = m_impl->m_regions[index];

    if ((overlay.lines.empty() && overlay.points.empty()) || region.clipMax.x <= region.clipMin.x
        || region.clipMax.y <= region.clipMin.y) {
      return {};
    }

    ContextScope context{m_impl->m_context};
    auto* data = ImGui::GetDrawData();
    const auto source = std::find(data->CmdLists.begin(), data->CmdLists.end(), region.source);

    if (source == data->CmdLists.end()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Viewport overlay lost its source window");
    }

    const auto offset = source - data->CmdLists.begin() + 1;
    auto list = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
    list->_ResetForNewFrame();
    list->PushTexture(ImGui::GetIO().Fonts->TexRef);
    list->PushClipRect(region.clipMin, region.clipMax);
    const auto position = [&](Math::Vec2 uv) -> ImVec2 {
      return {region.origin.x + uv.x * region.size.x, region.origin.y + uv.y * region.size.y};
    };
    const auto color = [&](Math::Vec4 value) -> ImU32 {
      return ImGui::ColorConvertFloat4ToU32({value.x, value.y, value.z, value.w * region.alpha});
    };

    for (const auto& line : overlay.lines) {
      list->AddLine(position(line.segment.from), position(line.segment.to), color(line.color),
                    line.thickness);
    }

    for (const auto& point : overlay.points) {
      list->AddCircleFilled(position(point.position), point.radius, color(point.color), 12);
    }

    list->PopClipRect();
    list->PopTexture();
    const auto vertices = static_cast<std::uint64_t>(data->TotalVtxCount)
                          + static_cast<std::uint64_t>(list->VtxBuffer.Size);
    const auto indices = static_cast<std::uint64_t>(data->TotalIdxCount)
                         + static_cast<std::uint64_t>(list->IdxBuffer.Size);

    if (vertices > m_impl->m_config.maxVertices || indices > m_impl->m_config.maxIndices
        || vertices > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
        || indices > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "Viewport overlay exceeds GUI geometry budget");
    }

    auto* resolved = list.get();
    m_impl->m_overlays.push_back(std::move(list));
    data->AddDrawList(resolved);
    std::rotate(data->CmdLists.begin() + offset, data->CmdLists.end() - 1, data->CmdLists.end());
    m_impl->m_stats.vertices = static_cast<std::uint32_t>(vertices);
    m_impl->m_stats.indices = static_cast<std::uint32_t>(indices);
    return {};
  }

  auto GuiBackend::stats() const -> GuiStats {
    return m_impl->m_stats;
  }

  auto GuiBackend::building() const -> bool {
    return m_impl->m_building;
  }

  auto GuiBackend::submitted() -> void {
    ++m_impl->m_stats.frames;
  }
}  // namespace Dot::Graphics::Detail
