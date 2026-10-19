module;

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <exception>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../Engine/Serialization/Json.hpp"

module Dot.Editor.Home;

import Dot.Core.Application;
import Dot.Core.Events;
import Dot.Core.StagedDirectory;
import Dot.Core.Control;
import Dot.Core.Text;
import Dot.Graphics.Renderer;
import Dot.Platform.System;
import Dot.Platform.Events;
import Dot.Project.Templates;
import Dot.Script.Compiler;

import Dot.Platform.Dialog;

namespace Dot::Editor {
  namespace {
    namespace Fs = std::filesystem;
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;

    auto path(const Json& value) -> Fs::path {
      const auto text = value.get<std::string>();

      if (text.empty() || !Core::isValidUtf8(text) || text.find('\0') != std::string::npos) {
        throw std::invalid_argument("Project paths require nonempty UTF-8 without NUL");
      }

      return Fs::path(std::u8string(text.begin(), text.end()));
    }

    auto utf(const Fs::path& value) -> std::string {
      const auto bytes = value.generic_u8string();
      return {bytes.begin(), bytes.end()};
    }

    auto parsed(std::string_view source) -> Json {
      if (source.empty() || source.size() > 1024 * 1024 || !Core::isValidUtf8(source)
          || source.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Project request requires bounded UTF-8 JSON");
      }

      return Codec::parseJson(source);
    }

    auto catalog() -> Json {
      return Json::array({{{"id", "blank"}, {"name", "Camera and cube"}},
                          {{"id", "lab"}, {"name", "Component and SDK lab"}}});
    }

    struct ActiveScope {
      bool& m_active;

      explicit ActiveScope(bool& active) : m_active(active) {
        m_active = true;
      }

      ~ActiveScope() {
        m_active = false;
      }

      ActiveScope(const ActiveScope&) = delete;
      auto operator=(const ActiveScope&) -> ActiveScope& = delete;
    };
  }  // namespace

