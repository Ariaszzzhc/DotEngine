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

#include "../Serialization/Json.hpp"

module Dot.Editor.Home;

import Dot.Core.Application;
import Dot.Core.Events;
import Dot.Core.StagedDirectory;
import Dot.Core.Text;
import Dot.Graphics.Renderer;
import Dot.Platform.System;
import Dot.Platform.Events;
import Dot.Project.Templates;
import Dot.Script.Compiler;

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

    auto error(const Core::Error& value) -> Json {
      constexpr const char* names[]{"InvalidArgument", "NotFound",     "Io",
                                    "Unavailable",     "InvalidState", "External"};
      return {
          {"ok", false},
          {"error", {{"code", names[static_cast<int>(value.code)]}, {"message", value.message}}}};
    }

    auto catalog() -> Json {
      return Json::array({{{"id", "blank"}, {"name", "Camera and cube"}},
                          {{"id", "prism"}, {"name", "Prism Relay game"}},
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

    auto requestId(std::string_view value) -> bool {
      return !value.empty() && value.size() <= 64 && std::ranges::all_of(value, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
               || c == '_' || c == '-';
      });
    }
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

      if (command != "create" || (choice != "blank" && choice != "prism" && choice != "lab")) {
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

      auto source = Core::FileSystem::open(resources.root() / "templates"
                                           / (choice == "prism" ? "PrismRelay" : "FeatureLab"));
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
    Core::StagedDirectory m_control;
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

    Impl(Core::FileSystem resources, Core::StagedDirectory control)
        : m_resources(std::move(resources)), m_control(std::move(control)) {
    }

    auto check() const -> Core::Result<void> {
      if (std::this_thread::get_id() != m_thread || m_active) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Home operations require owner thread without reentry");
      }

      return {};
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
              {"selected", m_selected ? Json(utf(m_selected->files()->root())) : Json(nullptr)}}
              .dump();
        }

        if (!m_application || !m_application->running() || m_selected) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Project home is no longer accepting actions");
        }

        if (command == "close") {
          Codec::requireFields(value, {"command"});
          m_application->stop();
          return Json{{"ok", true}, {"running", false}}.dump();
        }

        auto selected = performProjectAction(m_resources, request);

        if (!selected) {
          return std::unexpected(selected.error());
        }

        const auto root = utf(selected->files()->root());
        auto response = Json{{"ok", true}, {"root", root}}.dump();
        m_selected.emplace(std::move(*selected));
        m_application->stop();
        auto stored = m_control.files()->writeTextAtomic(
            "session.json",
            Json{{"protocol", 1}, {"state", "selected"}, {"kind", "project-home"}, {"root", root}}
                .dump());

        if (!stored) {
          return Core::fail(stored.error().code,
                            "Project is ready at " + root
                                + "; home metadata write failed: " + stored.error().message);
        }

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

      auto files = m_control.files().value();
      std::vector<Fs::path> pending;
      std::size_t entries = 0;

      for (const auto& entry : Fs::directory_iterator(files.root() / "requests")) {
        if (++entries > 4096 || entry.path().extension() != ".json"
            || !requestId(utf(entry.path().stem()))
            || !Fs::is_regular_file(entry.symlink_status())) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid/excessive home mailbox entries");
        }

        const auto responsePath = files.root() / "responses" / entry.path().filename();

        if (Fs::exists(responsePath)) {
          if (!Fs::is_regular_file(Fs::symlink_status(responsePath))) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Home response must be a regular file");
          }
        } else {
          pending.push_back(entry.path().filename());
        }

        if (pending.size() > 256) {
          return Core::fail(Core::ErrorCode::Unavailable, "Home pending request limit exceeded");
        }
      }

      std::ranges::sort(pending);

      for (std::size_t index = 0; index < std::min<std::size_t>(16, pending.size()); ++index) {
        auto source = files.readText(Fs::path{"requests"} / pending[index], 1024 * 1024);
        m_fault = Core::Error{
            Core::ErrorCode::External, "Home action may have committed; response incomplete", {}};
        auto response = source ? execute(*source)
                               : Core::Result<std::string>{std::unexpected(source.error())};
        auto written = files.writeTextAtomic(
            Fs::path{"responses"} / pending[index],
            Json{{"source", source ? Json(*source) : Json(nullptr)},
                 {"response", response ? Codec::parseJson(*response) : error(response.error())}}
                .dump());

        if (!written) {
          m_fault = Core::Error{
              written.error().code,
              "Home action may have committed; mailbox is terminal: " + written.error().message,
              {}};
          return std::unexpected(*m_fault);
        }

        m_fault.reset();

        if (!m_application->running()) {
          break;
        }
      }

      return {};
    }

    auto draw() -> Core::Result<void> {
      auto& io = ImGui::GetIO();
      ImGui::SetNextWindowPos({0, 0});
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::Begin("DotEngine Projects", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
      ImGui::TextUnformatted("DotEngine");
      ImGui::TextWrapped("Create a project or open an existing project folder.");
      ImGui::Separator();
      ImGui::InputText("Project folder", &m_root);
      const auto action = [&](const Json& request) {
        auto result = execute(request.dump());
        m_message = result ? "Project ready" : result.error().message;
      };

      if (ImGui::Button("Open project")) {
        action({{"command", "open"}, {"root", m_root}});
      }

      ImGui::Separator();
      ImGui::InputText("Existing parent directory", &m_parent);
      ImGui::InputText("New folder", &m_folder);
      ImGui::InputText("Project name", &m_name);
      constexpr const char* choices[]{"Camera and cube", "Prism Relay game",
                                      "Component and SDK lab"};
      ImGui::Combo("Template", &m_template, choices, 3);

      if (ImGui::Button("Create project")) {
        constexpr const char* ids[]{"blank", "prism", "lab"};
        action({{"command", "create"},
                {"parent", m_parent},
                {"folder", m_folder},
                {"name", m_name},
                {"template", ids[m_template]}});
      }

      if (!m_message.empty()) {
        ImGui::TextWrapped("%s", m_message.c_str());
      }

      ImGui::End();
      return {};
    }

    auto callbacks() -> Core::LoopCallbacks {
      Core::LoopCallbacks value;
      value.prepareFrame = [&](const auto&) { return mailbox(); };
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

      auto temporary = Core::FileSystem::open(Fs::temp_directory_path()).value();
      auto control = Core::StagedDirectory::create(temporary);

      if (!control) {
        return std::unexpected(control.error());
      }

      auto impl = std::make_unique<Impl>(std::move(*resources), std::move(*control));
      auto files = impl->m_control.files().value();
      auto requests = files.createDirectories("requests");
      auto responses = files.createDirectories("responses");
      auto metadata = files.writeTextAtomic(
          "session.json",
          Json{{"protocol", 1}, {"state", "ready"}, {"kind", "project-home"}}.dump());

      if (!requests || !responses || !metadata) {
        return std::unexpected(!requests    ? requests.error()
                               : !responses ? responses.error()
                                            : metadata.error());
      }

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

  auto Home::releaseWindow() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Home was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    m_impl->m_events = {};
    m_impl->m_renderer.reset();
    m_impl->m_application.reset();
    return {};
  }
}  // namespace Dot::Editor
