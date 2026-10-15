module;

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
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
import Dot.Editor.MaterialDocument;
import Dot.Editor.AssetPreview;
import Dot.Assets.Image;
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
import Dot.Editor.TransformTool;
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
    std::optional<MaterialDocument> m_material;
    Json m_materialForm;
    std::string m_materialPreviewKey;

    struct PreviewEntry {
      std::optional<Graphics::Texture> texture;
      std::optional<Graphics::Image> image;
      std::string error;
      std::string warning;
      std::uint64_t used{};
    };

    std::optional<AssetPreview> m_assetPreviewer;
    std::map<std::string, PreviewEntry> m_thumbnails;
    std::deque<std::string> m_thumbnailQueue;
    PreviewEntry m_materialSample;
    std::string m_materialSampleKey;
    std::string m_previewServiceError;
    std::uint64_t m_previewClock{};
    std::uint64_t m_previewJobs{};
    bool m_assetGrid{true};
    std::string m_assetDirectory{"assets"};
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
    enum class Tool { Select, Move, Rotate, Scale };
    Tool m_tool{Tool::Select};
    bool m_localTool{};
    float m_toolSnap{};
    std::optional<TransformGesture> m_transform;
    std::optional<AxisDrag> m_axisDrag;
    std::optional<RotationDrag> m_rotationDrag;
    Math::Mat4 m_rotationFrame;
    float m_handleLength{};
    float m_transformValue{};
    Math::Mat4 m_transformStart;
    int m_transformAxis{};
    bool m_pointerTransform{};
    std::string m_transformError;
    Json m_toolGeometry = Json::array();
    bool m_lightTheme{};
    bool m_resetLayout{true};
    std::uint64_t m_layoutGeneration{};
    Json m_panelGeometry = Json::object();
    std::optional<Json> m_dockRequest;
    std::string m_layoutError;
    std::string m_hierarchyFilter;
    std::string m_assetFilter;
    bool m_viewVisible{true};
    Json m_visibleHierarchy = Json::array();
    Json m_visibleResources = Json::array();
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
      if (m_material) {
        const auto material = m_material->snapshot().value();
        return material.dirty
               || m_materialForm
                      != Codec::parseJson(Assets::encodeMaterial(material.value).value());
      }

      if (!m_text) {
        return false;
      }

      const auto file = m_text->snapshot().value();
      return file.dirty;
    }

    auto materialStatus() const -> Json {
      if (!m_material) {
        return nullptr;
      }

      const auto state = m_material->snapshot().value();
      return {{"buffer", std::to_string(m_buffer)},
              {"path", state.path},
              {"value", Codec::parseJson(Assets::encodeMaterial(state.value).value())},
              {"draft", m_materialForm},
              {"revision", std::to_string(state.revision)},
              {"dirty", pendingText()},
              {"canUndo", state.canUndo},
              {"canRedo", state.canRedo}};
    }

    auto previewStatus() const -> Json {
      Json entries = Json::array();
      const bool currentMaterial
          = m_material && m_materialSampleKey == std::to_string(m_buffer) + m_materialForm.dump();

      for (const auto& [path, entry] : m_thumbnails) {
        entries.push_back({{"path", path},
                           {"ready", entry.texture.has_value()},
                           {"error", entry.error},
                           {"warning", entry.warning}});
      }

      return {{"entries", entries},
              {"pending", m_thumbnailQueue.size()},
              {"jobs", m_previewJobs},
              {"device", m_assetPreviewer.has_value()},
              {"serviceError", m_previewServiceError},
              {"materialReady", currentMaterial && m_materialSample.texture.has_value()},
              {"materialPending", m_material.has_value() && !currentMaterial},
              {"materialError", currentMaterial ? m_materialSample.error : std::string{}},
              {"layout", m_assetGrid ? "grid" : "list"},
              {"directory", m_assetDirectory}};
    }

    auto clearPreviews() -> void {
      m_thumbnails.clear();
      m_thumbnailQueue.clear();
      m_materialSample = {};
      m_materialSampleKey.clear();
      m_previewServiceError.clear();
    }

    auto requestPreview(const std::string& path) -> void {
      if (!assetPreviewKind(path)) {
        return;
      }

      auto found = m_thumbnails.find(path);

      if (found != m_thumbnails.end()) {
        found->second.used = ++m_previewClock;
        return;
      }

      if (m_thumbnails.size() == 64) {
        auto oldest = std::ranges::min_element(m_thumbnails, {},
                                               [](const auto& item) { return item.second.used; });
        const auto evicted = oldest->first;
        m_thumbnails.erase(oldest);
        std::erase(m_thumbnailQueue, evicted);
      }

      PreviewEntry pending;
      pending.used = ++m_previewClock;
      m_thumbnails.emplace(path, std::move(pending));
      m_thumbnailQueue.push_back(path);
    }

    auto generatePreview(const std::string& path, std::uint32_t size,
                         const Assets::MaterialDescription* material = nullptr) -> PreviewEntry {
      ++m_previewJobs;

      if (!m_assetPreviewer && m_previewServiceError.empty()) {
        auto location = resource("environments");
        auto files = location ? Core::FileSystem::open(*location)
                              : Core::Result<Core::FileSystem>{std::unexpected(location.error())};
        auto hdr = files ? Assets::loadHdrImage(*files, "studio_small_09_1k.hdr")
                         : Core::Result<Graphics::HdrImage>{std::unexpected(files.error())};
        auto preview = hdr ? AssetPreview::create(m_project->files().value(), &*hdr)
                           : Core::Result<AssetPreview>{std::unexpected(hdr.error())};

        if (preview) {
          m_assetPreviewer.emplace(std::move(*preview));
        } else {
          m_previewServiceError = preview.error().message;
        }
      }

      PreviewEntry entry;

      if (!m_assetPreviewer) {
        entry.error = m_previewServiceError;
        return entry;
      }

      auto rendered = m_assetPreviewer->render(path, size, material);

      if (!rendered) {
        entry.error = rendered.error().message;
        return entry;
      }

      auto texture
          = m_renderer->uploadTexture(rendered->image, {.colorSpace = Graphics::ColorSpace::Linear,
                                                        .addressU = Graphics::TextureAddress::Clamp,
                                                        .addressV = Graphics::TextureAddress::Clamp,
                                                        .mipmaps = false});

      if (!texture) {
        entry.error = texture.error().message;
        return entry;
      }

      entry.texture = std::move(*texture);
      entry.image = std::move(rendered->image);
      entry.warning = std::move(rendered->warning);
      return entry;
    }

    /** One synchronous preview job per editor frame, prioritized for the material
     * form. Cache at most 64 128px thumbnails plus one 256px material sphere.
     * 每编辑器帧至多一个同步预览任务，优先材质表单；缓存至多 64 张 128px 缩略图及
     * 一张 256px 材质球，CPU/GPU 图像各自至多约 4.25 MiB。 */
    auto updatePreviews() -> void {
      if (playing()) {
        return;
      }

      if (m_material) {
        const auto key = std::to_string(m_buffer) + m_materialForm.dump();

        if (key != m_materialSampleKey) {
          auto value = Assets::decodeMaterial(m_materialForm.dump());
          m_materialSample = {};

          if (value) {
            m_materialSample = generatePreview(m_material->snapshot()->path, 256, &*value);
          } else {
            m_materialSample.error = value.error().message;
          }

          m_materialSampleKey = key;
          return;
        }
      }

      if (!m_thumbnailQueue.empty()) {
        auto path = std::move(m_thumbnailQueue.front());
        m_thumbnailQueue.pop_front();
        auto found = m_thumbnails.find(path);

        if (found != m_thumbnails.end()) {
          const auto used = found->second.used;
          found->second = generatePreview(path, 128);
          found->second.used = used;
        }
      }
    }

    auto resetMaterialForm() -> void {
      m_materialForm
          = m_material
                ? Codec::parseJson(Assets::encodeMaterial(m_material->snapshot()->value).value())
                : Json(nullptr);

      if (!m_material) {
        m_materialSample = {};
        m_materialSampleKey.clear();
      }

      m_renderedView.reset();
      m_previewReady = false;
    }

    auto saveMaterial() -> Core::Result<void> {
      auto value = Assets::decodeMaterial(m_materialForm.dump());

      if (!value) {
        return std::unexpected(value.error());
      }

      auto edited = m_material->replace(*value, m_material->snapshot()->revision);

      if (!edited) {
        return edited;
      }

      resetMaterialForm();
      auto saved = m_material->save(m_material->snapshot()->revision);

      if (!saved) {
        return saved;
      }

      m_assetRefresh = true;
      return {};
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

      if (m_transform && command != "inspect" && command != "update-transform"
          && command != "finish-transform" && command != "close" && command != "set-theme"
          && command != "filter-panel") {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Finish or cancel the transform gesture first");
      }

      if (command == "set-tool") {
        Codec::requireFields(parsed, {"command", "tool", "space", "snap"});
        const auto tool = parsed["tool"].get<std::string>();
        const auto space = parsed["space"].get<std::string>();
        const auto snap = Codec::number(parsed["snap"]);

        if ((tool != "select" && tool != "move" && tool != "rotate" && tool != "scale")
            || (space != "local" && space != "world") || snap < 0
            || snap > (tool == "rotate"  ? 360
                       : tool == "scale" ? 10
                                         : 1000)
            || playing()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid transform tool configuration");
        }

        m_tool = tool == "move"     ? Tool::Move
                 : tool == "rotate" ? Tool::Rotate
                 : tool == "scale"  ? Tool::Scale
                                    : Tool::Select;
        m_localTool = space == "local";
        m_toolSnap = snap;
        m_toolGeometry = Json::array();
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "begin-transform" || command == "update-transform"
          || command == "finish-transform") {
        if (command == "begin-transform") {
          Codec::requireFields(parsed, {"command", "revision", "axis", "uv"});
        } else {
          Codec::requireFields(
              parsed, {"command", "revision", command == "update-transform" ? "uv" : "commit"});
        }

        const auto revision = Codec::id(parsed["revision"]);

        if (revision != m_document->stats()->revision
            || (m_transform && revision != m_transform->revision())) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Transform request has a stale revision");
        }

        Core::Result<void> applied;

        if (command == "finish-transform") {
          applied = finishTransform(parsed["commit"].get<bool>());
        } else {
          const auto uv = Codec::numbers<2>(parsed["uv"]);

          if (command == "begin-transform") {
            if (!parsed["axis"].is_number_integer()) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Move axis must be an integer");
            }

            const auto axis = parsed["axis"].get<std::int64_t>();

            if (axis < 0 || axis > 2) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Move axis must be 0..2");
            }

            applied = beginTransform(static_cast<int>(axis), {uv[0], uv[1]}, revision);
          } else {
            applied = updateTransform({uv[0], uv[1]});
          }
        }

        return applied ? execute("{\"command\":\"inspect\"}") : std::unexpected(applied.error());
      }

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

      if (command == "clear-asset-selection") {
        Codec::requireFields(parsed, {"command"});

        if (m_material && pendingText()) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Save or discard the current material first");
        }

        m_material.reset();
        resetMaterialForm();
        m_selectedResource.clear();
        return Json{{"ok", true}, {"selected", nullptr}}.dump();
      }

      if (command == "preview-assets" || command == "refresh-previews") {
        Codec::requireFields(parsed, {"command"});

        if (command == "refresh-previews") {
          auto refreshed = catalog();

          if (!refreshed) {
            return std::unexpected(refreshed.error());
          }

          clearPreviews();
        }

        return Json{{"ok", true}, {"previews", previewStatus()}}.dump();
      }

      if (command == "request-preview" || command == "select-asset"
          || command == "set-asset-directory") {
        Codec::requireFields(parsed, {"command", "path"});
        const auto path = parsed["path"].get<std::string>();
        const auto found = std::ranges::find(m_catalog, path, [](const auto& entry) {
          return entry["path"].template get<std::string>();
        });

        if (found == m_catalog.end() && !(command == "set-asset-directory" && path.empty())) {
          return Core::fail(Core::ErrorCode::NotFound,
                            "Asset is not in the current project catalog");
        }

        if (command == "set-asset-directory") {
          if (!path.empty() && (*found)["type"] != "directory") {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Asset directory is not a directory");
          }

          m_assetDirectory = path;
          m_assetFilter.clear();
        } else if (command == "select-asset") {
          if ((*found)["type"] == "directory") {
            return execute(Json{{"command", "set-asset-directory"}, {"path", path}}.dump());
          }

          if (m_selectedResource == path && m_material) {
            return Json{{"ok", true}, {"selected", path}}.dump();
          }

          if (m_material && pendingText()) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Save or discard the current material first");
          }

          if (path.ends_with(".dotmaterial")) {
            return execute(
                Json{{"command", "open-material"}, {"path", path}, {"discard", false}}.dump());
          }

          m_material.reset();
          resetMaterialForm();
          m_selectedResource = path;
          m_requestedPane = "inspector";
          requestPreview(path);
        } else {
          if (!assetPreviewKind(path)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Asset has no implemented preview type");
          }

          requestPreview(path);
        }

        return Json{{"ok", true}, {"previews", previewStatus()}}.dump();
      }

      if (command == "set-assets-view") {
        Codec::requireFields(parsed, {"command", "mode"});
        const auto mode = parsed["mode"].get<std::string>();

        if (mode != "grid" && mode != "list") {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown asset view mode");
        }

        m_assetGrid = mode == "grid";
        return Json{{"ok", true}, {"previews", previewStatus()}}.dump();
      }

      if (command == "create-material") {
        Codec::requireFields(parsed, {"command", "path"});
        const auto name = parsed["path"].get<std::string>();

        if (playing() || !name.starts_with("assets/") || !name.ends_with(".dotmaterial")) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Expected assets/*.dotmaterial while stopped");
        }

        auto created = m_resources->createText(name, Assets::encodeMaterial({}).value());

        if (!created) {
          return std::unexpected(created.error());
        }

        auto refreshed = catalog();

        if (!refreshed) {
          return Core::fail(refreshed.error().code,
                            "Material created; " + refreshed.error().message);
        }

        return Json{{"ok", true}, {"path", name}}.dump();
      }

      if (command == "open-material") {
        Codec::requireFields(parsed, {"command", "path", "discard"});

        if (playing() || pendingInspector() || pendingEnvironment() || pendingOcclusion()
            || (pendingText() && !parsed["discard"].get<bool>())) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Finish scene/file drafts before opening a material");
        }

        if (m_buffer == std::numeric_limits<std::uint64_t>::max() - 1) {
          return Core::fail(Core::ErrorCode::InvalidState, "Resource buffer identities exhausted");
        }

        const auto name = parsed["path"].get<std::string>();
        auto opened = MaterialDocument::open(*m_project, name);

        if (!opened) {
          return std::unexpected(opened.error());
        }

        m_material.emplace(std::move(*opened));
        m_text.reset();
        ++m_buffer;
        m_selectedResource = name;
        m_requestedPane = "inspector";
        resetMaterialForm();
        return Json{{"ok", true}, {"material", materialStatus()}}.dump();
      }

      if (command == "edit-material" || command == "stage-material" || command == "undo-material"
          || command == "redo-material" || command == "save-material" || command == "close-material"
          || command == "reset-material") {
        if (command == "edit-material" || command == "stage-material") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "value"});
        } else if (command == "close-material") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "discard"});
        } else {
          Codec::requireFields(parsed, {"command", "buffer", "revision"});
        }

        if (playing() || !m_material || Codec::id(parsed["buffer"]) != m_buffer
            || Codec::id(parsed["revision"]) != m_material->snapshot()->revision) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Material buffer/revision is stale or Play is active");
        }

        if (command == "close-material") {
          if (pendingText() && !parsed["discard"].get<bool>()) {
            return Core::fail(Core::ErrorCode::InvalidState, "Material has unsaved changes");
          }

          m_material.reset();
        } else if (command == "stage-material" || command == "edit-material") {
          auto value = Assets::decodeMaterial(parsed["value"].dump());

          if (!value) {
            return std::unexpected(value.error());
          }

          if (command == "stage-material") {
            m_materialForm = parsed["value"];
            m_renderedView.reset();
            m_previewReady = false;
            return Json{{"ok", true}, {"material", materialStatus()}}.dump();
          }

          auto changed = m_material->replace(*value, m_material->snapshot()->revision);

          if (!changed) {
            return std::unexpected(changed.error());
          }
        } else if (command == "save-material") {
          auto saved = saveMaterial();

          if (!saved) {
            return std::unexpected(saved.error());
          }
        } else if (command == "undo-material" || command == "redo-material") {
          const auto state = materialStatus();

          if (state["draft"] != state["value"]) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Apply or reset staged material fields first");
          }

          auto changed = command == "undo-material"
                             ? m_material->undo(m_material->snapshot()->revision)
                             : m_material->redo(m_material->snapshot()->revision);

          if (!changed) {
            return std::unexpected(changed.error());
          }
        }

        resetMaterialForm();
        return Json{{"ok", true}, {"material", materialStatus()}}.dump();
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
                    {"material", materialStatus()},
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

      if (command == "import-model") {
        Codec::requireFields(parsed, {"command", "path", "source"}, {"scene"});
        const auto name = parsed["path"].get<std::string>();
        const auto source = parsed["source"].get<std::string>();
        std::optional<std::uint32_t> scene;

        if (parsed.contains("scene")) {
          if (!parsed["scene"].is_number_unsigned()
              || parsed["scene"].get<std::uint64_t>() > 1023) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Model scene index must be 0..1023");
          }

          scene = parsed["scene"].get<std::uint32_t>();
        }

        auto result = m_resources->importModel(
            std::filesystem::path(std::u8string(source.begin(), source.end())), name, scene);

        if (!result) {
          return std::unexpected(result.error());
        }

        m_assetRefresh = true;
        auto refreshed = catalog();

        if (!refreshed) {
          return Core::fail(
              refreshed.error().code,
              "Model bundle committed; catalog refresh failed: " + refreshed.error().message);
        }

        m_selectedResource = result->scene;
        return Json{{"ok", true},
                    {"scene", result->scene},
                    {"meshes", result->meshes},
                    {"materials", result->materials},
                    {"images", result->images},
                    {"nodes", result->nodes},
                    {"sourceScene", result->sourceScene},
                    {"warnings", result->warnings}}
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
        m_material.reset();
        resetMaterialForm();
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
          auto savedText = m_material ? saveMaterial() : saveText();

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

      if (command == "filter-panel") {
        Codec::requireFields(parsed, {"command", "panel", "query"});
        const auto panel = parsed["panel"].get<std::string>();
        const auto query = parsed["query"].get<std::string>();

        if ((panel != "hierarchy" && panel != "resources") || query.size() > 128
            || query.find('\0') != std::string::npos || !Core::isValidUtf8(query)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid panel search");
        }

        (panel == "hierarchy" ? m_hierarchyFilter : m_assetFilter) = query;
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "set-theme") {
        Codec::requireFields(parsed, {"command", "theme"});
        const auto theme = parsed["theme"].get<std::string>();

        if (theme != "dark" && theme != "light") {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown editor theme");
        }

        m_lightTheme = theme == "light";
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "dock-panel") {
        Codec::requireFields(parsed, {"command", "panel", "target", "placement", "ratio"});
        const auto panel = parsed["panel"].get<std::string>();
        const auto target = parsed["target"].get<std::string>();
        const auto placement = parsed["placement"].get<std::string>();
        const auto ratio = Codec::number(parsed["ratio"]);

        if (!m_panelGeometry.contains(panel) || !m_panelGeometry.contains(target) || panel == target
            || !m_panelGeometry[target]["docked"].get<bool>()
            || (placement != "tab" && placement != "left" && placement != "right"
                && placement != "up" && placement != "down")
            || ratio < 0.1F || ratio > 0.9F) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid docked panel placement");
        }

        if (m_resetLayout || m_dockRequest
            || m_layoutGeneration == std::numeric_limits<std::uint64_t>::max()) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Layout operation is pending or exhausted");
        }

        m_dockRequest = parsed;
        m_layoutError.clear();
        auto response = Codec::parseJson(execute("{\"command\":\"inspect\"}").value());
        response["queued"] = true;
        return response.dump();
      }

      if (command == "reset-layout") {
        Codec::requireFields(parsed, {"command"});

        if (m_dockRequest) {
          return Core::fail(Core::ErrorCode::InvalidState, "Wait for the queued docking operation");
        }

        if (m_layoutGeneration == std::numeric_limits<std::uint64_t>::max()) {
          return Core::fail(Core::ErrorCode::Unavailable, "Layout generation exhausted");
        }

        m_resetLayout = true;
        m_layoutError.clear();
        return execute("{\"command\":\"inspect\"}");
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

        if (pane != "scene" && pane != "environment" && pane != "rendering" && pane != "resources"
            && pane != "inspector" && pane != "viewport" && pane != "console") {
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
             || pendingOcclusion() || pendingText() || m_transform)
            && !discard) {
          m_pendingClose = true;
        } else {
          m_pendingClose = false;
          m_transform.reset();
          m_axisDrag.reset();
          m_rotationDrag.reset();
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

      if (result && command == "select" && !m_material) {
        m_selectedResource.clear();
      }

      auto updated = refresh();

      if (!updated) {
        return Core::fail(updated.error().code,
                          "Document response may have committed; " + updated.error().message);
      }

      if (result && command == "inspect") {
        auto response = Codec::parseJson(*result);
        response["workspace"] = {
            {"pendingInspector", pendingInspector()},
            {"pendingEnvironment", pendingEnvironment()},
            {"environmentDraft", m_environmentDraft},
            {"pendingOcclusion", pendingOcclusion()},
            {"occlusionDraft", m_occlusionDraft},
            {"pane", m_pane},
            {"theme", m_lightTheme ? "light" : "dark"},
            {"tool", toolStatus()},
            {"filters", {{"hierarchy", m_hierarchyFilter}, {"resources", m_assetFilter}}},
            {"displayed", {{"hierarchy", m_visibleHierarchy}, {"resources", m_visibleResources}}},
            {"layout",
             {{"pending", m_resetLayout || m_dockRequest.has_value()},
              {"error", m_layoutError},
              {"generation", std::to_string(m_layoutGeneration)},
              {"panels", m_panelGeometry}}},
            {"requestedPane", m_requestedPane},
            {"pendingClose", m_pendingClose},
            {"running", m_application->running()},
            {"inspector", m_edit},
            {"propertiesText", m_propertiesText},
            {"propertiesInvalid", m_propertiesInvalid},
            {"play", playStatus()},
            {"view", viewStatus()},
            {"message", m_message},
            {"material", materialStatus()},
            {"previews", previewStatus()},
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

    auto sessionAction(const Json& request) -> bool {
      auto result = safeExecute(request.dump());
      m_message = result ? "" : result.error().message;
      return result.has_value();
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
      const bool ready = current && m_viewVisible && m_previewReady && m_renderedView
                         && *current == *m_renderedView
                         && m_renderedRevision == std::to_string(m_document->stats()->revision)
                         && !m_assetRefresh;
      auto stats = m_renderer->visibilityStats();
      Json draws = nullptr;

      if (!stats) {
        draws = errorJson(stats.error());
      } else if (stats->mesh) {
        const auto& frame = *stats->mesh;
        draws = {{"input", frame.inputInstances},
                 {"visible", frame.visibleInstances},
                 {"culled", frame.culledInstances},
                 {"shadow", frame.shadowInstances},
                 {"occlusion", frame.occlusionInstances},
                 {"colorDraws", frame.colorDraws},
                 {"solid", frame.solidInstances},
                 {"blended", frame.blendedInstances},
                 {"solidDraws", frame.solidDraws},
                 {"blendedDraws", frame.blendedDraws},
                 {"shadowDraws", frame.shadowDraws},
                 {"occlusionDraws", frame.occlusionDraws},
                 {"spatial",
                  {{"indexed", frame.spatial.indexedInstances},
                   {"nodes", frame.spatial.nodeTests},
                   {"bounds", frame.spatial.boundsTests},
                   {"exact", frame.spatial.exactTests},
                   {"unindexed", frame.spatial.unindexedTests},
                   {"changed", frame.spatial.changedBounds},
                   {"refitted", frame.spatial.refittedNodes},
                   {"rebuilt", frame.spatial.rebuilt}}}};
      }

      return {
          {"mode", playing() ? "play" : (m_editorView ? "editor" : "game")},
          {"ready", ready},
          {"draws", std::move(draws)},
          {"textInput", m_renderer->guiStats().value().text},
          {"source", playing()     ? "play"
                     : m_transform ? "transform"
                                   : "document"},
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

    auto preview(std::uint32_t width, std::uint32_t height) -> Core::Result<void> {
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

      auto scene = playing()     ? m_play->scene()
                   : m_transform ? m_transform->scene()
                                 : m_document->scene();

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto view = currentView();

      if (!view) {
        return std::unexpected(view.error());
      }

      const auto previewKey = m_material && !playing()
                                  ? std::to_string(m_buffer) + m_materialForm.dump()
                                  : std::string{};

      if (previewKey != m_materialPreviewKey) {
        std::optional<Graphics::MaterialPreview> material;

        if (m_material && !playing()) {
          auto value = Assets::decodeMaterial(m_materialForm.dump());

          if (!value) {
            m_previewReady = false;
            m_previewError = value.error().message;
            return {};
          }

          material = Graphics::MaterialPreview{m_material->snapshot()->path, std::move(*value)};
        }

        auto changed = m_drawing->previewMaterial(std::move(material));

        if (!changed) {
          m_previewReady = false;
          m_previewError = changed.error().message;
          return {};
        }

        m_materialPreviewKey = previewKey;
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

    /** ASCII case-insensitive search; UTF-8 names retain exact byte matching.
     * ASCII 忽略大小写，UTF-8 名称保持精确匹配；保留匹配子节点的祖先。 */
    static auto matchesFilter(std::string_view text, std::string_view query) -> bool {
      const auto folded = [](char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
      };
      return std::ranges::search(text, query,
                                 [&](char a, char b) { return folded(a) == folded(b); })
                 .size()
             == query.size();
    }

    auto matchesHierarchy(const Json& entities, const Json& entity, int depth) const -> bool {
      if (matchesFilter(entity["name"].get_ref<const std::string&>(), m_hierarchyFilter)) {
        return true;
      }

      if (depth >= 256) {
        return false;
      }

      return std::ranges::any_of(entities, [&](const auto& child) {
        return child["parent"] == entity["id"] && matchesHierarchy(entities, child, depth + 1);
      });
    }

    auto tree(const Json& entities, const Json& parent, int depth = 0) -> void {
      if (depth > 256) {
        return;
      }

      for (const auto& entity : entities) {
        if (entity["parent"] != parent) {
          continue;
        }

        if (!matchesHierarchy(entities, entity, depth)) {
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
        m_visibleHierarchy.push_back(id);

        if (!m_hierarchyFilter.empty() && children) {
          ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }

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

          if (std::string_view(component) == "material") {
            ImGui::BeginDisabled(pendingInspector());

            if (ImGui::SmallButton("Edit material asset")) {
              fileAction({{"command", "open-material"},
                          {"path", m_edit[component]["asset"]},
                          {"discard", false}});
            }

            ImGui::EndDisabled();
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
      const auto command = request["command"].get<std::string>();

      if (m_material && command != "open-material" && command != "create-material"
          && command.ends_with("-material")) {
        request["buffer"] = std::to_string(m_buffer);
        request["revision"] = std::to_string(m_material->snapshot()->revision);
      }

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

    /** Structured asset authoring shares the strict material codec and commands;
     * staged fields preview without file writes, Apply creates one history step.
     * 结构化资源编辑共用严格材质编解码与命令；字段草稿不写盘即可预览，应用形成一次历史。 */
    auto materialInspector() -> void {
      const auto state = materialStatus();
      ImGui::TextWrapped("Material: %s%s", state["path"].get_ref<const std::string&>().c_str(),
                         state["dirty"].get<bool>() ? " *" : "");
      ImGui::TextWrapped("Shared asset. Unsaved fields preview on all matching scene objects.");

      if (m_materialSample.texture) {
        const float side = std::min(160.F, ImGui::GetContentRegionAvail().x);
        auto shown = m_renderer->guiImage(*m_materialSample.texture, side, side);

        if (!shown) {
          m_message = shown.error().message;
        }
      } else {
        ImGui::TextWrapped("%s", m_materialSample.error.empty() ? "Generating material preview..."
                                                                : m_materialSample.error.c_str());
      }
      auto& form = m_materialForm;
      bool changed{};

      const auto row = [](const char* title) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextWrapped("%s", title);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
      };

      if (ImGui::BeginTable("material-properties", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 105);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        row("Shading");

        if (ImGui::BeginCombo("##shading", form["model"] == "unlit" ? "Unlit" : "PBR")) {
          for (const auto* name : {"metallicRoughness", "unlit"}) {
            if (ImGui::Selectable(std::string_view(name) == "unlit" ? "Unlit" : "PBR",
                                  form["model"] == name)) {
              form["model"] = name;
              changed = true;
            }
          }

          ImGui::EndCombo();
        }

        auto base = Codec::numbers<4>(form["baseColor"]);

        row("Base color");

        if (ImGui::ColorEdit4("##base-color", base.data())) {
          form["baseColor"] = base;
          changed = true;
        }

        auto emission = Codec::numbers<3>(form["emissive"]);

        row("Emission");

        if (ImGui::ColorEdit3("##emission", emission.data(),
                              ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)) {
          form["emissive"] = emission;
          changed = true;
        }

        const auto scalar = [&](const char* title, const char* key, float lower, float upper) {
          auto value = form[key].get<float>();

          row(title);

          if (ImGui::SliderFloat((std::string("##") + key).c_str(), &value, lower, upper)) {
            form[key] = value;
            changed = true;
          }
        };
        scalar("Metallic", "metallic", 0, 1);
        scalar("Roughness", "roughness", 0, 1);
        scalar("Normal scale", "normalScale", 0, 4);
        scalar("Baked occlusion", "occlusionStrength", 0, 1);
        bool sided = form["doubleSided"].get<bool>();

        row("Double sided");

        if (ImGui::Checkbox("##sided", &sided)) {
          form["doubleSided"] = sided;
          changed = true;
        }

        row("Coverage");

        if (ImGui::BeginCombo("##coverage",
                              form["alphaMode"].get_ref<const std::string&>().c_str())) {
          for (const auto* mode : {"opaque", "mask", "blend"}) {
            if (ImGui::Selectable(mode, form["alphaMode"] == mode)) {
              form["alphaMode"] = mode;
              changed = true;
            }
          }

          ImGui::EndCombo();
        }

        if (form["alphaMode"] == "mask") {
          auto cutoff = form["alphaCutoff"].get<float>();

          row("Alpha cutoff");

          if (ImGui::DragFloat("##cutoff", &cutoff, .01F, 0, 16, "%.3f",
                               ImGuiSliderFlags_AlwaysClamp)) {
            form["alphaCutoff"] = cutoff;
            changed = true;
          }
        }

        ImGui::EndTable();
      }

      ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));

      for (const auto& [title, key, data] :
           {std::tuple{"Base color texture", "baseColorTexture", false},
            std::tuple{"Metallic / roughness texture", "metallicRoughnessTexture", true},
            std::tuple{"Normal texture", "normalTexture", true},
            std::tuple{"Emission texture", "emissiveTexture", false},
            std::tuple{"Occlusion texture", "occlusionTexture", true}}) {
        ImGui::PushID(key);

        if (ImGui::CollapsingHeader(title)) {
          bool enabled = !form[key].is_null();

          if (ImGui::Checkbox("Use texture", &enabled)) {
            Assets::MaterialDescription defaults;
            defaults.baseColorTexture = Assets::TextureSource{"assets/texture.png"};
            form[key] = enabled ? Codec::parseJson(
                                      Assets::encodeMaterial(defaults).value())["baseColorTexture"]
                                : Json(nullptr);

            if (enabled && data) {
              form[key]["sampling"]["colorSpace"] = "linear";
            }

            changed = true;
          }

          if (enabled) {
            auto path = form[key]["path"].get<std::string>();

            ImGui::TextUnformatted("Project image");
            ImGui::SetNextItemWidth(-1);

            if (ImGui::InputText("##image-path", &path)) {
              form[key]["path"] = path;
              changed = true;
            }

            ImGui::SetNextItemWidth(-1);

            if (ImGui::BeginCombo("##image-choice", "Choose project image")) {
              for (const auto& entry : m_catalog) {
                const auto candidate = entry["path"].get<std::string>();

                if (candidate.ends_with(".png") || candidate.ends_with(".jpg")
                    || candidate.ends_with(".jpeg")) {
                  if (ImGui::Selectable(candidate.c_str(), path == candidate)) {
                    form[key]["path"] = candidate;
                    changed = true;
                  }
                }
              }

              ImGui::EndCombo();
            }

            if (ImGui::BeginTable("sampling", 2, ImGuiTableFlags_SizingStretchProp)) {
              ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 105);
              ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
              auto& sampling = form[key]["sampling"];
              const auto choice = [&](const char* title, const char* field,
                                      std::initializer_list<const char*> options) {
                row(title);

                if (ImGui::BeginCombo((std::string("##") + field).c_str(),
                                      sampling[field].get_ref<const std::string&>().c_str())) {
                  for (const auto* option : options) {
                    if (ImGui::Selectable(option, sampling[field] == option)) {
                      sampling[field] = option;
                      changed = true;
                    }
                  }

                  ImGui::EndCombo();
                }
              };
              ImGui::BeginDisabled(data);
              choice("Color space", "colorSpace", {"srgb", "linear"});
              ImGui::EndDisabled();
              choice("Filtering", "filter", {"nearest", "linear"});
              choice("Address U", "addressU", {"repeat", "clamp", "mirror"});
              choice("Address V", "addressV", {"repeat", "clamp", "mirror"});
              auto mipmaps = sampling["mipmaps"].get<bool>();
              auto anisotropy = sampling["maxAnisotropy"].get<int>();

              row("Mipmaps");

              if (ImGui::Checkbox("##mipmaps", &mipmaps)) {
                sampling["mipmaps"] = mipmaps;
                changed = true;
              }

              row("Anisotropy");

              if (ImGui::SliderInt("##anisotropy", &anisotropy, 1, 16)) {
                sampling["maxAnisotropy"] = anisotropy;
                changed = true;
              }
              ImGui::EndTable();
            }
          }
        }

        ImGui::PopID();
      }

      ImGui::PopStyleColor();

      if (changed && form != state["draft"]) {
        m_renderedView.reset();
        m_previewReady = false;
      }

      ImGui::Separator();
      const bool staged = form != state["value"];
      ImGui::BeginDisabled(!staged);

      if (ImGui::Button("Apply material")) {
        fileAction({{"command", "edit-material"}, {"value", form}});
      }

      ImGui::SameLine();

      if (ImGui::Button("Reset fields")) {
        fileAction({{"command", "reset-material"}});
      }

      ImGui::EndDisabled();
      ImGui::BeginDisabled(staged || !state["canUndo"].get<bool>());

      if (ImGui::Button("Undo material")) {
        fileAction({{"command", "undo-material"}});
      }

      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(staged || !state["canRedo"].get<bool>());

      if (ImGui::Button("Redo material")) {
        fileAction({{"command", "redo-material"}});
      }

      ImGui::EndDisabled();

      if (ImGui::Button("Save material")) {
        fileAction({{"command", "save-material"}});
      }

      ImGui::SameLine();

      if (ImGui::Button("Close material")) {
        fileAction({{"command", "close-material"}, {"discard", false}});
      }

      if (state["dirty"].get<bool>() && ImGui::Button("Discard material changes")) {
        fileAction({{"command", "close-material"}, {"discard", true}});
      }
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
      auto stats = m_renderer->visibilityStats();

      if (!stats) {
        ImGui::TextWrapped("Draw statistics: %s", stats.error().message.c_str());
      } else if (stats->mesh) {
        const auto& frame = *stats->mesh;
        ImGui::TextWrapped("Last mesh instances: %zu visible / %zu input, %zu culled",
                           frame.visibleInstances, frame.inputInstances, frame.culledInstances);
        ImGui::TextWrapped("Mesh instances: shadows %zu, SSAO depth %zu", frame.shadowInstances,
                           frame.occlusionInstances);
        ImGui::TextWrapped("GPU draws: color %zu, shadows %zu, SSAO %zu", frame.colorDraws,
                           frame.shadowDraws, frame.occlusionDraws);
        ImGui::TextWrapped("Solid / blended instances: %zu / %zu; GPU draws: %zu / %zu",
                           frame.solidInstances, frame.blendedInstances, frame.solidDraws,
                           frame.blendedDraws);
        ImGui::TextWrapped("Visibility tests: %zu BVH nodes, %zu bounds, %zu exact (%zu unindexed)",
                           frame.spatial.nodeTests, frame.spatial.boundsTests,
                           frame.spatial.exactTests, frame.spatial.unindexedTests);
      } else {
        ImGui::TextUnformatted("No successful mesh submission yet.");
      }

      ImGui::Separator();
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

    auto searchField(const char* id, const char* hint, const char* panel,
                     const std::string& current) -> void {
      auto text = current;

      if (ImGui::InputTextWithHint(id, hint, &text)) {
        sessionAction({{"command", "filter-panel"}, {"panel", panel}, {"query", text}});
      }
    }

    auto resourcesPanel() -> void {
      ImGui::SetNextItemWidth(std::max(100.F, ImGui::GetContentRegionAvail().x - 100));
      searchField("##asset-search", "Search project assets", "resources", m_assetFilter);
      ImGui::SameLine();

      if (ImGui::Button("Actions")) {
        ImGui::OpenPopup("Asset actions");
      }

      if (ImGui::BeginPopup("Asset actions")) {
        if (ImGui::Button("Refresh files")) {
          fileAction({{"command", "refresh-previews"}});
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

        if (ImGui::Button("New material")) {
          if (fileAction({{"command", "create-material"}, {"path", m_newPath}})) {
            fileAction({{"command", "open-material"}, {"path", m_newPath}, {"discard", false}});
          }
        }

        ImGui::InputText("Import source", &m_importSource);

        if (ImGui::Button("Import to path")) {
          fileAction(
              {{"command", "import-resource"}, {"path", m_newPath}, {"source", m_importSource}});
        }

        if (ImGui::Button("Import glTF / GLB model")) {
          auto result = safeExecute(
              Json{{"command", "import-model"}, {"path", m_newPath}, {"source", m_importSource}}
                  .dump());
          m_message = result ? "" : result.error().message;
        }

        if (m_selectedResource.starts_with("assets/") && m_selectedResource.ends_with(".dotscene")
            && ImGui::Button("Add model to scene")) {
          edit({{{"op", "instantiate-model"}, {"path", m_selectedResource}}});
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

        ImGui::EndPopup();
      }

      if (ImGui::SmallButton(m_assetGrid ? "Grid *" : "Grid")) {
        fileAction({{"command", "set-assets-view"}, {"mode", "grid"}});
      }

      ImGui::SameLine();

      if (ImGui::SmallButton(!m_assetGrid ? "List *" : "List")) {
        fileAction({{"command", "set-assets-view"}, {"mode", "list"}});
      }

      ImGui::SameLine();

      if (ImGui::SmallButton("Project")) {
        fileAction({{"command", "set-asset-directory"}, {"path", ""}});
      }

      std::size_t offset{};

      while (offset < m_assetDirectory.size()) {
        auto end = m_assetDirectory.find('/', offset);
        end = end == std::string::npos ? m_assetDirectory.size() : end;
        const auto prefix = m_assetDirectory.substr(0, end);
        const auto segment = m_assetDirectory.substr(offset, end - offset);
        ImGui::SameLine();
        ImGui::TextUnformatted("/");
        ImGui::SameLine();

        if (ImGui::SmallButton(segment.c_str())) {
          fileAction({{"command", "set-asset-directory"}, {"path", prefix}});
          break;
        }

        offset = end + 1;
      }

      ImGui::Separator();
      Json entries = Json::array();
      m_visibleResources = Json::array();

      for (const auto& entry : m_catalog) {
        const auto name = entry["path"].get<std::string>();
        const auto slash = name.rfind('/');
        const auto parent = slash == std::string::npos ? std::string{} : name.substr(0, slash);

        if ((!m_assetFilter.empty() && !matchesFilter(name, m_assetFilter))
            || (m_assetFilter.empty() && parent != m_assetDirectory)) {
          continue;
        }

        entries.push_back(entry);
        m_visibleResources.push_back(name);
      }

      const auto select = [&](const Json& entry, bool doubleClick) {
        const auto path = entry["path"].get<std::string>();

        if (fileAction({{"command", "select-asset"}, {"path", path}}) && doubleClick
            && entry["type"] == "text" && !path.ends_with(".dotmaterial")) {
          fileAction({{"command", "open-external"}, {"path", path}});
        }
      };
      const int columns
          = m_assetGrid
                ? std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x / 104), 1, 12)
                : 1;
      const int rows = (static_cast<int>(entries.size()) + columns - 1) / columns;
      ImGuiListClipper clipper;

      if (ImGui::BeginTable("asset-grid", columns, ImGuiTableFlags_SizingStretchSame)) {
        clipper.Begin(rows, m_assetGrid ? 112.F : ImGui::GetFrameHeightWithSpacing());
        while (clipper.Step()) {
          for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            ImGui::TableNextRow(0, m_assetGrid ? 112.F : ImGui::GetFrameHeightWithSpacing());

            for (int column = 0; column < columns; ++column) {
              const int index = row * columns + column;

              if (index >= static_cast<int>(entries.size())) {
                break;
              }

              ImGui::TableSetColumnIndex(column);
              const auto entry = entries[static_cast<std::size_t>(index)];
              const auto path = entry["path"].get<std::string>();
              const auto slash = path.rfind('/');
              const auto name = slash == std::string::npos ? path : path.substr(slash + 1);
              const auto kind = assetPreviewKind(path);
              const char* type = entry["type"] == "directory" ? "Folder"
                                 : !kind ? (entry["type"] == "text" ? "Text" : "Asset")
                                 : *kind == AssetPreviewKind::Image ? "Image"
                                 : *kind == AssetPreviewKind::Mesh  ? "Mesh"
                                                                    : "Material";
              ImGui::PushID(path.c_str());
              requestPreview(path);
              auto preview = m_thumbnails.find(path);

              if (m_assetGrid) {
                if (preview != m_thumbnails.end() && preview->second.texture) {
                  auto shown = m_renderer->guiImage(*preview->second.texture, 64, 64);

                  if (!shown) {
                    m_message = shown.error().message;
                  }

                  if (ImGui::IsItemClicked()) {
                    select(entry, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
                  }
                } else if (ImGui::Button(
                               preview != m_thumbnails.end() && !preview->second.error.empty()
                                   ? "Error"
                                   : type,
                               {64, 64})) {
                  select(entry, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
                }
              }

              const auto dot = name.rfind('.');
              const auto caption
                  = m_assetGrid && entry["type"] != "directory" && dot != std::string::npos
                        ? name.substr(0, dot)
                        : name;

              if (ImGui::Selectable(caption.c_str(), m_selectedResource == path,
                                    ImGuiSelectableFlags_AllowDoubleClick,
                                    {0, m_assetGrid ? 22.F : 0.F})) {
                select(entry, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
              }

              if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(path.c_str());

                if (preview != m_thumbnails.end()) {
                  const auto& message = preview->second.error.empty() ? preview->second.warning
                                                                      : preview->second.error;
                  ImGui::TextWrapped("%s", message.c_str());
                }

                ImGui::EndTooltip();
              }

              if (m_assetGrid) {
                ImGui::TextDisabled("%s", type);
              }

              ImGui::PopID();
            }
          }
        }

        ImGui::EndTable();
      }
    }

    auto assetInspector() -> void {
      ImGui::TextWrapped("Asset: %s", m_selectedResource.c_str());
      requestPreview(m_selectedResource);
      const auto found = m_thumbnails.find(m_selectedResource);

      if (found == m_thumbnails.end()) {
        ImGui::TextUnformatted("No preview for this asset type.");
      } else if (found->second.texture) {
        const auto size = std::min(196.F, ImGui::GetContentRegionAvail().x);
        auto shown = m_renderer->guiImage(*found->second.texture, size, size);

        if (!shown) {
          m_message = shown.error().message;
        }

        ImGui::TextWrapped("%s", found->second.warning.c_str());
      } else {
        ImGui::TextWrapped("%s", found->second.error.empty() ? "Generating preview..."
                                                             : found->second.error.c_str());
      }

      if (ImGui::Button("Return to scene Inspector")) {
        fileAction({{"command", "clear-asset-selection"}});
      }
    }

    auto toolName() const -> const char* {
      return m_tool == Tool::Move     ? "move"
             : m_tool == Tool::Rotate ? "rotate"
             : m_tool == Tool::Scale  ? "scale"
                                      : "select";
    }

    auto operationFrame() const -> Core::Result<Math::Mat4> {
      auto scene = m_document->scene();
      const auto selected = m_document->stats()->selected;

      if (!scene || !selected) {
        return Core::fail(Core::ErrorCode::InvalidState, "Transform frame requires selection");
      }

      return transformFrame(**scene, *selected, m_localTool);
    }

    auto handleLength(const Graphics::View& view, Math::Vec3 pivot) const -> float {
      const float height
          = view.orthographic
                ? view.orthographicHeight
                : static_cast<float>(2 * Math::length(pivot - view.position)
                                     * std::tan(view.verticalFovDegrees * 3.14159265 / 360));
      return std::max(0.001F, height * 75 / m_viewHeight);
    }

    auto toolStatus() const -> Json {
      return {{"mode", toolName()},
              {"snapUnit", m_tool == Tool::Rotate  ? "degrees"
                           : m_tool == Tool::Scale ? "ratio"
                                                   : "meters"},
              {"space", m_localTool ? "local" : "world"},
              {"snap", m_toolSnap},
              {"active", m_transform.has_value()},
              {"value", m_transform ? Json(m_transformValue) : Json(nullptr)},
              {"entity", m_transform ? Json(std::to_string(m_transform->entity())) : Json(nullptr)},
              {"error", m_transformError},
              {"handles", m_toolGeometry}};
    }

    auto toolBasis(int axis) const -> Core::Result<Math::Vec3> {
      if (axis < 0 || axis > 2 || !m_document->stats()->selected) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Move axis/selection is invalid");
      }

      Math::Vec3 value{axis == 0 ? 1.F : 0.F, axis == 1 ? 1.F : 0.F, axis == 2 ? 1.F : 0.F};

      if (m_localTool) {
        auto world = selectedWorld();
        auto transformed
            = world ? Math::transformVector(*world, value) : std::unexpected(world.error());
        return transformed ? Math::normalized(*transformed) : std::unexpected(transformed.error());
      }

      return value;
    }

    auto selectedWorld() const -> Core::Result<Math::Mat4> {
      if (m_transform) {
        return m_transform->world();
      }

      auto scene = m_document->scene();
      const auto selected = m_document->stats()->selected;

      if (!scene || !selected) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Transform tool requires a selected entity");
      }

      return (*scene)->worldTransform(*selected);
    }

    auto beginTransform(int axis, Math::Vec2 uv, std::uint64_t revision) -> Core::Result<void> {
      if (m_tool == Tool::Select || !m_editorView || playing() || m_transform || !m_viewVisible
          || !viewStatus()["ready"].get<bool>() || pendingInspector() || pendingEnvironment()
          || pendingOcclusion() || pendingText() || m_pendingClose || m_flying || m_orbiting
          || m_panning) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Transform tool is not ready for a gesture");
      }

      auto world = selectedWorld();
      auto axisDirection = toolBasis(axis);
      auto view = m_editorCamera.view();
      auto projection = view ? Graphics::viewProjection(*view, m_viewWidth / m_viewHeight)
                             : std::unexpected(view.error());

      if (!world || !axisDirection || !projection) {
        return std::unexpected(!world           ? world.error()
                               : !axisDirection ? axisDirection.error()
                                                : projection.error());
      }

      std::optional<AxisDrag> axisDrag;
      std::optional<RotationDrag> rotationDrag;
      Math::Mat4 frame;

      if (m_tool == Tool::Rotate) {
        auto basis = operationFrame();

        if (!basis) {
          return std::unexpected(basis.error());
        }

        frame = *basis;
        auto drag = RotationDrag::begin(*projection, frame, axis, uv);

        if (!drag) {
          return std::unexpected(drag.error());
        }

        rotationDrag.emplace(*drag);
      } else {
        auto drag = AxisDrag::begin(
            *projection, {world->data[12], world->data[13], world->data[14]}, *axisDirection, uv);
        if (!drag) {
          return std::unexpected(drag.error());
        }

        axisDrag.emplace(*drag);
      }

      auto gesture
          = TransformGesture::begin(*m_document, revision, m_document->stats()->selected->value);

      if (!gesture) {
        return std::unexpected(gesture.error());
      }

      m_pointerTransform = false;
      m_previewReady = false;
      m_transformStart = *world;
      m_transform.emplace(std::move(*gesture));
      m_axisDrag = std::move(axisDrag);
      m_rotationDrag = std::move(rotationDrag);
      m_rotationFrame = frame;
      m_handleLength = handleLength(*view, {world->data[12], world->data[13], world->data[14]});
      m_transformAxis = axis;
      m_transformValue = m_tool == Tool::Scale ? 1.F : 0.F;
      m_transformError.clear();
      return {};
    }

    auto updateTransform(Math::Vec2 uv) -> Core::Result<void> {
      if (!m_transform || (!m_axisDrag && !m_rotationDrag)) {
        return Core::fail(Core::ErrorCode::InvalidState, "There is no active transform gesture");
      }

      Core::Result<void> applied;

      if (m_tool == Tool::Rotate) {
        auto angle = m_rotationDrag->degrees(uv, m_toolSnap);

        if (!angle) {
          return std::unexpected(angle.error());
        }

        applied = m_transform->rotate(m_transformAxis, *angle, m_localTool);

        if (applied) {
          m_transformValue = *angle;
        }
      } else {
        auto delta = m_axisDrag->delta(uv, m_tool == Tool::Move ? m_toolSnap : 0);

        if (!delta) {
          return std::unexpected(delta.error());
        }

        if (m_tool == Tool::Scale) {
          const auto amount = Math::length(*delta);
          auto direction = toolBasis(m_transformAxis);

          if (!direction) {
            return std::unexpected(direction.error());
          }

          float factor = 1
                         + static_cast<float>(amount * (Math::dot(*delta, *direction) < 0 ? -1 : 1))
                               / m_handleLength;

          if (m_toolSnap > 0) {
            factor = 1 + std::round((factor - 1) / m_toolSnap) * m_toolSnap;
          }

          applied = m_transform->scale(m_transformAxis, factor, m_localTool);

          if (applied) {
            m_transformValue = factor;
          }
        } else {
          auto world = m_transformStart;
          world.data[12] += delta->x;
          world.data[13] += delta->y;
          world.data[14] += delta->z;
          applied = m_transform->update(world);

          if (applied) {
            auto direction = toolBasis(m_transformAxis);

            if (direction) {
              m_transformValue = static_cast<float>(Math::dot(*delta, *direction));
            }
          }
        }
      }

      m_transformError = applied ? "" : applied.error().message;

      if (applied) {
        m_previewReady = false;
      }

      return applied;
    }

    auto finishTransform(bool commit) -> Core::Result<void> {
      if (!m_transform) {
        return Core::fail(Core::ErrorCode::InvalidState, "There is no active transform gesture");
      }

      if (commit) {
        auto result = m_transform->commit(*m_document);

        if (!result) {
          m_transformError = result.error().message;
          return std::unexpected(result.error());
        }
      }

      m_transform.reset();
      m_axisDrag.reset();
      m_rotationDrag.reset();
      m_pointerTransform = false;
      m_transformError.clear();
      m_previewReady = false;
      return refresh();
    }

    /** Draw real projected world axes; handle picking shares these exact segments.
     * Only the preview clone changes during a gesture; GPU rendering resolves it
     * after layout in the same frame. Clip-behind/parallel axes are omitted.
     * 绘制真实世界轴投影，命中使用相同线段；拖动仅更改预览克隆，布局后同帧 GPU
     * 渲染。裁面后方及平行轴不显示，不伪造可用手柄。 */
    auto drawTransformTool() -> void {
      m_toolGeometry = Json::array();

      if (m_tool == Tool::Select || !m_editorView || playing() || !m_document->stats()->selected) {
        return;
      }

      auto world = selectedWorld();
      auto view = m_editorCamera.view();
      auto projection = view ? Graphics::viewProjection(*view, m_viewWidth / m_viewHeight)
                             : std::unexpected(view.error());

      if (!world || !view || !projection) {
        m_transformError = "Cannot project selected transform";
        return;
      }

      const Math::Vec3 pivot{world->data[12], world->data[13], world->data[14]};
      const auto project = [&](Math::Vec3 point) -> std::optional<Math::Vec2> {
        const auto& a = projection->data;
        const float w = a[3] * point.x + a[7] * point.y + a[11] * point.z + a[15];
        auto ndc = Math::transformPoint(*projection, point);

        if (!ndc || w <= 0 || ndc->z < 0 || ndc->z > 1 || std::abs(ndc->x) > 1
            || std::abs(ndc->y) > 1) {
          return std::nullopt;
        }

        return Math::Vec2{m_viewOrigin.x + (ndc->x + 1) * m_viewWidth * 0.5F,
                          m_viewOrigin.y + (1 - ndc->y) * m_viewHeight * 0.5F};
      };
      auto center = project(pivot);

      if (!center) {
        return;
      }

      const float length = handleLength(*view, pivot);
      constexpr std::array colors{IM_COL32(237, 89, 91, 255), IM_COL32(95, 205, 115, 255),
                                  IM_COL32(92, 150, 250, 255)};
      auto* draw = ImGui::GetWindowDrawList();
      draw->PushClipRect(m_viewOrigin,
                         {m_viewOrigin.x + m_viewWidth, m_viewOrigin.y + m_viewHeight}, true);

      if (m_tool == Tool::Rotate) {
        auto frame = m_transform ? Core::Result<Math::Mat4>(m_rotationFrame) : operationFrame();

        if (!frame) {
          m_transformError = frame.error().message;
          draw->PopClipRect();
          return;
        }

        float stretch = 0;

        for (int column = 0; column < 3; ++column) {
          const auto i = static_cast<std::size_t>(column) * 4;
          stretch = std::max(
              stretch, static_cast<float>(
                           Math::length({frame->data[i], frame->data[i + 1], frame->data[i + 2]})));
        }

        const float radius = length / std::max(stretch, 0.001F);

        for (int axis = 0; axis < 3; ++axis) {
          const auto color = m_transform && m_transformAxis == axis
                                 ? IM_COL32(255, 210, 75, 255)
                                 : colors[static_cast<std::size_t>(axis)];

          for (int segment = 0; segment < 64; ++segment) {
            const auto point = [&](int sample) -> std::optional<Math::Vec2> {
              float local[3]{};
              const float angle = static_cast<float>(sample) * 6.2831853F / 64;
              local[(axis + 1) % 3] = std::cos(angle) * radius;
              local[(axis + 2) % 3] = std::sin(angle) * radius;
              auto worldPoint = Math::transformPoint(*frame, {local[0], local[1], local[2]});
              return worldPoint ? project(*worldPoint) : std::nullopt;
            };
            auto first = point(segment);
            auto second = point(segment + 1);

            if (!first || !second
                || std::hypot(first->x - second->x, first->y - second->y) < 0.01F) {
              continue;
            }

            const Math::Vec2 uv{((first->x + second->x) * 0.5F - m_viewOrigin.x) / m_viewWidth,
                                ((first->y + second->y) * 0.5F - m_viewOrigin.y) / m_viewHeight};

            if (!RotationDrag::begin(*projection, *frame, axis, uv)) {
              continue;
            }

            draw->AddLine({first->x, first->y}, {second->x, second->y}, color, 2.5F);
            m_toolGeometry.push_back(
                {{"axis", axis}, {"start", {first->x, first->y}}, {"end", {second->x, second->y}}});
          }
        }

        draw->PopClipRect();
        return;
      }

      for (int axis = 0; axis < 3; ++axis) {
        auto direction = toolBasis(axis);
        auto endpoint = direction ? project(pivot + *direction * length) : std::nullopt;

        if (!endpoint) {
          continue;
        }

        const float dx = endpoint->x - center->x;
        const float dy = endpoint->y - center->y;
        const float pixels = std::sqrt(dx * dx + dy * dy);
        const Math::Vec2 uv{(center->x - m_viewOrigin.x) / m_viewWidth,
                            (center->y - m_viewOrigin.y) / m_viewHeight};

        if (pixels < 16 || !AxisDrag::begin(*projection, pivot, *direction, uv)) {
          continue;
        }

        const auto color = m_transform && m_transformAxis == axis
                               ? IM_COL32(255, 210, 75, 255)
                               : colors[static_cast<std::size_t>(axis)];
        draw->AddLine({center->x, center->y}, {endpoint->x, endpoint->y}, color, 3);

        if (m_tool == Tool::Scale) {
          draw->AddRectFilled({endpoint->x - 5, endpoint->y - 5},
                              {endpoint->x + 5, endpoint->y + 5}, color);
        } else {
          draw->AddCircleFilled({endpoint->x, endpoint->y}, 5, color);
        }

        draw->AddText({endpoint->x + 6, endpoint->y - 7}, color,
                      axis == 0   ? "X"
                      : axis == 1 ? "Y"
                                  : "Z");
        m_toolGeometry.push_back({{"axis", axis},
                                  {"start", {center->x, center->y}},
                                  {"end", {endpoint->x, endpoint->y}}});
      }

      draw->PopClipRect();
    }

    auto hitTransformTool(Platform::Point pointer) const -> std::optional<int> {
      std::optional<int> result;
      float nearest = 9;

      for (const auto& handle : m_toolGeometry) {
        const float x = handle["start"][0].get<float>();
        const float y = handle["start"][1].get<float>();
        const float dx = handle["end"][0].get<float>() - x;
        const float dy = handle["end"][1].get<float>() - y;
        const float t
            = std::clamp(((pointer.x - x) * dx + (pointer.y - y) * dy) / (dx * dx + dy * dy),
                         m_tool == Tool::Rotate ? 0.F : 0.15F, 1.F);
        const float distance = std::hypot(pointer.x - x - t * dx, pointer.y - y - t * dy);

        if (distance < nearest) {
          nearest = distance;
          result = handle["axis"].get<int>();
        }
      }

      return result;
    }

    auto viewportControls() -> void {
      if (playing()) {
        ImGui::TextUnformatted("Running game preview");
        m_flying = m_orbiting = m_panning = false;
        return;
      }

      if (ImGui::RadioButton("Scene", m_editorView)) {
        sessionAction({{"command", "set-view"}, {"mode", "editor"}});
      }

      ImGui::SameLine();

      if (ImGui::RadioButton("Game", !m_editorView)) {
        sessionAction({{"command", "set-view"}, {"mode", "game"}});
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
        ImGui::BeginDisabled(m_transform.has_value());

        for (const auto& [name, mode] :
             {std::pair{"Select", Tool::Select}, std::pair{"Move", Tool::Move},
              std::pair{"Rotate", Tool::Rotate}, std::pair{"Scale", Tool::Scale}}) {
          if (ImGui::RadioButton(name, m_tool == mode)) {
            sessionAction({{"command", "set-tool"},
                           {"tool", mode == Tool::Move     ? "move"
                                    : mode == Tool::Rotate ? "rotate"
                                    : mode == Tool::Scale  ? "scale"
                                                           : "select"},
                           {"space", m_localTool ? "local" : "world"},
                           {"snap", mode == Tool::Rotate  ? 15.F
                                    : mode == Tool::Scale ? 0.1F
                                                          : 0.5F}});
          }

          ImGui::SameLine();
        }

        ImGui::NewLine();
        bool local = m_localTool;
        ImGui::SetNextItemWidth(75);
        float snap = m_toolSnap;
        bool toolChanged = ImGui::Checkbox("Local", &local);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(65);
        toolChanged = ImGui::DragFloat(m_tool == Tool::Rotate  ? "Snap (deg)"
                                       : m_tool == Tool::Scale ? "Snap (ratio)"
                                                               : "Snap (m)",
                                       &snap, m_tool == Tool::Rotate ? 1.F : 0.1F, 0,
                                       m_tool == Tool::Rotate  ? 360.F
                                       : m_tool == Tool::Scale ? 10.F
                                                               : 1000.F,
                                       "%.2f", ImGuiSliderFlags_AlwaysClamp)
                      || toolChanged;

        if (toolChanged) {
          sessionAction({{"command", "set-tool"},
                         {"tool", toolName()},
                         {"space", local ? "local" : "world"},
                         {"snap", snap}});
        }

        ImGui::EndDisabled();
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
      if (m_transform) {
        if (input.key(Platform::Key::Escape).pressed || m_pendingClose || !m_viewVisible) {
          static_cast<void>(finishTransform(false));
          return;
        }

        if (!m_pointerTransform) {
          return;
        }

        const auto pointer = input.pointer();
        auto updated
            = updateTransform({std::clamp((pointer.x - m_viewOrigin.x) / m_viewWidth, 0.F, 1.F),
                               std::clamp((pointer.y - m_viewOrigin.y) / m_viewHeight, 0.F, 1.F)});

        if (!updated) {
          m_transformError = updated.error().message;
        }

        if (input.button(Platform::MouseButton::Left).released) {
          auto finished = finishTransform(true);

          if (!finished) {
            m_transformError = finished.error().message;
          }
        }

        return;
      }

      if (playing() || !m_viewVisible) {
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

      if (hovered && left.pressed && !alt && m_tool != Tool::Select && m_editorView) {
        auto hit = hitTransformTool(pointer);

        if (hit) {
          auto began = beginTransform(*hit,
                                      {(pointer.x - m_viewOrigin.x) / m_viewWidth,
                                       (pointer.y - m_viewOrigin.y) / m_viewHeight},
                                      m_document->stats()->revision);
          m_transformError = began ? "" : began.error().message;
          m_pointerTransform = began.has_value();

          if (began && left.released) {
            auto finished = finishTransform(true);
            m_transformError = finished ? "" : finished.error().message;
          }

          return;
        }
      }

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

    /** Apply chrome only; scene textures and camera state remain authoritative.
     * 仅改变界面皮肤，场景纹理与相机状态仍由实际引擎决定。 */
    auto applyTheme() -> void {
      auto& style = ImGui::GetStyle();
      m_lightTheme ? ImGui::StyleColorsLight() : ImGui::StyleColorsDark();
      style.WindowPadding = {8, 8};
      style.FramePadding = {8, 4};
      style.ItemSpacing = {8, 6};
      style.ItemInnerSpacing = {6, 4};
      style.WindowRounding = 0;
      style.ChildRounding = 0;
      style.FrameRounding = 4;
      style.PopupRounding = 4;
      style.TabRounding = 0;
      style.WindowBorderSize = 1;
      style.FrameBorderSize = 1;
      style.WindowMinSize = {120, 100};
      const auto color = [](unsigned rgb) {
        return ImVec4{static_cast<float>((rgb >> 16) & 255) / 255,
                      static_cast<float>((rgb >> 8) & 255) / 255,
                      static_cast<float>(rgb & 255) / 255, 1};
      };
      auto* c = style.Colors;
      c[ImGuiCol_Text] = color(m_lightTheme ? 0x252A32 : 0xE6E8ED);
      c[ImGuiCol_TextDisabled] = color(m_lightTheme ? 0x616976 : 0x979DA8);
      c[ImGuiCol_WindowBg] = color(m_lightTheme ? 0xF5F6F8 : 0x202226);
      c[ImGuiCol_ChildBg] = c[ImGuiCol_WindowBg];
      c[ImGuiCol_PopupBg] = c[ImGuiCol_WindowBg];
      c[ImGuiCol_Border] = color(m_lightTheme ? 0xCDD2DA : 0x373B43);
      c[ImGuiCol_Separator] = c[ImGuiCol_Border];
      c[ImGuiCol_FrameBg] = color(m_lightTheme ? 0xFFFFFF : 0x17191D);
      c[ImGuiCol_FrameBgHovered] = color(m_lightTheme ? 0xEDF0F3 : 0x30353D);
      c[ImGuiCol_FrameBgActive] = color(m_lightTheme ? 0xDCE8FC : 0x354B6D);
      c[ImGuiCol_Button] = color(m_lightTheme ? 0xEDF0F3 : 0x282C33);
      c[ImGuiCol_ButtonHovered] = c[ImGuiCol_FrameBgActive];
      c[ImGuiCol_ButtonActive] = color(m_lightTheme ? 0xBBD2F9 : 0x415F8D);
      c[ImGuiCol_Header] = c[ImGuiCol_FrameBgActive];
      c[ImGuiCol_HeaderHovered] = c[ImGuiCol_ButtonActive];
      c[ImGuiCol_HeaderActive] = c[ImGuiCol_HeaderHovered];
      c[ImGuiCol_Tab] = color(m_lightTheme ? 0xEDF0F3 : 0x181A1E);
      c[ImGuiCol_TabSelected] = c[ImGuiCol_WindowBg];
      c[ImGuiCol_TabDimmed] = c[ImGuiCol_Tab];
      c[ImGuiCol_TabDimmedSelected] = c[ImGuiCol_TabSelected];
      c[ImGuiCol_TabSelectedOverline] = color(m_lightTheme ? 0x356FD6 : 0x6C9EFF);
      c[ImGuiCol_TabDimmedSelectedOverline] = c[ImGuiCol_TabSelectedOverline];
      c[ImGuiCol_CheckMark] = c[ImGuiCol_TabSelectedOverline];
      c[ImGuiCol_SliderGrab] = c[ImGuiCol_CheckMark];
      c[ImGuiCol_SliderGrabActive] = c[ImGuiCol_CheckMark];
      c[ImGuiCol_TextSelectedBg] = c[ImGuiCol_Header];
      c[ImGuiCol_TitleBg] = c[ImGuiCol_Tab];
      c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TabSelected];
      c[ImGuiCol_MenuBarBg] = color(m_lightTheme ? 0xE8EBEF : 0x181A1E);
      c[ImGuiCol_DockingEmptyBg] = c[ImGuiCol_MenuBarBg];
    }

    auto consolePanel() -> void {
      bool any = false;

      for (const auto* message :
           {&m_message, &m_previewError, &m_checkMessage, &m_layoutError, &m_transformError}) {
        if (!message->empty()) {
          ImGui::TextWrapped("%s", message->c_str());
          ImGui::Separator();
          any = true;
        }
      }

      for (const auto& diagnostic : m_playDiagnostics) {
        if (diagnostic.contains("entity") && diagnostic.contains("phase")) {
          ImGui::TextDisabled("Entity %s / %s",
                              diagnostic["entity"].get_ref<const std::string&>().c_str(),
                              diagnostic["phase"].get_ref<const std::string&>().c_str());
        }

        ImGui::TextWrapped("%s",
                           diagnostic["error"]["message"].get_ref<const std::string&>().c_str());
        any = true;
      }

      for (const auto& item : m_scriptDiagnostics) {
        const auto label
            = item.path + ':' + std::to_string(item.line) + ':' + std::to_string(item.column);

        if (ImGui::Button(label.c_str())) {
          fileAction({{"command", "open-external"}, {"path", item.path}});
        }

        any = true;
      }

      if (!any) {
        ImGui::TextUnformatted("No workspace, Play or compiler diagnostics.");
      }
    }

    auto historyAvailable(bool redo) const -> bool {
      if (m_material) {
        const auto state = materialStatus();
        return state["draft"] == state["value"] && state[redo ? "canRedo" : "canUndo"].get<bool>();
      }

      const auto stats = m_document->stats().value();
      return redo ? stats.canRedo : stats.canUndo;
    }

    auto focusedSave() -> void {
      if (m_material) {
        fileAction({{"command", "save-material"}});
      } else if (applyInspector()) {
        action({{"command", "save"}});
      }
    }

    auto focusedHistory(bool redo) -> void {
      if (m_material) {
        fileAction({{"command", redo ? "redo-material" : "undo-material"}});
      } else {
        action({{"command", redo ? "redo" : "undo"}});
      }
    }

    auto menuBar() -> void {
      if (!ImGui::BeginMenuBar()) {
        return;
      }

      ImGui::TextUnformatted("DotEngine");

      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem(m_material ? "Save material" : "Save scene", nullptr, false,
                            !playing())) {
          focusedSave();
        }

        if (ImGui::MenuItem("Export game")) {
          m_exportDialog = true;
        }

        if (ImGui::MenuItem("Close project")) {
          sessionAction({{"command", "close"}, {"discard", false}});
        }

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", nullptr, false, !playing() && historyAvailable(false))) {
          focusedHistory(false);
        }

        if (ImGui::MenuItem("Redo", nullptr, false, !playing() && historyAvailable(true))) {
          focusedHistory(true);
        }

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Build")) {
        if (ImGui::MenuItem("Export game")) {
          m_exportDialog = true;
        }

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Window")) {
        for (const auto& [label, pane] :
             {std::pair{"Hierarchy", "scene"}, std::pair{"Scene", "viewport"},
              std::pair{"Inspector", "inspector"}, std::pair{"Environment", "environment"},
              std::pair{"Rendering", "rendering"}, std::pair{"Assets", "resources"},
              std::pair{"Console", "console"}}) {
          if (ImGui::MenuItem(label)) {
            sessionAction({{"command", "show-pane"}, {"pane", pane}});
          }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reset layout")) {
          sessionAction({{"command", "reset-layout"}});
        }

        if (ImGui::MenuItem("Dark theme", nullptr, !m_lightTheme)) {
          sessionAction({{"command", "set-theme"}, {"theme", "dark"}});
        }

        if (ImGui::MenuItem("Light theme", nullptr, m_lightTheme)) {
          sessionAction({{"command", "set-theme"}, {"theme", "light"}});
        }

        ImGui::EndMenu();
      }

      ImGui::EndMenuBar();
    }

    /** Docked/floating windows remain owned by this workspace's GUI context.
     * Record real window geometry after Begin; no concept-art positions are reported.
     * 停靠/浮动窗口归工作区 GUI 上下文拥有；Begin 后记录实际几何，不报告效果图坐标。 */
    auto beginPanel(const char* title, const char* key, const char* pane) -> bool {
      const bool requested = m_requestedPane == pane;

      if (requested) {
        ImGui::SetNextWindowFocus();
      }

      auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing;

      if (std::string_view(key) == "viewport") {
        flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
      }

      const bool visible = ImGui::Begin(title, nullptr, flags);
      const auto origin = ImGui::GetWindowPos();
      const auto size = ImGui::GetWindowSize();
      m_panelGeometry[key]
          = {{"origin", {origin.x, origin.y}},
             {"size", {size.x, size.y}},
             {"visible", visible},
             {"focused", ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)},
             {"docked", ImGui::GetWindowDockID() != 0}};

      if (requested && visible) {
        m_pane = pane;
        m_requestedPane.clear();
      }

      return visible;
    }

    static auto panelTitle(std::string_view panel) -> const char* {
      if (panel == "hierarchy") {
        return "Hierarchy";
      }

      if (panel == "viewport") {
        return "Scene";
      }

      if (panel == "inspector") {
        return "Inspector";
      }

      if (panel == "environment") {
        return "Environment";
      }

      if (panel == "rendering") {
        return "Rendering";
      }

      if (panel == "console") {
        return "Console";
      }

      return "Assets";
    }

    /** GUI docking is applied only inside its owning frame; accepted commands
     * explicitly report queued until the observed geometry/generation changes.
     * 停靠只在所属 GUI 帧中执行，接受命令后明确排队，直到几何/代次变化才完成。 */
    auto applyDockRequest(ImGuiID root) -> void {
      const auto request = std::move(*m_dockRequest);
      m_dockRequest.reset();
      const auto panel = request["panel"].get<std::string>();
      const auto target = request["target"].get<std::string>();
      auto* window = ImGui::FindWindowByName(panelTitle(target));

      if (!window || !window->DockId || !ImGui::DockBuilderGetNode(window->DockId)) {
        m_layoutError = "Dock target is no longer in the workspace";
        m_message = m_layoutError;
        return;
      }

      auto node = window->DockId;
      const auto placement = request["placement"].get<std::string>();

      if (placement != "tab") {
        const auto direction = placement == "left"    ? ImGuiDir_Left
                               : placement == "right" ? ImGuiDir_Right
                               : placement == "up"    ? ImGuiDir_Up
                                                      : ImGuiDir_Down;
        node = ImGui::DockBuilderSplitNode(node, direction, Codec::number(request["ratio"]),
                                           nullptr, nullptr);
      }

      ImGui::DockBuilderDockWindow(panelTitle(panel), node);
      ImGui::DockBuilderFinish(root);
      ++m_layoutGeneration;
    }

    auto draw() -> Core::Result<void> {
      applyTheme();
      const auto stats = m_document->stats().value();
      auto* viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(viewport->WorkPos);
      ImGui::SetNextWindowSize(viewport->WorkSize);
      ImGui::Begin("DotEngine", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                       | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar);
      menuBar();

      ImGui::BeginDisabled(playing());

      if ((ImGui::Button("Save")
           || (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)))
          && !playing()) {
        focusedSave();
      }

      ImGui::SameLine();
      ImGui::BeginDisabled(!historyAvailable(false));

      if ((ImGui::Button("Undo")
           || (!ImGui::GetIO().WantTextInput
               && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteAlways)))
          && historyAvailable(false) && !playing()) {
        focusedHistory(false);
      }

      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(!historyAvailable(true));

      if ((ImGui::Button("Redo")
           || (!ImGui::GetIO().WantTextInput
               && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z,
                                  ImGuiInputFlags_RouteAlways)))
          && historyAvailable(true) && !playing()) {
        focusedHistory(true);
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
      const bool unsaved = m_snapshot["dirty"].get<bool>() || pendingInspector()
                           || pendingEnvironment() || pendingOcclusion() || pendingText()
                           || m_transform.has_value();
      ImGui::TextUnformatted(unsaved ? "Unsaved changes" : "Saved");
      ImGui::Separator();

      const auto dock = ImGui::GetID("WorkspaceDock");
      const auto available = ImGui::GetContentRegionAvail();
      const bool rebuildingLayout = m_resetLayout;
      ImGuiID initialViewportDock{};

      struct InitialDockBounds {
        ImGuiDockNode* node;
        ImVec2 position;
        ImVec2 size;
      };

      std::vector<InitialDockBounds> initialDockBounds;

      if (m_resetLayout) {
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dock, ImGui::GetCursorScreenPos());
        ImGui::DockBuilderSetNodeSize(dock, available);
        auto center = dock;
        const auto inspector
            = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22F, nullptr, &center);
        const auto assets
            = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25F, nullptr, &center);
        const auto hierarchy
            = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18F / 0.78F, nullptr, &center);
        ImGui::DockBuilderDockWindow("Hierarchy", hierarchy);
        ImGui::DockBuilderDockWindow("Scene", center);
        ImGui::DockBuilderDockWindow("Inspector", inspector);
        ImGui::DockBuilderDockWindow("Environment", inspector);
        ImGui::DockBuilderDockWindow("Rendering", inspector);
        ImGui::DockBuilderDockWindow("Assets", assets);
        ImGui::DockBuilderDockWindow("Console", assets);
        ImGui::DockBuilderFinish(dock);
        ImGui::DockBuilderGetNode(inspector)->SelectedTabId = ImHashStr("Inspector");
        ImGui::DockBuilderGetNode(assets)->SelectedTabId = ImHashStr("Assets");
        ImGui::DockBuilderGetNode(center)->SelectedTabId = ImHashStr("Scene");
        initialViewportDock = center;
        const auto rememberBounds = [&](auto&& self, ImGuiDockNode* node) -> void {
          initialDockBounds.push_back({node, node->Pos, node->Size});

          for (auto* child : node->ChildNodes) {
            if (child) {
              self(self, child);
            }
          }
        };
        rememberBounds(rememberBounds, ImGui::DockBuilderGetNode(dock));
        m_resetLayout = false;
        ++m_layoutGeneration;
      }

      if (m_dockRequest) {
        applyDockRequest(dock);
      }

      ImGui::DockSpace(dock, {0, 0});

      if (rebuildingLayout) {
        /** Every seeded leaf hosts a real panel begun below this frame. Declare
         * their presence before first binding so inactive-node fitting does not
         * expand Scene across the entire root while windows are being created.
         * 每个初始叶节点本帧都创建真实面板；首次绑定前声明布局存在，避免未激活节点
         * 适配在创建窗口时把 Scene 扩张至整个根区域。 */
        const auto activate = [&](auto&& self, ImGuiDockNode* node) -> void {
          node->IsVisible = true;

          for (auto* child : node->ChildNodes) {
            if (child) {
              self(self, child);
            }
          }
        };
        activate(activate, ImGui::DockBuilderGetNode(dock));

        for (const auto& bounds : initialDockBounds) {
          bounds.node->Pos = bounds.position;
          bounds.node->Size = bounds.size;
          bounds.node->SizeRef = bounds.size;
        }
      }

      ImGui::End();
      m_panelGeometry = Json::object();

      m_visibleHierarchy = Json::array();
      m_visibleResources = Json::array();

      if (beginPanel("Hierarchy", "hierarchy", "scene")) {
        ImGui::SetNextItemWidth(-1);
        searchField("##hierarchy-search", "Search hierarchy", "hierarchy", m_hierarchyFilter);
        ImGui::Separator();
        ImGui::BeginDisabled(playing());
        tree(m_snapshot["scene"]["entities"], nullptr);
        ImGui::EndDisabled();
      }

      ImGui::End();

      if (rebuildingLayout) {
        ImGui::SetNextWindowDockID(initialViewportDock, ImGuiCond_Always);
        ImGui::SetNextWindowFocus();
      }

      m_viewVisible = beginPanel("Scene", "viewport", "viewport");

      if (m_viewVisible) {
        ImGui::BeginChild("Viewport controls", {0, ImGui::GetFrameHeightWithSpacing() * 5});
        ImGui::BeginDisabled(m_transform.has_value());
        viewportControls();
        ImGui::EndDisabled();
        ImGui::EndChild();

        m_viewOrigin = ImGui::GetCursorScreenPos();
        const auto content = ImGui::GetContentRegionAvail();
        m_viewWidth = std::max(1.F, content.x);
        m_viewHeight = std::max(1.F, content.y);

        /** Resolve GPU work after GUI layout, keeping renderer reentry protection.
         * Current pixel dimensions and retained image refer to the same frame.
         * GUI 布局后解析 GPU 工作，保留重入保护；本帧像素尺寸与保留图像一致。 */
        auto image = m_renderer->guiViewport(
            [&](std::uint32_t width,
                std::uint32_t height) -> Core::Result<std::optional<Graphics::RenderTarget>> {
              auto ready = preview(width, height);

              if (!ready) {
                return std::unexpected(ready.error());
              }

              return m_previewReady ? m_target : std::nullopt;
            },
            m_viewWidth, m_viewHeight);

        if (!image) {
          ImGui::End();
          return image;
        }

        drawTransformTool();
      }

      ImGui::End();

      if (beginPanel("Inspector", "inspector", "inspector")) {
        ImGui::BeginDisabled(playing() || m_transform.has_value());
        ImGui::BeginDisabled(pendingEnvironment() || pendingOcclusion());
        if (m_material) {
          materialInspector();
        } else if (!m_selectedResource.empty() && assetPreviewKind(m_selectedResource)) {
          assetInspector();
        } else {
          inspector();
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
      }

      ImGui::End();

      if (beginPanel("Environment", "environment", "environment")) {
        environmentPanel();
      }

      ImGui::End();

      if (beginPanel("Rendering", "rendering", "rendering")) {
        renderingPanel();
      }

      ImGui::End();

      if (beginPanel("Assets", "resources", "resources")) {
        resourcesPanel();
      }

      ImGui::End();

      if (beginPanel("Console", "console", "console")) {
        consolePanel();
      }

      ImGui::End();
      ImGui::Begin("DotEngine", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                       | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar);

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

      m_viewBlocked
          = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)
            || ImGui::IsAnyItemActive();
      ImGui::End();
      return {};
    }

    auto callbacks() -> Core::LoopCallbacks {
      Core::LoopCallbacks callbacks;
      callbacks.acceptClose = [&]() -> Core::Result<bool> {
        m_pendingClose = m_document->stats()->dirty || pendingInspector() || pendingEnvironment()
                         || pendingOcclusion() || pendingText() || m_transform.has_value();

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
          clearPreviews();
        }

        updatePreviews();
        viewportInput(m_application->input(), std::chrono::duration<double>(frame.elapsed).count());
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
      gui.fontSize = 15;
      gui.font = config.font;

      if (gui.font.empty()) {
        auto font = impl->resource("EditorSans.ttf");

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
        if (state->m_transform && std::holds_alternative<Platform::Focus>(event.data)
            && !std::get<Platform::Focus>(event.data).focused) {
          auto cancelled = state->finishTransform(false);

          if (!cancelled) {
            state->m_transformError = cancelled.error().message;
          }
        }

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

  auto Workspace::captureAssetPreview(std::string_view path) -> Core::Result<Graphics::Image> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Workspace was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      if (m_impl->m_material && m_impl->m_material->snapshot()->path == path) {
        const auto key = std::to_string(m_impl->m_buffer) + m_impl->m_materialForm.dump();

        if (key == m_impl->m_materialSampleKey && m_impl->m_materialSample.image) {
          return *m_impl->m_materialSample.image;
        }
      } else {
        auto found = m_impl->m_thumbnails.find(std::string(path));

        if (found != m_impl->m_thumbnails.end() && found->second.image) {
          return *found->second.image;
        }
      }

      return Core::fail(Core::ErrorCode::InvalidState, "Asset preview is pending, failed or stale");
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Capture asset preview: " + std::string(error.what()));
    }
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