  auto performProjectAction(const Core::FileSystem& resources, std::string_view request)
      -> Core::Result<Project::Project> {
    try {
      auto value = parsed(request);
      const auto command = value.at("command").get<std::string>();

      if (command == "open") {
        Codec::requireFields(value, {"command", "root"});
        return Project::Project::open(path(value["root"]));
      }

      Codec::requireFields(value, {"command", "parent", "folder", "name", "template"});
      const auto choice = value["template"].get<std::string>();

      if (command != "create" || (choice != "blank" && choice != "lab")) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown project command/template");
      }

      auto parent = Core::FileSystem::open(path(value["parent"]));
#ifdef _WIN32
      auto compiler
          = Script::TypeScriptCompiler::create(resources.root() / "typescript-native/tsc.exe");
#else
      auto compiler
          = Script::TypeScriptCompiler::create(resources.root() / "typescript-native/tsc");
#endif

      if (!parent || !compiler) {
        return std::unexpected(!parent ? parent.error() : compiler.error());
      }

      const auto folder = path(value["folder"]);
      const auto name = value["name"].get<std::string>();

      if (choice == "blank") {
        auto staging = Core::StagedDirectory::create(*parent);

        if (!staging) {
          return std::unexpected(staging.error());
        }

        Project::Description description;
        description.name = name;
        auto seed = Project::Project::create(staging->files().value(), "seed", description);

        if (!seed) {
          return std::unexpected(seed.error());
        }

        return Project::instantiateTemplate(seed->files().value(), *parent, folder, name,
                                            *compiler);
      }

      auto source = Core::FileSystem::open(resources.root() / "templates/FeatureLab");
      return source ? Project::instantiateTemplate(*source, *parent, folder, name, *compiler)
                    : Core::Result<Project::Project>{std::unexpected(source.error())};
    } catch (const Json::exception& value) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Project request JSON: " + std::string(value.what()));
    } catch (const std::invalid_argument& value) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Project request: " + std::string(value.what()));
    } catch (const std::exception& value) {
      return Core::fail(Core::ErrorCode::External, "Project action: " + std::string(value.what()));
    }
  }

  struct Home::Impl {
    std::thread::id m_thread{std::this_thread::get_id()};
    Core::FileSystem m_resources;
    Core::Control m_control;
    std::optional<Core::Application> m_application;
    std::optional<Graphics::Renderer> m_renderer;
    Core::Signal<Platform::Event>::Connection m_events;
    std::optional<Project::Project> m_selected;
    std::optional<Core::Error> m_fault;
    std::string m_root;
    std::string m_parent;
    std::string m_folder{"MyGame"};
    std::string m_name{"My Game"};
    std::string m_message;
    int m_template{};
    bool m_active{};
    bool m_createPage{};
    std::shared_ptr<Platform::DialogRequest> m_dialog;
    bool m_dialogProject{};
    Json m_controls = Json::object();

    Impl(Core::FileSystem resources, Core::Control control)
        : m_resources(std::move(resources)), m_control(std::move(control)) {
    }

    auto check() const -> Core::Result<void> {
      if (std::this_thread::get_id() != m_thread || m_active) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Home operations require owner thread without reentry");
      }

      return {};
    }

    auto browse(bool project) -> Core::Result<void> {
      if (m_dialog) {
        return Core::fail(Core::ErrorCode::InvalidState, "A folder dialog is already open");
      }

      const auto& initial = project ? m_root : m_parent;
      auto request = Platform::DialogRequest::open(m_application->window(),
                                                   Platform::DialogKind::Folder, initial);

      if (!request) {
        return std::unexpected(request.error());
      }

      m_dialog = std::move(*request);
      m_dialogProject = project;
      m_message.clear();
      return {};
    }

    auto folderReply() -> void {
      if (!m_dialog) {
        return;
      }

      auto reply = m_dialog->take();

      if (!reply) {
        return;
      }

      m_dialog.reset();

      if (reply->status == Platform::DialogStatus::Failed) {
        m_message = reply->error;
      } else if (reply->status == Platform::DialogStatus::Selected) {
        if (reply->path.empty() || reply->path.size() > 16384 || !Core::isValidUtf8(reply->path)) {
          m_message = "The selected folder path is invalid";
          return;
        }

        if (m_dialogProject) {
          m_root = std::move(reply->path);
          auto opened = execute(Json{{"command", "open"}, {"root", m_root}}.dump());
          m_message = opened ? "Project ready" : opened.error().message;
        } else {
          m_parent = std::move(reply->path);
        }
      }
    }

    auto execute(std::string_view request) -> Core::Result<std::string> {
      try {
        const auto value = parsed(request);
        const auto command = value.at("command").get<std::string>();

        if (command == "inspect") {
          Codec::requireFields(value, {"command"});
          return Json{
              {"ok", true},
              {"templates", catalog()},
              {"running", m_application && m_application->running()},
              {"form",
               {{"root", m_root},
                {"parent", m_parent},
                {"folder", m_folder},
                {"name", m_name},
                {"template", catalog()[static_cast<std::size_t>(m_template)]["id"]},
                {"page", m_createPage ? "create" : "open"}}},
              {"dialog",
               {{"pending", bool(m_dialog)},
                {"target",
                 m_dialog ? Json(m_dialogProject ? "project" : "parent") : Json(nullptr)}}},
              {"controls", m_controls},
              {"message", m_message},
              {"selected", m_selected ? Json(utf(m_selected->files()->root())) : Json(nullptr)}}
              .dump();
        }

        if (!m_application || !m_application->running() || m_selected) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Project home is no longer accepting actions");
        }

        if (command == "close") {
          Codec::requireFields(value, {"command"});

          if (m_dialog) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Cancel or complete the folder dialog before closing");
          }

          m_application->stop();
          return Json{{"ok", true}, {"running", false}}.dump();
        }

        if (command == "browse-folder") {
          Codec::requireFields(value, {"command", "target"});
          const auto target = value["target"].get<std::string>();

          if (target != "project" && target != "parent") {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown folder dialog target");
          }

          auto shown = browse(target == "project");
          return shown ? execute("{\"command\":\"inspect\"}") : std::unexpected(shown.error());
        }

        if (m_dialog) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Complete or cancel the folder dialog first");
        }

        if (command == "stage-form") {
          Codec::requireFields(value, {"command", "value"});
          const auto& form = value["value"];
          Codec::requireFields(form, {"root", "parent", "folder", "name", "template", "page"});

          for (const auto* key : {"root", "parent", "folder", "name"}) {
            const auto text = form[key].get<std::string>();

            if (text.size() > 16384 || !Core::isValidUtf8(text)
                || text.find('\0') != std::string::npos) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid project form text");
            }
          }

          const auto choices = catalog();
          int index = -1;

          for (std::size_t i = 0; i < choices.size(); ++i) {
            if (choices[i]["id"] == form["template"]) {
              index = static_cast<int>(i);
            }
          }

          if (index < 0 || (form["page"] != "open" && form["page"] != "create")) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Unknown project form template/page");
          }

          m_root = form["root"].get<std::string>();
          m_parent = form["parent"].get<std::string>();
          m_folder = form["folder"].get<std::string>();
          m_name = form["name"].get<std::string>();
          m_template = index;
          m_createPage = form["page"] == "create";
          m_message.clear();
          return execute("{\"command\":\"inspect\"}");
        }

        auto selected = performProjectAction(m_resources, request);

        if (!selected) {
          return std::unexpected(selected.error());
        }

        const auto root = utf(selected->files()->root());
        auto response = Json{{"ok", true}, {"root", root}}.dump();
        m_selected.emplace(std::move(*selected));
        m_application->stop();
        return response;
      } catch (const std::exception& value) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Home request: " + std::string(value.what()));
      }
    }

    auto mailbox() -> Core::Result<void> {
      if (m_fault) {
        return std::unexpected(*m_fault);
      }

      auto processed
          = m_control.process([this](std::string_view request) { return execute(request); },
                              [this] { return m_application->running(); });

      if (!processed) {
        auto failure = m_control.failure();

        if (failure && *failure) {
          m_fault = std::move(**failure);
        }

        return std::unexpected(processed.error());
      }

      return {};
    }

    auto control(const char* id) -> void {
      const auto begin = ImGui::GetItemRectMin();
      const auto end = ImGui::GetItemRectMax();
      m_controls[id]
          = {{"origin", {begin.x, begin.y}}, {"size", {end.x - begin.x, end.y - begin.y}}};
    }

    auto draw() -> Core::Result<void> {
      auto& style = ImGui::GetStyle();
      ImGui::StyleColorsDark();
      style.WindowPadding = {24, 24};
      style.FramePadding = {10, 7};
      style.ItemSpacing = {10, 10};
      style.FrameRounding = 4;
      style.ChildRounding = 4;
      style.FrameBorderSize = 1;
      style.Colors[ImGuiCol_WindowBg] = {0.125F, 0.133F, 0.149F, 1};
      style.Colors[ImGuiCol_ChildBg] = style.Colors[ImGuiCol_WindowBg];
      style.Colors[ImGuiCol_Text] = {.902F, .910F, .929F, 1};
      style.Colors[ImGuiCol_TextDisabled] = {.592F, .616F, .659F, 1};
      style.Colors[ImGuiCol_Border] = {.216F, .231F, .263F, 1};
      style.Colors[ImGuiCol_FrameBg] = {.090F, .098F, .114F, 1};
      style.Colors[ImGuiCol_Button] = {.157F, .173F, .200F, 1};
      style.Colors[ImGuiCol_ButtonHovered] = {.208F, .294F, .427F, 1};
      style.Colors[ImGuiCol_Header] = {.208F, .294F, .427F, 1};
      style.Colors[ImGuiCol_CheckMark] = {.424F, .620F, 1, 1};
      m_controls = Json::object();
      const auto size = ImGui::GetIO().DisplaySize;
      ImGui::SetNextWindowPos({0, 0});
      ImGui::SetNextWindowSize(size);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
      ImGui::Begin(
          "DotEngine Projects", nullptr,
          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
      ImGui::PopStyleVar();
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{.094F, .102F, .118F, 1});
      ImGui::BeginChild("Navigation", {184, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
      const auto logo = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddCircleFilled({logo.x + 9, logo.y + 9}, 4,
                                                  IM_COL32(108, 158, 255, 255));
      ImGui::Dummy({18, 18});
      ImGui::SameLine();
      ImGui::TextUnformatted("DotEngine");
      ImGui::Dummy({0, 26});
      ImGui::TextDisabled("PROJECTS");
      ImGui::BeginDisabled(bool(m_dialog));

      if (ImGui::Selectable("Open project", !m_createPage, 0, {0, 32})) {
        m_createPage = false;
        m_message.clear();
      }

      control("openPage");

      if (ImGui::Selectable("Create project", m_createPage, 0, {0, 32})) {
        m_createPage = true;
        m_message.clear();
      }

      control("createPage");
      ImGui::EndDisabled();
      ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), size.y - 72));
      ImGui::TextDisabled("Desktop editor");
      ImGui::TextDisabled("C++ / TypeScript");
      ImGui::EndChild();
      ImGui::PopStyleColor();
      ImGui::SameLine(0, 0);
      ImGui::BeginChild("Project content", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
      ImGui::SetWindowFontScale(1.5F);
      ImGui::TextUnformatted(m_createPage ? "Create a project" : "Projects");
      ImGui::SetWindowFontScale(1);
      ImGui::TextDisabled(m_createPage ? "Choose a starting point and a location."
                                       : "Open a project folder, or start something new.");
      ImGui::Dummy({0, 12});
      ImGui::BeginDisabled(bool(m_dialog));
      const auto primary = [&](const char* label, const char* id) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{.208F, .400F, .710F, 1});
        const bool clicked = ImGui::Button(label, {200, 38});
        control(id);
        ImGui::PopStyleColor();
        return clicked;
      };
      const auto action = [&](const Json& request) {
        auto result = execute(request.dump());
        m_message = result ? "Project ready" : result.error().message;
      };
      const auto browseAction = [&](bool project) {
        auto shown = browse(project);

        if (!shown) {
          m_message = shown.error().message;
        }
      };

      if (!m_createPage) {
        ImGui::TextUnformatted("Open an existing project");
        ImGui::TextDisabled("Select the folder containing project.dotproject.");

        if (primary("Browse for project...", "browseProject")) {
          browseAction(true);
        }

        ImGui::Dummy({0, 12});
        ImGui::TextUnformatted("Project folder");
        ImGui::SetNextItemWidth(std::max(100.F, ImGui::GetContentRegionAvail().x - 100));
        ImGui::InputTextWithHint("##project-path", "Paste a folder path", &m_root);
        control("projectRoot");
        ImGui::SameLine();
        ImGui::BeginDisabled(m_root.empty());

        if (ImGui::Button("Open", {90, 0})) {
          action({{"command", "open"}, {"root", m_root}});
        }

        control("open");
        ImGui::EndDisabled();
        ImGui::Dummy({0, 20});
        ImGui::Separator();
        ImGui::Dummy({0, 12});
        ImGui::TextUnformatted("Start a new project");
        ImGui::TextDisabled("A clean scene, an example game, or Feature Lab.");

        if (ImGui::Button("Choose a template", {200, 36})) {
          m_createPage = true;
        }
      } else {
        if (ImGui::BeginTable("Project identity", 2, ImGuiTableFlags_SizingStretchSame)) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted("Project name");
          ImGui::TableNextColumn();
          ImGui::TextUnformatted("Folder name");
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1);
          ImGui::InputText("##project-name", &m_name);
          control("projectName");
          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1);
          ImGui::InputText("##project-folder", &m_folder);
          control("projectFolder");
          ImGui::EndTable();
        }

        ImGui::TextUnformatted("Location");
        ImGui::SetNextItemWidth(std::max(100.F, ImGui::GetContentRegionAvail().x - 100));
        ImGui::InputTextWithHint("##parent-path", "Choose a parent folder", &m_parent);
        control("parentPath");
        ImGui::SameLine();

        if (ImGui::Button("Browse...", {90, 0})) {
          browseAction(false);
        }

        control("browseParent");
        ImGui::TextUnformatted("Template");
        constexpr const char* names[]{"Empty project", "Feature Lab"};
        constexpr const char* descriptions[]{"Camera, cube and a starter script",
                                             "Engine capabilities and scripting examples"};

        for (int i = 0; i < 2; ++i) {
          ImGui::PushID(i);
          const auto cursor = ImGui::GetCursorPos();

          if (ImGui::Selectable("##template", m_template == i, 0, {0, 48})) {
            m_template = i;
          }

          const auto id = "template" + std::to_string(i);
          control(id.c_str());
          const auto next = ImGui::GetCursorPos();
          ImGui::SetCursorPos({cursor.x + 10, cursor.y + 3});
          ImGui::TextUnformatted(names[i]);
          ImGui::SetCursorPos({cursor.x + 10, cursor.y + 25});
          ImGui::TextDisabled("%s", descriptions[i]);
          ImGui::SetCursorPos(next);
          ImGui::PopID();
        }

        if (!m_parent.empty() && !m_folder.empty()) {
          ImGui::TextDisabled("Create in:");
          ImGui::SameLine();
          ImGui::TextWrapped("%s / %s", m_parent.c_str(), m_folder.c_str());
        }

        ImGui::BeginDisabled(m_parent.empty() || m_folder.empty() || m_name.empty());

        if (primary("Create project", "create")) {
          constexpr const char* ids[]{"blank", "lab"};
          action({{"command", "create"},
                  {"parent", m_parent},
                  {"folder", m_folder},
                  {"name", m_name},
                  {"template", ids[m_template]}});
        }

        ImGui::EndDisabled();
      }

      ImGui::EndDisabled();

      if (m_dialog) {
        ImGui::TextDisabled("Waiting for folder selection...");
      }

      if (!m_message.empty()) {
        ImGui::Spacing();
        ImGui::TextColored({1, .62F, .52F, 1}, "Unable to continue");
        ImGui::TextWrapped("%s", m_message.c_str());
      }

      ImGui::EndChild();
      ImGui::End();
      return {};
    }

    auto callbacks() -> Core::LoopCallbacks {
      Core::LoopCallbacks value;
      value.prepareFrame = [&](const auto&) {
        folderReply();
        return mailbox();
      };
      value.render = [&](const auto& frame) {
        return m_renderer
            ->presentGui([&] { return draw(); },
                         std::min(1.0, std::chrono::duration<double>(frame.elapsed).count()))
            .transform([](auto) {});
      };
      return value;
    }
  };

  Home::Home(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Home::Home(Home&&) noexcept = default;

  auto Home::operator=(Home&&) noexcept -> Home& = default;

  Home::~Home() = default;

  auto Home::open(const HomeConfig& config) -> Core::Result<Home> {
    if (!Platform::isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Home creation requires process main thread");
    }

    if (config.width < 800 || config.height < 480) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Home dimensions must be at least 800x480");
    }

    try {
      auto root = config.resources;

      if (root.empty()) {
        const auto* base = SDL_GetBasePath();

        if (!base) {
          return Core::fail(Core::ErrorCode::Io,
                            "Resolve editor resources: " + std::string(SDL_GetError()));
        }

        root = path(Json(std::string(base))) / "resources";
      }

      auto resources = Core::FileSystem::open(root);

      if (!resources) {
        return std::unexpected(resources.error());
      }

      auto parent = Core::FileSystem::open(config.controlParent.empty() ? Fs::temp_directory_path()
                                                                        : config.controlParent);
      auto control = parent ? Core::Control::create(*parent, R"({"kind":"project-home"})")
                            : Core::Result<Core::Control>{std::unexpected(parent.error())};

      if (!control) {
        return std::unexpected(control.error());
      }

      auto impl = std::make_unique<Impl>(std::move(*resources), std::move(*control));
      Core::ApplicationConfig application;
      application.window = {"DotEngine Projects", config.width, config.height, false, true, true};
      application.pauseOnFocusLoss = false;
      auto created = Core::Application::create(application);

      if (!created) {
        return std::unexpected(created.error());
      }

      impl->m_application.emplace(std::move(*created));

      if (!SDL_SetWindowMinimumSize(
              static_cast<SDL_Window*>(impl->m_application->window().nativeHandle()), 800, 480)) {
        return Core::fail(Core::ErrorCode::External,
                          "Set home minimum size: " + std::string(SDL_GetError()));
      }
      auto text = impl->m_application->window().setTextInput(true);

      if (!text) {
        return std::unexpected(text.error());
      }

      auto renderer = Graphics::Renderer::create(&impl->m_application->window());

      if (!renderer) {
        return std::unexpected(renderer.error());
      }

      impl->m_renderer.emplace(std::move(*renderer));
      Graphics::GuiConfig gui;
      gui.font = impl->m_resources.root() / "EditorSans.ttf";
      auto enabled = impl->m_renderer->enableGui(gui);

      if (!enabled) {
        return std::unexpected(enabled.error());
      }

      impl->m_events
          = impl->m_application->events().subscribe([state = impl.get()](const auto& event) {
              auto handled = state->m_renderer->guiEvent(event);

              if (!handled) {
                state->m_fault = handled.error();
                state->m_application->stop();
              }
            });
      return Home(std::move(impl));
    } catch (const std::exception& value) {
      return Core::fail(Core::ErrorCode::External,
                        "Open project home: " + std::string(value.what()));
    }
  }

  auto Home::dispatch(std::string_view request) -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Home was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_fault) {
      return std::unexpected(*m_impl->m_fault);
    }

    return m_impl->execute(request);
  }

  auto Home::step(std::chrono::nanoseconds elapsed) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Home was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (m_impl->m_fault) {
      return std::unexpected(*m_impl->m_fault);
    }

    if (!m_impl->m_application) {
      return Core::fail(Core::ErrorCode::InvalidState, "Home has no live window");
    }

    ActiveScope active(m_impl->m_active);
    auto result = m_impl->m_application->step(elapsed, m_impl->callbacks());

    if (!result || !m_impl->m_application->running()) {
      auto retired = m_impl->m_control.close();

      if (!retired) {
        return Core::fail(retired.error().code,
                          (result ? std::string{} : result.error().message + "; ")
                              + "Retire home control: " + retired.error().message);
      }
    }

    return result.transform([](auto) {});
  }

  auto Home::run(std::size_t frames) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Home was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (m_impl->m_fault) {
      return std::unexpected(*m_impl->m_fault);
    }

    if (!m_impl->m_application) {
      return Core::fail(Core::ErrorCode::InvalidState, "Home has no live window");
    }

    ActiveScope active(m_impl->m_active);
    auto result = m_impl->m_application->run(m_impl->callbacks(), frames);

    if (!result || !m_impl->m_application->running()) {
      auto retired = m_impl->m_control.close();

      if (!retired) {
        return Core::fail(retired.error().code,
                          (result ? std::string{} : result.error().message + "; ")
                              + "Retire home control: " + retired.error().message);
      }
    }

    return result;
  }

  auto Home::selection() const -> Core::Result<std::optional<Project::Project>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Home was moved");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<std::optional<Project::Project>>{m_impl->m_selected}
                   : std::unexpected(checked.error());
  }

  auto Home::controlDirectory() const -> Core::Result<Fs::path> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Home was moved");
    }

    auto checked = m_impl->check();
    return checked
               ? m_impl->m_control.files().transform([](const auto& files) { return files.root(); })
               : std::unexpected(checked.error());
  }

  auto Home::receiptDirectory() const -> Core::Result<Fs::path> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Home was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_control.receipts().transform(
                         [](const auto& files) { return files.root(); })
                   : std::unexpected(checked.error());
  }

  auto Home::releaseWindow() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Home was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (m_impl->m_dialog) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Complete or cancel the native folder dialog before releasing the window");
    }

    m_impl->m_events = {};
    m_impl->m_renderer.reset();
    m_impl->m_application.reset();
    return {};
  }
}  // namespace Dot::Editor
