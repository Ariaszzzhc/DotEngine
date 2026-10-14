module;

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Editor.Workspace;

import Dot.Core.Application;
import Dot.Core.Events;
import Dot.Script.SceneScripts;
import Dot.Editor.Play;
import Dot.Editor.Resources;
import Dot.Editor.ScriptDiagnostics;
import Dot.Platform.ExternalFiles;
import Dot.Script.Compiler;
import Dot.Script.LiveScripts;
import Dot.Platform.Input;
import Dot.Core.Files;
import Dot.Core.Paths;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Core.Text;
import Dot.Editor.Document;
import Dot.Editor.Camera;
import Dot.Project;
import Dot.Project.GamePackage;
import Dot.Project.GameExport;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.Platform.Events;
import Dot.Platform.System;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace Dot::Editor {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;

    struct ActiveScope {
      bool& value;
      bool previous;

      explicit ActiveScope(bool& flag) noexcept : value(flag), previous(flag) {
        value = true;
      }

      ~ActiveScope() {
        value = previous;
      }
    };

    auto errorJson(const Core::Error& error) -> Json {
      constexpr std::array names{"InvalidArgument", "NotFound",     "Io",
                                 "Unavailable",     "InvalidState", "External"};
      return {
          {"ok", false},
          {"error",
           {{"code", names.at(static_cast<std::size_t>(error.code))}, {"message", error.message}}}};
    }

    auto validId(std::string_view id) -> bool {
      return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
               || (value >= '0' && value <= '9') || value == '_' || value == '-';
      });
    }

    auto validRequest(std::string_view request) -> Core::Result<void> {
      if (request.empty() || request.size() > 1024 * 1024
          || request.find('\0') != std::string_view::npos || !Core::isValidUtf8(request)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Workspace request must be UTF-8 without NUL, within 1 MiB");
      }

      try {
        auto parsed = Codec::parseJson(request);

        if (!parsed.is_object()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Workspace request must be an object");
        }

        return {};
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Workspace request JSON: " + std::string(error.what()));
      }
    }

    auto label(std::string_view field) -> std::string {
      std::string result;

      for (char value : field) {
        if (value >= 'A' && value <= 'Z' && !result.empty()) {
          result += ' ';
        }

        result += value;
      }

      if (!result.empty() && result.front() >= 'a' && result.front() <= 'z') {
        result.front() = static_cast<char>(result.front() - 'a' + 'A');
      }

      return result;
    }

    auto fields(Json& object, int depth = 0) -> void {
      if (depth > 32) {
        ImGui::TextUnformatted("Properties exceed inspector depth");
        return;
      }

      for (auto& item : object.items()) {
        ImGui::PushID(item.key().c_str());
        auto title = label(item.key());
        auto& value = item.value();

        if (value.is_boolean()) {
          bool current = value.get<bool>();

          if (ImGui::Checkbox(title.c_str(), &current)) {
            value = current;
          }
        } else if (value.is_number_integer()) {
          std::int64_t current = value.get<std::int64_t>();

          if (ImGui::InputScalar(title.c_str(), ImGuiDataType_S64, &current)) {
            value = current;
          }
        } else if (value.is_number()) {
          double current = value.get<double>();

          if (ImGui::InputDouble(title.c_str(), &current, 0, 0, "%.6g")) {
            value = current;
          }
        } else if (value.is_string()) {
          std::string current = value.get<std::string>();
          const char* options = nullptr;

          if (item.key() == "projection") {
            options = "perspective\0orthographic\0";
          } else if (item.key() == "kind") {
            options = object.contains("outerConeDegrees") ? "directional\0point\0spot\0"
                                                          : "cube\0quad\0asset\0";
          } else if (item.key() == "motion") {
            options = "static\0kinematic\0dynamic\0";
          } else if (item.key() == "shape") {
            options = "box\0sphere\0capsule\0";
          } else if (item.key() == "attenuation") {
            options = "inverse\0linear\0exponential\0";
          }

          if (options) {
            std::vector<const char*> values;

            for (auto* option = options; *option;
                 option += std::char_traits<char>::length(option) + 1) {
              values.push_back(option);
            }

            auto found = std::ranges::find(values, current,
                                           [](auto* option) { return std::string_view(option); });
            int selected = found == values.end() ? -1 : static_cast<int>(found - values.begin());

            if (ImGui::Combo(title.c_str(), &selected, values.data(),
                             static_cast<int>(values.size()))) {
              value = values.at(static_cast<std::size_t>(selected));
            }
          } else if (ImGui::InputText(title.c_str(), &current)) {
            value = current;
          }
        } else if (value.is_array() && !value.empty() && value.size() <= 4
                   && std::ranges::all_of(
                       value, [](const auto& element) { return element.is_number(); })) {
          std::array<float, 4> numbers{};

          for (std::size_t index = 0; index < value.size(); ++index) {
            numbers[index] = value[index].get<float>();
          }

          if (ImGui::InputScalarN(title.c_str(), ImGuiDataType_Float, numbers.data(),
                                  static_cast<int>(value.size()))) {
            for (std::size_t index = 0; index < value.size(); ++index) {
              value[index] = numbers[index];
            }
          }
        } else if (value.is_object()) {
          if (ImGui::TreeNode(title.c_str())) {
            fields(value, depth + 1);
            ImGui::TreePop();
          }
        }

        ImGui::PopID();
      }
    }

    auto presets() -> Core::Result<Json> {
      auto scene = World::Scene::create("Components");

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto entity = scene->createEntity();

      if (!entity) {
        return std::unexpected(entity.error());
      }

      World::AudioSource audio;
      audio.asset = "assets/sound.wav";
      const auto id = *entity;
      const std::array results{
          scene->setCamera(id, World::Camera{}),
          scene->setMeshRenderer(id, World::MeshRenderer{}),
          scene->setMaterial(id, World::MaterialBinding{"assets/material.dotmaterial"}),
          scene->setLight(id, World::Light{}),
          scene->setRigidBody(id, World::RigidBody{}),
          scene->setAudioSource(id, audio),
          scene->setAudioListener(id, World::AudioListener{}),
          scene->setScript(id, World::ScriptComponent{"behavior.ts", "Behavior"})};

      for (const auto& result : results) {
        if (!result) {
          return std::unexpected(result.error());
        }
      }

      auto encoded = World::encodeScene(*scene);
      return encoded ? Core::Result<Json>{Codec::parseJson(*encoded)["entities"][0]}
                     : std::unexpected(encoded.error());
    }
  }  // namespace

  struct Workspace::Impl {
    std::thread::id m_thread{std::this_thread::get_id()};
    std::optional<Document> m_document;
    std::optional<Project::Project> m_project;
    WorkspaceConfig m_config;
    std::optional<ResourceStore> m_resources;
    std::optional<TextDocument> m_text;
    std::uint64_t m_buffer{};
    Json m_catalog = Json::array();
    std::string m_selectedResource;
    std::vector<ScriptDiagnostic> m_scriptDiagnostics;
    std::string m_newPath;
    std::string m_importSource;
    std::string m_environmentRevision;
    Json m_environmentDraft;
    Json m_environmentOriginal;
    std::string m_occlusionRevision;
    Json m_occlusionDraft;
    Json m_occlusionOriginal;
    std::string m_requestedPane;
    std::string m_pane{"scene"};
    bool m_assetRefresh{};
    std::string m_checkMessage;
    std::string m_exportParent;
    std::string m_exportFolder{"game-export"};
    bool m_exportDialog{};
    bool m_exportDataOnly{};
    std::optional<PlaySession> m_play;
    Platform::InputState m_gameInput;
    bool m_captureInput{};
    bool m_resetGuiInput{};
    ImVec2 m_viewOrigin{};
    Json m_playDiagnostics = Json::array();
    std::optional<Core::Error> m_playError;
    std::optional<Core::Error> m_closeError;
    std::optional<Core::Application> m_application;
    std::optional<Graphics::Renderer> m_renderer;
    std::optional<Graphics::SceneRenderer> m_drawing;
    std::optional<Graphics::RenderTarget> m_target;
    std::optional<Core::StagedDirectory> m_control;
    Json m_snapshot;
    Json m_presets;
    Json m_edit;
    Json m_original;
    std::optional<std::uint64_t> m_editEntity;
    std::uint64_t m_editRevision{};
    std::string m_message;
    std::string m_propertiesText;
    bool m_propertiesInvalid{};
    std::string m_previewError;
    bool m_pendingClose{};
    bool m_previewReady{};
    bool m_active{};
    bool m_applying{};
    std::optional<Core::Error> m_mailboxFault;
    std::optional<Core::Error> m_actionError;
    Camera m_editorCamera;
    bool m_editorView{true};
    bool m_flying{};
    bool m_orbiting{};
    bool m_panning{};
    bool m_viewBlocked{};
    std::optional<Graphics::SceneView> m_renderedView;
    std::string m_renderedRevision;
    float m_viewWidth{640};
    float m_viewHeight{480};
    std::optional<Core::Error> m_inputError;
    Core::Signal<Platform::Event>::Connection m_events;

    ~Impl() {
      ActiveScope active{m_active};

      try {
        if (playing()) {
          auto stopped = m_play->stop();

          if (!stopped) {
            std::fprintf(stderr, "Workspace play cleanup: %s\n", stopped.error().message.c_str());
          } else {
            for (const auto& failure : stopped->failures) {
              std::fprintf(stderr, "Workspace stop hook: %s\n", failure.error.message.c_str());
            }
          }
        }
      } catch (const std::exception& error) {
        std::fprintf(stderr, "Workspace play cleanup: %s\n", error.what());
      } catch (...) {
        std::fprintf(stderr, "Workspace play cleanup: unknown exception\n");
      }
    }

    auto finish(Core::Result<void> result) -> Core::Result<void> {
      if (!m_application->running() && playing()) {
        auto stopped = stopPlay();

        if (!stopped) {
          return Core::fail(stopped.error().code,
                            (result ? std::string{} : result.error().message + "; ")
                                + "Play cleanup: " + stopped.error().message);
        }
      }

      if (!m_application->running() && m_closeError) {
        return std::unexpected(*m_closeError);
      }

      return result;
    }

    auto playing() const -> bool {
      return m_play && m_play->stats()->state != PlayState::Stopped;
    }

    auto resource(std::string_view name) const -> Core::Result<std::filesystem::path> {
      const auto* base = SDL_GetBasePath();

      if (!base) {
        return Core::fail(Core::ErrorCode::Io,
                          "Resolve editor resources: " + std::string(SDL_GetError()));
      }

      const std::string path = std::string(base) + "resources/" + std::string(name);
      return std::filesystem::path(std::u8string(path.begin(), path.end()));
    }

    auto diagnostic(Json value) -> void {
      if (!m_playDiagnostics.empty() && m_playDiagnostics.back() == value) {
        return;
      }

      if (m_playDiagnostics.size() == 64) {
        m_playDiagnostics.erase(m_playDiagnostics.begin());
      }

      m_playDiagnostics.push_back(std::move(value));
    }

    auto report(const Script::ScriptReport& report) -> void {
      constexpr std::array phases{"prepare", "start", "update", "fixedUpdate", "stop"};

      for (const auto& failure : report.failures) {
        auto value = errorJson(failure.error);
        value["phase"] = phases.at(static_cast<std::size_t>(failure.phase));
        value["entity"] = std::to_string(failure.entity.value);
        diagnostic(std::move(value));
      }
    }

    auto remember() -> Core::Result<void> {
      auto frame = m_play->lastFrame();

      if (!frame) {
        return std::unexpected(frame.error());
      }

      for (const auto& item : frame->reports) {
        if (item.source.error) {
          auto value = errorJson(*item.source.error);
          value["phase"] = "reload";
          diagnostic(std::move(value));
        }

        if (item.source.reload) {
          report(item.source.reload->retired);
        }

        report(item.frame);
      }

      return {};
    }

    auto captureInput(bool capture) -> void {
      if (m_captureInput != capture) {
        m_captureInput = capture;
        m_resetGuiInput = true;
      }
    }

    auto compiler() const -> Core::Result<Script::TypeScriptCompiler> {
      auto path = m_config.compiler.empty()
#if defined(_WIN32)
                      ? resource("typescript-native/tsc.exe")
#else
                      ? resource("typescript-native/tsc")
#endif
                      : Core::Result<std::filesystem::path>{m_config.compiler};

      return path ? Script::TypeScriptCompiler::create(*path) : std::unexpected(path.error());
    }

    auto pendingText() const -> bool {
      if (!m_text) {
        return false;
      }

      const auto file = m_text->snapshot().value();
      return file.dirty;
    }

    auto textStatus() const -> Json {
      if (!m_text) {
        return nullptr;
      }

      const auto file = m_text->snapshot().value();
      return {{"buffer", std::to_string(m_buffer)},
              {"path", file.path},
              {"text", file.text},
              {"revision", std::to_string(file.revision)},
              {"dirty", pendingText()},
              {"readOnly", file.readOnly}};
    }

    auto catalog() -> Core::Result<void> {
      auto entries = m_resources->list();

      if (!entries) {
        return std::unexpected(entries.error());
      }

      Json candidate = Json::array();

      for (const auto& entry : *entries) {
        candidate.push_back({{"path", entry.path},
                             {"bytes", std::to_string(entry.bytes)},
                             {"type", entry.directory ? "directory"
                                      : entry.text    ? "text"
                                                      : "binary"},
                             {"readOnly", entry.readOnly}});
      }

      m_catalog.swap(candidate);
      return {};
    }

    auto saveText() -> Core::Result<void> {
      if (!m_text) {
        return Core::fail(Core::ErrorCode::InvalidState, "No resource text buffer");
      }

      auto file = m_text->snapshot().value();

      auto saved = m_text->save(file.revision);

      if (!saved) {
        return saved;
      }

      m_assetRefresh = true;
      m_checkMessage.clear();
      m_scriptDiagnostics.clear();
      auto refreshed = catalog();
      return refreshed
                 ? Core::Result<void>{}
                 : Core::fail(refreshed.error().code, "Resource saved; catalog refresh failed: "
                                                          + refreshed.error().message);
    }

    auto textRequest(const Json& request) -> Core::Result<void> {
      if (!m_text || Codec::id(request.at("buffer")) != m_buffer
          || Codec::id(request.at("revision")) != m_text->snapshot().value().revision) {
        return Core::fail(Core::ErrorCode::InvalidState, "Resource buffer/revision is stale");
      }

      return {};
    }

    auto startPlay() -> Core::Result<void> {
      if (playing() || pendingInspector() || pendingEnvironment() || pendingOcclusion()
          || pendingText() || !m_application->running()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Play requires no pending scene/file edits or active game");
      }

      auto tool = compiler();

      if (!tool) {
        return std::unexpected(tool.error());
      }

      auto created
          = PlaySession::create(*m_project, *tool, *m_document->scene().value(), m_config.play);

      if (!created) {
        return std::unexpected(created.error());
      }

      m_play.emplace(std::move(*created));
      m_playDiagnostics = Json::array();
      m_playError.reset();
      m_gameInput = Platform::InputState{};
      captureInput(false);
      m_previewReady = false;
      auto remembered = remember();
      return remembered ? Core::Result<void>{}
                        : Core::fail(remembered.error().code, "Play started; report read failed: "
                                                                  + remembered.error().message);
    }

    auto stopPlay() -> Core::Result<void> {
      if (!playing()) {
        return {};
      }

      auto stopped = m_play->stop();
      captureInput(false);
      m_previewReady = false;

      if (!stopped) {
        m_playError = stopped.error();
        diagnostic(errorJson(stopped.error()));
        return std::unexpected(stopped.error());
      }

      report(*stopped);
      return {};
    }

    auto playStatus() const -> Json {
      Json status{{"state", "idle"},
                  {"captureInput", m_captureInput},
                  {"diagnostics", m_playDiagnostics},
                  {"error", m_playError ? errorJson(*m_playError)["error"] : Json(nullptr)}};

      if (m_play) {
        const auto stats = m_play->stats().value();
        constexpr std::array states{"running", "faulted", "stopped"};
        status["state"] = states.at(static_cast<std::size_t>(stats.state));
        status["fixedHz"] = stats.fixedHz;
        status["frames"] = std::to_string(stats.frames);
        status["fixedSteps"] = std::to_string(stats.fixedSteps);
        status["droppedNanoseconds"] = std::to_string(stats.dropped.count());
      }

      return status;
    }

    auto pendingEnvironment() const -> bool {
      return m_environmentDraft != m_environmentOriginal;
    }

    auto applyEnvironment() -> Core::Result<void> {
      if (!pendingEnvironment()) {
        return {};
      }

      if (pendingInspector() || pendingOcclusion()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Apply or reset inspector/AO changes before environment changes");
      }

      if (m_environmentRevision != std::to_string(m_document->stats()->revision)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Environment draft has a stale revision");
      }

      auto applied = m_document->apply(
          Json::array({{{"op", "environment"}, {"value", m_environmentDraft}}}).dump(),
          m_document->stats()->revision);

      if (!applied) {
        return std::unexpected(applied.error());
      }

      m_environmentOriginal = m_environmentDraft;
      m_environmentRevision.clear();
      return refresh();
    }

    auto pendingOcclusion() const -> bool {
      return m_occlusionDraft != m_occlusionOriginal;
    }

    auto applyOcclusion() -> Core::Result<void> {
      if (!pendingOcclusion()) {
        return {};
      }

      if (pendingInspector() || pendingEnvironment()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Apply or reset inspector/environment changes before AO changes");
      }

      if (m_occlusionRevision != std::to_string(m_document->stats()->revision)) {
        return Core::fail(Core::ErrorCode::InvalidState, "AO draft has a stale revision");
      }

      auto applied = m_document->apply(
          Json::array({{{"op", "occlusion"}, {"value", m_occlusionDraft}}}).dump(),
          m_document->stats()->revision);

      if (!applied) {
        return std::unexpected(applied.error());
      }

      m_occlusionOriginal = m_occlusionDraft;
      m_occlusionRevision.clear();
      return refresh();
    }

    auto pendingInspector() const -> bool {
      return m_edit != m_original || m_propertiesInvalid;
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || !Platform::isMainThread() || m_active) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Workspace requires its owner/main thread without reentry");
      }

      return {};
    }

    auto refresh() -> Core::Result<void> {
      auto text = m_document->snapshot();

      if (!text) {
        return std::unexpected(text.error());
      }

      m_snapshot = Codec::parseJson(*text);
      return {};
    }

    auto execute(std::string_view request) -> Core::Result<std::string> {
      auto valid = validRequest(request);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      auto parsed = Codec::parseJson(request);

      const auto command = parsed.value("command", std::string{});

      if (command == "export-data" || command == "export") {
        Codec::requireFields(parsed, {"command", "revision", "parent", "folder"});

        if (Codec::id(parsed["revision"]) != m_document->stats().value().revision || playing()
            || pendingInspector() || pendingEnvironment() || pendingOcclusion() || pendingText()) {
          return Core::fail(
              Core::ErrorCode::InvalidState,
              "Export requires current revision, stopped Play and no pending scene/file edits");
        }

        const auto parentText = parsed.at("parent").get<std::string>();
        const auto folderText = parsed.at("folder").get<std::string>();

        if (!Core::isValidUtf8(parentText) || parentText.find('\0') != std::string::npos) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Export parent must be a UTF-8 path without NUL");
        }

        auto parent = Core::FileSystem::open(
            std::filesystem::path(std::u8string(parentText.begin(), parentText.end())));
        auto tool = compiler();

        if (!parent || !tool) {
          return std::unexpected(parent ? tool.error() : parent.error());
        }

        const auto folder
            = std::filesystem::path(std::u8string(folderText.begin(), folderText.end()));
        auto output = [&]() -> Core::Result<Core::FileSystem> {
          if (command == "export-data") {
            return Project::GamePackage::publish(*m_project, *m_document->scene().value(), *tool,
                                                 *parent, folder);
          }

          auto location = m_config.runtimeKit.empty()
                              ? resource("runtime-kit")
                              : Core::Result<std::filesystem::path>{m_config.runtimeKit};

          if (!location) {
            return std::unexpected(location.error());
          }

          auto files = Core::FileSystem::open(*location);
          auto kit = files ? Project::RuntimeKit::open(*files)
                           : Core::Result<Project::RuntimeKit>{std::unexpected(files.error())};
          return kit ? Project::exportGame(*m_project, *m_document->scene().value(), *tool, *kit,
                                           *parent, folder)
                     : std::unexpected(kit.error());
        }();

        if (!output) {
          return std::unexpected(output.error());
        }

        const auto utf = output->root().generic_u8string();
        return Json{{"ok", true},
                    {"kind", command == "export" ? "game-native" : "game-data"},
                    {"package", std::string(utf.begin(), utf.end())}}
            .dump();
      }

      if (command == "resources") {
        Codec::requireFields(parsed, {"command"});
        auto refreshed = catalog();

        if (!refreshed) {
          return std::unexpected(refreshed.error());
        }

        Json diagnostics = Json::array();

        for (const auto& item : m_scriptDiagnostics) {
          diagnostics.push_back({{"path", item.path},
                                 {"line", item.line},
                                 {"column", item.column},
                                 {"message", item.message}});
        }

        return Json{{"ok", true},
                    {"entries", m_catalog},
                    {"file", textStatus()},
                    {"selected", m_selectedResource},
                    {"scripts", {{"message", m_checkMessage}, {"diagnostics", diagnostics}}}}
            .dump();
      }

      if (command == "open-external") {
        Codec::requireFields(parsed, {"command", "path"});

        if (pendingText()) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Save or discard the agent resource draft before external opening");
        }

        const auto name = parsed.at("path").get<std::string>();
        auto target = m_resources->externalTextPath(name);

        if (!target) {
          return std::unexpected(target.error());
        }

        auto uri = Platform::fileUri(*target);

        if (!uri) {
          return std::unexpected(uri.error());
        }

        auto launched = m_config.openExternal ? m_config.openExternal(*target)
                                              : Platform::openExternalFile(*target);

        if (!launched) {
          return std::unexpected(launched.error());
        }

        m_selectedResource = name;
        captureInput(false);
        return Json{{"ok", true},
                    {"kind", "external-file"},
                    {"path", name},
                    {"uri", *uri},
                    {"accepted", true}}
            .dump();
      }

      if (command == "create-directory" || command == "create-text" || command == "import-resource"
          || command == "create-script") {
        if (command == "create-directory") {
          Codec::requireFields(parsed, {"command", "path"});
        } else if (command == "create-script") {
          Codec::requireFields(parsed, {"command", "path"});
        } else if (command == "create-text") {
          Codec::requireFields(parsed, {"command", "path", "text"});
        } else {
          Codec::requireFields(parsed, {"command", "path", "source"});
        }

        const auto name = parsed.at("path").get<std::string>();
        Core::Result<void> result;

        if (command == "create-directory") {
          result = m_resources->createDirectory(name);
        } else if (command == "create-script") {
          const auto root = m_project->description().value().scriptsDirectory;
          const auto target = std::filesystem::path(std::u8string(name.begin(), name.end()));

          if (!Core::isPortableRelativePath(name) || !name.starts_with(root + "/")
              || (target.extension() != ".ts" && target.extension() != ".mts")
              || name.ends_with(".d.ts") || name.ends_with(".d.mts")) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "New script must be a portable .ts/.mts path inside the configured "
                              "scripts directory");
          }

          result = m_resources->createText(
              name,
              "import {ScriptComponent} from 'dot:engine';\n\nexport class Behavior extends "
              "ScriptComponent {\n  onUpdate(seconds: number): void {\n  }\n}\n");
        } else if (command == "create-text") {
          result = m_resources->createText(name, parsed.at("text").get<std::string>());
        } else {
          const auto source = parsed.at("source").get<std::string>();
          result = m_resources->importFile(
              std::filesystem::path(std::u8string(source.begin(), source.end())), name);
        }

        if (!result) {
          return std::unexpected(result.error());
        }

        m_assetRefresh = true;
        auto refreshed = catalog();

        if (!refreshed) {
          return Core::fail(
              refreshed.error().code,
              "Resource creation committed; catalog refresh failed: " + refreshed.error().message);
        }

        return Json{{"ok", true}, {"entries", m_catalog}, {"file", textStatus()}}.dump();
      }

      if (command == "open-text") {
        Codec::requireFields(parsed, {"command", "path", "discard"});
        const bool discard = parsed.at("discard").get<bool>();
        const auto name = parsed.at("path").get<std::string>();

        if (pendingText() && !discard) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Save or discard the current resource draft before opening another");
        }

        if (m_buffer == std::numeric_limits<std::uint64_t>::max() - 1) {
          return Core::fail(Core::ErrorCode::InvalidState, "Resource buffer identities exhausted");
        }

        auto opened = m_resources->openText(name);

        if (!opened) {
          return std::unexpected(opened.error());
        }

        m_text.emplace(std::move(*opened));
        m_checkMessage.clear();
        m_scriptDiagnostics.clear();
        ++m_buffer;
        return Json{{"ok", true}, {"file", textStatus()}}.dump();
      }

      if (command == "replace-text" || command == "save-text" || command == "close-text") {
        if (command == "replace-text") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "text"});
        } else if (command == "close-text") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "discard"});
        } else {
          Codec::requireFields(parsed, {"command", "buffer", "revision"});
        }

        auto current = textRequest(parsed);

        if (!current) {
          return std::unexpected(current.error());
        }

        if (command == "replace-text") {
          auto text = parsed.at("text").get<std::string>();
          auto replaced = m_text->replace(text, m_text->snapshot().value().revision);

          if (!replaced) {
            return std::unexpected(replaced.error());
          }

          m_checkMessage.clear();
          m_scriptDiagnostics.clear();
        } else if (command == "save-text") {
          auto saved = saveText();

          if (!saved) {
            return std::unexpected(saved.error());
          }
        } else {
          const bool discard = parsed.at("discard").get<bool>();

          if (pendingText() && !discard) {
            return Core::fail(Core::ErrorCode::InvalidState, "Resource buffer has unsaved changes");
          }

          m_text.reset();
        }

        return Json{{"ok", true}, {"file", textStatus()}}.dump();
      }

      if (command == "check-scripts") {
        Codec::requireFields(parsed, {"command"});

        if (pendingText()) {
          return Core::fail(
              Core::ErrorCode::InvalidState,
              "Save or discard the current resource draft before checking on-disk scripts");
        }

        m_checkMessage.clear();
        m_scriptDiagnostics.clear();
        auto tool = compiler();

        if (!tool) {
          m_checkMessage = tool.error().message;
          return std::unexpected(tool.error());
        }

        auto compiled = tool->compileProject(m_project->files().value(),
                                             m_project->description().value().scriptsDirectory);

        if (!compiled) {
          m_checkMessage = compiled.error().message;
          auto diagnostics = scriptDiagnostics(m_checkMessage,
                                               m_project->description().value().scriptsDirectory);
          m_scriptDiagnostics.clear();

          if (diagnostics) {
            m_scriptDiagnostics = std::move(*diagnostics);
          } else {
            m_checkMessage += "\nDiagnostic mapping: " + diagnostics.error().message;
          }

          return std::unexpected(compiled.error());
        }

        m_checkMessage = "TypeScript checked successfully";
        m_scriptDiagnostics.clear();
        return Json{{"ok", true}, {"sourceFiles", compiled->stats().value().sourceFiles}}.dump();
      }

      if (command == "save-close" || command == "cancel-close") {
        Codec::requireFields(parsed, {"command", "revision"});

        if (Codec::id(parsed["revision"]) != m_document->stats()->revision) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Close request has a stale document revision");
        }

        if (command == "cancel-close") {
          m_pendingClose = false;
          return execute("{\"command\":\"inspect\"}");
        }

        auto stopped = stopPlay();

        if (!stopped) {
          return std::unexpected(stopped.error());
        }

        auto applied = applyEnvironment();

        if (!applied) {
          return std::unexpected(applied.error());
        }

        applied = applyOcclusion();

        if (!applied) {
          return std::unexpected(applied.error());
        }

        applied = applyInspector();

        if (!applied) {
          return std::unexpected(applied.error());
        }

        if (pendingText()) {
          auto savedText = saveText();

          if (!savedText) {
            return std::unexpected(savedText.error());
          }
        }

        auto saved = execute(
            Json{{"command", "save"}, {"revision", std::to_string(m_document->stats()->revision)}}
                .dump());

        if (!saved) {
          return saved;
        }

        return execute("{\"command\":\"close\",\"discard\":false}");
      }

      if (command == "play" || command == "stop-play") {
        Codec::requireFields(parsed, {"command", "revision"});

        if (Codec::id(parsed["revision"]) != m_document->stats()->revision) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Play request has a stale document revision");
        }

        auto result = command == "play" ? startPlay() : stopPlay();

        if (!result) {
          return std::unexpected(result.error());
        }

        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "capture-input") {
        Codec::requireFields(parsed, {"command", "capture"});
        const bool capture = parsed["capture"].get<bool>();

        if (!playing() || m_play->stats()->state != PlayState::Running) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Game input capture requires running Play");
        }

        captureInput(capture);

        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "inspect-play") {
        Codec::requireFields(parsed, {"command"});

        if (!m_play) {
          return Core::fail(Core::ErrorCode::InvalidState, "No game preview session");
        }

        auto encoded = World::encodeScene(*m_play->scene().value());
        return encoded ? Core::Result<std::string>{Json{{"ok", true},
                                                        {"play", playStatus()},
                                                        {"view", viewStatus()},
                                                        {"scene", Codec::parseJson(*encoded)}}
                                                       .dump()}
                       : std::unexpected(encoded.error());
      }

      if (playing() && command != "inspect" && command != "close" && command != "show-pane") {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Stop Play before changing the authoring document");
      }

      if (command == "set-view") {
        Codec::requireFields(parsed, {"command", "mode"}, {"camera"});
        const auto mode = parsed["mode"].get<std::string>();

        if (mode != "editor" && mode != "game") {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown viewport mode");
        }

        if (parsed.contains("camera")) {
          if (mode != "editor") {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Game camera edits use scene transactions");
          }

          const auto& value = parsed["camera"];
          Codec::requireFields(value, {"position", "pivot", "yaw", "pitch", "speed", "orthographic",
                                       "orthographicHeight", "nearPlane", "farPlane"});
          const auto position = Codec::numbers<3>(value["position"]);
          const auto pivot = Codec::numbers<3>(value["pivot"]);
          auto applied = m_editorCamera.setState({{position[0], position[1], position[2]},
                                                  {pivot[0], pivot[1], pivot[2]},
                                                  Codec::number(value["yaw"]),
                                                  Codec::number(value["pitch"]),
                                                  Codec::number(value["speed"]),
                                                  value["orthographic"].get<bool>(),
                                                  Codec::number(value["orthographicHeight"]),
                                                  Codec::number(value["nearPlane"]),
                                                  Codec::number(value["farPlane"])});

          if (!applied) {
            return std::unexpected(applied.error());
          }
        }

        m_editorView = mode == "editor";
        m_flying = m_orbiting = m_panning = false;
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "navigate-view") {
        auto applied = navigateView(parsed);
        return applied ? execute("{\"command\":\"inspect\"}") : std::unexpected(applied.error());
      }

      if (command == "focus-view" || command == "pick-view") {
        Codec::requireFields(parsed,
                             {"command", "revision", command == "focus-view" ? "entity" : "uv"});

        if (Codec::id(parsed["revision"]) != m_document->stats()->revision) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Viewport request has a stale document revision");
        }

        Core::Result<void> applied;

        if (command == "focus-view") {
          std::optional<World::EntityId> entity;

          if (!parsed["entity"].is_null()) {
            auto scene = m_document->scene().value();
            auto found = scene->findEntity(Codec::id(parsed["entity"]));

            if (!found) {
              return std::unexpected(found.error());
            }

            entity = *found;
          }

          applied = focusView(entity);
        } else {
          const auto uv = Codec::numbers<2>(parsed["uv"]);
          applied = pickView({uv[0], uv[1]});
        }

        return applied ? execute("{\"command\":\"inspect\"}") : std::unexpected(applied.error());
      }

      if (command == "show-pane") {
        Codec::requireFields(parsed, {"command", "pane"});
        const auto pane = parsed["pane"].get<std::string>();

        if (pane != "scene" && pane != "environment" && pane != "rendering"
            && pane != "resources") {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown workspace pane: " + pane);
        }

        m_requestedPane = pane;
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "stage-environment") {
        Codec::requireFields(parsed, {"command", "revision", "value"});

        if (Codec::id(parsed["revision"]) != m_document->stats()->revision || pendingInspector()
            || pendingOcclusion()) {
          return Core::fail(
              Core::ErrorCode::InvalidState,
              "Environment staging requires the current revision and clean inspector/AO drafts");
        }

        if (!parsed["value"].is_object()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Environment draft requires an object");
        }

        auto tentative = m_snapshot["scene"];
        tentative["environment"] = parsed["value"];
        auto stagedScene = World::decodeScene(tentative.dump());

        if (!stagedScene) {
          return std::unexpected(stagedScene.error());
        }

        if (!pendingEnvironment()) {
          m_environmentOriginal = m_snapshot["scene"]["environment"];
        }

        m_environmentDraft = parsed["value"];
        m_environmentRevision = std::to_string(m_document->stats()->revision);
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "apply-environment" || command == "reset-environment") {
        Codec::requireFields(parsed, {"command", "revision"});

        if (Codec::id(parsed["revision"]) != m_document->stats()->revision) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Environment request has a stale revision");
        }

        if (command == "apply-environment") {
          auto applied = applyEnvironment();

          if (!applied) {
            return std::unexpected(applied.error());
          }
        } else {
          m_environmentDraft = m_environmentOriginal;
          m_environmentRevision.clear();
        }

        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "stage-occlusion") {
        Codec::requireFields(parsed, {"command", "revision", "value"});

        if (Codec::id(parsed["revision"]) != m_document->stats()->revision || pendingInspector()
            || pendingEnvironment()) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "AO staging requires the current revision and clean other drafts");
        }

        auto tentative = m_snapshot["scene"];
        tentative["occlusion"] = parsed["value"];
        auto stagedScene = World::decodeScene(tentative.dump());

        if (!stagedScene) {
          return std::unexpected(stagedScene.error());
        }

        if (!pendingOcclusion()) {
          m_occlusionOriginal = m_snapshot["scene"]["occlusion"];
        }

        m_occlusionDraft = parsed["value"];
        m_occlusionRevision = std::to_string(m_document->stats()->revision);
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "apply-occlusion" || command == "reset-occlusion") {
        Codec::requireFields(parsed, {"command", "revision"});

        if (Codec::id(parsed["revision"]) != m_document->stats()->revision) {
          return Core::fail(Core::ErrorCode::InvalidState, "AO request has a stale revision");
        }

        if (command == "apply-occlusion") {
          auto applied = applyOcclusion();

          if (!applied) {
            return std::unexpected(applied.error());
          }
        } else {
          m_occlusionDraft = m_occlusionOriginal;
          m_occlusionRevision.clear();
        }

        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "apply-inspector" || command == "reset-inspector") {
        Codec::requireFields(parsed, {"command", "revision"});

        if (Codec::id(parsed["revision"]) != m_document->stats()->revision) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Inspector request has a stale revision");
        }

        if (command == "apply-inspector") {
          auto applied = applyInspector();

          if (!applied) {
            return std::unexpected(applied.error());
          }
        } else {
          m_edit = m_original;
          m_editRevision = 0;
          m_propertiesInvalid = false;
        }

        return execute("{\"command\":\"inspect\"}");
      }

      if (parsed.value("command", std::string{}) == "close") {
        Codec::requireFields(parsed, {"command", "discard"});
        const bool discard = parsed["discard"].get<bool>();

        if ((m_document->stats()->dirty || pendingInspector() || pendingEnvironment()
             || pendingOcclusion() || pendingText())
            && !discard) {
          m_pendingClose = true;
        } else {
          m_pendingClose = false;
          auto stopped = stopPlay();
          m_application->stop();

          if (!stopped) {
            m_closeError = stopped.error();
            return std::unexpected(stopped.error());
          }
        }

        return Json{
            {"ok", true}, {"running", m_application->running()}, {"confirmation", m_pendingClose}}
            .dump();
      }

      if (!m_applying && (pendingInspector() || pendingEnvironment() || pendingOcclusion())
          && parsed.value("command", std::string{}) != "inspect") {
        return Core::fail(
            Core::ErrorCode::InvalidState,
            "Apply or reset pending inspector/environment/AO changes before another operation");
      }

      auto result = m_document->dispatch(request);
      auto updated = refresh();

      if (!updated) {
        return Core::fail(updated.error().code,
                          "Document response may have committed; " + updated.error().message);
      }

      if (result && command == "inspect") {
        auto response = Codec::parseJson(*result);
        response["workspace"] = {{"pendingInspector", pendingInspector()},
                                 {"pendingEnvironment", pendingEnvironment()},
                                 {"environmentDraft", m_environmentDraft},
                                 {"pendingOcclusion", pendingOcclusion()},
                                 {"occlusionDraft", m_occlusionDraft},
                                 {"pane", m_pane},
                                 {"requestedPane", m_requestedPane},
                                 {"pendingClose", m_pendingClose},
                                 {"running", m_application->running()},
                                 {"inspector", m_edit},
                                 {"propertiesText", m_propertiesText},
                                 {"propertiesInvalid", m_propertiesInvalid},
                                 {"play", playStatus()},
                                 {"view", viewStatus()},
                                 {"message", m_message},
                                 {"file", textStatus()}};
        return response.dump();
      }

      return result;
    }

    auto action(Json request) -> bool {
      request["revision"] = std::to_string(m_document->stats()->revision);
      auto result = safeExecute(request.dump());

      if (!result) {
        m_message = result.error().message;
        m_actionError = result.error();
        return false;
      }

      m_message.clear();
      m_actionError.reset();
      return true;
    }

    auto edit(Json operations) -> bool {
      return action({{"command", "edit"}, {"operations", std::move(operations)}});
    }

    auto processRequests() -> Core::Result<void> {
      if (m_mailboxFault) {
        return std::unexpected(*m_mailboxFault);
      }

      auto files = m_control->files();

      if (!files) {
        return std::unexpected(files.error());
      }

      std::vector<std::filesystem::path> pending;
      std::size_t entries = 0;

      for (const auto& entry : std::filesystem::directory_iterator(files->root() / "requests")) {
        if (++entries > 4096) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Workspace session request limit exceeded");
        }

        if (entry.path().extension() != ".json") {
          continue;
        }

        const auto utf = entry.path().stem().u8string();
        const std::string id(utf.begin(), utf.end());

        if (!validId(id) || !std::filesystem::is_regular_file(entry.symlink_status())) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid workspace request filename/type");
        }

        if (!std::filesystem::exists(files->root() / "responses" / entry.path().filename())) {
          pending.push_back(entry.path().filename());
        }

        if (pending.size() > 256) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Workspace pending request capacity exceeded");
        }
      }

      std::ranges::sort(pending);

      for (std::size_t index = 0; index < std::min<std::size_t>(16, pending.size()); ++index) {
        auto source
            = files->readText(std::filesystem::path{"requests"} / pending[index], 1024 * 1024);
        m_mailboxFault
            = Core::Error{Core::ErrorCode::External,
                          "Workspace request may have committed; response did not complete",
                          {}};
        auto response = source ? safeExecute(*source)
                               : Core::Result<std::string>{std::unexpected(source.error())};
        Json record{
            {"source", source && Core::isValidUtf8(*source) ? Json(*source) : Json(nullptr)},
            {"response", response ? Codec::parseJson(*response) : errorJson(response.error())}};
        auto written = files->writeTextAtomic(std::filesystem::path{"responses"} / pending[index],
                                              record.dump());

        if (!written) {
          m_mailboxFault = Core::Error{written.error().code,
                                       "Workspace request may have committed; session is terminal "
                                       "after response write failure: "
                                           + written.error().message,
                                       {}};
          return std::unexpected(*m_mailboxFault);
        }

        m_mailboxFault.reset();

        if (!m_application->running()) {
          break;
        }
      }

      return {};
    }

    auto currentView() const -> Core::Result<Graphics::SceneView> {
      if (playing() || !m_editorView) {
        return Graphics::SceneView{};
      }

      auto view = m_editorCamera.view();
      return view ? Core::Result<Graphics::SceneView>(*view) : std::unexpected(view.error());
    }

    auto viewStatus() const -> Json {
      const auto state = m_editorCamera.state();
      auto current = currentView();
      const bool ready = current && m_previewReady && m_renderedView && *current == *m_renderedView
                         && m_renderedRevision == std::to_string(m_document->stats()->revision)
                         && !m_assetRefresh;
      return {
          {"mode", playing() ? "play" : (m_editorView ? "editor" : "game")},
          {"ready", ready},
          {"flying", m_flying},
          {"viewport",
           {{"origin", {m_viewOrigin.x, m_viewOrigin.y}}, {"size", {m_viewWidth, m_viewHeight}}}},
          {"camera",
           {{"position", {state.position.x, state.position.y, state.position.z}},
            {"pivot", {state.pivot.x, state.pivot.y, state.pivot.z}},
            {"yaw", state.yaw},
            {"pitch", state.pitch},
            {"speed", state.speed},
            {"orthographic", state.orthographic},
            {"orthographicHeight", state.orthographicHeight},
            {"nearPlane", state.nearPlane},
            {"farPlane", state.farPlane}}}};
    }

    auto navigateView(const Json& request) -> Core::Result<void> {
      if (!m_editorView || playing()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Navigation requires the editor view");
      }

      const auto action = request.at("action").get<std::string>();

      if (action == "turn") {
        Codec::requireFields(request, {"command", "action", "delta", "orbit"});
        const auto delta = Codec::numbers<2>(request["delta"]);
        return m_editorCamera.turn({delta[0], delta[1]}, request["orbit"].get<bool>());
      }

      if (action == "move") {
        Codec::requireFields(request, {"command", "action", "direction", "seconds", "fast"});
        const auto direction = Codec::numbers<3>(request["direction"]);
        return m_editorCamera.move({direction[0], direction[1], direction[2]},
                                   Codec::number(request["seconds"]), request["fast"].get<bool>());
      }

      if (action == "pan") {
        Codec::requireFields(request, {"command", "action", "delta"});
        const auto delta = Codec::numbers<2>(request["delta"]);
        return m_editorCamera.pan({delta[0], delta[1]}, m_viewWidth / m_viewHeight);
      }

      if (action == "dolly") {
        Codec::requireFields(request, {"command", "action", "steps"});
        return m_editorCamera.dolly(Codec::number(request["steps"]));
      }

      return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown editor navigation action");
    }

    auto focusView(std::optional<World::EntityId> entity) -> Core::Result<void> {
      auto scene = m_document->scene();

      if (!scene) {
        return std::unexpected(scene.error());
      }

      Core::Result<Graphics::Bounds> bounds = Graphics::Bounds{};

      if (entity) {
        bounds = m_drawing->bounds(**scene, *entity);
      } else {
        auto entities = (*scene)->entities();

        if (!entities) {
          return std::unexpected(entities.error());
        }

        bool first = true;

        for (const auto& info : *entities) {
          if (!info.mesh || !info.mesh->visible) {
            continue;
          }

          auto next = m_drawing->bounds(**scene, info.id);

          if (!next) {
            return std::unexpected(next.error());
          }

          if (first) {
            bounds = *next;
            first = false;
          } else {
            auto& merged = *bounds;
            merged.minimum = {std::min(merged.minimum.x, next->minimum.x),
                              std::min(merged.minimum.y, next->minimum.y),
                              std::min(merged.minimum.z, next->minimum.z)};
            merged.maximum = {std::max(merged.maximum.x, next->maximum.x),
                              std::max(merged.maximum.y, next->maximum.y),
                              std::max(merged.maximum.z, next->maximum.z)};
          }
        }

        if (first) {
          return Core::fail(Core::ErrorCode::NotFound, "Scene has no visible meshes to frame");
        }
      }

      if (!bounds) {
        return std::unexpected(bounds.error());
      }

      auto focused = m_editorCamera.focus(*bounds, m_viewWidth / m_viewHeight);

      if (focused) {
        m_editorView = true;
      }

      return focused;
    }

    /** A pick must refer to the displayed view/current document, not a stale image.
     * 选择必须对应已显示视图及当前文档，不能用过期图像悄悄选择新场景。 */
    auto pickView(Math::Vec2 uv) -> Core::Result<void> {
      if (pendingInspector() || pendingEnvironment() || pendingOcclusion()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Apply or reset scene drafts before selecting");
      }

      auto view = currentView();

      if (!view || !m_previewReady || !m_target || !m_renderedView || *view != *m_renderedView
          || m_renderedRevision != std::to_string(m_document->stats()->revision)
          || m_assetRefresh) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Render the current view before selecting");
      }

      auto scene = m_document->scene();
      auto target = m_target->info();

      if (!scene || !target) {
        return std::unexpected(!scene ? scene.error() : target.error());
      }

      auto selected = m_drawing->pick(**scene, target->width, target->height, uv, *m_renderedView);

      if (!selected) {
        return std::unexpected(selected.error());
      }

      auto changed = m_document->select(*selected, m_document->stats()->revision);
      return changed ? refresh() : changed;
    }

    auto preview() -> Core::Result<void> {
      const auto size = m_application->window().pixelSize();
      const auto logical = m_application->window().size();

      if (!size || !logical) {
        return std::unexpected(size ? logical.error() : size.error());
      }

      if (size->width <= 0 || size->height <= 0 || logical->width <= 0) {
        return {};
      }

      const float dpi = static_cast<float>(size->width) / static_cast<float>(logical->width);
      const float scale = std::min(
          {dpi, 4096.F / std::max(1.F, m_viewWidth), 4096.F / std::max(1.F, m_viewHeight)});
      const auto width = static_cast<std::uint32_t>(std::max(1.F, m_viewWidth * scale));
      const auto height = static_cast<std::uint32_t>(std::max(1.F, m_viewHeight * scale));

      bool resize = !m_target;

      if (m_target) {
        auto info = m_target->info();

        if (!info) {
          return std::unexpected(info.error());
        }

        resize = info->width != width || info->height != height;
      }

      if (resize) {
        auto created = m_renderer->createTarget(width, height);

        if (!created) {
          return std::unexpected(created.error());
        }

        m_target.emplace(std::move(*created));
      }

      auto scene = playing() ? m_play->scene() : m_document->scene();

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto view = currentView();

      if (!view) {
        return std::unexpected(view.error());
      }

      auto drawn = m_drawing->renderInto(**scene, *m_target, {0.055F, 0.06F, 0.075F, 1}, *view);

      if (drawn) {
        m_renderedView = *view;
        m_renderedRevision = std::to_string(m_document->stats()->revision);
      }
      m_previewReady = drawn.has_value();
      m_previewError = drawn ? "" : drawn.error().message;

      if (!drawn
          && (drawn.error().code == Core::ErrorCode::External
              || drawn.error().code == Core::ErrorCode::InvalidState)) {
        return drawn;
      }

      return {};
    }

    auto tree(const Json& entities, const Json& parent, int depth = 0) -> void {
      if (depth > 256) {
        return;
      }

      for (const auto& entity : entities) {
        if (entity["parent"] != parent) {
          continue;
        }

        const auto id = entity["id"].get<std::string>();
        const auto name = entity["name"].get<std::string>();
        const bool selected = m_snapshot["selected"] == entity["id"];
        const bool children = std::ranges::any_of(
            entities, [&](const auto& child) { return child["parent"] == entity["id"]; });
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        flags |= selected ? ImGuiTreeNodeFlags_Selected : 0;
        flags |= children ? 0 : ImGuiTreeNodeFlags_Leaf;
        ImGui::PushID(id.c_str());
        const bool open = ImGui::TreeNodeEx("entity", flags, "%s", name.c_str());

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
          action({{"command", "select"}, {"entity", id}});
        }

        if (open) {
          tree(entities, entity["id"], depth + 1);
          ImGui::TreePop();
        }

        ImGui::PopID();
      }
    }

    auto inspector() -> void {
      const auto stats = m_document->stats().value();

      if (!stats.selected) {
        ImGui::TextWrapped("Select an entity in the scene tree to edit its properties.");
        m_editEntity.reset();
        return;
      }

      auto found = std::ranges::find_if(m_snapshot["scene"]["entities"], [&](const auto& entity) {
        return entity["id"] == std::to_string(stats.selected->value);
      });

      if (found == m_snapshot["scene"]["entities"].end()) {
        return;
      }

      if (m_editEntity != stats.selected->value || m_editRevision != stats.revision) {
        m_editEntity = stats.selected->value;
        m_editRevision = stats.revision;
        m_original = *found;
        m_edit = *found;
        m_propertiesInvalid = false;
        m_propertiesText = m_edit["script"].is_null() ? "" : m_edit["script"]["properties"].dump(2);
      }

      std::string name = m_edit["name"].get<std::string>();

      if (ImGui::InputText("Name", &name)) {
        m_edit["name"] = name;
      }

      std::string parent
          = m_edit["parent"].is_null() ? "None" : m_edit["parent"].get<std::string>();

      if (ImGui::BeginCombo("Parent", parent.c_str())) {
        if (ImGui::Selectable("None", m_edit["parent"].is_null())) {
          m_edit["parent"] = nullptr;
        }

        for (const auto& entity : m_snapshot["scene"]["entities"]) {
          if (entity["id"] == m_edit["id"]) {
            continue;
          }

          auto text = entity["name"].get<std::string>() + "##" + entity["id"].get<std::string>();

          if (ImGui::Selectable(text.c_str(), entity["id"] == m_edit["parent"])) {
            m_edit["parent"] = entity["id"];
          }
        }

        ImGui::EndCombo();
      }

      if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        fields(m_edit["transform"]);
      }

      for (const auto* component : {"camera", "mesh", "rigidBody", "audioSource", "audioListener",
                                    "script", "material", "light"}) {
        ImGui::PushID(component);
        const auto title = label(component);

        if (m_edit[component].is_null()) {
          if (ImGui::Button(("Add " + title).c_str())) {
            m_edit[component] = m_presets[component];

            if (std::string_view(component) == "light") {
              auto& light = m_edit[component];
              bool shadows = light.contains("shadows") && !light["shadows"].is_null();
              ImGui::BeginDisabled(light["kind"] != "directional" && !shadows);

              if (ImGui::Checkbox("Cast directional shadows", &shadows)) {
                light["shadows"] = shadows ? Json{{"resolution", 1024},   {"cascades", 4},
                                                  {"distance", 100.0F},   {"splitLambda", 0.65F},
                                                  {"depthBias", 0.0002F}, {"slopeBias", 0.001F},
                                                  {"normalBias", 0.01F},  {"filterRadius", 2.0F}}
                                           : Json(nullptr);
              }

              ImGui::EndDisabled();
            }

            if (std::string_view(component) == "script") {
              m_propertiesText = m_edit[component]["properties"].dump(2);
              m_propertiesInvalid = false;
            }
          }
        } else if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
          if (std::string_view(component) == "script") {
            auto description = m_edit[component];
            description.erase("properties");
            fields(description);
            description["properties"] = m_edit[component]["properties"];
            m_edit[component] = std::move(description);
            ImGui::TextUnformatted("Properties (JSON object)");

            if (ImGui::InputTextMultiline("##properties", &m_propertiesText, {0, 120})) {
              try {
                auto properties = Codec::parseJson(m_propertiesText);
                m_propertiesInvalid = !properties.is_object();

                if (!m_propertiesInvalid) {
                  m_edit[component]["properties"] = std::move(properties);
                }
              } catch (const std::exception&) {
                m_propertiesInvalid = true;
              }
            }

            if (m_propertiesInvalid) {
              ImGui::TextWrapped("Properties must be a valid JSON object with unique fields.");
            }
          } else {
            fields(m_edit[component]);
          }

          if (std::string_view(component) == "script") {
            if (ImGui::SmallButton("Open script externally")) {
              const auto source = m_project->description().value().scriptsDirectory + "/"
                                  + m_edit[component]["source"].get<std::string>();
              fileAction({{"command", "open-external"}, {"path", source}});
            }
          }

          if (ImGui::SmallButton("Remove component")) {
            m_edit[component] = nullptr;

            if (std::string_view(component) == "script") {
              m_propertiesInvalid = false;
            }
          }
        }

        ImGui::PopID();
      }

      ImGui::Separator();
      ImGui::BeginDisabled(!pendingInspector());

      if (ImGui::Button("Apply changes")) {
        action({{"command", "apply-inspector"}});
      }

      ImGui::SameLine();

      if (ImGui::Button("Reset")) {
        action({{"command", "reset-inspector"}});
      }

      ImGui::EndDisabled();
    }

    auto safeExecute(std::string_view request) -> Core::Result<std::string> {
      try {
        return execute(request);
      } catch (const Json::exception& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Workspace command: " + std::string(error.what()));
      } catch (const std::invalid_argument& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Workspace command: " + std::string(error.what()));
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          "Workspace command: " + std::string(error.what()));
      }
    }

    auto applyInspector() -> Core::Result<void> {
      if (m_propertiesInvalid) {
        m_message = "Fix script properties JSON before applying or saving";
        return Core::fail(Core::ErrorCode::InvalidArgument, m_message);
      }

      if (m_edit == m_original) {
        return {};
      }

      const auto id = m_edit["id"].get<std::string>();
      Json operations = Json::array();

      if (m_edit["name"] != m_original["name"]) {
        operations.push_back({{"op", "name"}, {"entity", id}, {"name", m_edit["name"]}});
      }

      if (m_edit["parent"] != m_original["parent"]) {
        operations.push_back({{"op", "parent"}, {"entity", id}, {"parent", m_edit["parent"]}});
      }

      if (m_edit["transform"] != m_original["transform"]) {
        operations.push_back({{"op", "transform"}, {"entity", id}, {"value", m_edit["transform"]}});
      }

      for (const auto* component : {"camera", "mesh", "rigidBody", "audioSource", "audioListener",
                                    "script", "material", "light"}) {
        if (m_edit[component] != m_original[component]) {
          operations.push_back({{"op", "component"},
                                {"entity", id},
                                {"component", component},
                                {"value", m_edit[component]}});
        }
      }

      /** Switch primary cameras as one atomic document/history operation.
       * 切换主相机作为一个原子文档/历史操作。 */

      if (!m_edit["camera"].is_null() && m_edit["camera"]["primary"] == true) {
        for (const auto& entity : m_snapshot["scene"]["entities"]) {
          if (entity["id"] != id && !entity["camera"].is_null()
              && entity["camera"]["primary"] == true) {
            auto camera = entity["camera"];
            camera["primary"] = false;
            operations.push_back({{"op", "component"},
                                  {"entity", entity["id"]},
                                  {"component", "camera"},
                                  {"value", camera}});
          }
        }
      }

      ActiveScope applying{m_applying};
      const bool applied = edit(std::move(operations));

      if (applied) {
        m_original = m_edit;
        m_editRevision = 0;
      }

      return applied ? Core::Result<void>{} : std::unexpected(*m_actionError);
    }

    auto fileAction(Json request) -> bool {
      if (m_text
          && (request["command"] == "save-text" || request["command"] == "replace-text"
              || request["command"] == "close-text")) {
        request["buffer"] = std::to_string(m_buffer);
        request["revision"] = std::to_string(m_text->snapshot().value().revision);
      }

      auto result = safeExecute(request.dump());
      m_message = result ? "" : result.error().message;
      return result.has_value();
    }

    auto environmentPanel() -> void {
      const auto revision = std::to_string(m_document->stats()->revision);

      if (m_environmentRevision != revision && !pendingEnvironment()) {
        m_environmentDraft = m_snapshot["scene"]["environment"];

        if (m_environmentDraft.is_null()) {
          m_environmentDraft
              = {{"asset", ""},         {"intensity", 1.0F},   {"rotationDegrees", 0.0F},
                 {"background", true},  {"radianceSize", 256}, {"diffuseSize", 32},
                 {"specularSize", 128}, {"brdfSize", 128},     {"samples", 256}};
        }

        m_environmentOriginal = m_environmentDraft;
        m_environmentRevision = revision;
      }

      ImGui::BeginDisabled(playing() || pendingInspector() || pendingOcclusion());
      ImGui::TextWrapped("Scene HDR environment");
      auto asset = m_environmentDraft["asset"].get<std::string>();

      if (ImGui::InputText("HDRI", &asset)) {
        m_environmentDraft["asset"] = asset;
      }

      ImGui::BeginDisabled(!m_selectedResource.ends_with(".hdr"));

      if (ImGui::Button("Use selected HDRI")) {
        m_environmentDraft["asset"] = m_selectedResource;
      }

      ImGui::EndDisabled();
      auto intensity = m_environmentDraft["intensity"].get<float>();
      auto rotation = m_environmentDraft["rotationDegrees"].get<float>();
      auto background = m_environmentDraft["background"].get<bool>();

      if (ImGui::InputFloat("Intensity", &intensity, 0.1F, 1.0F)) {
        m_environmentDraft["intensity"] = intensity;
      }

      if (ImGui::SliderFloat("Yaw", &rotation, -360, 360, "%.1f deg")) {
        m_environmentDraft["rotationDegrees"] = rotation;
      }

      if (ImGui::Checkbox("Sky background", &background)) {
        m_environmentDraft["background"] = background;
      }

      const std::array<const char*, 3> names{"Preview", "Balanced", "High"};
      const std::array<std::array<int, 5>, 3> quality{
          {{64, 16, 64, 64, 128}, {256, 32, 128, 128, 256}, {512, 64, 256, 256, 1024}}};
      const std::array<const char*, 5> keys{"radianceSize", "diffuseSize", "specularSize",
                                            "brdfSize", "samples"};
      const char* selected = "Custom";

      for (std::size_t index = 0; index < quality.size(); ++index) {
        bool matches = true;

        for (std::size_t field = 0; field < keys.size(); ++field) {
          matches = matches && m_environmentDraft[keys[field]] == quality[index][field];
        }

        if (matches) {
          selected = names[index];
        }
      }

      if (ImGui::BeginCombo("Quality", selected)) {
        for (std::size_t index = 0; index < quality.size(); ++index) {
          if (ImGui::Selectable(names[index], selected == names[index])) {
            for (std::size_t field = 0; field < keys.size(); ++field) {
              m_environmentDraft[keys[field]] = quality[index][field];
            }
          }
        }

        ImGui::EndCombo();
      }

      if (ImGui::Button("Apply environment")) {
        action({{"command", "apply-environment"}});
      }

      if (ImGui::Button("Reset draft")) {
        action({{"command", "reset-environment"}});
      }

      ImGui::BeginDisabled(pendingEnvironment());

      if (ImGui::Button("Remove environment")) {
        edit(Json::array({{{"op", "environment"}, {"value", nullptr}}}));
      }

      ImGui::EndDisabled();
      ImGui::TextWrapped(
          "Save the scene to retain these settings. Higher quality increases processing time.");
      ImGui::EndDisabled();
    }

    /** Native controls and agent requests share the saved document transaction.
     * 原生控件与 agent 请求共用可保存的文档事务，未应用草稿不会悄悄丢失。 */
    auto renderingPanel() -> void {
      const auto revision = std::to_string(m_document->stats()->revision);

      if (m_occlusionRevision != revision && !pendingOcclusion()) {
        m_occlusionDraft = m_snapshot["scene"]["occlusion"];
        m_occlusionOriginal = m_occlusionDraft;
        m_occlusionRevision = revision;
      }

      ImGui::BeginDisabled(playing() || pendingInspector() || pendingEnvironment());
      ImGui::TextWrapped("Ambient occlusion");
      bool enabled = !m_occlusionDraft.is_null();
      const Json defaults{{"radius", 0.5F},  {"bias", 0.02F}, {"strength", 1.0F},
                          {"directions", 8}, {"steps", 4},    {"filterRadius", 2}};

      if (ImGui::Checkbox("Enable SSAO", &enabled)) {
        m_occlusionDraft = enabled ? defaults : Json(nullptr);
      }

      ImGui::BeginDisabled(!enabled);
      auto value = enabled ? m_occlusionDraft : defaults;
      auto radius = value["radius"].get<float>();
      auto bias = value["bias"].get<float>();
      auto strength = value["strength"].get<float>();

      ImGui::TextUnformatted("Radius");
      ImGui::SetNextItemWidth(-1.F);

      if (ImGui::SliderFloat("##Radius", &radius, 0.05F, 5.0F, "%.2f m")) {
        m_occlusionDraft["radius"] = radius;
      }

      ImGui::TextUnformatted("Surface bias");
      ImGui::SetNextItemWidth(-1.F);

      if (ImGui::SliderFloat("##Surface bias", &bias, 0.0F, 0.2F, "%.3f m")) {
        m_occlusionDraft["bias"] = bias;
      }

      ImGui::TextUnformatted("Strength");
      ImGui::SetNextItemWidth(-1.F);

      if (ImGui::SliderFloat("##Strength", &strength, 0.0F, 4.0F)) {
        m_occlusionDraft["strength"] = strength;
      }

      const std::array<const char*, 3> names{"Low", "Balanced", "High"};
      const std::array<std::array<int, 3>, 3> quality{{{4, 2, 1}, {8, 4, 2}, {16, 8, 4}}};
      const std::array<const char*, 3> keys{"directions", "steps", "filterRadius"};
      const char* selected = "Custom";

      for (std::size_t index = 0; index < quality.size(); ++index) {
        if (value[keys[0]] == quality[index][0] && value[keys[1]] == quality[index][1]
            && value[keys[2]] == quality[index][2]) {
          selected = names[index];
        }
      }

      ImGui::TextUnformatted("AO quality");
      ImGui::SetNextItemWidth(-1.F);

      if (ImGui::BeginCombo("##AO quality", selected)) {
        for (std::size_t index = 0; index < quality.size(); ++index) {
          if (ImGui::Selectable(names[index], selected == names[index])) {
            for (std::size_t field = 0; field < keys.size(); ++field) {
              m_occlusionDraft[keys[field]] = quality[index][field];
            }
          }
        }

        ImGui::EndCombo();
      }

      ImGui::EndDisabled();

      if (ImGui::Button("Apply rendering")) {
        action({{"command", "apply-occlusion"}});
      }

      if (ImGui::Button("Reset AO draft")) {
        action({{"command", "reset-occlusion"}});
      }

      ImGui::TextWrapped("AO shades diffuse HDR environment light. Add an HDRI in Environment.");
      ImGui::TextWrapped(
          "Apply and save the scene to retain these settings in Play and exported games.");
      ImGui::EndDisabled();
    }

    auto resourcesPanel() -> void {
      if (ImGui::Button("Refresh files")) {
        fileAction({{"command", "resources"}});
      }

      if (!m_selectedResource.empty()) {
        ImGui::TextWrapped("%s", m_selectedResource.c_str());

        if (ImGui::Button("Open externally")) {
          fileAction({{"command", "open-external"}, {"path", m_selectedResource}});
        }
      }

      ImGui::InputText("Path", &m_newPath);

      if (ImGui::Button("New script")) {
        if (fileAction({{"command", "create-script"}, {"path", m_newPath}})) {
          fileAction({{"command", "open-external"}, {"path", m_newPath}});
        }
      }

      ImGui::SameLine();

      if (ImGui::Button("Folder")) {
        fileAction({{"command", "create-directory"}, {"path", m_newPath}});
      }

      ImGui::InputText("Import source", &m_importSource);

      if (ImGui::Button("Import to path")) {
        fileAction(
            {{"command", "import-resource"}, {"path", m_newPath}, {"source", m_importSource}});
      }

      if (ImGui::Button("Check scripts")) {
        fileAction({{"command", "check-scripts"}});
      }

      if (m_text) {
        const auto draft = m_text->snapshot().value();
        ImGui::TextWrapped("Agent buffer: %s%s", draft.path.c_str(),
                           draft.dirty ? " (unsaved)" : "");
        ImGui::BeginDisabled(draft.readOnly);

        if (ImGui::Button("Save agent draft")) {
          fileAction({{"command", "save-text"}});
        }

        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button("Close agent buffer")) {
          fileAction({{"command", "close-text"}, {"discard", false}});
        }

        if (draft.dirty && ImGui::Button("Discard agent draft")) {
          fileAction({{"command", "close-text"}, {"discard", true}});
        }
      }

      if (!m_checkMessage.empty()) {
        ImGui::TextWrapped("%s", m_checkMessage.c_str());
      }

      for (std::size_t index = 0; index < m_scriptDiagnostics.size(); ++index) {
        const auto item = m_scriptDiagnostics[index];
        ImGui::PushID(static_cast<int>(index));
        const auto label
            = item.path + ':' + std::to_string(item.line) + ':' + std::to_string(item.column);

        if (ImGui::Button(label.c_str())) {
          fileAction({{"command", "open-external"}, {"path", item.path}});
        }

        ImGui::PopID();
      }

      ImGui::TextWrapped("Open text resources in the system default application.");
      ImGui::Separator();
      const auto entries = m_catalog;

      for (const auto& entry : entries) {
        const auto name = entry["path"].get<std::string>();

        if (entry["type"] == "text") {
          if (ImGui::Selectable(name.c_str(), m_selectedResource == name,
                                ImGuiSelectableFlags_AllowDoubleClick)) {
            m_selectedResource = name;

            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
              fileAction({{"command", "open-external"}, {"path", name}});
            }
          }
        } else {
          ImGui::TextWrapped("%s%s", entry["type"] == "directory" ? "[dir] " : "[asset] ",
                             name.c_str());
        }
      }
    }

    auto viewportControls() -> void {
      if (playing()) {
        ImGui::TextUnformatted("Running game preview");
        m_flying = m_orbiting = m_panning = false;
        return;
      }

      ImGui::SetNextItemWidth(std::min(160.F, ImGui::GetContentRegionAvail().x));
      const char* mode = m_editorView ? "Editor view" : "Game camera";

      if (ImGui::BeginCombo("##viewport-mode", mode)) {
        for (const auto* value : {"editor", "game"}) {
          if (ImGui::Selectable(
                  value, (m_editorView ? std::string_view("editor") : std::string_view("game"))
                             == value)) {
            auto result = safeExecute(Json{{"command", "set-view"}, {"mode", value}}.dump());
            m_message = result ? "" : result.error().message;
          }
        }

        ImGui::EndCombo();
      }

      ImGui::SameLine();
      ImGui::BeginDisabled(!m_document->stats()->selected);

      if (ImGui::Button("Focus (F)")) {
        action({{"command", "focus-view"}, {"entity", m_snapshot["selected"]}});
      }

      ImGui::EndDisabled();
      ImGui::SameLine();

      if (ImGui::Button("Frame scene")) {
        action({{"command", "focus-view"}, {"entity", nullptr}});
      }

      if (m_editorView) {
        auto state = m_editorCamera.state();
        bool changed = ImGui::Checkbox("Orthographic", &state.orthographic);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.F);
        changed = ImGui::DragFloat("Fly speed", &state.speed, 0.1F, 0.01F, 1000.F, "%.2f m/s",
                                   ImGuiSliderFlags_AlwaysClamp)
                  || changed;

        if (changed) {
          auto applied = m_editorCamera.setState(state);
          m_message = applied ? "" : applied.error().message;
        }

        ImGui::TextWrapped("RMB + WASD/QE fly; Shift faster. Alt+LMB orbit; MMB pan; wheel zoom.");
      }
    }

    /** Route navigation only from the viewport; it never consumes scene history.
     * 仅从视口路由导航输入，不消耗场景历史；游戏运行时仍使用游戏输入路径。 */
    auto viewportInput(const Platform::InputState& input, double seconds) -> void {
      if (playing()) {
        return;
      }

      const auto pointer = input.pointer();
      const bool hovered = pointer.x >= m_viewOrigin.x && pointer.y >= m_viewOrigin.y
                           && pointer.x < m_viewOrigin.x + m_viewWidth
                           && pointer.y < m_viewOrigin.y + m_viewHeight;
      auto gui = m_renderer->guiStats();

      if (m_viewBlocked || !gui || gui->text || m_pendingClose) {
        m_flying = m_orbiting = m_panning = false;
        return;
      }

      if (hovered && input.key(Platform::Key::F).pressed && m_document->stats()->selected) {
        action({{"command", "focus-view"}, {"entity", m_snapshot["selected"]}});
      }

      const auto left = input.button(Platform::MouseButton::Left);
      const auto right = input.button(Platform::MouseButton::Right);
      const auto middle = input.button(Platform::MouseButton::Middle);
      const bool alt
          = input.key(Platform::Key::LeftAlt).down || input.key(Platform::Key::RightAlt).down;

      if (!m_editorView) {
        if (hovered && left.pressed) {
          auto selected = pickView({(pointer.x - m_viewOrigin.x) / m_viewWidth,
                                    (pointer.y - m_viewOrigin.y) / m_viewHeight});
          m_message = selected ? "" : selected.error().message;
        }

        return;
      }

      if (hovered) {
        m_flying = m_flying || right.pressed;
        m_panning = m_panning || middle.pressed;
        m_orbiting = m_orbiting || (alt && left.pressed);
      }

      m_flying = m_flying && right.down;
      m_panning = m_panning && middle.down;
      m_orbiting = m_orbiting && left.down;

      if (input.key(Platform::Key::Escape).pressed) {
        m_flying = m_orbiting = m_panning = false;
      }

      const auto remember = [&](Core::Result<void> result) {
        if (!result) {
          m_message = result.error().message;
        }
      };
      const auto delta = input.pointerDelta();

      if (m_flying || m_orbiting) {
        remember(m_editorCamera.turn({-delta.x * 0.15F, -delta.y * 0.15F}, m_orbiting));
      }

      if (m_flying) {
        const auto axis = [&](Platform::Key positive, Platform::Key negative) -> float {
          return static_cast<float>(input.key(positive).down)
                 - static_cast<float>(input.key(negative).down);
        };
        const bool fast
            = input.key(Platform::Key::LeftShift).down || input.key(Platform::Key::RightShift).down;
        remember(m_editorCamera.move(
            {axis(Platform::Key::D, Platform::Key::A), axis(Platform::Key::E, Platform::Key::Q),
             axis(Platform::Key::W, Platform::Key::S)},
            std::min(1.0, seconds), fast));
      }

      if (m_panning) {
        remember(m_editorCamera.pan({delta.x / m_viewWidth, delta.y / m_viewHeight},
                                    m_viewWidth / m_viewHeight));
      }

      if (hovered && input.wheel().y != 0) {
        remember(m_editorCamera.dolly(input.wheel().y));
      }

      if (hovered && left.pressed && !alt && !m_flying) {
        remember(pickView({(pointer.x - m_viewOrigin.x) / m_viewWidth,
                           (pointer.y - m_viewOrigin.y) / m_viewHeight}));
      }
    }

    auto draw() -> Core::Result<void> {
      const auto stats = m_document->stats().value();
      auto* viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(viewport->WorkPos);
      ImGui::SetNextWindowSize(viewport->WorkSize);
      ImGui::Begin("DotEngine", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                       | ImGuiWindowFlags_NoSavedSettings);

      ImGui::BeginDisabled(playing());

      if ((ImGui::Button("Save") || (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S))) && !playing()) {
        if (applyInspector()) {
          action({{"command", "save"}});
        }
      }

      ImGui::SameLine();
      ImGui::BeginDisabled(!stats.canUndo);

      if ((ImGui::Button("Undo") || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) && stats.canUndo
          && !playing()) {
        action({{"command", "undo"}});
      }

      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(!stats.canRedo);

      if ((ImGui::Button("Redo") || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
          && stats.canRedo && !playing()) {
        action({{"command", "redo"}});
      }

      ImGui::EndDisabled();
      ImGui::SameLine();

      if (ImGui::Button("New entity") && !playing()) {
        edit(Json::array({{{"op", "create"}, {"name", "Entity"}}}));
      }

      ImGui::SameLine();
      ImGui::BeginDisabled(!stats.selected);

      if ((ImGui::Button("Delete")
           || (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete)))
          && m_document->stats()->selected && !playing()) {
        edit(Json::array({{{"op", "remove"},
                           {"entity", std::to_string(m_document->stats()->selected->value)}}}));
      }

      ImGui::EndDisabled();
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(
          !playing()
          && (pendingInspector() || pendingEnvironment() || pendingOcclusion() || pendingText()));

      if (ImGui::Button(playing() ? "Stop" : "Play")
          || ImGui::Shortcut(ImGuiKey_F5, ImGuiInputFlags_RouteGlobal)) {
        action({{"command", playing() ? "stop-play" : "play"}});
      }

      ImGui::EndDisabled();
      ImGui::SameLine();

      if (playing()) {
        bool capture = m_captureInput;

        if (ImGui::Checkbox("Game input (Esc releases)", &capture)) {
          auto response
              = safeExecute(Json{{"command", "capture-input"}, {"capture", capture}}.dump());

          if (!response) {
            m_message = response.error().message;
          }
        }

        ImGui::SameLine();
      }

      if (ImGui::Button("Export game")) {
        m_exportDialog = true;
        m_message.clear();
      }

      ImGui::SameLine();
      ImGui::TextUnformatted(m_snapshot["dirty"].get<bool>() ? "Unsaved changes" : "Saved");
      ImGui::Separator();

      if (!m_message.empty()) {
        ImGui::TextWrapped("%s", m_message.c_str());
      }

      const auto available = ImGui::GetContentRegionAvail();
      ImGui::BeginChild("Scene", {220, available.y}, ImGuiChildFlags_Borders);

      if (ImGui::BeginTabBar("Project panes", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Scene", nullptr,
                                m_requestedPane == "scene" ? ImGuiTabItemFlags_SetSelected : 0)) {
          m_pane = "scene";

          if (m_requestedPane == "scene") {
            m_requestedPane.clear();
          }

          auto entities = m_snapshot["scene"]["entities"];
          ImGui::BeginDisabled(playing());
          tree(entities, nullptr);
          ImGui::EndDisabled();
          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(
                "Environment", nullptr,
                m_requestedPane == "environment" ? ImGuiTabItemFlags_SetSelected : 0)) {
          m_pane = "environment";

          if (m_requestedPane == "environment") {
            m_requestedPane.clear();
          }

          environmentPanel();
          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(
                "Rendering", nullptr,
                m_requestedPane == "rendering" ? ImGuiTabItemFlags_SetSelected : 0)) {
          m_pane = "rendering";

          if (m_requestedPane == "rendering") {
            m_requestedPane.clear();
          }

          renderingPanel();
          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(
                "Resources", nullptr,
                m_requestedPane == "resources" ? ImGuiTabItemFlags_SetSelected : 0)) {
          m_pane = "resources";

          if (m_requestedPane == "resources") {
            m_requestedPane.clear();
          }

          resourcesPanel();
          ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
      }

      ImGui::EndChild();
      ImGui::SameLine();
      ImGui::BeginChild("Viewport", {std::max(100.F, available.x - 220 - 340 - 24), available.y},
                        ImGuiChildFlags_Borders);

      ImGui::BeginChild("Viewport controls", {0, ImGui::GetFrameHeightWithSpacing() * 4});
      viewportControls();
      ImGui::EndChild();

      if (m_playError) {
        ImGui::TextWrapped("Play stopped advancing: %s", m_playError->message.c_str());
      }

      if (!m_playDiagnostics.empty()) {
        ImGui::TextWrapped(
            "Play diagnostics: %s",
            m_playDiagnostics.back()["error"]["message"].get_ref<const std::string&>().c_str());
      }

      m_viewOrigin = ImGui::GetCursorScreenPos();
      const auto content = ImGui::GetContentRegionAvail();
      m_viewWidth = std::max(1.F, content.x);
      m_viewHeight = std::max(1.F, content.y);

      if (m_previewReady && m_target) {
        auto image = m_renderer->guiImage(*m_target, m_viewWidth, m_viewHeight);

        if (!image) {
          ImGui::EndChild();
          ImGui::End();
          return image;
        }

        m_viewBlocked = ImGui::IsPopupOpen(
                            nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)
                        || ImGui::IsAnyItemActive();
      } else {
        ImGui::TextWrapped(
            "%s", m_previewError.empty() ? "Preparing scene preview" : m_previewError.c_str());
      }

      ImGui::EndChild();
      ImGui::SameLine();
      ImGui::BeginChild("Inspector", {0, available.y}, ImGuiChildFlags_Borders);
      ImGui::TextUnformatted("Inspector");
      ImGui::Separator();
      ImGui::BeginDisabled(playing());
      ImGui::BeginDisabled(pendingEnvironment() || pendingOcclusion());
      inspector();
      ImGui::EndDisabled();
      ImGui::EndDisabled();
      ImGui::EndChild();

      if (m_exportDialog) {
        ImGui::OpenPopup("Export game");
        m_exportDialog = false;
      }

      ImGui::SetNextWindowSize({600, 0}, ImGuiCond_Appearing);

      if (ImGui::BeginPopupModal("Export game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Export the current scene, resources and compiled scripts with a native player.");
        ImGui::Checkbox("Data package only", &m_exportDataOnly);
        ImGui::InputText("Existing parent directory", &m_exportParent);
        ImGui::InputText("New folder", &m_exportFolder);
        ImGui::BeginDisabled(playing() || pendingInspector() || pendingEnvironment()
                             || pendingOcclusion() || pendingText());

        if (ImGui::Button("Export")) {
          auto request = Json{{"command", m_exportDataOnly ? "export-data" : "export"},
                              {"revision", std::to_string(m_document->stats().value().revision)},
                              {"parent", m_exportParent},
                              {"folder", m_exportFolder}};
          auto result = safeExecute(request.dump());

          if (result) {
            m_message = "Game exported: " + Codec::parseJson(*result)["package"].get<std::string>();
            ImGui::CloseCurrentPopup();
          } else {
            m_message = result.error().message;
          }
        }

        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button("Cancel export")) {
          ImGui::CloseCurrentPopup();
        }

        if (!m_message.empty()) {
          ImGui::TextWrapped("%s", m_message.c_str());
        }

        ImGui::EndPopup();
      }

      if (m_pendingClose) {
        ImGui::OpenPopup("Unsaved changes");
      }

      if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes before closing?");

        if (!m_pendingClose) {
          ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Save and close")) {
          action({{"command", "save-close"}});
        }

        ImGui::SameLine();

        if (ImGui::Button("Discard")) {
          auto result = safeExecute("{\"command\":\"close\",\"discard\":true}");

          if (!result) {
            m_message = result.error().message;
          }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
          action({{"command", "cancel-close"}});
          ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
      }

      ImGui::End();
      return {};
    }

    auto callbacks() -> Core::LoopCallbacks {
      Core::LoopCallbacks callbacks;
      callbacks.acceptClose = [&]() -> Core::Result<bool> {
        m_pendingClose = m_document->stats()->dirty || pendingInspector() || pendingEnvironment()
                         || pendingOcclusion() || pendingText();

        if (!m_pendingClose) {
          auto stopped = stopPlay();

          if (!stopped) {
            return std::unexpected(stopped.error());
          }
        }

        return !m_pendingClose;
      };
      callbacks.prepareFrame = [&](const Platform::InputState& input) -> Core::Result<void> {
        auto processed = processRequests();

        if (!processed) {
          return processed;
        }

        if (m_captureInput && (input.key(Platform::Key::Escape).pressed || m_pendingClose)) {
          captureInput(false);
        }

        if (m_resetGuiInput) {
          for (bool focused : {false, true}) {
            auto forwarded
                = m_renderer->guiEvent({m_application->window().id(), 0, Platform::Focus{focused}});

            if (!forwarded) {
              return std::unexpected(forwarded.error());
            }
          }

          m_resetGuiInput = false;
        }

        if (m_captureInput) {
          m_gameInput = input;
          auto pointer = input.pointer();
          static_cast<void>(
              m_gameInput.consume({m_application->window().id(), 0,
                                   Platform::PointerMoved{pointer.x - m_viewOrigin.x,
                                                          pointer.y - m_viewOrigin.y, 0, 0}}));
        } else {
          m_gameInput.beginFrame();
          static_cast<void>(
              m_gameInput.consume({m_application->window().id(), 0, Platform::Focus{false}}));
        }

        return {};
      };
      callbacks.render = [&](const auto& frame) -> Core::Result<void> {
        if (playing() && m_play->stats()->state == PlayState::Running) {
          auto advanced = m_play->advance(m_gameInput, frame.elapsed);
          auto remembered = remember();

          if (!remembered) {
            return remembered;
          }

          if (!advanced) {
            m_playError = advanced.error();
            diagnostic(errorJson(advanced.error()));
            captureInput(false);
          }
        }

        if (m_assetRefresh) {
          auto cleared = m_drawing->clearCache();

          if (!cleared) {
            return cleared;
          }

          m_assetRefresh = false;
          m_renderedView.reset();
        }

        viewportInput(m_application->input(), std::chrono::duration<double>(frame.elapsed).count());
        auto ready = preview();

        if (!ready) {
          return ready;
        }

        auto shown = m_renderer->presentGui(
            [&] { return draw(); },
            std::min(std::chrono::duration<double>(frame.elapsed).count(), 1.0));
        return shown.transform([](auto) {});
      };
      return callbacks;
    }
  };

  Workspace::Workspace(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Workspace::Workspace(Workspace&&) noexcept = default;

  auto Workspace::operator=(Workspace&&) noexcept -> Workspace& = default;

  Workspace::~Workspace() = default;

  auto Workspace::open(const Project::Project& project, const WorkspaceConfig& config)
      -> Core::Result<Workspace> {
    if (!Platform::isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Workspace creation requires process main thread");
    }

    if (config.width < 800 || config.height < 480) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Workspace dimensions must be at least 800x480");
    }

    try {
      auto impl = std::make_unique<Impl>();
      impl->m_project = project;
      impl->m_config = config;
      auto document = Document::open(project);

      if (!document) {
        return std::unexpected(document.error());
      }

      impl->m_document.emplace(std::move(*document));
      auto resources = ResourceStore::open(project);

      if (!resources) {
        return std::unexpected(resources.error());
      }

      impl->m_resources.emplace(std::move(*resources));
      impl->m_newPath = project.description().value().scriptsDirectory + "/behavior.ts";
      const auto exportParent = project.files().value().root().parent_path().generic_u8string();
      impl->m_exportParent.assign(exportParent.begin(), exportParent.end());
      auto listed = impl->catalog();

      if (!listed) {
        return std::unexpected(listed.error());
      }

      Core::ApplicationConfig application;
      application.window = {"DotEngine — " + project.description()->name,
                            config.width,
                            config.height,
                            false,
                            true,
                            true};
      application.pauseOnFocusLoss = false;
      auto created = Core::Application::create(application);

      if (!created) {
        return std::unexpected(created.error());
      }

      impl->m_application.emplace(std::move(*created));

      if (!SDL_SetWindowMinimumSize(
              static_cast<SDL_Window*>(impl->m_application->window().nativeHandle()), 800, 480)) {
        return Core::fail(Core::ErrorCode::External,
                          "Set editor minimum size: " + std::string(SDL_GetError()));
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
      gui.font = config.font;

      if (gui.font.empty()) {
        auto font = impl->resource("NotoSansSC.ttf");

        if (!font) {
          return std::unexpected(font.error());
        }

        gui.font = std::move(*font);
      }

      auto enabled = impl->m_renderer->enableGui(gui);

      if (!enabled) {
        return std::unexpected(enabled.error());
      }

      auto drawing = Graphics::SceneRenderer::create(*impl->m_renderer, project.files().value());

      if (!drawing) {
        return std::unexpected(drawing.error());
      }

      impl->m_drawing.emplace(std::move(*drawing));
      auto control = Core::StagedDirectory::create(project.files().value());

      if (!control) {
        return std::unexpected(control.error());
      }

      impl->m_control.emplace(std::move(*control));
      auto files = impl->m_control->files().value();
      auto requests = files.createDirectories("requests");
      auto responses = files.createDirectories("responses");
      auto defaults = presets();

      if (!requests || !responses || !defaults) {
        return std::unexpected(!requests    ? requests.error()
                               : !responses ? responses.error()
                                            : defaults.error());
      }

      impl->m_presets = std::move(*defaults);
      auto snapshot = impl->refresh();

      if (!snapshot) {
        return std::unexpected(snapshot.error());
      }

      auto metadata = files.writeTextAtomic(
          "session.json",
          Json{{"protocol", 1}, {"state", "ready"}, {"document", impl->m_snapshot}}.dump());

      if (!metadata) {
        return std::unexpected(metadata.error());
      }

      impl->m_events = impl->m_application->events().subscribe([state
                                                                = impl.get()](const auto& event) {
        if (state->m_captureInput && std::holds_alternative<Platform::ButtonChanged>(event.data)) {
          const auto pointer = state->m_application->input().pointer();

          if (pointer.x < state->m_viewOrigin.x || pointer.y < state->m_viewOrigin.y
              || pointer.x >= state->m_viewOrigin.x + state->m_viewWidth
              || pointer.y >= state->m_viewOrigin.y + state->m_viewHeight) {
            state->captureInput(false);
          }
        }

        if (state->m_captureInput
            && ((std::holds_alternative<Platform::KeyChanged>(event.data)
                 && std::get<Platform::KeyChanged>(event.data).key != Platform::Key::F5)
                || std::holds_alternative<Platform::Text>(event.data))) {
          return;
        }

        auto handled = state->m_renderer->guiEvent(event);

        if (!handled) {
          state->m_inputError = handled.error();
          state->m_application->stop();
        }
      });
      return Workspace(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Open editor workspace: " + std::string(error.what()));
    }
  }

  auto Workspace::run(std::size_t frames) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Workspace was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (m_impl->m_mailboxFault) {
      return std::unexpected(*m_impl->m_mailboxFault);
    }

    auto callbacks = m_impl->callbacks();
    ActiveScope scope{m_impl->m_active};

    auto result = m_impl->m_application->run(callbacks, frames);
    return m_impl->finish(
        m_impl->m_inputError ? Core::Result<void>{std::unexpected(*m_impl->m_inputError)} : result);
  }

  auto Workspace::step(std::chrono::nanoseconds elapsed) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Workspace was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (m_impl->m_mailboxFault) {
      return std::unexpected(*m_impl->m_mailboxFault);
    }

    auto callbacks = m_impl->callbacks();
    ActiveScope scope{m_impl->m_active};

    auto result = m_impl->m_application->step(elapsed, callbacks);
    return m_impl->finish(m_impl->m_inputError
                              ? Core::Result<void>{std::unexpected(*m_impl->m_inputError)}
                              : result.transform([](const auto&) {}));
  }

  auto Workspace::dispatch(std::string_view request) -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Workspace was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};
    return m_impl->safeExecute(request);
  }

  auto Workspace::snapshot() const -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Workspace was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_document->snapshot() : std::unexpected(checked.error());
  }

  auto Workspace::controlDirectory() const -> Core::Result<std::filesystem::path> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Workspace was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->m_control->files().transform([](const auto& files) { return files.root(); });
  }

  auto Workspace::captureViewport() -> Core::Result<Graphics::Image> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Workspace was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->m_previewReady || !m_impl->m_target) {
      return Core::fail(Core::ErrorCode::InvalidState, "Workspace has no rendered preview");
    }

    return m_impl->m_renderer->readTarget(*m_impl->m_target);
  }

  auto submitWorkspaceRequest(const std::filesystem::path& control, std::string_view id,
                              std::string_view request) -> Core::Result<std::filesystem::path> {
    if (!validId(id)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Workspace request ID must be 1..64 ASCII letters/digits/_/-");
    }

    auto valid = validRequest(request);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto files = Core::FileSystem::open(control);

    if (!files) {
      return std::unexpected(files.error());
    }

    auto session = files->readText("session.json", 64 * 1024 * 1024);

    if (!session) {
      return std::unexpected(session.error());
    }

    try {
      const auto metadata = Codec::parseJson(*session);

      if (metadata.at("protocol") != 1 || metadata.at("state") != "ready") {
        return Core::fail(Core::ErrorCode::InvalidState, "Workspace control session is not ready");
      }

      const auto name = std::string(id) + ".json";
      const auto destination = std::filesystem::path{"requests"} / name;
      const auto response = std::filesystem::path{"responses"} / name;
      auto resolved = files->resolve(destination);

      if (!resolved) {
        return std::unexpected(resolved.error());
      }

      if (std::filesystem::exists(*resolved)) {
        auto old = files->readText(destination, 1024 * 1024);

        if (!old || *old != request) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Workspace request ID already has different content");
        }
      } else {
        std::size_t entries = 0;

        for ([[maybe_unused]] const auto& entry :
             std::filesystem::directory_iterator(control / "requests")) {
          if (++entries >= 4096) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Workspace session request limit exceeded");
          }
        }

        auto saved = files->writeTextAtomic(destination, request);

        if (!saved) {
          return std::unexpected(saved.error());
        }
      }

      return files->resolve(response);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Submit workspace request: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor
