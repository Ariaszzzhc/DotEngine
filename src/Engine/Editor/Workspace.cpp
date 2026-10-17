module;

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Editor.Workspace;

import Dot.Core.Application;
import Dot.Core.Events;
import Dot.Script.SceneScripts;
import Dot.Editor.Play;
import Dot.Editor.PlayInput;
import Dot.Editor.ViewportCapture;
import Dot.Editor.AnimationPreview;
import Dot.Editor.AnimationDocument;
import Dot.Editor.AnimationKeyMove;
import Dot.Editor.AnimationClipboard;
import Dot.Animation.Files;
import Dot.Animation.Source;
import Dot.Animation.ScenePlayers;
import Dot.Assets.SceneSkins;
import Dot.Editor.SkeletonView;
import Dot.Assets.ImageExport;
import Dot.Platform.Dialog;
import Dot.Graphics.View;
import Dot.Editor.Resources;
import Dot.Editor.MaterialDocument;
import Dot.Editor.AssetPreview;
import Dot.Editor.PreviewWorker;
import Dot.Assets.Image;
import Dot.Editor.ScriptDiagnostics;
import Dot.Platform.ExternalFiles;
import Dot.Script.Compiler;
import Dot.Script.LiveScripts;
import Dot.Script.SceneFlow;
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

    auto meshWorkJson(const Graphics::MeshFrameStats& frame) -> Json {
      return {{"input", frame.inputInstances},
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
              {"shadowPagesUpdated", frame.shadowPagesUpdated},
              {"shadowPagesReused", frame.shadowPagesReused},
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

    auto cpuTimingJson(const Graphics::MeshCpuTiming& sample) -> Json {
      return {{"serial", std::to_string(sample.serial)},
              {"size", {sample.width, sample.height}},
              {"prepareNanoseconds", std::to_string(sample.prepareNanoseconds)},
              {"encodeNanoseconds", std::to_string(sample.encodeNanoseconds)},
              {"submitNanoseconds", std::to_string(sample.submitNanoseconds)},
              {"work", meshWorkJson(sample.work)}};
    }

    auto gpuTimingJson(const Graphics::MeshGpuTiming& sample) -> Json {
      Json passes = Json::array();

      for (const auto& value : sample.passNanoseconds) {
        passes.push_back(value ? Json(std::to_string(*value)) : Json(nullptr));
      }

      return {{"serial", std::to_string(sample.serial)},
              {"size", {sample.width, sample.height}},
              {"totalNanoseconds", std::to_string(sample.totalNanoseconds)},
              {"passNanoseconds", std::move(passes)},
              {"work", meshWorkJson(sample.work)}};
    }

    auto profilingJson(const Graphics::MeshProfiling& profile, bool samples) -> Json {
      constexpr std::array backends{"Metal", "D3D12", "Vulkan"};
      Json result{
          {"enabled", profile.enabled},
          {"gpuSupported", profile.gpuSupported},
          {"session", std::to_string(profile.session)},
          {"adapter",
           {{"name", profile.adapter.name},
            {"description", profile.adapter.description},
            {"backend", backends[static_cast<std::size_t>(profile.adapter.backend)]},
            {"maxTextureDimension", profile.adapter.maxTextureDimension}}},
          {"pending", profile.pending},
          {"dropped", std::to_string(profile.dropped)},
          {"gpuBufferBytes", std::to_string(profile.gpuBufferBytes)},
          {"error", profile.error ? Json(*profile.error) : Json(nullptr)},
          {"cpuSamples", profile.cpu.size()},
          {"gpuSamples", profile.gpu.size()},
          {"latestCpu", profile.cpu.empty() ? Json(nullptr) : cpuTimingJson(profile.cpu.back())},
          {"latestGpu", profile.gpu.empty() ? Json(nullptr) : gpuTimingJson(profile.gpu.back())}};

      if (samples) {
        result["cpu"] = Json::array();
        result["gpu"] = Json::array();

        for (const auto& value : profile.cpu) {
          result["cpu"].push_back(cpuTimingJson(value));
        }

        for (const auto& value : profile.gpu) {
          result["gpu"].push_back(gpuTimingJson(value));
        }
      }

      return result;
    }

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

    auto sceneRequestJson(const Script::SceneRequest& value) -> Json {
      constexpr std::array states{"pending", "loading", "succeeded", "failed", "cancelled"};
      return {{"id", std::to_string(value.id)},
              {"path", value.path},
              {"state", states.at(static_cast<std::size_t>(value.state))},
              {"committed", value.committed},
              {"error", value.error ? errorJson(*value.error)["error"] : Json(nullptr)}};
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

    /** Labels stay visible beside wide controls, or above them in narrow panels.
     * 宽面板采用双列，窄面板上下排列，确保标签和输入框均可阅读。 */
    class PropertyRow {
    public:
      explicit PropertyRow(const char* title, bool stacked = false) {
        ImGui::PushID(title);
        const float width = ImGui::GetContentRegionAvail().x;
        stacked = stacked || width < ImGui::GetFontSize() * 16.F;
        m_open = ImGui::BeginTable("##property", stacked ? 1 : 2,
                                   ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX);

        if (m_open) {
          if (!stacked) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,
                                    std::min(width * .4F, ImGui::GetFontSize() * 10.F));
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
          }

          ImGui::TableNextColumn();
          ImGui::AlignTextToFramePadding();
          ImGui::TextWrapped("%s", title);
          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
        }
      }

      PropertyRow(const PropertyRow&) = delete;
      auto operator=(const PropertyRow&) -> PropertyRow& = delete;

      ~PropertyRow() {
        if (m_open) {
          ImGui::EndTable();
        }

        ImGui::PopID();
      }

      [[nodiscard]] auto visible() const -> bool {
        return m_open;
      }

    private:
      bool m_open{};
    };

    /** Edit one scalar without narrowing or rewriting untouched vector elements.
     * 逐项编辑标量，不窄化数值，也不重写向量中未编辑的分量。 */
    auto scalar(Json& value) -> void {
      if (value.is_number_unsigned()) {
        auto current = value.get<std::uint64_t>();

        if (ImGui::InputScalar("##value", ImGuiDataType_U64, &current)) {
          value = current;
        }
      } else if (value.is_number_integer()) {
        auto current = value.get<std::int64_t>();

        if (ImGui::InputScalar("##value", ImGuiDataType_S64, &current)) {
          value = current;
        }
      } else {
        auto current = value.get<double>();

        if (ImGui::InputDouble("##value", &current, 0, 0, "%.6g")) {
          value = current;
        }

        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%.17g", current);
        }
      }
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
        const bool vector = value.is_array() && !value.empty() && value.size() <= 4
                            && std::ranges::all_of(
                                value, [](const auto& element) { return element.is_number(); });

        if (value.is_object()) {
          if (ImGui::TreeNode(title.c_str())) {
            fields(value, depth + 1);
            ImGui::TreePop();
          }
        } else if (PropertyRow row{title.c_str(), vector}; row.visible()) {
          if (value.is_boolean()) {
            bool current = value.get<bool>();

            if (ImGui::Checkbox("##value", &current)) {
              value = current;
            }
          } else if (value.is_number()) {
            scalar(value);
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
            } else if (item.key() == "collisionDetection") {
              options = "discrete\0continuous\0";
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

              if (ImGui::Combo("##value", &selected, values.data(),
                               static_cast<int>(values.size()))) {
                value = values.at(static_cast<std::size_t>(selected));
              }
            } else if (ImGui::InputText("##value", &current)) {
              value = current;
            }

            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("%s", current.c_str());
            }
          } else if (vector) {
            const auto count = static_cast<int>(value.size());
            const int columns = std::clamp(
                static_cast<int>(ImGui::GetContentRegionAvail().x / (ImGui::GetFontSize() * 4.F)),
                1, count);

            if (ImGui::BeginTable("##axes", columns,
                                  ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
              const char* axes = item.key() == "color" || item.key() == "tint" ? "RGBA" : "XYZW";

              for (int index = 0; index < count; ++index) {
                ImGui::TableNextColumn();
                ImGui::PushID(index);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%c", axes[index]);
                ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
                scalar(value[static_cast<std::size_t>(index)]);
                ImGui::PopID();
              }

              ImGui::EndTable();
            }
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

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      auto result = Codec::parseJson(*encoded)["entities"][0];
      auto removed = scene->setRigidBody(id, std::nullopt);

      if (!removed) {
        return std::unexpected(removed.error());
      }

      auto attached = scene->setCharacterController(id, World::CharacterController{});

      if (!attached) {
        return std::unexpected(attached.error());
      }

      encoded = World::encodeScene(*scene);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      result["character"] = Codec::parseJson(*encoded)["entities"][0]["character"];
      return result;
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
      std::uint64_t ticket{};
    };

    std::optional<PreviewWorker> m_previewWorker;
    std::string m_materialRequestedKey;
    std::uint64_t m_materialTicket{};
    bool m_previewsStopped{};
    std::map<std::string, PreviewEntry> m_thumbnails;
    std::deque<std::string> m_thumbnailQueue;
    PreviewEntry m_materialSample;
    std::string m_materialSampleKey;
    std::string m_previewServiceError;
    std::uint64_t m_previewClock{};
    bool m_assetGrid{true};
    std::string m_assetDirectory{"assets"};
    std::uint64_t m_buffer{};
    Json m_catalog = Json::array();
    std::string m_selectedResource;
    std::vector<ScriptDiagnostic> m_scriptDiagnostics;
    std::string m_newPath;
    std::string m_contentName{"Scene content"};
    Json m_overrideQuery;
    Json m_overridePage;
    std::string m_overrideRevision;
    std::string m_overrideContext;
    std::string m_overrideError;
    Json m_fieldOverrides;
    std::string m_fieldRevision;
    std::string m_fieldContext;
    std::string m_fieldError;
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
    std::string m_skeletonFilter;
    SkeletonView m_skeleton;
    bool m_viewVisible{true};
    Json m_visibleHierarchy = Json::array();
    Json m_hierarchyRows = Json::object();
    Json m_hierarchyExpansion = Json::object();
    std::unordered_set<std::string> m_selectionIds;
    std::string m_selectionAnchor;
    Json m_visibleResources = Json::array();
    bool m_assetRefresh{};
    std::string m_checkMessage;
    std::string m_exportParent;
    std::string m_exportFolder{"game-export"};
    bool m_exportDialog{};
    bool m_exportDataOnly{};
    std::optional<AnimationPreview> m_animationPreview;
    std::optional<AnimationDocument> m_clipDocument;

    AnimationClipboard m_animationClipboard;
    AnimationClipboardContext m_clipContext;
    float m_clipPasteTime{};
    bool m_clipPasteOverwrite{};
    Json m_clipForm = nullptr;
    Json m_clipOriginal = nullptr;
    std::vector<Animation::KeyReference> m_clipSelection;
    std::set<std::tuple<std::uint32_t, Animation::Path, float>> m_clipSelected;
    int m_clipNewNode{};
    int m_clipNewPath{};
    bool m_clipTrackScroll{};
    bool m_clipKeyScroll{};
    double m_curveFrom{};
    double m_curveTo{1};
    Json m_curveCache = nullptr;
    std::string m_curveCacheKey;

    struct ClipKeyMove {
      AnimationKeyMove move;
      std::uint64_t buffer;
      std::uint64_t revision;
      std::uint64_t session;
    };

    struct CurveKeyDrag {
      std::uint64_t session;
      float sourceTime;
      float mouseStart;
      float minX;
      float maxX;
    };

    std::optional<ClipKeyMove> m_clipMove;
    std::uint64_t m_keyMoveSession{};
    std::optional<CurveKeyDrag> m_curveDrag;
    std::map<std::uint32_t, std::pair<std::uint64_t, std::string>> m_clipBindings;
    std::uint64_t m_animationSession{};
    std::optional<PlaySession> m_play;
    std::uint64_t m_playSession{};
    std::string m_saveSlot{"checkpoint"};
    bool m_saveOverwrite{};
    bool m_saveSlotsKnown{};
    Json m_saveSlots = Json::array();
    PlayInput m_agentInput;
    std::string m_inputSource{"none"};
    Platform::InputState m_gameInput;
    bool m_captureInput{};
    bool m_pointerCaptured{};
    bool m_resetGuiInput{};
    ImVec2 m_viewOrigin{};
    Json m_playDiagnostics = Json::array();
    std::optional<Core::Error> m_playError;
    std::optional<Core::Error> m_closeError;
    std::optional<Core::Application> m_application;
    std::optional<Platform::Event> m_lastPointerPress;
    std::optional<Platform::Event> m_lastPointerRelease;
    std::optional<Platform::Event> m_lastPointerMotion;
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
    bool m_sceneDialog{};
    bool m_sceneDialogRequested{};
    bool m_sceneDialogClose{};
    int m_sceneDialogMode{};
    bool m_discardScene{};
    std::string m_scenePath{"scenes/new.dotscene"};
    std::string m_sceneName{"New Scene"};
    bool m_previewReady{};
    Graphics::ScenePreparationStage m_preparationStage{Graphics::ScenePreparationStage::Ready};
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
    std::uint64_t m_viewFrame{};
    std::optional<CaptureFrame> m_renderedFrame;
    ViewportCapture m_capture;
    bool m_captureBrowse{};
    std::shared_ptr<Platform::DialogRequest> m_captureDialog;
    Json m_captureOutput = nullptr;
    std::string m_captureMessage;
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
        value["scene"] = std::to_string(failure.entity.scene);
        diagnostic(std::move(value));
      }
    }

    auto rememberPlayFrame() -> Core::Result<void> {
      if (!m_play) {
        return Core::fail(Core::ErrorCode::InvalidState, "No Play session to read frame reports");
      }

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

    auto synchronizePointerCapture() -> Core::Result<bool> {
      auto focused = m_application->window().focused();
      auto relative = m_application->window().relativeMouse();

      if (!focused || !relative) {
        return std::unexpected(!focused ? focused.error() : relative.error());
      }

      bool requested{};

      if (m_captureInput && playing() && m_play->stats()->state == PlayState::Running) {
        auto capture = m_play->pointerCaptureRequested();

        if (!capture) {
          return std::unexpected(capture.error());
        }

        requested = *capture && *focused;
      }

      auto text = m_application->window().setTextInput(!requested);

      if (!text) {
        return std::unexpected(text.error());
      }

      auto changed = m_application->window().setRelativeMouse(requested);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      m_pointerCaptured = requested;
      return requested != *relative;
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
      if (m_clipDocument) {
        return m_clipDocument->snapshot()->dirty || m_clipForm != m_clipOriginal || m_clipMove;
      }

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

      const auto worker = m_previewWorker ? m_previewWorker->stats().value() : PreviewWorkerStats{};
      return {{"entries", entries},
              {"pending", m_thumbnailQueue.size() + worker.pending + worker.completed
                              + (worker.active ? 1 : 0)},
              {"jobs", worker.finished},
              {"device", worker.ready},
              {"serviceError", worker.initializationError.empty() ? m_previewServiceError
                                                                  : worker.initializationError},
              {"worker",
               {{"pending", worker.pending},
                {"completed", worker.completed},
                {"active", worker.active},
                {"paused", worker.paused},
                {"stopped", worker.stopped || m_previewsStopped},
                {"submitted", worker.submitted},
                {"started", worker.started},
                {"finished", worker.finished},
                {"discarded", worker.discarded}}},
              {"materialReady", currentMaterial && m_materialSample.texture.has_value()},
              {"materialPending", m_material.has_value() && !currentMaterial && !m_previewsStopped},
              {"materialError", currentMaterial ? m_materialSample.error : std::string{}},
              {"layout", m_assetGrid ? "grid" : "list"},
              {"directory", m_assetDirectory}};
    }

    auto stopPreviews() -> Core::Result<void> {
      m_previewsStopped = true;
      m_thumbnailQueue.clear();
      return m_previewWorker ? m_previewWorker->stop() : Core::Result<void>{};
    }

    auto clearPreviews(bool resetProvider = false) -> void {
      if (m_previewWorker) {
        m_previewWorker->invalidate(resetProvider).value();
      }

      m_materialRequestedKey.clear();
      m_materialTicket = 0;
      m_thumbnails.clear();
      m_thumbnailQueue.clear();
      m_materialSample = {};
      m_materialSampleKey.clear();
      m_previewServiceError.clear();
    }

    auto requestPreview(const std::string& path) -> void {
      if (m_previewsStopped || !assetPreviewKind(path)) {
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

        if (m_previewWorker) {
          m_previewWorker->cancel("asset/" + evicted).value();
        }

        m_thumbnails.erase(oldest);
        std::erase(m_thumbnailQueue, evicted);
      }

      PreviewEntry pending;
      pending.used = ++m_previewClock;
      m_thumbnails.emplace(path, std::move(pending));
      m_thumbnailQueue.push_back(path);
    }

    auto ensurePreviewWorker() -> Core::Result<void> {
      if (m_previewsStopped) {
        return Core::fail(Core::ErrorCode::InvalidState, "Preview generation is stopped");
      }

      if (m_previewWorker) {
        return {};
      }

      auto location = resource("environments");

      if (!location) {
        return std::unexpected(location.error());
      }

      auto worker = createAssetPreviewWorker(m_project->files().value(),
                                             *location / "studio_small_09_1k.hdr");

      if (!worker) {
        return std::unexpected(worker.error());
      }

      m_previewWorker.emplace(std::move(*worker));
      return {};
    }

    auto receivePreview(Core::Result<AssetPreviewImage> rendered) -> PreviewEntry {
      PreviewEntry entry;

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

    /** Decode/bake/render/readback run on one owned worker. The GUI polls up to
     * eight bounded results and uploads only current tickets; no wait for rendering.
     * 解码、烘焙、渲染、回读由单一工作线程拥有；GUI 轮询至多八份有界结果，只上传当前
     * 票号的图像，不等待渲染。 */
    auto updatePreviews() -> void {
      if (playing() || m_previewsStopped) {
        return;
      }

      if (m_material) {
        const auto key = std::to_string(m_buffer) + m_materialForm.dump();

        if (key != m_materialRequestedKey) {
          m_materialSample = {};
          m_materialSampleKey.clear();
          m_materialTicket = 0;

          if (m_previewWorker) {
            m_previewWorker->cancel("material").value();
          }

          auto value = Assets::decodeMaterial(m_materialForm.dump());
          auto available
              = value ? ensurePreviewWorker() : Core::Result<void>{std::unexpected(value.error())};

          if (!available) {
            m_materialSample.error = available.error().message;
            m_materialSampleKey = m_materialRequestedKey = key;
          } else {
            auto submitted = m_previewWorker->submit(
                {"material", m_material->snapshot()->path, 256, std::move(*value), true});

            if (submitted) {
              m_materialTicket = *submitted;
              m_materialRequestedKey = key;
            } else if (submitted.error().code != Core::ErrorCode::Unavailable) {
              m_materialSample.error = submitted.error().message;
              m_materialSampleKey = m_materialRequestedKey = key;
            }
          }
        }
      }

      for (int count = 0; count < 8 && !m_thumbnailQueue.empty(); ++count) {
        const auto path = m_thumbnailQueue.front();
        auto found = m_thumbnails.find(path);

        if (found == m_thumbnails.end()) {
          m_thumbnailQueue.pop_front();
          continue;
        }

        auto available = ensurePreviewWorker();

        if (!available) {
          found->second.error = available.error().message;
          m_previewServiceError = available.error().message;
          m_thumbnailQueue.pop_front();
          continue;
        }

        auto submitted = m_previewWorker->submit({"asset/" + path, path, 128, {}, false});

        if (!submitted && submitted.error().code == Core::ErrorCode::Unavailable) {
          break;
        }

        m_thumbnailQueue.pop_front();

        if (submitted) {
          found->second.ticket = *submitted;
        } else {
          found->second.error = submitted.error().message;
        }
      }

      if (!m_previewWorker) {
        return;
      }

      auto completed = m_previewWorker->takeCompleted().value();

      for (auto& item : completed) {
        if (item.slot == "material") {
          if (m_material && item.ticket == m_materialTicket
              && m_materialRequestedKey == std::to_string(m_buffer) + m_materialForm.dump()) {
            m_materialSample = receivePreview(std::move(item.result));
            m_materialSampleKey = m_materialRequestedKey;
          }
        } else if (item.slot.starts_with("asset/")) {
          auto found = m_thumbnails.find(item.slot.substr(6));

          if (found != m_thumbnails.end() && found->second.ticket == item.ticket) {
            const auto used = found->second.used;
            found->second = receivePreview(std::move(item.result));
            found->second.used = used;
            found->second.ticket = item.ticket;
          }
        }
      }
    }

    auto resetMaterialForm() -> void {
      m_materialForm
          = m_material
                ? Codec::parseJson(Assets::encodeMaterial(m_material->snapshot()->value).value())
                : Json(nullptr);

      if (!m_material) {
        if (m_previewWorker) {
          m_previewWorker->cancel("material").value();
        }

        m_materialSample = {};
        m_materialSampleKey.clear();
        m_materialRequestedKey.clear();
        m_materialTicket = 0;
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

    auto synchronizeMaterialPreview() -> Core::Result<void> {
      const auto previewKey = m_material && !playing()
                                  ? std::to_string(m_buffer) + m_materialForm.dump()
                                  : std::string{};

      if (previewKey != m_materialPreviewKey) {
        std::optional<Graphics::MaterialPreview> material;

        if (m_material && !playing()) {
          auto value = Assets::decodeMaterial(m_materialForm.dump());

          if (!value) {
            return std::unexpected(value.error());
          }

          material = Graphics::MaterialPreview{m_material->snapshot()->path, std::move(*value)};
        }

        auto changed = m_drawing->previewMaterial(std::move(material));

        if (!changed) {
          return changed;
        }

        m_materialPreviewKey = previewKey;
      }

      return {};
    }

    auto startPlay() -> Core::Result<void> {
      auto materialPreview = synchronizeMaterialPreview();

      if (!materialPreview) {
        return materialPreview;
      }

      auto scene = m_document->scene();
      auto stage
          = scene ? m_drawing->readiness(**scene)
                  : Core::Result<Graphics::ScenePreparationStage>{std::unexpected(scene.error())};

      if (!stage) {
        return std::unexpected(stage.error());
      }

      if (*stage != Graphics::ScenePreparationStage::Ready) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene resources are still preparing");
      }

      if (playing() || pendingInspector() || pendingEnvironment() || pendingOcclusion()
          || pendingText() || !m_application->running()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Play requires no pending scene/file edits or active game");
      }

      if (m_playSession == std::numeric_limits<std::uint64_t>::max()) {
        return Core::fail(Core::ErrorCode::Unavailable, "Play session identities exhausted");
      }

      if (m_play) {
        auto cleaned = m_play->stop();

        if (!cleaned) {
          return std::unexpected(cleaned.error());
        }
      }

      auto tool = compiler();

      if (!tool) {
        return std::unexpected(tool.error());
      }

      auto created = PlaySession::create(*m_project, *tool, *m_document->scene().value(),
                                         m_config.play, m_document->stats()->scenePath);

      if (!created) {
        return std::unexpected(created.error());
      }

      m_play.emplace(std::move(*created));
      ++m_playSession;
      m_saveSlotsKnown = false;
      m_saveSlots = Json::array();
      m_agentInput.reset();
      m_inputSource = "none";

      if (m_previewWorker) {
        m_previewWorker->setPaused(true).value();
      }

      m_playDiagnostics = Json::array();
      m_playError.reset();
      m_gameInput = Platform::InputState{};
      captureInput(false);
      m_previewReady = false;
      auto remembered = rememberPlayFrame();
      return remembered ? Core::Result<void>{}
                        : Core::fail(remembered.error().code, "Play started; report read failed: "
                                                                  + remembered.error().message);
    }

    auto stopPlay() -> Core::Result<void> {
      if (!m_play) {
        return {};
      }

      auto stopped = m_play->stop();
      m_agentInput.reset();
      m_inputSource = "none";
      captureInput(false);
      m_previewReady = false;

      if (!stopped) {
        m_playError = stopped.error();
        diagnostic(errorJson(stopped.error()));
        return std::unexpected(stopped.error());
      }

      if (m_previewWorker) {
        m_previewWorker->setPaused(false).value();
      }

      report(*stopped);
      return {};
    }

    auto playStatus() const -> Json {
      const auto agent = m_agentInput.stats();
      Json status{
          {"state", "idle"},
          {"session", m_playSession == 0 ? Json(nullptr) : Json(std::to_string(m_playSession))},
          {"inputSource", m_inputSource},
          {"agentInput",
           {{"active", agent.active},
            {"remaining", agent.remaining},
            {"published", agent.published}}},
          {"captureInput", m_captureInput},
          {"pointerCaptured", m_pointerCaptured},
          {"pointerCaptureRequested", nullptr},
          {"diagnostics", m_playDiagnostics},
          {"error", m_playError ? errorJson(*m_playError)["error"] : Json(nullptr)}};
      auto text = m_application->window().textInputActive();
      status["textInputActive"] = text ? Json(*text) : Json(nullptr);

      if (!text) {
        status["inputModeError"] = errorJson(text.error())["error"];
      }

      if (m_play) {
        const auto stats = m_play->stats().value();
        constexpr std::array states{"running", "faulted", "stopped"};
        status["state"] = states.at(static_cast<std::size_t>(stats.state));
        status["fixedHz"] = stats.fixedHz;
        status["frames"] = std::to_string(stats.frames);
        status["fixedSteps"] = std::to_string(stats.fixedSteps);
        status["droppedNanoseconds"] = std::to_string(stats.dropped.count());
        status["scene"]
            = {{"scope", std::to_string(stats.location.scope)},
               {"path", stats.location.path ? Json(*stats.location.path) : Json(nullptr)}};
        status["transition"]
            = stats.transition ? sceneRequestJson(*stats.transition) : Json(nullptr);

        if (stats.state == PlayState::Running) {
          auto requested = m_play->pointerCaptureRequested();

          if (requested) {
            status["pointerCaptureRequested"] = *requested;
          } else {
            status["pointerCaptureError"] = errorJson(requested.error())["error"];
          }
        }
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

    auto syncInspector() -> void {
      const auto stats = m_document->stats().value();

      if (!stats.selected) {
        m_editEntity.reset();
        m_edit = m_original = nullptr;
        m_propertiesText.clear();
        m_propertiesInvalid = false;
        return;
      }

      if (m_editEntity == stats.selected->value && m_editRevision == stats.revision) {
        return;
      }

      const auto found
          = std::ranges::find_if(m_snapshot["scene"]["entities"], [&](const auto& entity) {
              return entity["id"] == std::to_string(stats.selected->value);
            });
      m_editEntity = stats.selected->value;
      m_editRevision = stats.revision;
      m_original = *found;
      m_edit = *found;
      m_propertiesInvalid = false;
      m_propertiesText = m_edit["script"].is_null() ? "" : m_edit["script"]["properties"].dump(2);
    }

    auto refresh() -> Core::Result<void> {
      auto text = m_document->snapshot();

      if (!text) {
        return std::unexpected(text.error());
      }

      auto next = Codec::parseJson(*text);

      if (m_snapshot.is_object() && m_snapshot.contains("scenePath")
          && m_snapshot["scenePath"] != next["scenePath"] && m_snapshot["scope"] != next["scope"]) {
        m_hierarchyExpansion = Json::object();
      }

      std::unordered_set<std::string> live;

      for (const auto& entity : next["scene"]["entities"]) {
        live.insert(entity["id"].get<std::string>());
      }

      auto& expansion = m_hierarchyExpansion.get_ref<Json::object_t&>();
      std::erase_if(expansion, [&](const auto& entry) { return !live.contains(entry.first); });
      m_snapshot = std::move(next);
      syncInspector();
      return {};
    }

    auto displayedScene() const -> Core::Result<const World::Scene*> {
      if (playing()) {
        return m_play->scene();
      }

      if (m_animationPreview) {
        return m_animationPreview->scene();
      }

      return m_transform ? m_transform->scene() : m_document->scene();
    }

    auto animationStatus() const -> Json {
      if (!m_animationPreview) {
        return nullptr;
      }

      auto state = m_animationPreview->stats();

      if (!state) {
        return errorJson(state.error());
      }

      const auto& player = state->player;
      return {{"session", std::to_string(m_animationSession)},
              {"entity", std::to_string(state->owner)},
              {"source", std::to_string(state->source)},
              {"generation", std::to_string(state->generation)},
              {"clip", player.clip},
              {"asset", player.asset},
              {"name", player.name},
              {"clips", state->clips},
              {"duration", player.duration},
              {"time", player.playback.time},
              {"playing", player.playback.playing},
              {"speed", player.playback.speed},
              {"looping", player.playback.looping},
              {"bindPose", state->bindPose},
              {"draft", state->draft ? Json(*state->draft) : Json(nullptr)},
              {"error", state->error ? errorJson(*state->error)["error"] : Json(nullptr)}};
    }

    /** Report geometry only when it belongs to the currently completed viewport.
     * 仅报告属于当前已完成视口的几何，跨场景或未完成帧不冒充当前骨架。 */
    auto skeletonStatus() const -> Json {
      auto state = m_skeleton.stats();

      if (!state) {
        return errorJson(state.error());
      }

      const bool ready = state->frame && viewportCurrent() && m_renderedFrame
                         && state->frame->serial == m_renderedFrame->serial
                         && state->frame->pose.owner.scene == m_renderedFrame->context.scene;
      Json frame = nullptr;

      if (ready) {
        const auto& pose = state->frame->pose;
        Json joints = Json::array();
        std::size_t roots{};

        for (std::size_t i = 0; i < pose.joints.size(); ++i) {
          const auto& joint = pose.joints[i];
          const auto point = state->frame->projected[i];
          roots += !joint.parent;
          joints.push_back({{"index", i},
                            {"entity", std::to_string(joint.entity.value)},
                            {"name", joint.name},
                            {"parent", joint.parent ? Json(std::to_string(
                                                          pose.joints[*joint.parent].entity.value))
                                                    : Json(nullptr)},
                            {"parentLinks", joint.parentLinks},
                            {"world", {joint.position.x, joint.position.y, joint.position.z}},
                            {"uv", point ? Json{point->x, point->y} : Json(nullptr)}});
        }

        frame = {{"serial", std::to_string(state->frame->serial)},
                 {"scene", std::to_string(pose.owner.scene)},
                 {"asset", pose.asset},
                 {"roots", roots},
                 {"joints", std::move(joints)},
                 {"projectedJoints", state->frame->overlay.points.size()},
                 {"projectedBones", state->frame->overlay.lines.size()},
                 {"playFrame", std::to_string(m_renderedFrame->playFrame)}};
      }

      return {
          {"enabled", state->entity.has_value()},
          {"entity", state->entity ? Json(std::to_string(*state->entity)) : Json(nullptr)},
          {"selected", state->selected ? Json(std::to_string(*state->selected)) : Json(nullptr)},
          {"ready", ready},
          {"frame", std::move(frame)},
          {"filter", m_skeletonFilter},
          {"error", state->error ? errorJson(*state->error)["error"] : Json(nullptr)}};
    }

    /** Hit testing references the last displayed frame, not a newly advanced pose.
     * Native selection is revalidated against the live binding before committing.
     * 拾取引用上次显示的帧而非刚推进的姿态，提交前按实时绑定重新验证关节。 */
    auto pickSkeleton(Math::Vec2 uv, std::uint64_t serial)
        -> Core::Result<std::optional<std::uint64_t>> {
      auto state = m_skeleton.stats();
      auto scene = displayedScene();
      auto view = currentView();
      auto context = captureContext();

      if (!state || !scene) {
        return std::unexpected(!state ? state.error() : scene.error());
      }

      if (!std::isfinite(uv.x) || !std::isfinite(uv.y) || uv.x < 0 || uv.x > 1 || uv.y < 0
          || uv.y > 1) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Skeleton pick UV must be in [0,1]");
      }

      if (!view || !context || !m_renderedView || *view != *m_renderedView || m_assetRefresh
          || !m_viewVisible || !m_previewReady || !state->frame || !m_renderedFrame
          || *context != m_renderedFrame->context || state->frame->serial != serial
          || m_renderedFrame->serial != serial
          || state->frame->pose.owner.scene != (*scene)->id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Skeleton pick frame is stale");
      }

      float best = 36;
      std::optional<std::uint64_t> hit;

      for (std::size_t i = 0; i < state->frame->projected.size(); ++i) {
        if (const auto point = state->frame->projected[i]) {
          const auto x = (point->x - uv.x) * m_viewWidth;
          const auto y = (point->y - uv.y) * m_viewHeight;
          const auto distance = x * x + y * y;

          if (distance < best) {
            best = distance;
            hit = state->frame->pose.joints[i].entity.value;
          }
        }
      }

      if (hit) {
        auto selected = m_skeleton.select(**scene, m_project->files().value(), hit);

        if (!selected) {
          return std::unexpected(selected.error());
        }
      }

      return hit;
    }

    auto captureContext() const -> Core::Result<CaptureContext> {
      auto scene = displayedScene();

      if (!scene) {
        return std::unexpected(scene.error());
      }

      return CaptureContext{
          m_document->stats()->revision,
          (*scene)->id(),
          playing() ? m_playSession : (m_animationPreview ? m_animationSession : 0),
          playing()      ? ViewportMode::Play
          : m_editorView ? ViewportMode::Editor
                         : ViewportMode::Game,
          m_transform.has_value(),
          m_animationPreview.has_value()};
    }

    auto captureStatus() const -> Json {
      constexpr std::array states{"empty", "queued", "ready", "saved", "cancelled", "failed"};
      constexpr std::array modes{"editor", "game", "play"};
      const auto state = m_capture.stats();
      const auto& frame = state.frame;
      return {{"id", std::to_string(state.id)},
              {"state", states.at(static_cast<std::size_t>(state.state))},
              {"dialog", bool(m_captureDialog) || m_captureBrowse},
              {"message", m_captureMessage},
              {"output", m_captureOutput},
              {"error", state.error ? errorJson(*state.error)["error"] : Json(nullptr)},
              {"frame",
               {{"serial", std::to_string(frame.serial)},
                {"revision", std::to_string(frame.context.revision)},
                {"scene", std::to_string(frame.context.scene)},
                {"session", std::to_string(frame.context.session)},
                {"playFrame", std::to_string(frame.playFrame)},
                {"mode", modes.at(static_cast<std::size_t>(frame.context.mode))},
                {"source", frame.context.animation                    ? "animation"
                           : frame.context.transform                  ? "transform"
                           : frame.context.mode == ViewportMode::Play ? "play"
                                                                      : "document"},
                {"size", {frame.width, frame.height}},
                {"viewProjection", frame.viewProjection.data},
                {"materialPreview", frame.materialPreview},
                {"animation",
                 frame.animation ? Json{{"entity", std::to_string(frame.animation->owner)},
                                        {"generation", std::to_string(frame.animation->generation)},
                                        {"clip", frame.animation->clip},
                                        {"time", frame.animation->time},
                                        {"bindPose", frame.animation->bindPose}}
                                 : Json(nullptr)}}}};
    }

    auto viewportCurrent() const -> bool {
      auto context = captureContext();
      auto view = currentView();
      const auto previewKey = m_material && !playing()
                                  ? std::to_string(m_buffer) + m_materialForm.dump()
                                  : std::string{};

      if (!context || !view || !m_viewVisible || !m_previewReady || !m_target || !m_renderedFrame
          || !m_renderedView || *view != *m_renderedView || *context != m_renderedFrame->context
          || m_assetRefresh || previewKey != m_materialPreviewKey
          || (playing() && m_play->stats()->frames != m_renderedFrame->playFrame)) {
        return false;
      }

      if (m_animationPreview) {
        auto animation = m_animationPreview->stats();

        if (!animation || !m_renderedFrame->animation
            || animation->generation != m_renderedFrame->animation->generation) {
          return false;
        }
      }

      return true;
    }

    auto readViewport() -> Core::Result<Graphics::Image> {
      return viewportCurrent()
                 ? m_renderer->readTarget(*m_target)
                 : Core::fail(Core::ErrorCode::InvalidState,
                              m_previewError.empty() ? "Viewport has no current completed frame"
                                                     : "Viewport preview: " + m_previewError);
    }

    auto browseCapture(std::uint64_t id) -> Core::Result<void> {
      auto image = m_capture.image(id);

      if (!image) {
        return std::unexpected(image.error());
      }

      captureInput(false);
      auto released = synchronizePointerCapture();

      if (!released) {
        return std::unexpected(released.error());
      }

      const auto initial = (m_project->files()->root() / "viewport.png").generic_u8string();
      auto dialog
          = Platform::DialogRequest::open(m_application->window(), Platform::DialogKind::PngSave,
                                          std::string(initial.begin(), initial.end()));

      if (!dialog) {
        return std::unexpected(dialog.error());
      }

      m_captureDialog = std::move(*dialog);
      return {};
    }

    auto saveCapture(std::uint64_t id, const std::string& parentText, const std::string& filename,
                     bool overwrite) -> Core::Result<void> {
      auto image = m_capture.image(id);

      if (!image) {
        return std::unexpected(image.error());
      }

      if (parentText.empty() || parentText.size() > 16384 || !Core::isValidUtf8(parentText)
          || parentText.find('\0') != std::string::npos) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "PNG parent requires bounded UTF-8");
      }

      const auto path = std::filesystem::path(std::u8string(parentText.begin(), parentText.end()));

      if (!path.is_absolute()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "PNG parent must be absolute");
      }

      auto parent = Core::FileSystem::open(path);

      if (!parent) {
        return std::unexpected(parent.error());
      }

      auto plan = Assets::ImageExport::prepare(*parent, filename, overwrite);

      if (!plan) {
        return std::unexpected(plan.error());
      }

      auto report = plan->write(**image);

      if (!report) {
        return std::unexpected(report.error());
      }

      auto acknowledged = m_capture.saved(id);

      if (!acknowledged) {
        return std::unexpected(acknowledged.error());
      }

      m_captureOutput = {{"path", report->path}, {"bytes", std::to_string(report->bytes)}};
      auto refreshed = catalog();

      if (!refreshed) {
        return Core::fail(
            Core::ErrorCode::Io,
            "PNG saved at " + report->path + "; catalog refresh: " + refreshed.error().message);
      }

      return {};
    }

    auto resolveCapture() -> void {
      auto state = m_capture.stats();

      if (state.state == CaptureState::Queued) {
        auto completed = [&]() -> Core::Result<void> {
          auto image = readViewport();

          if (!image) {
            return std::unexpected(image.error());
          }

          return m_capture.complete(state.id, *m_renderedFrame, std::move(*image));
        }();

        if (!completed) {
          static_cast<void>(m_capture.fail(state.id, completed.error()));
          m_captureBrowse = false;
          m_message = completed.error().message;
        } else if (m_captureBrowse) {
          m_captureBrowse = false;
          auto opened = browseCapture(state.id);

          if (!opened) {
            m_message = m_captureMessage = opened.error().message;
          }
        }
      }

      if (!m_captureDialog) {
        return;
      }

      auto reply = m_captureDialog->take();

      if (!reply) {
        return;
      }

      m_captureDialog.reset();
      state = m_capture.stats();

      if (reply->status == Platform::DialogStatus::Cancelled) {
        sessionAction({{"command", "discard-capture"}, {"capture", std::to_string(state.id)}});
      } else if (reply->status == Platform::DialogStatus::Failed) {
        m_message = m_captureMessage = reply->error;
      } else {
        const auto path
            = std::filesystem::path(std::u8string(reply->path.begin(), reply->path.end()));
        const auto parent = path.parent_path().generic_u8string();
        const auto filename = path.filename().generic_u8string();
        sessionAction({{"command", "save-capture"},
                       {"capture", std::to_string(state.id)},
                       {"parent", std::string(parent.begin(), parent.end())},
                       {"file", std::string(filename.begin(), filename.end())},
                       {"overwrite", true}});
      }
    }

    static auto clipPath(const Json& value) -> Animation::Path {
      const auto name = value.get<std::string>();

      if (name == "translation") {
        return Animation::Path::Translation;
      }

      if (name == "rotation") {
        return Animation::Path::Rotation;
      }

      if (name == "scale") {
        return Animation::Path::Scale;
      }

      throw std::invalid_argument("Unknown animation channel path");
    }

    static auto clipInterpolation(const Json& value) -> Animation::Interpolation {
      const auto name = value.get<std::string>();

      if (name == "step") {
        return Animation::Interpolation::Step;
      }

      if (name == "linear") {
        return Animation::Interpolation::Linear;
      }

      if (name == "cubic") {
        return Animation::Interpolation::CubicSpline;
      }

      throw std::invalid_argument("Unknown animation interpolation");
    }

    static auto clipIndex(const Json& value, std::uint32_t maximum = 9999) -> std::uint32_t {
      if (!value.is_number_unsigned() || value.get<std::uint64_t>() > maximum) {
        throw std::invalid_argument("Animation index exceeds bounds");
      }

      return value.get<std::uint32_t>();
    }

    static auto curveTime(const Json& value) -> double {
      if (!value.is_number() || !std::isfinite(value.get<double>())) {
        throw std::invalid_argument("Curve time requires a finite number");
      }

      return value.get<double>();
    }

    static auto clipKey(const Json& value) -> Animation::Key {
      Codec::requireFields(value, {"time", "value", "inTangent", "outTangent"});
      const auto vector = [](const Json& item) -> Math::Vec4 {
        const auto data = Codec::numbers<4>(item);
        return {data[0], data[1], data[2], data[3]};
      };
      return {Codec::number(value["time"]), vector(value["value"]), vector(value["inTangent"]),
              vector(value["outTangent"])};
    }

    static auto keyJson(const Animation::Key& key) -> Json {
      return {
          {"time", key.time},
          {"value", {key.value.x, key.value.y, key.value.z, key.value.w}},
          {"inTangent", {key.inTangent.x, key.inTangent.y, key.inTangent.z, key.inTangent.w}},
          {"outTangent", {key.outTangent.x, key.outTangent.y, key.outTangent.z, key.outTangent.w}}};
    }

    static auto clipReferences(const Json& values) -> std::vector<Animation::KeyReference> {
      if (!values.is_array() || values.size() > Animation::MaxSelectedKeys) {
        throw std::invalid_argument("Key selection requires at most 16384 references");
      }

      std::vector<Animation::KeyReference> result;
      result.reserve(values.size());

      for (const auto& item : values) {
        Codec::requireFields(item, {"node", "path", "time"});
        result.push_back(
            {clipIndex(item["node"]), clipPath(item["path"]), Codec::number(item["time"])});
      }

      return result;
    }

    static auto referencesJson(std::span<const Animation::KeyReference> keys) -> Json {
      constexpr std::array paths{"translation", "rotation", "scale"};
      auto result = Json::array();

      for (const auto& key : keys) {
        result.push_back({{"node", key.node},
                          {"path", paths.at(static_cast<std::size_t>(key.path))},
                          {"time", key.time}});
      }

      return result;
    }

    auto setClipSelection(std::vector<Animation::KeyReference> keys) -> void {
      std::set<std::tuple<std::uint32_t, Animation::Path, float>> selected;

      for (const auto& key : keys) {
        selected.emplace(key.node, key.path, key.time);
      }

      m_clipSelection = std::move(keys);
      m_clipSelected = std::move(selected);
    }

    /** Prepare all selection/form allocations before changing product state.
     * Last reference is the active key and move anchor; input order is retained.
     * 修改产品状态前完成选择/表单分配；末引用为活动键及移键锚点，保留输入顺序。 */
    auto selectClipKeys(const AnimationSnapshot& state, std::vector<Animation::KeyReference> keys)
        -> Core::Result<void> {
      Json form = nullptr;
      std::set<std::tuple<std::uint32_t, Animation::Path, float>> selected;

      if (!keys.empty()) {
        auto valid = AnimationKeyMove::begin(state.value, keys);
        auto resolved = valid ? valid->snapshot()
                              : Core::Result<KeyMoveSnapshot>{std::unexpected(valid.error())};

        if (!resolved) {
          return std::unexpected(resolved.error());
        }

        const auto& last = resolved->keys.back();
        const auto& tracks = state.value.description().value()->tracks;
        const auto track = std::ranges::find_if(tracks, [&](const auto& item) {
          return item.node == last.node && item.path == last.path;
        });
        constexpr std::array paths{"translation", "rotation", "scale"};
        constexpr std::array modes{"step", "linear", "cubic"};
        form = {{"node", last.node},
                {"path", paths.at(static_cast<std::size_t>(last.path))},
                {"interpolation", modes.at(static_cast<std::size_t>(track->interpolation))},
                {"key", keyJson(last.key)}};

        for (std::size_t i = 0; i < keys.size(); ++i) {
          keys[i].time = resolved->keys[i].sourceTime;
          selected.emplace(keys[i].node, keys[i].path, keys[i].time);
        }
      }

      auto original = form;
      m_clipSelection = std::move(keys);
      m_clipSelected = std::move(selected);
      m_clipForm = std::move(form);
      m_clipOriginal = std::move(original);
      m_requestedPane = "inspector";
      m_clipTrackScroll = m_clipKeyScroll = true;
      return {};
    }

    static auto clipEdits(const Json& operations) -> std::vector<Animation::ClipEdit> {
      if (!operations.is_array() || operations.empty() || operations.size() > 256) {
        throw std::invalid_argument("Animation operations require 1..256 entries");
      }

      std::vector<Animation::ClipEdit> result;

      for (const auto& operation : operations) {
        const auto op = operation.at("op").get<std::string>();

        if (op == "rename") {
          Codec::requireFields(operation, {"op", "name"});
          result.push_back(Animation::RenameClip{operation["name"].get<std::string>()});
          continue;
        }

        if (op == "put-key") {
          Codec::requireFields(operation, {"op", "node", "path", "interpolation", "key"});
          result.push_back(Animation::PutKey{
              clipIndex(operation["node"]), clipPath(operation["path"]),
              clipInterpolation(operation["interpolation"]), clipKey(operation["key"])});
        } else if (op == "remove-key") {
          Codec::requireFields(operation, {"op", "node", "path", "time"});
          result.push_back(Animation::RemoveKey{clipIndex(operation["node"]),
                                                clipPath(operation["path"]),
                                                Codec::number(operation["time"])});
        } else if (op == "update-key") {
          Codec::requireFields(operation, {"op", "node", "path", "time", "key"});
          result.push_back(
              Animation::UpdateKey{clipIndex(operation["node"]), clipPath(operation["path"]),
                                   Codec::number(operation["time"]), clipKey(operation["key"])});
        } else if (op == "retime-keys") {
          Codec::requireFields(operation, {"op", "keys"});
          const auto& keys = operation["keys"];

          if (!keys.is_array() || keys.empty() || keys.size() > Animation::MaxSelectedKeys) {
            throw std::invalid_argument("Retime requires 1..16384 keys");
          }

          Animation::RetimeKeys edit;
          edit.keys.reserve(keys.size());

          for (const auto& key : keys) {
            Codec::requireFields(key, {"node", "path", "time", "target"});
            edit.keys.push_back({clipIndex(key["node"]), clipPath(key["path"]),
                                 Codec::number(key["time"]), Codec::number(key["target"])});
          }

          result.push_back(std::move(edit));
        } else if (op == "remove-track") {
          Codec::requireFields(operation, {"op", "node", "path"});
          result.push_back(
              Animation::RemoveTrack{clipIndex(operation["node"]), clipPath(operation["path"])});
        } else if (op == "set-interpolation") {
          Codec::requireFields(operation, {"op", "node", "path", "interpolation"});
          result.push_back(
              Animation::SetInterpolation{clipIndex(operation["node"]), clipPath(operation["path"]),
                                          clipInterpolation(operation["interpolation"])});
        } else {
          throw std::invalid_argument("Unknown animation edit operation");
        }
      }

      return result;
    }

    auto clipboardCompatible() const -> bool {
      return m_clipDocument && m_animationPreview && m_animationClipboard.compatible(m_clipContext);
    }

    auto clipboardStatus() const -> Json {
      const auto copied = m_animationClipboard.snapshot();

      if (!copied) {
        return nullptr;
      }

      const auto value = copied->keys.description();

      if (!value) {
        return errorJson(value.error());
      }

      return {{"id", std::to_string(copied->id)},
              {"source", std::to_string(copied->source.context.scene)},
              {"entity", std::to_string(copied->source.context.owner)},
              {"buffer", std::to_string(copied->source.buffer)},
              {"revision", std::to_string(copied->source.revision)},
              {"path", copied->source.path},
              {"keys", (*value)->keys.size()},
              {"channels", (*value)->channels},
              {"from", (*value)->start},
              {"to", (*value)->end},
              {"compatible", clipboardCompatible()}};
    }

    auto keyMoveStatus() const -> Json {
      if (!m_clipMove) {
        return nullptr;
      }

      const auto value = m_clipMove->move.snapshot();

      if (!value) {
        return errorJson(value.error());
      }

      constexpr std::array paths{"translation", "rotation", "scale"};
      auto keys = Json::array();

      for (const auto& item : value->keys) {
        keys.push_back({{"node", item.node},
                        {"path", paths.at(static_cast<std::size_t>(item.path))},
                        {"sourceTime", item.sourceTime},
                        {"time", item.key.time},
                        {"occupied", item.occupied}});
      }

      return {{"session", std::to_string(m_clipMove->session)},
              {"buffer", std::to_string(m_clipMove->buffer)},
              {"revision", std::to_string(m_clipMove->revision)},
              {"keys", std::move(keys)},
              {"time", value->time},
              {"changed", value->changed},
              {"occupied", value->occupied}};
    }

    auto clipStatus() const -> Json {
      if (!m_clipDocument) {
        return nullptr;
      }

      auto state = m_clipDocument->snapshot();

      if (!state) {
        return errorJson(state.error());
      }

      const auto* value = state->value.description().value();
      std::size_t keys{};

      for (const auto& track : value->tracks) {
        keys += track.keys.size();
      }

      return {{"buffer", std::to_string(m_buffer)},
              {"revision", std::to_string(state->revision)},
              {"path", state->path},
              {"name", value->name},
              {"duration", state->value.duration().value()},
              {"tracks", value->tracks.size()},
              {"keys", keys},
              {"dirty", state->dirty},
              {"canUndo", state->canUndo},
              {"canRedo", state->canRedo},
              {"historyStates", state->historyStates},
              {"historyBytes", state->historyBytes},
              {"curve", {{"from", m_curveFrom}, {"to", m_curveTo}}},
              {"move", keyMoveStatus()},
              {"selection", referencesJson(m_clipSelection)},
              {"form", m_clipForm},
              {"pendingForm", m_clipForm != m_clipOriginal}};
    }

    /** Curves use the native single-track evaluator, including normalized rotation
     * and sampling errors. A bounded uniform polyline is a view, not proof that
     * every intermediate pose is valid. No document/preview state is changed.
     * 曲线复用原生单轨采样，包括旋转归一化及采样失败；有界均匀折线仅供显示，不能证明
     * 所有中间姿态有效。不修改文档或预览。 */
    static auto sampleCurve(const Animation::Clip& clip, std::uint32_t node, Animation::Path path,
                            double from, double to, std::uint32_t count) -> Core::Result<Json> {
      if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || to > 86400 || to - from < 1e-6
          || count < 2 || count > 1024) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Curve requires 0..86400s, a >=1us span and 2..1024 samples");
      }

      auto source = clip.description();

      if (!source) {
        return std::unexpected(source.error());
      }

      const auto& tracks = (*source)->tracks;
      const auto found = std::ranges::find_if(
          tracks, [&](const auto& track) { return track.node == node && track.path == path; });

      if (found == tracks.end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Curve track does not exist");
      }

      Json points = Json::array();
      const auto index = static_cast<std::size_t>(found - tracks.begin());

      for (std::uint32_t i = 0; i < count; ++i) {
        const double time = i == count - 1 ? to : from + (to - from) * i / (count - 1);
        auto sample = clip.sampleTrack(index, time);

        if (!sample) {
          return Core::fail(sample.error().code,
                            "Curve at " + std::to_string(time) + "s: " + sample.error().message);
        }

        const auto value = sample->value;
        points.push_back({{"time", time}, {"value", {value.x, value.y, value.z, value.w}}});
      }

      return Json{{"ok", true}, {"from", from}, {"to", to}, {"points", std::move(points)}};
    }

    /** Preflight the candidate native pose before publishing document history.
     * Recreate only the isolated preview; retain clock/configuration and invalidate
     * old scoped control sessions after commit. Validate against authored source.
     * 提交文档历史前预检原生候选姿态；仅重建隔离预览并保留时钟/设置，提交后使旧控制会话
     * 失效，按编辑源校验权限。 */
    auto prepareClipPreview(Animation::Clip clip) -> Core::Result<AnimationPreview> {
      auto state = m_animationPreview->stats();
      auto document = m_clipDocument->snapshot();

      if (!state || !document) {
        return std::unexpected(!state ? state.error() : document.error());
      }

      if (m_animationSession >= std::numeric_limits<std::uint64_t>::max() - 1) {
        return Core::fail(Core::ErrorCode::InvalidState, "Animation session identity exhausted");
      }

      auto preview = AnimationPreview::create(
          *m_document->scene().value(), m_project->files().value(), state->owner,
          Animation::ClipAsset{document->path, std::move(clip)});

      if (!preview) {
        return std::unexpected(preview.error());
      }

      auto next = preview->stats();

      if (!next) {
        return std::unexpected(next.error());
      }

      auto configured
          = preview->configure(state->player.playback.speed, state->player.playback.looping);
      auto posed = configured
                       ? (state->bindPose ? preview->bindPose()
                                          : preview->seek(std::min(state->player.playback.time,
                                                                   next->player.duration)))
                       : configured;

      if (!posed) {
        return std::unexpected(posed.error());
      }

      if (state->player.playback.playing) {
        auto played = preview->play(next->player.clip, false);

        if (!played) {
          return std::unexpected(played.error());
        }
      }

      auto posedScene = preview->scene();

      if (!posedScene) {
        return std::unexpected(posedScene.error());
      }

      auto skins = Assets::validateSceneSkins(m_project->files().value(), **posedScene);
      return skins ? std::move(preview)
                   : Core::Result<AnimationPreview>{std::unexpected(skins.error())};
    }

    auto clipAction(Json request) -> bool {
      request["buffer"] = std::to_string(m_buffer);
      request["revision"] = std::to_string(m_clipDocument->snapshot()->revision);
      return sessionAction(request);
    }

    auto cancelKeyMove() noexcept -> void {
      m_clipMove.reset();
      m_curveDrag.reset();
    }

    enum class ClipPublication { Edit, Undo, Redo };

    /** Publish one validated resource/history change only after native preview
     * preflight. No-ops retain revision, redo and preview identity. Both ordinary
     * edits and key-move transactions use this same publication boundary.
     * 原生预览预检后才发布一次资源/历史修改；无变化保留版本、重做及预览身份。普通编辑
     * 与移键事务共用此发布边界。 */
    auto publishClipValue(const AnimationSnapshot& state, const Animation::Clip& candidate,
                          ClipPublication operation = ClipPublication::Edit) -> Core::Result<bool> {
      auto before = Animation::encodeClip(state.value);
      auto after = Animation::encodeClip(candidate);

      if (!before || !after) {
        return std::unexpected(!before ? before.error() : after.error());
      }

      if (*before == *after) {
        return false;
      }

      auto preview = prepareClipPreview(candidate);

      if (!preview) {
        return std::unexpected(preview.error());
      }

      auto changed = operation == ClipPublication::Undo ? m_clipDocument->undo(state.revision)
                     : operation == ClipPublication::Redo
                         ? m_clipDocument->redo(state.revision)
                         : m_clipDocument->replace(candidate, state.revision);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      m_animationPreview.emplace(std::move(*preview));
      ++m_animationSession;
      m_previewReady = false;
      m_renderedView.reset();
      return true;
    }

    auto saveClip() -> Core::Result<void> {
      if (m_clipMove) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Finish or cancel the key move before saving");
      }

      if (m_clipForm != m_clipOriginal) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Apply or reset the key form before saving");
      }

      auto state = m_clipDocument->snapshot();

      if (!state) {
        return std::unexpected(state.error());
      }

      auto saved = m_clipDocument->save(state->revision);

      if (!saved) {
        return saved;
      }

      m_assetRefresh = true;
      auto refreshed = catalog();
      return refreshed
                 ? Core::Result<void>{}
                 : Core::fail(refreshed.error().code, "Animation saved; resource refresh failed: "
                                                          + refreshed.error().message);
    }

    /** Read last consumed native state and latest GUI NewFrame in their logical
     * coordinate spaces. Event snapshots are bounded to one press/release/motion,
     * with no keyboard/text recording or input injection. Samples may differ in age.
     * 读取最后消费的原生状态及最近 GUI NewFrame 的逻辑坐标；事件仅各保留一次按下/释放/
     * 移动，不记录键盘文本、不注入输入。各采样更新时间可能不同。 */
    auto inputStatus() const -> Core::Result<Json> {
      auto logical = m_application->window().size();
      auto pixels = m_application->window().pixelSize();
      auto gui = m_renderer->guiStats();

      if (!logical || !pixels || !gui) {
        return std::unexpected(!logical ? logical.error() : !pixels ? pixels.error() : gui.error());
      }

      const auto& input = m_application->input();
      const auto pointer = input.pointer();
      const auto delta = input.pointerDelta();
      Json buttons = Json::array();

      for (const auto& [name, button] : Platform::buttonNames()) {
        const auto state = input.button(button);
        buttons.push_back({{"name", name},
                           {"down", state.down},
                           {"pressed", state.pressed},
                           {"released", state.released}});
      }

      const auto eventJson = [](const std::optional<Platform::Event>& event) -> Json {
        if (!event) {
          return nullptr;
        }

        Json result{{"window", event->windowId}, {"timestamp", std::to_string(event->timestamp)}};

        if (const auto* button = std::get_if<Platform::ButtonChanged>(&event->data)) {
          const bool valid = button->position && std::isfinite((*button->position)[0])
                             && std::isfinite((*button->position)[1]);
          result["button"] = static_cast<unsigned>(button->button);
          result["down"] = button->down;
          result["hasPosition"] = button->position.has_value();
          result["positionValid"] = valid;
          result["position"] = valid ? Json::array({(*button->position)[0], (*button->position)[1]})
                                     : Json(nullptr);
        } else if (const auto* motion = std::get_if<Platform::PointerMoved>(&event->data)) {
          const bool positionValid = std::isfinite(motion->x) && std::isfinite(motion->y);
          const bool deltaValid = std::isfinite(motion->deltaX) && std::isfinite(motion->deltaY);
          result["positionValid"] = positionValid;
          result["deltaValid"] = deltaValid;
          result["position"] = positionValid ? Json::array({motion->x, motion->y}) : Json(nullptr);
          result["delta"]
              = deltaValid ? Json::array({motion->deltaX, motion->deltaY}) : Json(nullptr);
        }

        return result;
      };
      const auto& consumed = gui->input;
      return Json{
          {"window",
           {{"logical", {logical->width, logical->height}},
            {"pixels", {pixels->width, pixels->height}}}},
          {"native",
           {{"pointer", {pointer.x, pointer.y}},
            {"delta", {delta.x, delta.y}},
            {"buttons", std::move(buttons)},
            {"lastPress", eventJson(m_lastPointerPress)},
            {"lastRelease", eventJson(m_lastPointerRelease)},
            {"lastMotion", eventJson(m_lastPointerMotion)}}},
          {"gui",
           {{"frame", std::to_string(consumed.frame)},
            {"logicalSize", {consumed.logicalSize.x, consumed.logicalSize.y}},
            {"framebufferScale", {consumed.framebufferScale.x, consumed.framebufferScale.y}},
            {"pointer", consumed.pointer ? Json::array({consumed.pointer->x, consumed.pointer->y})
                                         : Json(nullptr)},
            {"buttons", consumed.buttons}}}};
    }

    auto execute(std::string_view request) -> Core::Result<std::string> {
      auto valid = validRequest(request);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      auto parsed = Codec::parseJson(request);

      const auto command = parsed.value("command", std::string{});

      const bool sceneOperation
          = command == "open-scene" || command == "create-scene" || command == "save-scene-as";

      if (command == "scene-dialog") {
        Codec::requireFields(parsed, {"command", "mode"});
        const auto mode = parsed["mode"].get<std::string>();

        if (mode != "open" && mode != "create" && mode != "save-as" && mode != "close") {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown scene dialog mode");
        }

        if (mode == "close") {
          m_sceneDialog = false;
          m_sceneDialogRequested = false;
          m_sceneDialogClose = true;
          return execute("{\"command\":\"inspect\"}");
        }

        if (playing() || pendingInspector() || pendingEnvironment() || pendingOcclusion()
            || pendingText() || m_animationPreview || m_transform || m_pendingClose
            || m_captureDialog) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Finish active Play, drafts, gestures and dialogs before scene files");
        }

        m_sceneDialogMode = mode == "open" ? 0 : mode == "create" ? 1 : 2;
        m_scenePath = mode == "open" ? (m_selectedResource.ends_with(".dotscene")
                                            ? m_selectedResource
                                            : m_document->stats()->scenePath)
                                     : "scenes/new.dotscene";
        m_discardScene = false;
        m_sceneDialog = true;
        m_sceneDialogRequested = true;
        m_sceneDialogClose = false;
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "stage-scene-dialog" || command == "confirm-scene-dialog") {
        if (command == "stage-scene-dialog") {
          Codec::requireFields(parsed, {"command", "revision", "path", "name", "discard"});
        } else {
          Codec::requireFields(parsed, {"command", "revision"});
        }

        if (!m_sceneDialog || Codec::id(parsed["revision"]) != m_document->stats()->revision) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Scene dialog requires an active form and current revision");
        }

        if (command == "stage-scene-dialog") {
          auto path = parsed["path"].get<std::string>();
          auto name = parsed["name"].get<std::string>();
          const auto discard = parsed["discard"].get<bool>();
          const auto validSceneFormText = [](const std::string& value) {
            return value.size() <= 4096 && value.find('\0') == std::string::npos
                   && Core::isValidUtf8(value);
          };

          if (!validSceneFormText(path) || !validSceneFormText(name)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Scene form text requires UTF-8 without NUL, at most 4096 bytes");
          }

          m_scenePath.swap(path);
          m_sceneName.swap(name);
          m_discardScene = discard;
          return execute("{\"command\":\"inspect\"}");
        }

        Json operation{{"command", m_sceneDialogMode == 0   ? "open-scene"
                                   : m_sceneDialogMode == 1 ? "create-scene"
                                                            : "save-scene-as"},
                       {"revision", parsed["revision"]},
                       {"path", m_scenePath}};

        if (m_sceneDialogMode != 2) {
          operation["discard"] = m_discardScene;
        }

        if (m_sceneDialogMode == 1) {
          operation["name"] = m_sceneName;
        }

        return execute(operation.dump());
      }

      if (command == "inspect-scene-overrides") {
        auto result = querySceneOverrides(parsed);

        if (!result) {
          return std::unexpected(result.error());
        }

        auto status = workspaceStatus();

        if (!status) {
          return std::unexpected(status.error());
        }

        auto response = Codec::parseJson(*result);
        response["workspace"] = std::move(*status);
        return response.dump();
      }

      if (m_sceneDialog && !sceneOperation && command != "inspect" && command != "save"
          && command != "close" && command != "save-close" && command != "cancel-close"
          && command != "request-focus" && command != "resources"
          && command != "inspect-animation-clipboard" && command != "read-animation-clipboard") {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Complete or close the scene file dialog first");
      }

      if (sceneOperation && (pendingText() || m_pendingClose || m_captureDialog)) {
        return Core::fail(
            Core::ErrorCode::InvalidState,
            "Save or close resource drafts and finish active dialogs before scene files");
      }

      if (command == "inspect-animation-clipboard" || command == "read-animation-clipboard"
          || command == "clear-animation-clipboard") {
        if (command == "inspect-animation-clipboard") {
          Codec::requireFields(parsed, {"command"});
          return Json{{"ok", true}, {"clipboard", clipboardStatus()}}.dump();
        }

        if (command == "read-animation-clipboard") {
          Codec::requireFields(parsed, {"command", "clipboard", "offset", "count"});
        } else {
          Codec::requireFields(parsed, {"command", "clipboard"});
        }

        const auto copied = m_animationClipboard.read(Codec::id(parsed["clipboard"]));

        if (!copied) {
          return std::unexpected(copied.error());
        }

        if (command == "clear-animation-clipboard") {
          auto cleared = m_animationClipboard.clear(Codec::id(parsed["clipboard"]));

          if (!cleared) {
            return std::unexpected(cleared.error());
          }

          m_clipPasteTime = 0;
          m_clipPasteOverwrite = false;
          return Json{{"ok", true}, {"clipboard", nullptr}}.dump();
        }

        const auto value = (*copied)->keys.description();

        if (!value) {
          return std::unexpected(value.error());
        }

        const auto offset
            = clipIndex(parsed["offset"], static_cast<std::uint32_t>(Animation::MaxSelectedKeys));
        const auto count = clipIndex(parsed["count"], 256);

        if (count == 0 || offset > (*value)->keys.size()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Clipboard page requires valid offset and 1..256 keys");
        }

        auto keys = Json::array();
        constexpr std::array paths{"translation", "rotation", "scale"};
        constexpr std::array modes{"step", "linear", "cubic"};

        for (std::size_t i = offset;
             i < std::min((*value)->keys.size(), std::size_t(offset) + count); ++i) {
          const auto& item = (*value)->keys[i];
          keys.push_back({{"node", item.node},
                          {"path", paths.at(static_cast<std::size_t>(item.path))},
                          {"interpolation", modes.at(static_cast<std::size_t>(item.interpolation))},
                          {"key", keyJson(item.key)}});
        }

        return Json{{"ok", true},
                    {"clipboard", clipboardStatus()},
                    {"offset", offset},
                    {"keys", std::move(keys)}}
            .dump();
      }

      if (command == "open-animation") {
        Codec::requireFields(parsed, {"command", "revision", "entity", "clip"});

        if (playing() || m_animationPreview || m_transform || pendingInspector()
            || pendingEnvironment() || pendingOcclusion() || pendingText() || m_pendingClose
            || m_captureDialog || Codec::id(parsed["revision"]) != m_document->stats()->revision
            || m_animationSession >= std::numeric_limits<std::uint64_t>::max() - 1
            || m_buffer == std::numeric_limits<std::uint64_t>::max()) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Animation editing requires current authoring revision and no other "
                            "mode/draft/dialog");
        }

        auto scene = m_document->scene().value();
        auto owner = scene->findEntity(Codec::id(parsed["entity"]));

        if (!owner) {
          return std::unexpected(owner.error());
        }

        auto info = scene->entity(*owner);
        const auto index = clipIndex(parsed["clip"], 1023);

        if (!info || !info->animation || !info->animation->enabled
            || index >= info->animation->clips.size()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Choose an enabled player's library clip");
        }

        auto document = AnimationDocument::open(*m_project, info->animation->clips[index]);

        if (!document) {
          return std::unexpected(document.error());
        }

        auto draft = document->snapshot();

        if (!draft) {
          return std::unexpected(draft.error());
        }

        auto preview = AnimationPreview::create(*scene, m_project->files().value(), owner->value,
                                                Animation::ClipAsset{draft->path, draft->value});

        if (!preview) {
          return std::unexpected(preview.error());
        }

        const auto audition = preview->stats();

        if (!audition) {
          return std::unexpected(audition.error());
        }

        AnimationClipboardContext context{audition->source, audition->owner, {}};
        std::map<std::uint32_t, std::pair<std::uint64_t, std::string>> bindings;

        for (const auto& target : info->animation->targets) {
          auto entity = scene->findEntity(target.entity);
          auto item = entity ? scene->entity(*entity)
                             : Core::Result<World::EntityInfo>{std::unexpected(entity.error())};

          if (!item) {
            return std::unexpected(item.error());
          }

          bindings.emplace(target.node, std::pair{target.entity, std::move(item->name)});
          context.bindings.emplace(target.node, target.entity);
        }

        m_text.reset();
        m_material.reset();
        resetMaterialForm();
        m_clipDocument.emplace(std::move(*document));
        m_animationPreview.emplace(std::move(*preview));
        m_clipBindings.swap(bindings);
        m_clipContext = std::move(context);
        m_clipForm = m_clipOriginal = nullptr;
        setClipSelection({});
        m_curveFrom = 0;
        m_curveTo = std::max(1., draft->value.duration().value());
        m_curveCacheKey.clear();
        m_curveCache = nullptr;
        cancelKeyMove();
        ++m_animationSession;
        ++m_buffer;
        m_editorView = true;
        m_flying = m_orbiting = m_panning = false;
        m_previewReady = false;
        m_requestedPane = "animation";
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "begin-animation-key-move" || command == "update-animation-key-move"
          || command == "finish-animation-key-move") {
        const bool begin = command == "begin-animation-key-move";
        const bool finish = command == "finish-animation-key-move";

        if (begin) {
          Codec::requireFields(parsed, {"command", "buffer", "revision"});
        } else {
          Codec::requireFields(parsed, {"command", "session", finish ? "commit" : "time"});
        }

        const bool cancel = finish && !parsed["commit"].get<bool>();

        if (!m_clipDocument || !m_animationPreview
            || ((m_pendingClose || m_captureDialog) && !cancel)) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Key move requires a live animation document without a pending dialog");
        }

        auto state = m_clipDocument->snapshot();

        if (!state) {
          return std::unexpected(state.error());
        }

        if (begin) {
          if (m_clipMove || Codec::id(parsed["buffer"]) != m_buffer
              || Codec::id(parsed["revision"]) != state->revision || m_clipForm != m_clipOriginal
              || m_keyMoveSession >= std::numeric_limits<std::uint64_t>::max() - 1) {
            return Core::fail(
                Core::ErrorCode::InvalidState,
                "Key move requires current resource identity, a clean form and no active move");
          }

          auto move = AnimationKeyMove::begin(state->value, m_clipSelection);

          if (!move) {
            return std::unexpected(move.error());
          }

          m_clipMove.emplace(std::move(*move), m_buffer, state->revision, ++m_keyMoveSession);
        } else {
          if (!m_clipMove || Codec::id(parsed["session"]) != m_clipMove->session
              || m_clipMove->buffer != m_buffer || m_clipMove->revision != state->revision) {
            return Core::fail(Core::ErrorCode::InvalidState, "Animation key move session is stale");
          }

          if (cancel) {
            cancelKeyMove();
          } else if (!finish) {
            auto updated = m_clipMove->move.update(Codec::number(parsed["time"]));

            if (!updated) {
              return std::unexpected(updated.error());
            }
          } else {
            auto edit = m_clipMove->move.edit();

            if (!edit) {
              return std::unexpected(edit.error());
            }

            const std::array<Animation::ClipEdit, 1> edits{*edit};
            auto candidate = Animation::editClip(state->value, edits);

            if (!candidate) {
              return std::unexpected(candidate.error());
            }

            std::vector<Animation::KeyReference> keys;
            keys.reserve(edit->keys.size());

            for (const auto& item : edit->keys) {
              keys.push_back({item.node, item.path, item.target});
            }

            auto published = publishClipValue(*state, *candidate);

            if (!published) {
              return std::unexpected(published.error());
            }

            cancelKeyMove();
            auto selected = selectClipKeys(m_clipDocument->snapshot().value(), std::move(keys));

            if (!selected) {
              m_clipForm = m_clipOriginal = nullptr;
              setClipSelection({});
              return Core::fail(selected.error().code, "Key move committed; selection failed: "
                                                           + selected.error().message);
            }
          }
        }

        return Json{{"ok", true}, {"animationClip", clipStatus()}, {"preview", animationStatus()}}
            .dump();
      }

      if (command == "inspect-animation-clip" || command == "list-animation-tracks"
          || command == "read-animation-track" || command == "select-animation-key"
          || command == "select-animation-keys" || command == "select-all-animation-keys"
          || command == "copy-animation-keys" || command == "paste-animation-keys"
          || command == "stage-animation-key" || command == "reset-animation-key"
          || command == "apply-animation-key" || command == "move-animation-key"
          || command == "read-animation-curve" || command == "set-animation-curve-view"
          || command == "edit-animation-clip" || command == "undo-animation-clip"
          || command == "redo-animation-clip" || command == "save-animation-clip"
          || command == "close-animation") {
        if (command == "inspect-animation-clip") {
          Codec::requireFields(parsed, {"command", "buffer"});
        } else if (command == "list-animation-tracks") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "offset", "count"});
        } else if (command == "read-animation-track") {
          Codec::requireFields(
              parsed, {"command", "buffer", "revision", "node", "path", "offset", "count"});
        } else if (command == "paste-animation-keys") {
          Codec::requireFields(parsed,
                               {"command", "buffer", "revision", "clipboard", "time", "overwrite"});
        } else if (command == "select-animation-keys") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "keys"});
        } else if (command == "select-animation-key") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "node", "path", "time"});
        } else if (command == "stage-animation-key") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "value"});
        } else if (command == "read-animation-curve") {
          Codec::requireFields(
              parsed, {"command", "buffer", "revision", "node", "path", "from", "to", "samples"});
        } else if (command == "set-animation-curve-view") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "from", "to"});
        } else if (command == "edit-animation-clip") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "operations"});
        } else if (command == "close-animation") {
          Codec::requireFields(parsed, {"command", "buffer", "revision", "discard"});
        } else {
          Codec::requireFields(parsed, {"command", "buffer", "revision"});
        }

        if (!m_clipDocument || !m_animationPreview || Codec::id(parsed["buffer"]) != m_buffer) {
          return Core::fail(Core::ErrorCode::InvalidState, "Animation resource buffer is stale");
        }

        auto state = m_clipDocument->snapshot();

        if (!state) {
          return std::unexpected(state.error());
        }

        if (command == "inspect-animation-clip") {
          return Json{{"ok", true}, {"animationClip", clipStatus()}, {"preview", animationStatus()}}
              .dump();
        }

        if (Codec::id(parsed["revision"]) != state->revision || m_pendingClose) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Animation resource revision is stale or close is pending");
        }

        const auto* value = state->value.description().value();
        constexpr std::array paths{"translation", "rotation", "scale"};
        constexpr std::array modes{"step", "linear", "cubic"};

        if (m_clipMove && command != "read-animation-curve" && command != "list-animation-tracks"
            && command != "read-animation-track"
            && !(command == "close-animation" && parsed["discard"].get<bool>())) {
          return Core::fail(
              Core::ErrorCode::InvalidState,
              "Finish or cancel the key move before changing resource, form or time range");
        }

        if (command == "copy-animation-keys" || command == "paste-animation-keys") {
          if (m_clipForm != m_clipOriginal || m_captureDialog) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Copy/paste requires a clean key form without a pending dialog");
          }

          const auto preview = m_animationPreview->stats();

          if (!preview) {
            return std::unexpected(preview.error());
          }

          if (command == "copy-animation-keys") {
            auto copied = m_animationClipboard.copy(
                state->value, m_clipSelection,
                AnimationClipboardSource{m_clipContext, m_buffer, state->revision, state->path});

            if (!copied) {
              return std::unexpected(copied.error());
            }

            m_clipPasteTime = static_cast<float>(preview->player.playback.time);
            m_clipPasteOverwrite = false;
            return Json{{"ok", true}, {"clipboard", clipboardStatus()}}.dump();
          }

          const auto time = Codec::number(parsed["time"]);
          const bool overwrite = parsed["overwrite"].get<bool>();
          auto prepared = m_animationClipboard.prepare(
              state->value, m_clipContext, Codec::id(parsed["clipboard"]), time,
              overwrite ? Animation::KeyCollision::Replace : Animation::KeyCollision::Reject);

          if (!prepared) {
            return std::unexpected(prepared.error());
          }

          auto published = publishClipValue(*state, prepared->value);

          if (!published) {
            return std::unexpected(published.error());
          }

          auto selected
              = selectClipKeys(m_clipDocument->snapshot().value(), std::move(prepared->selection));

          if (!selected) {
            m_clipForm = m_clipOriginal = nullptr;
            setClipSelection({});
            return Core::fail(selected.error().code,
                              "Keys pasted; selection failed: " + selected.error().message);
          }

          m_clipPasteTime = time;
          m_clipPasteOverwrite = overwrite;
          return Json{{"ok", true},
                      {"animationClip", clipStatus()},
                      {"preview", animationStatus()},
                      {"clipboard", clipboardStatus()}}
              .dump();
        }

        if (command == "select-animation-keys" || command == "select-all-animation-keys") {
          if (m_clipForm != m_clipOriginal) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Apply or reset the key form before changing selection");
          }

          std::vector<Animation::KeyReference> keys;

          if (command == "select-all-animation-keys") {
            for (const auto& track : value->tracks) {
              if (track.keys.size() > Animation::MaxSelectedKeys - keys.size()) {
                return Core::fail(Core::ErrorCode::InvalidArgument,
                                  "Selection exceeds 16384 keys; select a smaller group");
              }

              for (const auto& key : track.keys) {
                keys.push_back({track.node, track.path, key.time});
              }
            }
          } else {
            keys = clipReferences(parsed["keys"]);
          }

          auto selected = selectClipKeys(*state, std::move(keys));

          if (!selected) {
            return std::unexpected(selected.error());
          }

          return Json{{"ok", true}, {"animationClip", clipStatus()}}.dump();
        }

        if (m_clipSelection.size() > 1
            && (command == "stage-animation-key" || command == "apply-animation-key"
                || command == "move-animation-key")) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Select one key before editing its values or tangents");
        }

        if (command == "read-animation-curve") {
          auto curve = sampleCurve(state->value, clipIndex(parsed["node"]),
                                   clipPath(parsed["path"]), curveTime(parsed["from"]),
                                   curveTime(parsed["to"]), clipIndex(parsed["samples"], 1024));
          return curve ? Core::Result<std::string>{curve->dump()}
                       : Core::Result<std::string>{std::unexpected(curve.error())};
        }

        if (command == "set-animation-curve-view") {
          const auto from = curveTime(parsed["from"]);
          const auto to = curveTime(parsed["to"]);

          if (from < 0 || to > 86400 || to - from < 1e-6) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Curve view requires 0..86400s and a >=1us span");
          }

          m_curveFrom = from;
          m_curveTo = to;
          m_curveDrag.reset();
          return Json{{"ok", true}, {"animationClip", clipStatus()}}.dump();
        }

        if (command == "list-animation-tracks" || command == "read-animation-track") {
          const auto offset = clipIndex(parsed["offset"], 1000000);
          const auto count = clipIndex(parsed["count"], 256);

          if (!count) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Animation page count requires 1..256");
          }

          Json entries = Json::array();

          if (command == "list-animation-tracks") {
            if (offset > value->tracks.size()) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Track page offset exceeds total");
            }

            for (std::size_t i = offset;
                 i < std::min(value->tracks.size(), std::size_t(offset) + count); ++i) {
              const auto& track = value->tracks[i];
              entries.push_back(
                  {{"node", track.node},
                   {"entity", std::to_string(m_clipBindings.at(track.node).first)},
                   {"name", m_clipBindings.at(track.node).second},
                   {"path", paths.at(static_cast<std::size_t>(track.path))},
                   {"interpolation", modes.at(static_cast<std::size_t>(track.interpolation))},
                   {"keys", track.keys.size()}});
            }

            return Json{{"ok", true},
                        {"total", value->tracks.size()},
                        {"offset", offset},
                        {"tracks", std::move(entries)}}
                .dump();
          }

          const auto node = clipIndex(parsed["node"]);
          const auto path = clipPath(parsed["path"]);
          auto track = std::ranges::find_if(value->tracks, [&](const auto& item) {
            return item.node == node && item.path == path;
          });

          if (track == value->tracks.end() || offset > track->keys.size()) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Animation track/page does not exist");
          }

          for (std::size_t i = offset;
               i < std::min(track->keys.size(), std::size_t(offset) + count); ++i) {
            entries.push_back(keyJson(track->keys[i]));
          }

          return Json{{"ok", true},
                      {"node", node},
                      {"path", paths.at(static_cast<std::size_t>(path))},
                      {"interpolation", modes.at(static_cast<std::size_t>(track->interpolation))},
                      {"total", track->keys.size()},
                      {"offset", offset},
                      {"keys", std::move(entries)}}
              .dump();
        }

        if (command == "close-animation") {
          if (pendingText() && !parsed["discard"].get<bool>()) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Save or explicitly discard animation changes before closing");
          }

          m_clipDocument.reset();
          m_clipContext = {};
          m_animationPreview.reset();
          m_clipForm = m_clipOriginal = nullptr;
          setClipSelection({});
          cancelKeyMove();
          m_previewReady = false;
          return execute("{\"command\":\"inspect\"}");
        }

        if (command == "stage-animation-key") {
          const auto& form = parsed["value"];
          Codec::requireFields(form, {"node", "path", "interpolation", "key"});
          const auto node = clipIndex(form["node"]);
          static_cast<void>(clipPath(form["path"]));
          static_cast<void>(clipInterpolation(form["interpolation"]));
          const auto key = clipKey(form["key"]);
          auto canonical = form;
          canonical["node"] = node;
          canonical["key"] = keyJson(key);
          m_clipForm = std::move(canonical);
          return Json{{"ok", true}, {"animationClip", clipStatus()}}.dump();
        }

        if (command == "reset-animation-key") {
          m_clipForm = m_clipOriginal;
          return Json{{"ok", true}, {"animationClip", clipStatus()}}.dump();
        }

        if (command == "select-animation-key") {
          if (m_clipForm != m_clipOriginal) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Apply or reset the key form before changing selection");
          }

          const auto node = clipIndex(parsed["node"]);
          const auto path = clipPath(parsed["path"]);

          if (!parsed["time"].is_null()) {
            auto selected = selectClipKeys(*state, {{node, path, Codec::number(parsed["time"])}});

            if (!selected) {
              return std::unexpected(selected.error());
            }

            return Json{{"ok", true}, {"animationClip", clipStatus()}}.dump();
          }

          const auto track = std::ranges::find_if(value->tracks, [&](const auto& item) {
            return item.node == node && item.path == path;
          });
          Animation::Key key;

          {
            auto preview = m_animationPreview->scene();
            auto owner = preview ? (*preview)->findEntity(m_animationPreview->stats()->owner)
                                 : Core::Result<World::EntityId>{std::unexpected(preview.error())};
            auto info = owner ? (*preview)->entity(*owner)
                              : Core::Result<World::EntityInfo>{std::unexpected(owner.error())};

            if (!info || !info->animation) {
              return Core::fail(Core::ErrorCode::InvalidState,
                                "Animation preview owner is unavailable");
            }

            auto binding
                = std::ranges::find(info->animation->targets, node, &Animation::Target::node);

            if (binding == info->animation->targets.end()) {
              return Core::fail(Core::ErrorCode::NotFound,
                                "Node is not bound to the preview player");
            }

            auto entity = (*preview)->findEntity(binding->entity);
            auto pose = entity ? (*preview)->transform(*entity)
                               : Core::Result<World::Transform>{std::unexpected(entity.error())};

            if (!pose) {
              return std::unexpected(pose.error());
            }

            key.time = static_cast<float>(m_animationPreview->stats()->player.playback.time);
            key.value = path == Animation::Path::Translation
                            ? Math::Vec4{pose->position.x, pose->position.y, pose->position.z, 0}
                        : path == Animation::Path::Scale
                            ? Math::Vec4{pose->scale.x, pose->scale.y, pose->scale.z, 0}
                            : Math::Vec4{pose->rotation.x, pose->rotation.y, pose->rotation.z,
                                         pose->rotation.w};
          }

          m_clipForm
              = {{"node", node},
                 {"path", paths.at(static_cast<std::size_t>(path))},
                 {"interpolation", track == value->tracks.end()
                                       ? "linear"
                                       : modes.at(static_cast<std::size_t>(track->interpolation))},
                 {"key", keyJson(key)}};
          m_clipOriginal = m_clipForm;
          setClipSelection({});
          m_requestedPane = "inspector";
          m_clipTrackScroll = m_clipKeyScroll = true;
          return Json{{"ok", true}, {"animationClip", clipStatus()}}.dump();
        }

        if (command == "save-animation-clip") {
          auto saved = saveClip();

          if (!saved) {
            return std::unexpected(saved.error());
          }

          return Json{{"ok", true}, {"animationClip", clipStatus()}}.dump();
        }

        if (command != "apply-animation-key" && command != "move-animation-key"
            && m_clipForm != m_clipOriginal) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Apply or reset the key form before editing history");
        }

        Core::Result<Animation::Clip> candidate = state->value;

        if (command == "undo-animation-clip" || command == "redo-animation-clip") {
          candidate
              = m_clipDocument->historyValue(command == "redo-animation-clip", state->revision);
        } else {
          const bool formEdit = command == "apply-animation-key" || command == "move-animation-key";
          auto operations = formEdit ? Json::array() : parsed["operations"];

          if (formEdit) {
            if (m_clipForm.is_null()) {
              return Core::fail(Core::ErrorCode::InvalidState, "Select or stage a key first");
            }

            auto put = m_clipForm;

            if (command == "move-animation-key") {
              if (m_clipOriginal.is_null() || m_clipOriginal["node"] != m_clipForm["node"]
                  || m_clipOriginal["path"] != m_clipForm["path"]
                  || m_clipOriginal["interpolation"] != m_clipForm["interpolation"]) {
                return Core::fail(
                    Core::ErrorCode::InvalidState,
                    "Move requires an existing selected key on the same channel/mode");
              }

              put.erase("interpolation");
              put["op"] = "update-key";
              put["time"] = m_clipOriginal["key"]["time"];
            } else {
              put["op"] = "put-key";
            }

            operations.push_back(std::move(put));
          }

          auto edits = clipEdits(operations);
          candidate = Animation::editClip(state->value, edits);
        }

        if (!candidate) {
          return std::unexpected(candidate.error());
        }

        auto published
            = publishClipValue(*state, *candidate,
                               command == "undo-animation-clip"   ? ClipPublication::Undo
                               : command == "redo-animation-clip" ? ClipPublication::Redo
                                                                  : ClipPublication::Edit);

        if (!published) {
          return std::unexpected(published.error());
        }

        if (command == "apply-animation-key" || command == "move-animation-key") {
          m_clipOriginal = m_clipForm;
          const auto key = clipKey(m_clipForm["key"]);
          setClipSelection(
              {{clipIndex(m_clipForm["node"]), clipPath(m_clipForm["path"]), key.time}});
        } else if (*published) {
          m_clipForm = m_clipOriginal = nullptr;
          setClipSelection({});
        }

        return Json{{"ok", true}, {"animationClip", clipStatus()}, {"preview", animationStatus()}}
            .dump();
      }

      if (command == "set-skeleton" || command == "select-skeleton-joint"
          || command == "focus-skeleton" || command == "inspect-skeleton"
          || command == "pick-skeleton") {
        if (command == "inspect-skeleton") {
          Codec::requireFields(parsed, {"command", "scene"});
        } else if (command == "pick-skeleton") {
          Codec::requireFields(parsed, {"command", "scene", "uv"});
        } else if (command == "set-skeleton") {
          Codec::requireFields(parsed, {"command", "scene", "entity"});
        } else {
          Codec::requireFields(parsed, {"command", "scene", "entity", "joint"});
        }

        auto scene = displayedScene();

        if (!scene || Codec::id(parsed["scene"]) != (*scene)->id()) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Skeleton operation requires current scene");
        }

        if (command == "inspect-skeleton") {
          return Json{{"ok", true}, {"skeleton", skeletonStatus()}}.dump();
        }

        if (command == "pick-skeleton") {
          const auto uv = Codec::numbers<2>(parsed["uv"]);
          const auto serial = m_renderedFrame ? m_renderedFrame->serial : 0;
          auto hit = pickSkeleton({uv[0], uv[1]}, serial);
          return hit ? Core::Result<std::string>{Json{
                           {"ok", true},
                           {"frame", std::to_string(serial)},
                           {"hit", *hit ? Json(std::to_string(**hit)) : Json(nullptr)},
                           {"skeleton",
                            skeletonStatus()}}.dump()}
                     : std::unexpected(hit.error());
        }

        const auto entity = parsed["entity"].is_null() ? std::optional<std::uint64_t>{}
                                                       : std::optional{Codec::id(parsed["entity"])};
        Core::Result<void> applied;

        if (command == "set-skeleton") {
          applied = m_skeleton.set(**scene, m_project->files().value(), entity);
        } else {
          auto state = m_skeleton.stats();

          if (!state || !entity || state->entity != entity) {
            return Core::fail(Core::ErrorCode::InvalidState, "Skeleton target changed");
          }

          const auto joint = parsed["joint"].is_null() ? std::optional<std::uint64_t>{}
                                                       : std::optional{Codec::id(parsed["joint"])};

          if (command == "select-skeleton-joint") {
            applied = m_skeleton.select(**scene, m_project->files().value(), joint);
          } else {
            if (playing() || m_transform) {
              return Core::fail(Core::ErrorCode::InvalidState,
                                "Skeleton focus requires editor navigation");
            }

            auto bounds = m_skeleton.bounds(**scene, m_project->files().value(), joint);
            applied = bounds ? m_editorCamera.focus(*bounds, m_viewWidth / m_viewHeight)
                             : std::unexpected(bounds.error());

            if (applied) {
              m_editorView = true;
            }
          }
        }

        if (!applied) {
          return std::unexpected(applied.error());
        }

        m_requestedPane = "skeleton";
        return Json{{"ok", true}, {"skeleton", skeletonStatus()}}.dump();
      }

      if (command == "preview-animation") {
        Codec::requireFields(parsed, {"command", "revision", "entity"});

        if (playing() || m_animationPreview || m_transform || pendingInspector()
            || pendingEnvironment() || pendingOcclusion() || pendingText() || m_pendingClose
            || m_captureDialog || Codec::id(parsed["revision"]) != m_document->stats()->revision
            || m_animationSession >= std::numeric_limits<std::uint64_t>::max() - 1) {
          return Core::fail(
              Core::ErrorCode::InvalidState,
              "Animation preview requires current revision without another mode/draft/dialog");
        }

        auto preview = AnimationPreview::create(
            *m_document->scene().value(), m_project->files().value(), Codec::id(parsed["entity"]));

        if (!preview) {
          return std::unexpected(preview.error());
        }

        m_animationPreview.emplace(std::move(*preview));
        ++m_animationSession;
        m_editorView = true;
        m_flying = m_orbiting = m_panning = false;
        m_previewReady = false;
        m_requestedPane = "viewport";
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "control-animation-preview" || command == "inspect-animation-preview") {
        if (command == "inspect-animation-preview") {
          Codec::requireFields(parsed, {"command", "session"});
        } else {
          const auto action = parsed.at("action").get<std::string>();

          if (action == "play") {
            Codec::requireFields(parsed, {"command", "session", "action", "clip", "restart"});
          } else if (action == "seek") {
            Codec::requireFields(parsed, {"command", "session", "action", "seconds"});
          } else if (action == "configure") {
            Codec::requireFields(parsed, {"command", "session", "action", "speed", "looping"});
          } else if (action == "pause" || action == "bind-pose" || action == "end") {
            Codec::requireFields(parsed, {"command", "session", "action"});
          } else {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown animation preview action");
          }
        }

        if (!m_animationPreview || Codec::id(parsed["session"]) != m_animationSession) {
          return Core::fail(Core::ErrorCode::InvalidState, "Animation preview session is stale");
        }

        if (command == "inspect-animation-preview") {
          auto encoded = World::encodeScene(*m_animationPreview->scene().value());
          return encoded ? Core::Result<std::string>{Json{{"ok", true},
                                                          {"preview", animationStatus()},
                                                          {"scene", Codec::parseJson(*encoded)}}
                                                         .dump()}
                         : std::unexpected(encoded.error());
        }

        const auto action = parsed.at("action").get<std::string>();
        Core::Result<void> applied;

        if (action == "end") {
          if (m_clipDocument && pendingText()) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Save or explicitly discard animation changes before ending preview");
          }

          m_clipDocument.reset();
          m_clipContext = {};
          m_clipForm = m_clipOriginal = nullptr;
          setClipSelection({});
          cancelKeyMove();
          m_animationPreview.reset();
        } else if (action == "play") {
          if (!parsed["clip"].is_number_unsigned()
              || parsed["clip"].get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Clip must be an unsigned index");
          }

          if (m_clipDocument
              && parsed["clip"].get<std::uint32_t>() != m_animationPreview->stats()->player.clip) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Close the animation document before switching library clips");
          }

          applied = m_animationPreview->play(parsed["clip"].get<std::uint32_t>(),
                                             parsed["restart"].get<bool>());
        } else if (action == "pause") {
          applied = m_animationPreview->pause();
        } else if (action == "bind-pose") {
          applied = m_animationPreview->bindPose();
        } else if (action == "seek") {
          if (!parsed["seconds"].is_number()) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Seek seconds must be numeric");
          }

          applied = m_animationPreview->seek(parsed["seconds"].get<double>());
        } else {
          if (!parsed["speed"].is_number()) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Animation speed must be numeric");
          }

          applied = m_animationPreview->configure(parsed["speed"].get<double>(),
                                                  parsed["looping"].get<bool>());
        }

        if (!applied) {
          return std::unexpected(applied.error());
        }

        m_previewReady = false;
        return execute("{\"command\":\"inspect\"}");
      }

      if (command == "capture-viewport" || command == "save-capture" || command == "browse-capture"
          || command == "discard-capture") {
        if (command == "capture-viewport") {
          Codec::requireFields(parsed, {"command", "revision", "dialog"});
        } else if (command == "save-capture") {
          Codec::requireFields(parsed, {"command", "capture", "parent", "file", "overwrite"});
        } else {
          Codec::requireFields(parsed, {"command", "capture"});
        }

        if (m_captureDialog || m_pendingClose) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Complete the native dialog or close confirmation first");
        }

        Core::Result<void> applied;

        if (command == "capture-viewport") {
          const auto dialog = parsed["dialog"].get<bool>();

          if (Codec::id(parsed["revision"]) != m_document->stats()->revision) {
            return Core::fail(Core::ErrorCode::InvalidState, "Capture has a stale revision");
          }

          auto context = captureContext();

          if (!context) {
            return std::unexpected(context.error());
          }

          auto queued = m_capture.request(*context, m_viewFrame);

          if (!queued) {
            return std::unexpected(queued.error());
          }

          m_captureBrowse = dialog;
          m_captureOutput = nullptr;
          m_captureMessage.clear();
        } else {
          const auto id = Codec::id(parsed["capture"]);

          if (command == "save-capture") {
            applied
                = saveCapture(id, parsed["parent"].get<std::string>(),
                              parsed["file"].get<std::string>(), parsed["overwrite"].get<bool>());
          } else if (command == "browse-capture") {
            applied = browseCapture(id);
          } else {
            applied = m_capture.discard(id);

            if (applied) {
              m_captureBrowse = false;
            }
          }

          m_captureMessage = applied ? "" : applied.error().message;
        }

        return applied ? execute("{\"command\":\"inspect\"}") : std::unexpected(applied.error());
      }

      if (command == "focus-window") {
        Codec::requireFields(parsed, {"command"});
        auto raised = m_application->window().raise();
        return raised ? execute("{\"command\":\"inspect\"}") : std::unexpected(raised.error());
      }

      if (command == "set-profiling") {
        Codec::requireFields(parsed, {"command", "enabled"});

        if (!parsed["enabled"].is_boolean()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Profiling enabled must be boolean");
        }

        auto changed = m_renderer->setProfiling(parsed["enabled"].get<bool>());
        return changed ? execute("{\"command\":\"inspect\"}") : std::unexpected(changed.error());
      }

      if (command == "inspect-profile") {
        Codec::requireFields(parsed, {"command"});
        auto profile = m_renderer->profiling();
        auto application = applicationProfileStatus(true);

        if (!profile || !application) {
          return std::unexpected(profile ? application.error() : profile.error());
        }

        return Json{{"ok", true},
                    {"profile", profilingJson(*profile, true)},
                    {"application", std::move(*application)}}
            .dump();
      }

      if (command == "set-application-profiling") {
        Codec::requireFields(parsed, {"command", "enabled"});

        if (!parsed["enabled"].is_boolean()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Profiling enabled must be boolean");
        }

        auto changed = m_application->setProfiling(parsed["enabled"].get<bool>());
        return changed ? execute("{\"command\":\"inspect\"}") : std::unexpected(changed.error());
      }

      if (m_animationPreview && command != "inspect" && command != "close" && command != "set-theme"
          && command != "filter-panel" && command != "dock-panel" && command != "reset-layout"
          && command != "show-pane" && command != "set-view" && command != "navigate-view"
          && command != "focus-view" && command != "save-close" && command != "cancel-close") {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "End animation preview before authoring or Play");
      }

      if (m_transform && command != "inspect" && command != "update-transform"
          && command != "finish-transform" && command != "close" && command != "set-theme"
          && command != "filter-panel" && command != "cancel-close") {
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
                                                 *parent, folder, {},
                                                 m_document->stats()->scenePath);
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
                                           *parent, folder, m_document->stats()->scenePath)
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

          clearPreviews(true);
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
          if (m_previewsStopped) {
            return Core::fail(Core::ErrorCode::InvalidState, "Preview generation is stopped");
          }

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
                    {"model", result->model},
                    {"animations", result->animations},
                    {"skins", result->skins},
                    {"skinnedMeshes", result->skinnedMeshes},
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

        if (m_animationPreview) {
          if (m_clipDocument) {
            auto saved = saveClip();

            if (!saved) {
              return std::unexpected(saved.error());
            }
          }

          auto saved = m_document->save(m_document->stats()->revision);

          if (!saved) {
            return std::unexpected(saved.error());
          }

          auto refreshed = refresh();

          if (!refreshed) {
            return Core::fail(
                refreshed.error().code,
                "Authoring data saved; snapshot refresh failed: " + refreshed.error().message);
          }

          return execute("{\"command\":\"close\",\"discard\":false}");
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

      if (command == "game-data") {
        Codec::requireFields(parsed, {"command", "session", "value"});

        if (!m_play || Codec::id(parsed["session"]) != m_playSession) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Game data requires the current Play session");
        }

        const auto operation = parsed["value"].at("command").get<std::string>();

        if ((m_pendingClose || m_play->stats()->state != PlayState::Running)
            && operation != "snapshot" && operation != "get") {
          return Core::fail(Core::ErrorCode::InvalidState, "Game data is closing");
        }

        auto service = m_play->gameData();
        auto result = service ? service->perform(parsed["value"].dump())
                              : Core::Result<std::string>{std::unexpected(service.error())};

        if (!result) {
          return std::unexpected(result.error());
        }

        auto value = Codec::parseJson(*result, 36);

        if (operation == "list-saves") {
          m_saveSlots = value;
          m_saveSlotsKnown = true;
        } else if (operation == "save" || operation == "delete-save") {
          m_saveSlotsKnown = false;
        }

        return Json{{"ok", true}, {"result", std::move(value)}}.dump();
      }

      if (command == "request-play-scene" || command == "play-scene-status") {
        if (command == "request-play-scene") {
          Codec::requireFields(parsed, {"command", "session", "revision", "path"});
        } else {
          Codec::requireFields(parsed, {"command", "session", "request"});
        }

        if (!m_play || Codec::id(parsed["session"]) != m_playSession) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Scene control requires the current Play session");
        }

        if (command == "play-scene-status") {
          auto status = m_play->sceneRequest(Codec::id(parsed["request"]));
          return status ? Core::Result<std::string>{Json{{"ok", true},
                                                         {"transition", sceneRequestJson(*status)}}
                                                        .dump()}
                        : Core::Result<std::string>{std::unexpected(status.error())};
        }

        if (m_pendingClose || m_play->stats()->state != PlayState::Running
            || Codec::id(parsed["revision"]) != m_document->stats()->revision) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Scene request requires running Play and the current document revision "
                            "without pending close");
        }

        auto id = m_play->requestScene(parsed["path"].get<std::string>());
        return id ? Core::Result<std::string>{Json{
                        {"ok", true},
                        {"request", std::to_string(*id)},
                        {"play", playStatus()}}.dump()}
                  : Core::Result<std::string>{std::unexpected(id.error())};
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

      if (command == "input-play" || command == "cancel-play-input") {
        if (command == "input-play") {
          Codec::requireFields(parsed, {"command", "session", "value"});
        } else {
          Codec::requireFields(parsed, {"command", "session"});
        }

        if (!playing() || m_play->stats()->state != PlayState::Running
            || Codec::id(parsed["session"]) != m_playSession || m_pendingClose) {
          return Core::fail(
              Core::ErrorCode::InvalidState,
              "Input requires the current running Play session without pending close");
        }

        if (command == "input-play") {
          auto staged = m_agentInput.submit(parsed["value"].dump(), m_gameInput);

          if (!staged) {
            return std::unexpected(staged.error());
          }
        } else {
          m_agentInput.cancel();
        }

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

      if (command == "set-hierarchy-expanded") {
        Codec::requireFields(parsed, {"command", "revision", "scope", "entity", "expanded"});
        const auto stats = m_document->stats().value();

        if (playing() || m_animationPreview || m_transform || m_pendingClose
            || Codec::id(parsed["revision"]) != stats.revision
            || Codec::id(parsed["scope"]) != stats.scene) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Hierarchy expansion requires the current stopped authoring scene");
        }

        const auto id = Codec::id(parsed["entity"]);
        auto found = m_document->scene().value()->findEntity(id);

        if (!found) {
          return std::unexpected(found.error());
        }

        const bool expanded = parsed["expanded"].get<bool>();

        if (!expanded && !m_hierarchyFilter.empty()) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Clear hierarchy search before collapsing matching branches");
        }

        m_hierarchyExpansion[std::to_string(id)] = expanded;
        return execute(R"({"command":"inspect"})");
      }

      if (command == "filter-panel") {
        Codec::requireFields(parsed, {"command", "panel", "query"});
        const auto panel = parsed["panel"].get<std::string>();
        const auto query = parsed["query"].get<std::string>();

        if ((panel != "hierarchy" && panel != "resources" && panel != "skeleton")
            || query.size() > 128 || query.find('\0') != std::string::npos
            || !Core::isValidUtf8(query)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid panel search");
        }

        (panel == "hierarchy"  ? m_hierarchyFilter
         : panel == "skeleton" ? m_skeletonFilter
                               : m_assetFilter) = query;
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
            && pane != "inspector" && pane != "viewport" && pane != "console" && pane != "skeleton"
            && pane != "animation" && pane != "game-data") {
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

      if (command == "stage-inspector") {
        Codec::requireFields(parsed, {"command", "revision", "entity", "value"});
        const auto stats = m_document->stats().value();
        const auto entity = Codec::id(parsed["entity"]);

        if (Codec::id(parsed["revision"]) != stats.revision || !stats.selected
            || stats.selected->value != entity || pendingEnvironment() || pendingOcclusion()
            || m_material) {
          return Core::fail(
              Core::ErrorCode::InvalidState,
              "Inspector staging requires current selection/revision and clean other drafts");
        }

        if (!parsed["value"].is_object()
            || parsed["value"].value("id", Json{}) != parsed["entity"]) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Inspector draft must describe the selected entity");
        }

        auto prepared = m_document->prepareEntityEdit(parsed["value"].dump(), stats.revision);

        if (!prepared) {
          return std::unexpected(prepared.error());
        }

        syncInspector();
        m_edit = parsed["value"];
        m_propertiesInvalid = false;
        m_propertiesText = m_edit["script"].is_null() ? "" : m_edit["script"]["properties"].dump(2);
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
          m_editRevision = 0;
          syncInspector();
        }

        return execute("{\"command\":\"inspect\"}");
      }

      if (parsed.value("command", std::string{}) == "close") {
        Codec::requireFields(parsed, {"command", "discard"});
        const bool discard = parsed["discard"].get<bool>();

        if (m_captureDialog) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Complete or cancel the native save dialog before closing");
        }

        if ((m_document->stats()->dirty || pendingInspector() || pendingEnvironment()
             || pendingOcclusion() || pendingText() || m_transform)
            && !discard) {
          if (!m_pendingClose) {
            m_message.clear();
          }

          m_pendingClose = true;
        } else {
          m_pendingClose = false;
          m_animationPreview.reset();
          m_clipDocument.reset();
          m_clipContext = {};
          m_clipForm = m_clipOriginal = nullptr;
          setClipSelection({});
          cancelKeyMove();
          m_transform.reset();
          m_axisDrag.reset();
          m_rotationDrag.reset();
          auto stopped = stopPlay();
          auto previewsStopped = stopPreviews();
          m_application->stop();

          if (!previewsStopped) {
            m_closeError = previewsStopped.error();
            return std::unexpected(previewsStopped.error());
          }

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

      if (command == "save-selection-scene"
          && (m_animationPreview || m_transform || m_pendingClose || pendingText()
              || m_captureDialog)) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Finish drafts, gestures and dialogs before saving scene content");
      }

      std::string documentRequest(request);

      if (command == "select-hierarchy") {
        Codec::requireFields(parsed, {"command", "revision", "entity", "mode"});
        const auto id = parsed["entity"].get<std::string>();
        const auto mode = parsed["mode"].get<std::string>();
        const auto found = std::ranges::find(m_visibleHierarchy, Json(id));

        if (found == m_visibleHierarchy.end()
            || (mode != "replace" && mode != "toggle" && mode != "range" && mode != "range-add")) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Hierarchy selection requires a visible entity and valid mode");
        }

        auto selection
            = (mode == "toggle" || mode == "range-add") ? m_snapshot["selection"] : Json::array();
        auto& values = selection.get_ref<Json::array_t&>();

        if (mode == "toggle") {
          const auto removed = std::erase(values, Json(id));

          if (!removed) {
            selection.push_back(id);
          }
        } else if (mode == "range" || mode == "range-add") {
          auto anchor = std::ranges::find(m_visibleHierarchy, Json(m_selectionAnchor));

          if (anchor == m_visibleHierarchy.end()) {
            anchor = found;
          }

          const auto first = std::min(anchor, found);
          const auto last = std::max(anchor, found);
          std::unordered_set<std::string> included;

          for (const auto& value : selection) {
            included.insert(value.get<std::string>());
          }

          for (auto item = first; item <= last; ++item) {
            if (included.insert(item->get<std::string>()).second) {
              selection.push_back(*item);
            }
          }

          std::erase(values, Json(id));
          selection.push_back(id);
        } else {
          selection.push_back(id);
        }

        documentRequest = Json{{"command", "select-many"},
                               {"revision", parsed["revision"]},
                               {"entities", std::move(selection)}}
                              .dump();
      }

      const auto previousRevision = m_document->stats()->revision;
      auto result = m_document->dispatch(documentRequest);

      auto updated = refresh();

      if (!updated) {
        return Core::fail(updated.error().code,
                          "Document response may have committed; " + updated.error().message);
      }

      if (sceneOperation && m_document->stats()->revision != previousRevision) {
        if (command != "save-scene-as") {
          m_play.reset();
          m_playError.reset();
          m_playDiagnostics.clear();
          m_clipDocument.reset();
          m_clipContext = {};
          m_clipForm = m_clipOriginal = nullptr;
          setClipSelection({});
          cancelKeyMove();
          m_material.reset();
          m_materialForm = nullptr;
          m_text.reset();
          m_selectedResource.clear();
          m_selectionAnchor.clear();
          m_editRevision = 0;
          m_editorView = true;
          m_environmentRevision.clear();
          m_occlusionRevision.clear();
          m_previewReady = false;
          m_previewError.clear();
          m_renderedFrame.reset();
          m_renderedView.reset();
          m_assetRefresh = true;
          auto cleared
              = m_skeleton.set(*m_document->scene().value(), m_project->files().value(), {});

          if (!cleared) {
            return Core::fail(cleared.error().code,
                              "Scene changed; skeleton reset failed: " + cleared.error().message);
          }
        }

        m_sceneDialog = false;
        m_sceneDialogRequested = false;
        m_sceneDialogClose = true;
        auto listed = catalog();

        if (!listed) {
          return Core::fail(
              listed.error().code,
              "Scene operation committed; catalog refresh failed: " + listed.error().message);
        }
      }

      if (result && command == "save-selection-scene") {
        auto listed = catalog();
        m_assetRefresh = true;

        if (!listed) {
          return Core::fail(listed.error().code, "Scene content saved; resource refresh failed: "
                                                     + listed.error().message);
        }
      }

      if (result && command == "select-hierarchy") {
        const auto mode = parsed["mode"].get<std::string>();

        if (mode != "range" && mode != "range-add") {
          m_selectionAnchor = parsed["entity"].get<std::string>();
        }
      }

      if (result && command == "group-selection" && m_document->stats()->selected) {
        m_hierarchyExpansion[std::to_string(m_document->stats()->selected->value)] = true;
      }

      if (result
          && (command == "select" || command == "select-many" || command == "select-hierarchy")
          && !m_material) {
        m_selectedResource.clear();
      }

      if (result
          && (command == "select" || command == "select-many" || command == "edit"
              || command == "duplicate-selection" || command == "delete-selection"
              || command == "group-selection" || command == "ungroup-selection" || command == "undo"
              || command == "redo")) {
        m_selectionAnchor = m_snapshot["selected"].is_null()
                                ? std::string{}
                                : m_snapshot["selected"].get<std::string>();
      }

      if (result && command == "inspect") {
        auto status = workspaceStatus();

        if (!status) {
          return std::unexpected(status.error());
        }

        auto response = Codec::parseJson(*result);
        response["workspace"] = std::move(*status);
        return response.dump();
      }

      return result;
    }

    auto workspaceStatus() -> Core::Result<Json> {
      refreshFieldOverrides();
      auto focused = m_application->window().focused();

      if (!focused) {
        return std::unexpected(focused.error());
      }

      auto input = inputStatus();
      auto applicationProfile = applicationProfileStatus(false);

      if (!input || !applicationProfile) {
        return std::unexpected(input ? applicationProfile.error() : input.error());
      }

      return Json{
          {"focused", *focused},
          {"input", std::move(*input)},
          {"applicationProfiling", std::move(*applicationProfile)},
          {"pendingInspector", pendingInspector()},
          {"pendingEnvironment", pendingEnvironment()},
          {"environmentDraft", m_environmentDraft},
          {"pendingOcclusion", pendingOcclusion()},
          {"occlusionDraft", m_occlusionDraft},
          {"pane", m_pane},
          {"theme", m_lightTheme ? "light" : "dark"},
          {"tool", toolStatus()},
          {"filters",
           {{"hierarchy", m_hierarchyFilter},
            {"resources", m_assetFilter},
            {"skeleton", m_skeletonFilter}}},
          {"displayed", {{"hierarchy", m_visibleHierarchy}, {"resources", m_visibleResources}}},
          {"hierarchyRows", m_hierarchyRows},
          {"hierarchyExpansion", m_hierarchyExpansion},
          {"layout",
           {{"pending", m_resetLayout || m_dockRequest.has_value()},
            {"error", m_layoutError},
            {"generation", std::to_string(m_layoutGeneration)},
            {"panels", m_panelGeometry}}},
          {"requestedPane", m_requestedPane},
          {"pendingClose", m_pendingClose},
          {"sceneDialog", m_sceneDialog ? Json{{"mode", m_sceneDialogMode == 0   ? "open"
                                                        : m_sceneDialogMode == 1 ? "create"
                                                                                 : "save-as"},
                                               {"path", m_scenePath},
                                               {"name", m_sceneName},
                                               {"discard", m_discardScene}}
                                        : Json(nullptr)},
          {"running", m_application->running()},
          {"inspector", m_edit},
          {"sceneOverrides",
           {{"query", m_overrideQuery},
            {"revision", m_overrideRevision},
            {"page", m_overridePage},
            {"error", m_overrideError}}},
          {"sceneFieldOverrides",
           {{"revision", m_fieldRevision}, {"page", m_fieldOverrides}, {"error", m_fieldError}}},
          {"propertiesText", m_propertiesText},
          {"propertiesInvalid", m_propertiesInvalid},
          {"play", playStatus()},
          {"animationPreview", animationStatus()},
          {"animationClip", clipStatus()},
          {"animationClipboard", clipboardStatus()},
          {"skeleton", skeletonStatus()},
          {"view", viewStatus()},
          {"capture", captureStatus()},
          {"message", m_message},
          {"material", materialStatus()},
          {"previews", previewStatus()},
          {"file", textStatus()}};
    }

    auto querySceneOverrides(const Json& request) -> Core::Result<std::string> {
      auto result = m_document->dispatch(request.dump());

      if (result && m_snapshot["selected"] == request["entity"]) {
        const auto response = Codec::parseJson(*result);
        m_overrideQuery = request;
        m_overridePage = response["sceneOverrides"];
        m_overrideRevision = response["revision"].get<std::string>();
        m_overrideContext
            = response["scenePath"].get<std::string>() + ':' + request["entity"].get<std::string>();
        m_overrideError.clear();
      }

      return result;
    }

    /** Field indicators use complete native counts, never the displayed override
     * page. Keep the entity filter separate from the whole-instance list.
     * 字段标记采用原生完整计数，不从当前分页推算；实体过滤独立于完整实例列表。 */
    auto refreshFieldOverrides() -> void {
      const auto revision = m_snapshot["revision"].get<std::string>();
      const auto context
          = m_snapshot["scenePath"].get<std::string>() + ':' + m_snapshot["selected"].dump();

      if (revision == m_fieldRevision && context == m_fieldContext) {
        return;
      }

      m_fieldRevision = revision;
      m_fieldContext = context;
      m_fieldOverrides = nullptr;
      m_fieldError.clear();

      if (m_snapshot["selected"].is_null()) {
        return;
      }

      auto result = m_document->dispatch(Json{{"command", "inspect-scene-overrides"},
                                              {"entity", m_snapshot["selected"]},
                                              {"scope", "entity"},
                                              {"offset", 0},
                                              {"limit", 1}}
                                             .dump());

      if (result) {
        m_fieldOverrides = Codec::parseJson(*result)["sceneOverrides"];
      } else {
        m_fieldError = result.error().message;
      }
    }

    auto fieldOverrideCount(std::string_view field) const -> std::size_t {
      if (m_fieldOverrides.is_null()) {
        return 0;
      }

      const auto& fields = m_fieldOverrides["fields"];
      const auto found = fields.find(field);
      return found == fields.end() ? 0 : found->get<std::size_t>();
    }

    auto fieldOverrideControl(const char* field, std::optional<std::string>& restore) -> void {
      const auto count = fieldOverrideCount(field);

      if (count == 0) {
        return;
      }

      ImGui::PushID(field);
      ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),
                         "%s: %zu applied override(s)", label(field).c_str(), count);

      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Saved baseline differences; unapplied Inspector edits are excluded.");
      }

      ImGui::BeginDisabled(pendingInspector() || pendingText() || m_pendingClose || m_sceneDialog
                           || m_captureDialog);

      if (ImGui::SmallButton("Restore field")) {
        restore = field;
      }

      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            "Restore this complete field from the last refreshed baseline.\n"
            "Apply or reset pending drafts first. Undo restores the local edits.");
      }

      ImGui::EndDisabled();
      ImGui::PopID();
    }

    auto sceneOverrideInspector() -> void {
      if (!m_editEntity) {
        return;
      }

      const auto entity = std::to_string(*m_editEntity);
      const auto context = m_snapshot["scenePath"].get<std::string>() + ':' + entity;

      if (m_overrideRevision != m_snapshot["revision"].get<std::string>()
          || m_overrideContext != context) {
        auto request = m_overrideQuery;

        if (m_overrideContext != context || request.is_null()) {
          request = {{"command", "inspect-scene-overrides"},
                     {"entity", entity},
                     {"scope", "auto"},
                     {"offset", 0},
                     {"limit", 32}};
        } else {
          request["offset"] = 0;
        }

        auto result = querySceneOverrides(request);

        if (!result) {
          m_overrideQuery = request;
          m_overridePage = nullptr;
          m_overrideRevision = m_snapshot["revision"].get<std::string>();
          m_overrideContext = context;
          m_overrideError = result.error().message;
        }
      }

      if (!m_overrideError.empty()) {
        ImGui::TextWrapped("%s", m_overrideError.c_str());

        if (ImGui::Button("Retry override inspection")) {
          sessionAction(m_overrideQuery);
        }

        return;
      }

      if (m_overridePage.is_null() || !m_overridePage["linked"].get<bool>()) {
        return;
      }

      /** Freeze view data until all controls finish; edits may replace snapshots.
       * 在全部控件结束前冻结视图数据，编辑可能替换快照。 */
      const auto page = m_overridePage;
      const auto query = m_overrideQuery;
      std::optional<Json> requestedView, requestedEdit;
      std::optional<std::string> requestedSource;
      ImGui::SeparatorText("Linked scene overrides");
      ImGui::TextWrapped("Source: %s", page["asset"].get<std::string>().c_str());
      ImGui::TextWrapped(
          "Applied changes against the last refresh. Revert all also removes added nodes.");
      bool all = page["scope"] == "instance";

      if (ImGui::Checkbox("Whole instance", &all)) {
        requestedView = query;
        (*requestedView)["scope"] = all ? "instance" : "entity";
        (*requestedView)["offset"] = 0;
      }

      ImGui::Text("%zu overrides", page["total"].get<std::size_t>());
      const bool readOnly = playing() || m_animationPreview || m_transform || pendingInspector()
                            || pendingEnvironment() || pendingOcclusion();
      ImGui::BeginDisabled(readOnly);

      if (ImGui::Button("Open source scene")) {
        requestedSource = page["asset"].get<std::string>();
      }

      for (const auto& [label, op] : {std::pair{"Refresh source", "refresh-scene-instance"},
                                      std::pair{"Restore source nodes", "revert-scene-instance"},
                                      std::pair{"Unlink scene", "unlink-scene-instance"}}) {
        if (ImGui::Button(label)) {
          requestedEdit = {{"op", op}, {"entity", page["root"]}};
        }
      }

      if (ImGui::Button("Revert all overrides")) {
        requestedEdit = {{"op", "revert-scene-baseline"}, {"root", page["root"]}};
      }

      Json pageKeys = Json::array();

      for (const auto& change : page["changes"]) {
        pageKeys.push_back(
            {{"kind", change["kind"]}, {"entity", change["entity"]}, {"path", change["path"]}});
      }

      ImGui::BeginDisabled(pageKeys.empty());

      if (ImGui::Button("Revert displayed changes")) {
        requestedEdit
            = {{"op", "revert-scene-overrides"}, {"root", page["root"]}, {"keys", pageKeys}};
      }

      ImGui::EndDisabled();
      ImGui::EndDisabled();
      std::size_t index{};

      for (const auto& change : page["changes"]) {
        ImGui::PushID(static_cast<int>(index++));
        const auto kind = change["kind"].get<std::string>();
        auto caption = kind + " | " + change["entity"].get<std::string>();

        if (kind == "property") {
          caption += " | " + change["path"].dump();
        }

        const bool expanded = ImGui::TreeNodeEx("change", 0, "%s", caption.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(readOnly);

        if (ImGui::SmallButton(kind == "added-node"     ? "Remove subtree"
                               : kind == "deleted-node" ? "Restore"
                                                        : "Revert")) {
          requestedEdit = {{"op", "revert-scene-overrides"},
                           {"root", page["root"]},
                           {"keys", Json::array({pageKeys[index - 1]})}};
        }

        ImGui::EndDisabled();

        if (expanded) {
          for (const auto* field : {"baseline", "local"}) {
            ImGui::TextUnformatted(field);
            const auto text = change[field]["present"].get<bool>() ? change[field]["value"].dump(2)
                                                                   : "(absent)";
            ImGui::TextWrapped("%s", text.c_str());
          }

          ImGui::TreePop();
        }

        ImGui::PopID();
      }

      const auto offset = page["offset"].get<std::size_t>();
      const auto limit = page["limit"].get<std::size_t>();
      ImGui::BeginDisabled(offset == 0);

      if (ImGui::Button("Previous overrides")) {
        requestedView = query;
        (*requestedView)["offset"] = offset > limit ? offset - limit : 0;
      }

      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(page["nextOffset"].is_null());

      if (ImGui::Button("Next overrides")) {
        requestedView = query;
        (*requestedView)["offset"] = page["nextOffset"];
      }

      ImGui::EndDisabled();

      if (requestedView) {
        sessionAction(*requestedView);
      } else if (requestedEdit) {
        edit(Json::array({*requestedEdit}));
      } else if (requestedSource) {
        openSceneAsset(*requestedSource);
      }
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

    auto profileStatus() const -> Json {
      auto profile = m_renderer->profiling();
      return profile ? profilingJson(*profile, false) : errorJson(profile.error());
    }

    auto applicationProfileStatus(bool history) const -> Core::Result<Json> {
      auto profile = m_application->profiling();

      if (!profile) {
        return std::unexpected(profile.error());
      }

      auto encoded = Core::encodeApplicationProfiling(*profile, history);
      return encoded ? Core::Result<Json>{Codec::parseJson(*encoded)}
                     : std::unexpected(encoded.error());
    }

    auto viewStatus() const -> Json {
      const auto state = m_editorCamera.state();
      const bool ready = viewportCurrent();
      auto scene = displayedScene();
      auto stats = m_renderer->visibilityStats();
      Json draws = nullptr;

      if (!stats) {
        draws = errorJson(stats.error());
      } else if (stats->mesh) {
        const auto& frame = *stats->mesh;
        draws = meshWorkJson(frame);
      }

      return {
          {"mode", playing() ? "play" : (m_editorView ? "editor" : "game")},
          {"ready", ready},
          {"loading", m_preparationStage == Graphics::ScenePreparationStage::Environment
                          ? Json("environment-pipeline")
                      : m_preparationStage == Graphics::ScenePreparationStage::Shadows
                          ? Json("shadow-pipelines")
                      : m_preparationStage == Graphics::ScenePreparationStage::Occlusion
                          ? Json("occlusion-pipelines")
                      : m_preparationStage == Graphics::ScenePreparationStage::Skins
                          ? Json("skin-pipelines")
                      : m_preparationStage == Graphics::ScenePreparationStage::EnvironmentMap
                          ? Json("environment-map")
                      : m_preparationStage == Graphics::ScenePreparationStage::Materials
                          ? Json("material-textures")
                          : Json(nullptr)},
          {"scene", scene ? Json(std::to_string((*scene)->id())) : Json(nullptr)},
          {"error", m_previewError.empty() ? Json(nullptr) : Json(m_previewError)},
          {"draws", std::move(draws)},
          {"profiling", profileStatus()},
          {"textInput", m_renderer->guiStats().value().text},
          {"source", m_animationPreview ? "animation"
                     : playing()        ? "play"
                     : m_transform      ? "transform"
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

    /** Frame visible selected-subtree meshes in the displayed scene, including
     * posed skin bounds. Resolve values into the clone scope; empty groups use
     * their own bounds. Imported model roots must not frame a placeholder cube.
     * 对显示场景内选中子树的可见网格取景，包括蒙皮姿态边界；将数值 ID 解析到克隆范围，
     * 无网格组使用自身边界，导入模型根不能只对占位立方体取景。 */
    auto focusView(std::optional<World::EntityId> entity) -> Core::Result<void> {
      auto scene = displayedScene();

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto entities = (*scene)->entities();

      if (!entities) {
        return std::unexpected(entities.error());
      }

      std::unordered_set<std::uint64_t> subtree;

      if (entity) {
        auto root = (*scene)->findEntity(entity->value);

        if (!root) {
          return std::unexpected(root.error());
        }

        std::map<std::uint64_t, std::vector<std::uint64_t>> children;

        for (const auto& info : *entities) {
          if (info.parent) {
            children[info.parent->value].push_back(info.id.value);
          }
        }

        std::vector<std::uint64_t> pending{entity->value};

        while (!pending.empty()) {
          const auto current = pending.back();
          pending.pop_back();

          if (!subtree.insert(current).second) {
            continue;
          }

          if (auto found = children.find(current); found != children.end()) {
            pending.insert(pending.end(), found->second.begin(), found->second.end());
          }
        }
      }

      Core::Result<Graphics::Bounds> bounds = Graphics::Bounds{};
      bool first = true;

      for (const auto& info : *entities) {
        if (!info.mesh || !info.mesh->visible || (entity && !subtree.contains(info.id.value))) {
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
        if (!entity) {
          return Core::fail(Core::ErrorCode::NotFound, "Scene has no visible meshes to frame");
        }

        bounds = m_drawing->bounds(**scene, (*scene)->findEntity(entity->value).value());
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
      if (m_animationPreview) {
        return Core::fail(Core::ErrorCode::InvalidState, "End animation preview before selecting");
      }

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

      auto scene = displayedScene();

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto materialPreview = synchronizeMaterialPreview();

      if (!materialPreview) {
        m_previewReady = false;
        m_previewError = materialPreview.error().message;
        return {};
      }

      auto prepared = m_drawing->prepare(**scene);
      m_preparationStage = prepared ? *prepared : Graphics::ScenePreparationStage::Ready;

      if (!prepared || m_preparationStage != Graphics::ScenePreparationStage::Ready) {
        m_previewReady = false;
        m_previewError = prepared ? "" : prepared.error().message;
        return {};
      }

      auto view = currentView();

      if (!view) {
        return std::unexpected(view.error());
      }

      if (m_viewFrame == std::numeric_limits<std::uint64_t>::max()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Viewport frame identity exhausted");
      }

      const auto aspect = static_cast<float>(width) / static_cast<float>(height);
      auto projection = [&]() -> Core::Result<Math::Mat4> {
        if (const auto* custom = std::get_if<Graphics::View>(&*view)) {
          return Graphics::viewProjection(*custom, aspect);
        }

        const auto* entity = std::get_if<World::EntityId>(&*view);
        auto camera = entity ? Core::Result<World::EntityId>(*entity) : (*scene)->primaryCamera();
        return camera ? (*scene)->viewProjection(*camera, aspect) : std::unexpected(camera.error());
      }();

      if (!projection) {
        m_previewReady = false;
        m_previewError = projection.error().message;
        return {};
      }

      std::optional<AnimationCapture> animation;

      if (m_animationPreview) {
        auto state = m_animationPreview->stats();

        if (!state) {
          return std::unexpected(state.error());
        }

        animation = AnimationCapture{state->owner, state->generation, state->player.clip,
                                     state->player.playback.time, state->bindPose};
      }

      auto drawn = m_drawing->renderInto(**scene, *m_target, {0.055F, 0.06F, 0.075F, 1}, *view);

      if (drawn) {
        m_renderedView = *view;
        m_renderedRevision = std::to_string(m_document->stats()->revision);
        m_renderedFrame = CaptureFrame{captureContext().value(),
                                       ++m_viewFrame,
                                       playing() ? m_play->stats()->frames : 0,
                                       width,
                                       height,
                                       *projection,
                                       !m_materialPreviewKey.empty(),
                                       animation};
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

    /** Defer hierarchy actions until traversal releases all snapshot references.
     * 遍历释放全部快照引用后再执行层级操作，避免刷新快照使递归引用失效。 */
    auto hierarchyDrop(const Json& parent, std::optional<Json>& action) -> void {
      if (!ImGui::BeginDragDropTarget()) {
        return;
      }

      if (const auto* payload = ImGui::AcceptDragDropPayload("DOT_SCENE_ENTITIES")) {
        const auto bytes = static_cast<std::size_t>(payload->DataSize);

        if (!payload->Data || bytes < 3 * sizeof(std::uint64_t)
            || bytes > 10002 * sizeof(std::uint64_t) || bytes % sizeof(std::uint64_t) != 0) {
          m_message = "Invalid hierarchy drag payload";
        } else {
          std::vector<std::uint64_t> values(bytes / sizeof(std::uint64_t));
          std::memcpy(values.data(), payload->Data, bytes);
          const auto stats = m_document->stats().value();

          if (values[0] != stats.scene || values[1] != stats.revision) {
            m_message = "Hierarchy drag belongs to an obsolete scene or revision";
          } else {
            auto entities = Json::array();

            for (std::size_t index = 2; index < values.size(); ++index) {
              entities.push_back(std::to_string(values[index]));
            }

            action = Json{{"command", "edit"},
                          {"operations", Json::array({{{"op", "reparent"},
                                                       {"entities", std::move(entities)},
                                                       {"parent", parent},
                                                       {"space", "world"}}})}};
          }
        }
      }

      ImGui::EndDragDropTarget();
    }

    auto tree(const Json& entities, const Json& parent, std::optional<Json>& selection,
              int depth = 0) -> void {
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
        const bool selected = m_selectionIds.contains(id);
        const bool children = std::ranges::any_of(
            entities, [&](const auto& child) { return child["parent"] == entity["id"]; });
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        flags |= selected ? ImGuiTreeNodeFlags_Selected : 0;
        flags |= children ? 0 : ImGuiTreeNodeFlags_Leaf;
        m_visibleHierarchy.push_back(id);

        if (!m_hierarchyFilter.empty() && children) {
          ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        } else if (m_hierarchyExpansion.contains(id)) {
          ImGui::SetNextItemOpen(m_hierarchyExpansion[id].get<bool>(), ImGuiCond_Always);
        }

        ImGui::PushID(id.c_str());
        const bool open = ImGui::TreeNodeEx("entity", flags, "%s", name.c_str());
        const auto origin = ImGui::GetItemRectMin();
        const auto end = ImGui::GetItemRectMax();
        m_hierarchyRows[id] = {{"origin", {origin.x, origin.y}},
                               {"size", {end.x - origin.x, end.y - origin.y}},
                               {"open", open},
                               {"selected", selected},
                               {"active", m_snapshot["selected"] == id}};

        if (ImGui::IsItemToggledOpen()) {
          selection = Json{{"command", "set-hierarchy-expanded"},
                           {"scope", m_snapshot["scope"]},
                           {"entity", id},
                           {"expanded", open}};
        }

        const auto& io = ImGui::GetIO();
        const bool additive = io.KeyCtrl || io.KeySuper;
        const bool deferReplace
            = selected && m_selectionIds.size() > 1 && !additive && !io.KeyShift;

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && !deferReplace) {
          const auto mode = io.KeyShift ? (additive ? "range-add" : "range")
                                        : (additive ? "toggle" : "replace");
          selection = Json{{"command", "select-hierarchy"}, {"entity", id}, {"mode", mode}};
        }

        /** Keep the multi-selection until a click releases without a drag.
         * 多选保持至无拖动的单击释放，避免开始拖动时丢失其他选项。 */
        if (deferReplace && ImGui::IsItemHovered()
            && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
          const auto delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);

          if (delta.x == 0 && delta.y == 0) {
            selection = Json{{"command", "select-hierarchy"}, {"entity", id}, {"mode", "replace"}};
          }
        }

        if (ImGui::BeginDragDropSource()) {
          const auto stats = m_document->stats().value();
          std::vector<std::uint64_t> payload{stats.scene, stats.revision};

          if (selected) {
            for (const auto member : stats.selection) {
              payload.push_back(member.value);
            }
          } else {
            payload.push_back(Codec::id(entity["id"]));
          }

          ImGui::SetDragDropPayload("DOT_SCENE_ENTITIES", payload.data(),
                                    payload.size() * sizeof(std::uint64_t), ImGuiCond_Once);
          ImGui::Text("Move %zu selected subtree(s)", payload.size() - 2);
          ImGui::EndDragDropSource();
        }

        hierarchyDrop(entity["id"], selection);

        if (open) {
          tree(entities, entity["id"], selection, depth + 1);
          ImGui::TreePop();
        }

        ImGui::PopID();
      }
    }

    auto inspector() -> void {
      const auto stats = m_document->stats().value();
      refreshFieldOverrides();

      if (!stats.selected) {
        ImGui::TextWrapped("Select an entity in the scene tree to edit its properties.");
        m_editEntity.reset();
        return;
      }

      if (stats.selection.size() > 1) {
        ImGui::TextWrapped("%zu selected. Properties below edit the active entity only.",
                           stats.selection.size());
      }

      if (!m_fieldError.empty()) {
        ImGui::TextWrapped("Field override inspection: %s", m_fieldError.c_str());
      }

      std::optional<std::string> restore;
      fieldOverrideControl("name", restore);

      std::string name = m_edit["name"].get<std::string>();

      {
        PropertyRow row{"Name"};

        if (row.visible() && ImGui::InputText("##value", &name)) {
          m_edit["name"] = name;
        }
      }

      std::string parent
          = m_edit["parent"].is_null() ? "None" : m_edit["parent"].get<std::string>();
      fieldOverrideControl("parent", restore);

      {
        PropertyRow row{"Parent (local pose)"};

        if (row.visible() && ImGui::BeginCombo("##value", parent.c_str())) {
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
      }

      const auto transformTitle
          = fieldOverrideCount("transform") ? "Transform *###transform" : "Transform###transform";

      if (ImGui::CollapsingHeader(transformTitle, ImGuiTreeNodeFlags_DefaultOpen)) {
        fieldOverrideControl("transform", restore);
        fields(m_edit["transform"]);
      }

      for (const auto* component :
           {"camera", "mesh", "rigidBody", "audioSource", "audioListener", "script", "material",
            "light", "animation", "skin", "character"}) {
        ImGui::PushID(component);
        const auto title = label(component) + (fieldOverrideCount(component) ? " *" : "");
        const auto header = title + "###component";

        if (m_edit[component].is_null()) {
          fieldOverrideControl(component, restore);

          if (std::string_view(component) == "skin" && m_edit["mesh"].is_null()) {
            ImGui::TextDisabled("Add a mesh before a skin binding");
          } else if ((std::string_view(component) == "character" && !m_edit["rigidBody"].is_null())
                     || (std::string_view(component) == "rigidBody"
                         && !m_edit["character"].is_null())) {
            ImGui::TextDisabled("Rigid body and character controller are exclusive");
          } else if (ImGui::Button(("Add " + title).c_str())) {
            if (std::string_view(component) == "skin") {
              m_edit[component] = {{"asset", "assets/skin.dotskin"},
                                   {"joints", Json::array({m_edit["id"]})},
                                   {"enabled", true}};
            } else if (std::string_view(component) == "animation") {
              m_edit[component]
                  = {{"clips", Json::array({"assets/animation.dotanimation"})},
                     {"targets", Json::array({{{"node", 0}, {"entity", m_edit["id"]}}})},
                     {"clip", 0},
                     {"speed", 1.0},
                     {"looping", true},
                     {"playOnStart", true},
                     {"enabled", true}};
            } else {
              m_edit[component] = m_presets[component];
            }

            if (std::string_view(component) == "script") {
              m_propertiesText = m_edit[component]["properties"].dump(2);
              m_propertiesInvalid = false;
            }
          }
        } else if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
          fieldOverrideControl(component, restore);

          if (std::string_view(component) == "script") {
            auto description = m_edit[component];
            description.erase("properties");
            fields(description);
            description["properties"] = m_edit[component]["properties"];
            m_edit[component] = std::move(description);
            ImGui::TextUnformatted("Properties (JSON object)");

            if (ImGui::InputTextMultiline("##properties", &m_propertiesText,
                                          {-std::numeric_limits<float>::min(), 120})) {
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
          } else if (std::string_view(component) == "light") {
            auto description = m_edit[component];
            description.erase("shadows");
            fields(description);
            description["shadows"] = m_edit[component]["shadows"];
            m_edit[component] = std::move(description);
            auto& light = m_edit[component];
            bool shadows = light.contains("shadows") && !light["shadows"].is_null();

            if (PropertyRow row{"Cast shadows"};
                row.visible() && ImGui::Checkbox("##value", &shadows)) {
              light["shadows"] = shadows ? Json{{"resolution", 1024},   {"cascades", 4},
                                                {"distance", 100.0F},   {"splitLambda", 0.65F},
                                                {"depthBias", 0.0002F}, {"slopeBias", 0.001F},
                                                {"normalBias", 0.01F},  {"filterRadius", 2.0F}}
                                         : Json(nullptr);
            }

            if (shadows) {
              fields(light["shadows"]);
            }
          } else if (std::string_view(component) == "skin") {
            auto& skin = m_edit[component];
            bool enabled = skin["enabled"].get<bool>();

            if (ImGui::Checkbox("Enabled", &enabled)) {
              skin["enabled"] = enabled;
            }

            const auto asset = skin["asset"].get<std::string>();

            if (PropertyRow row{"Skin asset"};
                row.visible() && ImGui::BeginCombo("##value", asset.c_str())) {
              for (const auto& entry : m_catalog) {
                const auto path = entry["path"].get<std::string>();

                if (path.ends_with(".dotskin") && ImGui::Selectable(path.c_str(), path == asset)) {
                  skin["asset"] = path;
                }
              }

              ImGui::EndCombo();
            }

            ImGui::TextWrapped("Joint order must match the skin asset's inverse binds.");
            auto& joints = skin["joints"];

            if (ImGui::TreeNode("Joint bindings")) {
              for (std::size_t i = 0; i < joints.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const auto joint = joints[i].get<std::string>();
                std::string current = joint;

                for (const auto& entity : m_snapshot["scene"]["entities"]) {
                  if (entity["id"] == joint) {
                    current = entity["name"].get<std::string>() + " (" + joint + ")";
                    break;
                  }
                }

                if (PropertyRow row{("Joint " + std::to_string(i)).c_str()};
                    row.visible() && ImGui::BeginCombo("##value", current.c_str())) {
                  for (const auto& entity : m_snapshot["scene"]["entities"]) {
                    const auto id = entity["id"].get<std::string>();

                    if (id != joint && std::ranges::find(joints, entity["id"]) != joints.end()) {
                      continue;
                    }

                    const auto jointName = entity["name"].get<std::string>() + " (" + id + ")";

                    if (ImGui::Selectable(jointName.c_str(), id == joint)) {
                      joints[i] = id;
                    }
                  }

                  ImGui::EndCombo();
                }

                ImGui::BeginDisabled(joints.size() <= 1);
                const bool remove = ImGui::SmallButton("Remove joint");
                ImGui::EndDisabled();
                ImGui::PopID();

                if (remove) {
                  joints.erase(joints.begin() + static_cast<Json::difference_type>(i));
                  break;
                }
              }

              ImGui::BeginDisabled(joints.size() >= 1024);

              if (ImGui::SmallButton("Add joint")) {
                for (const auto& entity : m_snapshot["scene"]["entities"]) {
                  if (std::ranges::find(joints, entity["id"]) == joints.end()) {
                    joints.push_back(entity["id"]);
                    break;
                  }
                }
              }

              ImGui::EndDisabled();
              ImGui::TreePop();
            }
          } else if (std::string_view(component) == "animation") {
            auto& player = m_edit[component];
            auto settings = player;
            settings.erase("clips");
            settings.erase("targets");
            settings.erase("clip");
            fields(settings);

            for (const auto& [key, value] : settings.items()) {
              player[key] = value;
            }

            const auto& clips = player["clips"];
            const auto selected = player["clip"].get<std::size_t>();
            const auto current
                = selected < clips.size() ? clips[selected].get<std::string>() : "Choose clip";

            if (PropertyRow row{"Clip"};
                row.visible() && ImGui::BeginCombo("##value", current.c_str())) {
              for (std::size_t i = 0; i < clips.size(); ++i) {
                if (ImGui::Selectable(clips[i].get_ref<const std::string&>().c_str(),
                                      selected == i)) {
                  player["clip"] = i;
                }
              }

              ImGui::EndCombo();
            }

            ImGui::TextDisabled("%zu node bindings", player["targets"].size());

            if (ImGui::TreeNode("Clip library")) {
              for (std::size_t i = 0; i < player["clips"].size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                std::string path = player["clips"][i].get<std::string>();

                if (PropertyRow row{"Asset"};
                    row.visible() && ImGui::BeginCombo("##value", path.c_str())) {
                  for (const auto& entry : m_catalog) {
                    const auto asset = entry["path"].get<std::string>();

                    if (asset.ends_with(".dotanimation")
                        && ImGui::Selectable(asset.c_str(), asset == path)) {
                      player["clips"][i] = asset;
                    }
                  }

                  ImGui::EndCombo();
                }

                ImGui::PopID();
              }

              ImGui::TreePop();
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

      if (restore) {
        edit(Json::array({{{"op", "revert-scene-field"},
                           {"root", m_fieldOverrides["root"]},
                           {"entity", m_edit["id"]},
                           {"field", *restore}}}));
      }
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

      auto prepared = m_document->prepareEntityEdit(m_edit.dump(), m_editRevision);

      if (!prepared) {
        m_message = prepared.error().message;
        return std::unexpected(prepared.error());
      }

      auto operations = Codec::parseJson(*prepared);

      if (operations.empty()) {
        m_editRevision = 0;
        syncInspector();
        return {};
      }

      ActiveScope applying{m_applying};
      const bool applied = edit(std::move(operations));
      return applied ? Core::Result<void>{} : std::unexpected(*m_actionError);
    }

    auto openSceneAsset(std::string_view path) -> bool {
      if (m_document->stats()->dirty) {
        return sessionAction({{"command", "scene-dialog"}, {"mode", "open"}});
      }

      return action({{"command", "open-scene"}, {"path", path}, {"discard", false}});
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

      ImGui::BeginDisabled(playing() || m_animationPreview || pendingInspector()
                           || pendingOcclusion());
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
      auto applicationProfile = m_application->profiling();

      if (!applicationProfile) {
        ImGui::TextWrapped("Application profiling: %s", applicationProfile.error().message.c_str());
      } else {
        bool enabled = applicationProfile->enabled;

        if (ImGui::Checkbox("Profile application frames", &enabled)) {
          sessionAction({{"command", "set-application-profiling"}, {"enabled", enabled}});
          applicationProfile = m_application->profiling();
        }

        if (applicationProfile && applicationProfile->enabled) {
          ImGui::TextWrapped(
              "Frame wall time includes loading, GUI and GPU waits; "
              "excludes process setup and idle time.");

          if (!applicationProfile->frames.empty()) {
            const auto& frame = applicationProfile->frames.back();
            ImGui::Text("Step %llu: %.3f ms", static_cast<unsigned long long>(frame.frame.index),
                        static_cast<double>(frame.totalNanoseconds) / 1e6);
            const auto simulation = frame.fixedNanoseconds || frame.updateNanoseconds
                                        ? std::optional(frame.fixedNanoseconds.value_or(0)
                                                        + frame.updateNanoseconds.value_or(0))
                                        : std::nullopt;
            const std::array phases{frame.eventsNanoseconds, frame.prepareNanoseconds, simulation,
                                    frame.renderNanoseconds};
            constexpr std::array labels{"Events", "Input / commands", "Game / animation",
                                        "Rendering / GUI"};

            for (std::size_t index = 0; index < phases.size(); ++index) {
              if (phases[index]) {
                ImGui::Text("%s: %.3f ms", labels[index],
                            static_cast<double>(*phases[index]) / 1e6);
              } else {
                ImGui::Text("%s: no callback", labels[index]);
              }
            }

            if (frame.error) {
              ImGui::TextWrapped("Failed step: %s", frame.error->message.c_str());
            }

            std::vector<float> history;

            for (const auto& value : applicationProfile->frames) {
              history.push_back(static_cast<float>(value.totalNanoseconds) / 1e6F);
            }

            ImGui::TextUnformatted("Application frame (ms)");
            ImGui::PlotLines("##application-profile", history.data(),
                             static_cast<int>(history.size()), 0, nullptr, 0,
                             std::numeric_limits<float>::max(),
                             {-std::numeric_limits<float>::min(), 64});
          }

          if (applicationProfile->slowest) {
            const auto& slowest = *applicationProfile->slowest;
            ImGui::TextWrapped("Slowest since enable: step %llu, %.3f ms",
                               static_cast<unsigned long long>(slowest.frame.index),
                               static_cast<double>(slowest.totalNanoseconds) / 1e6);
          }

          if (applicationProfile->longestInterval) {
            const auto& interval = applicationProfile->longestInterval->frame;
            ImGui::TextWrapped("Longest frame interval %.3f ms%s",
                               static_cast<double>(interval.elapsed.count()) / 1e6,
                               interval.paused ? " (paused)" : "");
          }
        }
      }

      ImGui::Separator();
      auto profile = m_renderer->profiling();

      if (!profile) {
        ImGui::TextWrapped("Mesh profiling: %s", profile.error().message.c_str());
      } else {
        bool enabled = profile->enabled;

        if (ImGui::Checkbox("Profile mesh rendering", &enabled)) {
          sessionAction({{"command", "set-profiling"}, {"enabled", enabled}});
          profile = m_renderer->profiling();
        }

        if (profile && profile->enabled) {
          ImGui::TextWrapped("GPU: %s", profile->adapter.name.c_str());
          ImGui::TextWrapped(
              "Renderer work only; scene, scripts, physics and skin updates excluded.");

          if (!profile->cpu.empty()) {
            const auto& cpu = profile->cpu.back();
            ImGui::TextWrapped("CPU wall ms: prepare %.3f, encode %.3f, submit %.3f",
                               static_cast<double>(cpu.prepareNanoseconds) / 1e6,
                               static_cast<double>(cpu.encodeNanoseconds) / 1e6,
                               static_cast<double>(cpu.submitNanoseconds) / 1e6);
          }

          if (!profile->gpuSupported) {
            ImGui::TextUnformatted("GPU timestamps unavailable on this device.");
          } else if (profile->gpu.empty()) {
            ImGui::TextUnformatted("Waiting for GPU timing samples.");
          } else {
            const auto& gpu = profile->gpu.back();
            ImGui::TextUnformatted("GPU intervals may overlap; do not add them as frame time.");
            ImGui::TextWrapped(
                "GPU sample %llu: %.3f ms at %ux%u", static_cast<unsigned long long>(gpu.serial),
                static_cast<double>(gpu.totalNanoseconds) / 1e6, gpu.width, gpu.height);
            constexpr std::array labels{"Shadows", "SSAO", "Surface / sky", "Tone mapping"};

            for (std::size_t i = 0; i < labels.size(); ++i) {
              if (gpu.passNanoseconds[i]) {
                ImGui::Text("%s: %.3f ms", labels[i],
                            static_cast<double>(*gpu.passNanoseconds[i]) / 1e6);
              } else {
                ImGui::Text("%s: no pass", labels[i]);
              }
            }

            std::vector<float> history;
            history.reserve(profile->gpu.size());

            for (const auto& value : profile->gpu) {
              history.push_back(static_cast<float>(value.totalNanoseconds) / 1e6F);
            }

            ImGui::PlotLines("Mesh GPU (ms)", history.data(), static_cast<int>(history.size()), 0,
                             nullptr, 0, std::numeric_limits<float>::max(), {0, 64});
          }

          ImGui::TextWrapped("GPU samples: %zu, pending %llu, dropped %llu", profile->gpu.size(),
                             static_cast<unsigned long long>(profile->pending),
                             static_cast<unsigned long long>(profile->dropped));

          if (profile->error) {
            ImGui::TextWrapped("Profiling error: %s", profile->error->c_str());
          }
        }
      }

      ImGui::Separator();
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
        ImGui::TextWrapped("Shadow atlas pages: %zu updated, %zu reused", frame.shadowPagesUpdated,
                           frame.shadowPagesReused);
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

      ImGui::BeginDisabled(playing() || m_animationPreview || pendingInspector()
                           || pendingEnvironment());
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

        if (m_selectedResource.ends_with(".dotscene") && ImGui::Button("Open scene")) {
          openSceneAsset(m_selectedResource);
        }

        if (m_selectedResource.ends_with(".dotscene") && ImGui::Button("Add scene instance")) {
          edit({{{"op", "instantiate-scene"},
                 {"path", m_selectedResource},
                 {"parent", nullptr},
                 {"transform",
                  {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}});
        }

        if (m_selectedResource.ends_with(".dotscene") && ImGui::Button("Add linked scene")) {
          edit({{{"op", "link-scene"},
                 {"path", m_selectedResource},
                 {"parent", nullptr},
                 {"transform",
                  {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}});
        }

        ImGui::InputText("Content name", &m_contentName);
        ImGui::BeginDisabled(!m_document->stats()->selected);

        if (ImGui::Button("Save selection as scene")) {
          action(
              {{"command", "save-selection-scene"}, {"path", m_newPath}, {"name", m_contentName}});
        }

        ImGui::EndDisabled();

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
          if (path.ends_with(".dotscene")) {
            openSceneAsset(path);
          } else {
            fileAction({{"command", "open-external"}, {"path", path}});
          }
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
      auto entities = Json::array();

      if (m_transform) {
        for (auto id : m_transform->entities()) {
          entities.push_back(std::to_string(id));
        }
      }

      return {{"mode", toolName()},
              {"snapUnit", m_tool == Tool::Rotate  ? "degrees"
                           : m_tool == Tool::Scale ? "ratio"
                                                   : "meters"},
              {"space", m_localTool ? "local" : "world"},
              {"snap", m_toolSnap},
              {"active", m_transform.has_value()},
              {"entities", std::move(entities)},
              {"roots", m_transform ? m_transform->rootCount() : 0},
              {"pivot", "active"},
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
      if (m_tool == Tool::Select || !m_editorView || playing() || m_animationPreview || m_transform
          || !m_viewVisible || !viewStatus()["ready"].get<bool>() || pendingInspector()
          || pendingEnvironment() || pendingOcclusion() || pendingText() || m_pendingClose
          || m_flying || m_orbiting || m_panning) {
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

      auto gesture = TransformGesture::beginSelection(*m_document, revision);

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

      if (m_tool == Tool::Select || !m_editorView || playing() || m_animationPreview
          || m_document->stats()->selection.empty()) {
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

    auto animationControls() -> void {
      auto state = m_animationPreview->stats();

      if (!state) {
        ImGui::TextWrapped("%s", state.error().message.c_str());
        return;
      }

      const auto command = [&](Json request) {
        request["command"] = "control-animation-preview";
        request["session"] = std::to_string(m_animationSession);
        return sessionAction(request);
      };

      ImGui::TextWrapped("%s — %s", state->bindPose ? "Bind pose" : "Animation preview",
                         state->player.name.c_str());

      const auto nextButton = [](const char* label) -> bool {
        ImGui::SameLine();
        const auto width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;

        if (ImGui::GetContentRegionAvail().x < width) {
          ImGui::NewLine();
        }

        return ImGui::Button(label);
      };

      ImGui::BeginDisabled(state->error.has_value());

      if (ImGui::Button(state->player.playback.playing ? "Pause animation" : "Play animation")) {
        if (state->player.playback.playing) {
          command({{"action", "pause"}});
        } else {
          command({{"action", "play"}, {"clip", state->player.clip}, {"restart", false}});
        }
      }

      if (nextButton("Bind pose")) {
        command({{"action", "bind-pose"}});
      }

      ImGui::EndDisabled();

      if (nextButton(m_clipDocument ? "Close clip" : "End preview")) {
        if (m_clipDocument) {
          clipAction({{"command", "close-animation"}, {"discard", false}});
        } else {
          command({{"action", "end"}});
        }
        return;
      }

      if (nextButton("Frame model")) {
        action({{"command", "focus-view"}, {"entity", std::to_string(state->owner)}});
      }

      if (m_clipDocument) {
        return;
      }

      ImGui::BeginDisabled(state->error.has_value());

      ImGui::BeginDisabled(m_clipDocument.has_value());

      if (PropertyRow row{"Play clip"};
          row.visible() && ImGui::BeginCombo("##value", state->player.asset.c_str())) {
        for (std::size_t i = 0; i < state->clips.size(); ++i) {
          if (ImGui::Selectable(state->clips[i].c_str(), i == state->player.clip)) {
            command({{"action", "play"}, {"clip", i}, {"restart", true}});
          }
        }

        ImGui::EndCombo();
      }

      ImGui::EndDisabled();

      double time = state->player.playback.time;
      const double zero = 0;
      const double duration = state->player.duration;

      if (PropertyRow row{"Time (s)"}; row.visible()
                                       && ImGui::SliderScalar("##value", ImGuiDataType_Double,
                                                              &time, &zero, &duration, "%.3f")) {
        command({{"action", "seek"}, {"seconds", time}});
      }

      double speed = state->player.playback.speed;
      bool looping = state->player.playback.looping;

      if (PropertyRow row{"Speed"};
          row.visible() && ImGui::InputDouble("##value", &speed, .1, 1, "%.2f")) {
        command({{"action", "configure"}, {"speed", speed}, {"looping", looping}});
      }

      if (ImGui::Checkbox("Loop animation", &looping)) {
        command({{"action", "configure"}, {"speed", speed}, {"looping", looping}});
      }

      ImGui::EndDisabled();

      if (state->error) {
        ImGui::TextWrapped("%s", state->error->message.c_str());
      }
    }

    auto gameDataPanel() -> void {
      if (!m_play) {
        ImGui::TextWrapped("Start Play to inspect game state and try isolated saves.");
        return;
      }

      auto service = m_play->gameData();
      auto snapshot = service ? service->perform(R"({"command":"snapshot"})")
                              : Core::Result<std::string>{std::unexpected(service.error())};

      if (!snapshot) {
        ImGui::TextWrapped("%s", snapshot.error().message.c_str());
        return;
      }

      auto state = Codec::parseJson(*snapshot, 36);
      ImGui::Text("Revision %s", state["revision"].get_ref<const std::string&>().c_str());
      ImGui::TextWrapped("Play saves are temporary and are removed when Play stops.");
      const auto request = [&](Json value) {
        return sessionAction({{"command", "game-data"},
                              {"session", std::to_string(m_playSession)},
                              {"value", std::move(value)}});
      };
      const bool running = m_play->stats()->state == PlayState::Running && !m_pendingClose;
      ImGui::BeginDisabled(!running);

      if (running && !m_saveSlotsKnown) {
        m_saveSlotsKnown = true;
        request({{"command", "list-saves"}});
      }

      ImGui::SetNextItemWidth(-1);
      ImGui::InputText("##save-slot", &m_saveSlot);
      ImGui::Checkbox("Replace an existing slot", &m_saveOverwrite);

      if (ImGui::Button("Save state")) {
        request({{"command", "save"}, {"slot", m_saveSlot}, {"overwrite", m_saveOverwrite}});
      }

      ImGui::SameLine();

      if (ImGui::Button("Load state")) {
        request({{"command", "load"}, {"slot", m_saveSlot}, {"revision", state["revision"]}});
      }

      if (ImGui::Button("Delete slot")) {
        request({{"command", "delete-save"}, {"slot", m_saveSlot}});
      }

      ImGui::SameLine();

      if (ImGui::Button("Refresh slots")) {
        request({{"command", "list-saves"}});
      }

      for (const auto& slot : m_saveSlots) {
        const auto& name = slot["name"].get_ref<const std::string&>();
        const auto label = name + " (" + slot["bytes"].get<std::string>() + " bytes)";

        if (ImGui::Selectable(label.c_str(), name == m_saveSlot)) {
          m_saveSlot = name;
        }
      }

      ImGui::EndDisabled();
      ImGui::Separator();
      ImGui::BeginChild("Game state values", {0, 0}, ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar);
      const auto text = state["data"].dump(2);
      ImGui::TextUnformatted(text.c_str());
      ImGui::EndChild();
    }

    auto viewportControls() -> void {
      ImGui::TextDisabled("%s%s", m_snapshot["scenePath"].get_ref<const std::string&>().c_str(),
                          m_document->stats()->dirty ? " *" : "");

      if (m_animationPreview) {
        animationControls();
        return;
      }

      if (playing()) {
        const auto stats = m_play->stats().value();
        const auto label = stats.location.path ? stats.location.path->c_str() : "Unsaved scene";
        const bool busy = stats.transition
                          && (stats.transition->state == Script::SceneRequestState::Pending
                              || stats.transition->state == Script::SceneRequestState::Loading);
        ImGui::BeginDisabled(stats.state != PlayState::Running || busy || m_pendingClose);
        ImGui::SetNextItemWidth(std::min(360.0F, ImGui::GetContentRegionAvail().x));

        if (ImGui::BeginCombo("Change game scene", label)) {
          for (const auto& entry : m_catalog) {
            const auto& path = entry["path"].get_ref<const std::string&>();

            if (entry["type"] != "directory" && path.ends_with(".dotscene")
                && ImGui::Selectable(path.c_str(), stats.location.path == path)) {
              sessionAction({{"command", "request-play-scene"},
                             {"session", std::to_string(m_playSession)},
                             {"revision", std::to_string(m_document->stats()->revision)},
                             {"path", path}});
            }
          }

          ImGui::EndCombo();
        }

        ImGui::EndDisabled();

        if (stats.transition && stats.transition->error) {
          ImGui::TextWrapped("%s", stats.transition->error->message.c_str());
        }

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

        if (m_document->stats()->selection.size() > 1) {
          ImGui::TextDisabled("%zu selected | Active pivot", m_document->stats()->selection.size());
        } else {
          ImGui::NewLine();
        }

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

      if (hovered && left.pressed && !alt && !right.down && !middle.down) {
        auto state = m_skeleton.stats();

        if (state && state->frame) {
          auto hit = pickSkeleton({(pointer.x - m_viewOrigin.x) / m_viewWidth,
                                   (pointer.y - m_viewOrigin.y) / m_viewHeight},
                                  state->frame->serial);

          if (!hit) {
            m_message = hit.error().message;
          } else if (*hit) {
            m_requestedPane = "skeleton";
            return;
          }
        }
      }

      if (hovered && left.pressed && !alt && m_tool != Tool::Select && m_editorView
          && !m_animationPreview) {
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
        if (hovered && left.pressed && !m_animationPreview) {
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

      if (hovered && left.pressed && !alt && !m_flying && !m_animationPreview) {
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
      if (m_clipDocument) {
        auto state = m_clipDocument->snapshot();
        return state && !m_clipMove && m_clipForm == m_clipOriginal
               && (redo ? state->canRedo : state->canUndo);
      }

      if (m_material) {
        const auto state = materialStatus();
        return state["draft"] == state["value"] && state[redo ? "canRedo" : "canUndo"].get<bool>();
      }

      const auto stats = m_document->stats().value();
      return redo ? stats.canRedo : stats.canUndo;
    }

    auto focusedSave() -> void {
      if (m_clipDocument) {
        clipAction({{"command", "save-animation-clip"}});
      } else if (m_material) {
        fileAction({{"command", "save-material"}});
      } else if (applyInspector()) {
        action({{"command", "save"}});
      }
    }

    auto focusedHistory(bool redo) -> void {
      if (m_clipDocument) {
        clipAction({{"command", redo ? "redo-animation-clip" : "undo-animation-clip"}});
      } else if (m_material) {
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
        for (const auto& item :
             {std::pair{"New scene...", "create"}, std::pair{"Open scene...", "open"},
              std::pair{"Save scene as...", "save-as"}}) {
          if (ImGui::MenuItem(item.first, nullptr, false,
                              !playing() && !m_animationPreview && !m_transform)) {
            sessionAction({{"command", "scene-dialog"}, {"mode", item.second}});
          }
        }

        ImGui::Separator();
        if (ImGui::MenuItem(m_clipDocument ? "Save animation"
                            : m_material   ? "Save material"
                                           : "Save scene",
                            nullptr, false,
                            !playing() && (!m_animationPreview || m_clipDocument))) {
          focusedSave();
        }

        const auto capture = m_capture.stats();
        const bool retained = capture.state == CaptureState::Ready;
        const bool busy = m_captureDialog || capture.state == CaptureState::Queued;

        if (ImGui::MenuItem(retained ? "Save captured PNG..." : "Save viewport PNG...", nullptr,
                            false, !busy)) {
          if (retained) {
            sessionAction({{"command", "browse-capture"}, {"capture", std::to_string(capture.id)}});
          } else {
            action({{"command", "capture-viewport"}, {"dialog", true}});
          }
        }

        if (ImGui::MenuItem("Discard viewport capture", nullptr, false, retained && !busy)) {
          sessionAction({{"command", "discard-capture"}, {"capture", std::to_string(capture.id)}});
        }

        if (ImGui::MenuItem("Export game", nullptr, false, !m_animationPreview)) {
          m_exportDialog = true;
        }

        if (ImGui::MenuItem("Close project")) {
          sessionAction({{"command", "close"}, {"discard", false}});
        }

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem(
                "Undo", nullptr, false,
                !playing() && (!m_animationPreview || m_clipDocument) && historyAvailable(false))) {
          focusedHistory(false);
        }

        if (ImGui::MenuItem(
                "Redo", nullptr, false,
                !playing() && (!m_animationPreview || m_clipDocument) && historyAvailable(true))) {
          focusedHistory(true);
        }

        ImGui::Separator();
        const bool hierarchy = !playing() && !m_animationPreview && !m_transform
                               && !pendingInspector() && !pendingEnvironment()
                               && !pendingOcclusion() && !pendingText()
                               && m_document->stats()->selected.has_value();

        if (ImGui::MenuItem("Group selection", "Cmd/Ctrl+G", false, hierarchy)) {
          action({{"command", "group-selection"}, {"name", "Group"}});
        }

        if (ImGui::MenuItem("Ungroup selection", "Cmd/Ctrl+Shift+G", false, hierarchy)) {
          action({{"command", "ungroup-selection"}});
        }

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Build")) {
        if (ImGui::MenuItem("Export game", nullptr, false, !m_animationPreview)) {
          m_exportDialog = true;
        }

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Window")) {
        for (const auto& [label, pane] :
             {std::pair{"Hierarchy", "scene"}, std::pair{"Scene", "viewport"},
              std::pair{"Inspector", "inspector"}, std::pair{"Environment", "environment"},
              std::pair{"Rendering", "rendering"}, std::pair{"Assets", "resources"},
              std::pair{"Skeleton", "skeleton"}, std::pair{"Animation", "animation"},
              std::pair{"Console", "console"}, std::pair{"Game state", "game-data"}}) {
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

      if (panel == "game-data") {
        return "Game state";
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

      if (panel == "skeleton") {
        return "Skeleton";
      }

      if (panel == "animation") {
        return "Animation";
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

    auto skeletonPanel() -> void {
      auto scene = displayedScene();
      auto state = m_skeleton.stats();

      if (!scene || !state) {
        ImGui::TextWrapped("%s", (!scene ? scene.error() : state.error()).message.c_str());
        return;
      }

      auto entities = (*scene)->entities();

      if (!entities) {
        ImGui::TextWrapped("%s", entities.error().message.c_str());
        return;
      }

      const auto scope = std::to_string((*scene)->id());
      std::string label = "Off";

      if (state->entity) {
        const auto found = std::ranges::find_if(
            *entities, [&](const auto& entity) { return entity.id.value == *state->entity; });
        label = found == entities->end() ? "Missing target" : found->name;
        label += " (" + std::to_string(*state->entity) + ")";
      }

      bool changed{};
      ImGui::TextUnformatted("Mesh skin");
      ImGui::SetNextItemWidth(-1);

      if (ImGui::BeginCombo("##skeleton-target", label.c_str())) {
        if (ImGui::Selectable("Off", !state->entity)) {
          changed
              = sessionAction({{"command", "set-skeleton"}, {"scene", scope}, {"entity", nullptr}});
        }

        for (const auto& entity : *entities) {
          if (!entity.mesh || !entity.skin || !entity.skin->enabled) {
            continue;
          }

          ImGui::PushID(static_cast<int>(entity.id.value));
          const auto name = entity.name + " (" + std::to_string(entity.id.value) + ")";

          if (ImGui::Selectable(name.c_str(), state->entity == entity.id.value)) {
            changed = sessionAction({{"command", "set-skeleton"},
                                     {"scene", scope},
                                     {"entity", std::to_string(entity.id.value)}});
          }

          ImGui::PopID();
        }

        ImGui::EndCombo();
      }

      if (changed || !state->entity) {
        ImGui::TextWrapped("Choose a skinned mesh to inspect its live skeleton.");
        return;
      }

      ImGui::TextWrapped(
          "X-ray overlay shows joints through surfaces. Click a joint in Scene or select it "
          "below.");
      const auto target = std::to_string(*state->entity);
      ImGui::BeginDisabled(playing() || m_transform.has_value());

      if (ImGui::Button("Frame skeleton")) {
        sessionAction({{"command", "focus-skeleton"},
                       {"scene", scope},
                       {"entity", target},
                       {"joint", nullptr}});
      }

      ImGui::BeginDisabled(!state->selected);

      if (ImGui::Button("Frame joint")) {
        sessionAction({{"command", "focus-skeleton"},
                       {"scene", scope},
                       {"entity", target},
                       {"joint", std::to_string(*state->selected)}});
      }

      ImGui::EndDisabled();
      ImGui::EndDisabled();
      ImGui::BeginDisabled(!state->selected || playing() || m_animationPreview || m_transform);

      if (ImGui::Button("Edit selected joint")) {
        if (action({{"command", "select"}, {"entity", std::to_string(*state->selected)}})) {
          sessionAction({{"command", "show-pane"}, {"pane", "inspector"}});
        }
      }

      ImGui::EndDisabled();
      ImGui::SetNextItemWidth(-1);
      searchField("##skeleton-search", "Search joints", "skeleton", m_skeletonFilter);

      if (state->error) {
        ImGui::TextWrapped("%s", state->error->message.c_str());
      }

      if (!state->frame || state->frame->pose.owner.scene != (*scene)->id()) {
        ImGui::TextWrapped("Waiting for a completed Scene viewport frame.");
        return;
      }

      const auto& pose = state->frame->pose;
      ImGui::TextWrapped("Last completed frame %s / %zu joints",
                         std::to_string(state->frame->serial).c_str(), pose.joints.size());
      ImGui::BeginChild("Joint list", {0, 0});

      for (std::size_t index = 0; index < pose.joints.size(); ++index) {
        const auto& joint = pose.joints[index];

        if (!matchesFilter(joint.name, m_skeletonFilter)) {
          continue;
        }

        ImGui::PushID(static_cast<int>(index));

        if (ImGui::Selectable(joint.name.c_str(), state->selected == joint.entity.value)) {
          sessionAction({{"command", "select-skeleton-joint"},
                         {"scene", scope},
                         {"entity", target},
                         {"joint", std::to_string(joint.entity.value)}});
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::Text("Joint %s / palette %zu", std::to_string(joint.entity.value).c_str(), index);
          ImGui::Text("World: %.4f, %.4f, %.4f", joint.position.x, joint.position.y,
                      joint.position.z);

          if (joint.parent) {
            ImGui::Text("Parent: %s (%u hierarchy links)", pose.joints[*joint.parent].name.c_str(),
                        joint.parentLinks);
          } else {
            ImGui::TextUnformatted("Skeleton root");
          }

          ImGui::EndTooltip();
        }

        ImGui::PopID();
      }

      ImGui::EndChild();
    }

    auto animationKeyProperties() -> void {
      auto state = m_clipDocument->snapshot();

      if (!state) {
        ImGui::TextWrapped("%s", state.error().message.c_str());
        return;
      }

      const auto* value = state->value.description().value();
      constexpr std::array paths{"translation", "rotation", "scale"};
      constexpr std::array modes{"step", "linear", "cubic"};
      auto selected
          = m_clipForm.is_null()
                ? value->tracks.end()
                : std::ranges::find_if(value->tracks, [&](const auto& track) {
                    return m_clipForm["node"] == track.node
                           && m_clipForm["path"] == paths.at(static_cast<std::size_t>(track.path));
                  });
      ImGui::TextWrapped("Animation key / %s", value->name.c_str());
      ImGui::TextWrapped("%s / %s", state->path.c_str(),
                         m_clipForm != m_clipOriginal ? "Unapplied key"
                         : state->dirty               ? "Unsaved"
                                                      : "Saved");
      const auto vectorField = [](const char* label, Math::Vec4& vector, bool rotation) -> bool {
        ImGui::TextUnformatted(label);
        ImGui::PushID(label);
        const std::array components{&vector.x, &vector.y, &vector.z, &vector.w};
        constexpr std::array labels{"X", "Y", "Z", "W"};
        bool changed{};

        for (std::size_t i = 0; i < (rotation ? 4U : 3U); ++i) {
          ImGui::PushID(static_cast<int>(i));
          ImGui::TextUnformatted(labels[i]);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(-1);
          changed |= ImGui::InputFloat("##component", components[i], 0, 0, "%.9g");
          ImGui::PopID();
        }

        ImGui::PopID();
        return changed;
      };

      if (!m_clipForm.is_null()) {
        const auto node = clipIndex(m_clipForm["node"]);
        const auto binding = m_clipBindings.find(node);
        const auto jointLabel = binding == m_clipBindings.end()
                                    ? "Unbound node " + std::to_string(node)
                                    : binding->second.second + " (" + std::to_string(node) + ")";
        ImGui::TextWrapped("%s / %s", jointLabel.c_str(),
                           m_clipForm["path"].get<std::string>().c_str());
        auto form = m_clipForm;
        auto key = clipKey(form["key"]);
        bool changed{};
        ImGui::TextUnformatted("Time (s)");
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::InputFloat("##key-time", &key.time, 0, 0, "%.9g");
        const bool rotation = form["path"] == "rotation";
        changed |= vectorField(rotation ? "Value XYZW" : "Value XYZ", key.value, rotation);

        if (form["interpolation"] == "cubic") {
          for (auto [label, tangent] : {std::pair{"In tangent", &key.inTangent},
                                        std::pair{"Out tangent", &key.outTangent}}) {
            changed |= vectorField(label, *tangent, rotation);
          }
        }

        if (changed) {
          form["key"] = keyJson(key);
          clipAction({{"command", "stage-animation-key"}, {"value", std::move(form)}});
        }

        if (ImGui::Button("Insert / replace key")) {
          clipAction({{"command", "apply-animation-key"}});
        }

        const bool originalExists = selected != value->tracks.end() && !m_clipOriginal.is_null()
                                    && m_clipOriginal["node"] == m_clipForm["node"]
                                    && m_clipOriginal["path"] == m_clipForm["path"]
                                    && std::ranges::any_of(selected->keys, [&](const auto& item) {
                                         return m_clipOriginal["key"]["time"] == item.time;
                                       });
        ImGui::BeginDisabled(!originalExists);

        if (ImGui::Button("Update / move selected key")) {
          clipAction({{"command", "move-animation-key"}});
        }

        ImGui::EndDisabled();

        if (ImGui::Button("Reset key form")) {
          clipAction({{"command", "reset-animation-key"}});
        }

        ImGui::BeginDisabled(m_clipForm != m_clipOriginal);

        if (ImGui::Button("Key at playhead")) {
          clipAction({{"command", "select-animation-key"},
                      {"node", m_clipForm["node"]},
                      {"path", m_clipForm["path"]},
                      {"time", nullptr}});
        }

        if (selected != value->tracks.end()) {
          int mode = static_cast<int>(selected->interpolation);
          ImGui::TextUnformatted("Interpolation");
          ImGui::SetNextItemWidth(-1);

          if (ImGui::Combo("##key-interpolation", &mode, modes.data(),
                           static_cast<int>(modes.size()))) {
            clipAction(
                {{"command", "edit-animation-clip"},
                 {"operations",
                  Json::array({{{"op", "set-interpolation"},
                                {"node", selected->node},
                                {"path", m_clipForm["path"]},
                                {"interpolation", modes.at(static_cast<std::size_t>(mode))}}})}});
          }

          const bool hasKey = !m_clipOriginal.is_null()
                              && std::ranges::any_of(selected->keys, [&](const auto& item) {
                                   return m_clipOriginal["key"]["time"] == item.time;
                                 });
          ImGui::BeginDisabled(!hasKey);

          if (ImGui::Button("Delete key")) {
            clipAction({{"command", "edit-animation-clip"},
                        {"operations", Json::array({{{"op", "remove-key"},
                                                     {"node", selected->node},
                                                     {"path", m_clipForm["path"]},
                                                     {"time", m_clipOriginal["key"]["time"]}}})}});
          }

          ImGui::EndDisabled();

          if (ImGui::Button("Delete track")) {
            clipAction({{"command", "edit-animation-clip"},
                        {"operations", Json::array({{{"op", "remove-track"},
                                                     {"node", selected->node},
                                                     {"path", m_clipForm["path"]}}})}});
          }
        }

        ImGui::EndDisabled();
      }

      ImGui::Separator();
      ImGui::TextWrapped("New channel from a bound node's current pose");
      ImGui::TextUnformatted("Node");
      ImGui::SetNextItemWidth(-1);
      const auto selectedNode = m_clipBindings.find(static_cast<std::uint32_t>(m_clipNewNode));
      const auto nodeLabel = selectedNode == m_clipBindings.end() ? std::string("Choose node")
                                                                  : selectedNode->second.second;

      if (ImGui::BeginCombo("##new-key-node", nodeLabel.c_str())) {
        for (const auto& [node, binding] : m_clipBindings) {
          const auto name = std::to_string(node) + " / " + binding.second;
          ImGui::PushID(static_cast<int>(node));

          if (ImGui::Selectable(name.c_str(), static_cast<int>(node) == m_clipNewNode)) {
            m_clipNewNode = static_cast<int>(node);
          }

          ImGui::PopID();
        }

        ImGui::EndCombo();
      }
      ImGui::TextUnformatted("Path");
      ImGui::SetNextItemWidth(-1);
      ImGui::Combo("##new-key-path", &m_clipNewPath, paths.data(), static_cast<int>(paths.size()));
      ImGui::BeginDisabled(m_clipNewNode < 0 || m_clipNewNode > 9999);

      if (ImGui::Button("Prepare new key")) {
        clipAction({{"command", "select-animation-key"},
                    {"node", static_cast<std::uint32_t>(m_clipNewNode)},
                    {"path", paths.at(static_cast<std::size_t>(m_clipNewPath))},
                    {"time", nullptr}});
      }

      ImGui::EndDisabled();
    }

    auto chooseClipKey(std::uint32_t node, Animation::Path path, float time) -> void {
      const auto& io = ImGui::GetIO();
      const bool additive = io.KeyCtrl || io.KeySuper;
      auto keys = additive ? m_clipSelection : std::vector<Animation::KeyReference>{};
      auto selected = additive ? m_clipSelected : decltype(m_clipSelected){};
      const auto same = [&](const auto& item) {
        return item.node == node && item.path == path && item.time == time;
      };

      if (io.KeyShift && !m_clipSelection.empty() && m_clipSelection.back().node == node
          && m_clipSelection.back().path == path) {
        const auto anchor = m_clipSelection.back().time;
        const auto state = m_clipDocument->snapshot().value();
        const auto& tracks = state.value.description().value()->tracks;
        const auto track = std::ranges::find_if(
            tracks, [&](const auto& item) { return item.node == node && item.path == path; });

        if (track != tracks.end()) {
          const auto first = std::ranges::lower_bound(track->keys, std::min(anchor, time), {},
                                                      &Animation::Key::time);
          const auto last = std::ranges::upper_bound(track->keys, std::max(anchor, time), {},
                                                     &Animation::Key::time);

          if (static_cast<std::size_t>(last - first) > Animation::MaxSelectedKeys) {
            m_message = "Selection exceeds 16384 keys; narrow the range";
            return;
          }

          for (auto key = first; key != last; ++key) {
            if (selected.emplace(node, path, key->time).second) {
              keys.push_back({node, path, key->time});
            }
          }
        }
      } else if (additive) {
        const auto found = std::ranges::find_if(keys, same);

        if (found != keys.end()) {
          keys.erase(found);
        } else {
          keys.push_back({node, path, time});
        }
      } else {
        keys.push_back({node, path, time});
      }

      if (io.KeyShift) {
        const auto at = std::ranges::find_if(keys, same);

        if (at != keys.end()) {
          const auto active = *at;
          keys.erase(at);
          keys.push_back(active);
        }
      }

      clipAction({{"command", "select-animation-keys"}, {"keys", referencesJson(keys)}});
    }

    auto animationCurve(const AnimationSnapshot& state, const Animation::Track& track) -> void {
      constexpr std::array paths{"translation", "rotation", "scale"};
      const auto* path = paths.at(static_cast<std::size_t>(track.path));
      const auto cacheKey
          = Json::array({m_buffer, state.revision, track.node, path, m_curveFrom, m_curveTo})
                .dump();

      if (m_curveCacheKey != cacheKey) {
        auto sampled
            = sampleCurve(state.value, track.node, track.path, m_curveFrom, m_curveTo, 256);
        m_curveCache = sampled ? std::move(*sampled) : errorJson(sampled.error());
        m_curveCacheKey = cacheKey;
      }

      if (!m_curveCache.value("ok", false)) {
        ImGui::TextWrapped("Curve sampling failed: %s", m_curveCache.dump().c_str());
        return;
      }

      const auto first
          = std::ranges::lower_bound(track.keys, m_curveFrom, {}, &Animation::Key::time);
      const auto last = std::ranges::upper_bound(track.keys, m_curveTo, {}, &Animation::Key::time);

      if (last - first > 2048) {
        ImGui::TextWrapped("More than 2048 visible keys. Narrow the time range to edit the curve.");
        return;
      }

      const auto& points = m_curveCache["points"];
      const auto count = track.path == Animation::Path::Rotation ? 4U : 3U;
      double low = std::numeric_limits<double>::max();
      double high = std::numeric_limits<double>::lowest();

      for (const auto& point : points) {
        for (std::size_t component = 0; component < count; ++component) {
          const auto value = point["value"][component].get<double>();
          low = std::min(low, value);
          high = std::max(high, value);
        }
      }

      for (auto key = first; key != last; ++key) {
        const auto values = std::array{key->value.x, key->value.y, key->value.z, key->value.w};

        for (std::size_t component = 0; component < count; ++component) {
          low = std::min(low, double(values[component]));
          high = std::max(high, double(values[component]));
        }
      }

      const auto padding = high > low ? (high - low) * .08 : std::max(1e-6, std::abs(high) * .05);
      low -= padding;
      high += padding;
      const auto origin = ImGui::GetCursorScreenPos();
      const auto size = ImVec2(std::max(80.F, ImGui::GetContentRegionAvail().x),
                               std::max(60.F, ImGui::GetContentRegionAvail().y));
      ImGui::InvisibleButton("Native animation curve", size);
      const bool hovered = ImGui::IsItemHovered();
      const ImVec2 minimum{origin.x + 8, origin.y + 22};
      const ImVec2 maximum{origin.x + size.x - 8, origin.y + size.y - 22};
      const auto x = [&](double time) -> float {
        return minimum.x
               + static_cast<float>((time - m_curveFrom) / (m_curveTo - m_curveFrom))
                     * (maximum.x - minimum.x);
      };
      const auto y = [&](double value) -> float {
        return maximum.y
               - static_cast<float>((value - low) / (high - low)) * (maximum.y - minimum.y);
      };
      auto* draw = ImGui::GetWindowDrawList();
      draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                          ImGui::GetColorU32(ImGuiCol_FrameBg), 3);
      const auto colors
          = m_lightTheme ? std::array{IM_COL32(175, 55, 51, 255), IM_COL32(30, 125, 58, 255),
                                      IM_COL32(35, 99, 175, 255), IM_COL32(145, 101, 15, 255)}
                         : std::array{IM_COL32(231, 105, 102, 255), IM_COL32(107, 201, 131, 255),
                                      IM_COL32(102, 166, 240, 255), IM_COL32(219, 185, 93, 255)};
      constexpr std::array labels{"X", "Y", "Z", "W"};

      for (std::size_t component = 0; component < count; ++component) {
        draw->AddText({minimum.x + 24 * static_cast<float>(component), origin.y + 2},
                      colors[component], labels[component]);
      }

      char valuesLabel[100];
      std::snprintf(valuesLabel, sizeof(valuesLabel), "%.4g .. %.4g", low, high);
      const auto textWidth = ImGui::CalcTextSize(valuesLabel).x;
      draw->AddText({std::max(minimum.x + 100, maximum.x - textWidth), origin.y + 2},
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), valuesLabel);

      char range[100];
      std::snprintf(range, sizeof(range), "%.6g - %.6g s", m_curveFrom, m_curveTo);
      draw->AddText({minimum.x, maximum.y + 3}, ImGui::GetColorU32(ImGuiCol_Text), range);
      draw->PushClipRect(minimum, maximum, true);

      for (int line = 0; line <= 4; ++line) {
        const auto at = minimum.y + (maximum.y - minimum.y) * static_cast<float>(line) / 4;
        draw->AddLine({minimum.x, at}, {maximum.x, at}, ImGui::GetColorU32(ImGuiCol_Border));
      }

      for (std::size_t component = 0; component < count; ++component) {
        for (std::size_t i = 1; i < points.size(); ++i) {
          const auto& a = points[i - 1];
          const auto& b = points[i];

          if (track.interpolation != Animation::Interpolation::Step) {
            draw->AddLine({x(a["time"].get<double>()), y(a["value"][component].get<double>())},
                          {x(b["time"].get<double>()), y(b["value"][component].get<double>())},
                          colors[component], 1.5F);
          }
        }

        if (track.interpolation == Animation::Interpolation::Step) {
          const auto value = [&](const Math::Vec4& v) -> float {
            return std::array{v.x, v.y, v.z, v.w}[component];
          };
          float previous = y(points.front()["value"][component].get<double>());
          float start = minimum.x;

          for (auto key = first; key != last; ++key) {
            const auto end = x(key->time);
            const auto next = y(value(key->value));
            draw->AddLine({start, previous}, {end, previous}, colors[component], 1.5F);
            draw->AddLine({end, previous}, {end, next}, colors[component], 1.5F);
            start = end;
            previous = next;
          }

          draw->AddLine({start, previous}, {maximum.x, previous}, colors[component], 1.5F);
        }
      }

      const Animation::Key* hit{};
      float distance = 49;
      const auto mouse = ImGui::GetIO().MousePos;

      for (auto key = first; key != last; ++key) {
        const auto values = std::array{key->value.x, key->value.y, key->value.z, key->value.w};
        const bool selected = m_clipSelected.contains({track.node, track.path, key->time});

        for (std::size_t component = 0; component < count; ++component) {
          const ImVec2 point{x(key->time), y(values[component])};
          draw->AddCircleFilled(point, selected ? 4 : 2.5F, colors[component]);
          const auto squared = (mouse.x - point.x) * (mouse.x - point.x)
                               + (mouse.y - point.y) * (mouse.y - point.y);

          if (squared < distance) {
            distance = squared;
            hit = &*key;
          }
        }
      }

      const auto preview = m_animationPreview->stats();

      if (preview) {
        const auto at = x(preview->player.playback.time);
        draw->AddLine({at, minimum.y}, {at, maximum.y}, ImGui::GetColorU32(ImGuiCol_Text), 1);
      }

      if (hovered && hit) {
        ImGui::SetTooltip("%.9g s / drag horizontally to move key", double(hit->time));
      } else if (hovered && !m_clipMove) {
        const auto time = std::clamp(
            m_curveFrom
                + (mouse.x - minimum.x) / (maximum.x - minimum.x) * (m_curveTo - m_curveFrom),
            m_curveFrom, m_curveTo);
        const auto* description = state.value.description().value();
        const auto index = static_cast<std::size_t>(&track - description->tracks.data());
        auto sample = state.value.sampleTrack(index, time);

        if (sample) {
          const auto v = sample->value;
          ImGui::SetTooltip("%.9g s\nX %.9g  Y %.9g\nZ %.9g  W %.9g", time, double(v.x),
                            double(v.y), double(v.z), double(v.w));
        } else {
          ImGui::SetTooltip("%s", sample.error().message.c_str());
        }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_clipMove) {
        if (hit) {
          const auto& io = ImGui::GetIO();

          if (io.KeyCtrl || io.KeySuper || io.KeyShift) {
            chooseClipKey(track.node, track.path, hit->time);
          } else {
            bool selected = m_clipSelected.contains({track.node, track.path, hit->time});

            if (!selected) {
              selected = clipAction({{"command", "select-animation-key"},
                                     {"node", track.node},
                                     {"path", path},
                                     {"time", hit->time}});
            }

            if (selected && clipAction({{"command", "begin-animation-key-move"}})) {
              const auto sourceTime = m_clipSelection.back().time;
              m_curveDrag.emplace(m_clipMove->session, sourceTime, mouse.x, minimum.x, maximum.x);
            }
          }
        } else if (!hit && preview) {
          const auto time = std::clamp(
              m_curveFrom
                  + (mouse.x - minimum.x) / (maximum.x - minimum.x) * (m_curveTo - m_curveFrom),
              m_curveFrom, m_curveTo);
          sessionAction({{"command", "control-animation-preview"},
                         {"session", std::to_string(m_animationSession)},
                         {"action", "seek"},
                         {"seconds", time}});
        }
      }

      if (m_curveDrag) {
        const auto drag = *m_curveDrag;
        const auto focused = m_application->window().focused();

        if (!m_clipMove || m_clipMove->session != drag.session) {
          m_curveDrag.reset();
        } else if (!focused || !*focused || minimum.x != drag.minX || maximum.x != drag.maxX
                   || (!ImGui::IsMouseDown(ImGuiMouseButton_Left)
                       && !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                   || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
          sessionAction({{"command", "finish-animation-key-move"},
                         {"session", std::to_string(drag.session)},
                         {"commit", false}});
        } else {
          const auto value = m_clipMove->move.snapshot().value();

          /** Include final release displacement even without an earlier held
           * motion frame. Returning to origin restores a semantic no-op.
           * 即使没有更早的按住移动帧，也纳入释放位置；回到起点恢复语义无变化。 */
          if ((ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3)
               || ImGui::IsMouseReleased(ImGuiMouseButton_Left))
              && (std::abs(mouse.x - drag.mouseStart) >= 3 || value.changed)) {
            const auto target = static_cast<float>(
                std::clamp(double(drag.sourceTime)
                               + (mouse.x - drag.mouseStart) / (drag.maxX - drag.minX)
                                     * (m_curveTo - m_curveFrom),
                           0., 86400.));

            if (target != value.time) {
              sessionAction({{"command", "update-animation-key-move"},
                             {"session", std::to_string(drag.session)},
                             {"time", target}});
            }
          }

          if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_curveDrag.reset();
            sessionAction({{"command", "finish-animation-key-move"},
                           {"session", std::to_string(drag.session)},
                           {"commit", true}});
          }
        }
      }

      if (m_clipMove) {
        const auto value = m_clipMove->move.snapshot().value();

        for (const auto& item : value.keys) {
          if (item.node == track.node && item.path == track.path) {
            const auto at = x(item.key.time);
            draw->AddLine({at, minimum.y}, {at, maximum.y},
                          item.occupied ? IM_COL32(230, 75, 75, 255)
                                        : ImGui::GetColorU32(ImGuiCol_ButtonActive),
                          2);
          }
        }

        if (m_curveDrag) {
          ImGui::SetTooltip("Move %zu keys / anchor %.9g s%s / release to commit, Escape to cancel",
                            value.keys.size(), double(value.time),
                            value.occupied ? " (occupied)" : "");
        }
      }

      draw->PopClipRect();
    }

    auto animationPanel() -> void {
      const auto clipboard = m_animationClipboard.snapshot();

      if (!m_clipDocument) {
        ImGui::TextWrapped("Select an animation player in Inspector, then Edit animation clip.");

        if (clipboard) {
          ImGui::TextWrapped("%zu copied keys retained from %s",
                             clipboard->keys.description().value()->keys.size(),
                             clipboard->source.path.c_str());

          if (ImGui::Button("Clear copied keys")) {
            sessionAction({{"command", "clear-animation-clipboard"},
                           {"clipboard", std::to_string(clipboard->id)}});
          }
        }

        return;
      }

      auto state = m_clipDocument->snapshot();
      auto preview = m_animationPreview->stats();

      if (!state || !preview) {
        ImGui::TextWrapped("%s", (!state ? state.error() : preview.error()).message.c_str());
        return;
      }

      const auto* value = state->value.description().value();
      const bool pending = m_clipForm != m_clipOriginal;
      const auto nextButton = [](const char* label) -> bool {
        const auto width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine();

        if (ImGui::GetContentRegionAvail().x < width) {
          ImGui::NewLine();
        }

        return ImGui::Button(label);
      };
      ImGui::BeginDisabled(pending || m_clipMove.has_value());

      if (ImGui::Button("Save clip")) {
        clipAction({{"command", "save-animation-clip"}});
      }

      ImGui::BeginDisabled(!state->canUndo);

      if (nextButton("Undo clip")) {
        clipAction({{"command", "undo-animation-clip"}});
      }

      ImGui::EndDisabled();
      ImGui::BeginDisabled(!state->canRedo);

      if (nextButton("Redo clip")) {
        clipAction({{"command", "redo-animation-clip"}});
      }

      ImGui::EndDisabled();
      ImGui::EndDisabled();

      if (nextButton("Playback options")) {
        ImGui::OpenPopup("Clip playback options");
      }

      if (ImGui::BeginPopup("Clip playback options")) {
        double speed = preview->player.playback.speed;
        bool loop = preview->player.playback.looping;
        bool changed = ImGui::InputDouble("Speed", &speed, .1, 1, "%.3f");
        changed |= ImGui::Checkbox("Loop", &loop);

        if (changed) {
          sessionAction({{"command", "control-animation-preview"},
                         {"session", std::to_string(m_animationSession)},
                         {"action", "configure"},
                         {"speed", speed},
                         {"looping", loop}});
        }

        ImGui::EndPopup();
      }

      if (nextButton("Close clip")) {
        if (clipAction({{"command", "close-animation"}, {"discard", false}})) {
          return;
        }
      }

      if (state->dirty || pending || m_clipMove) {
        if (nextButton("Discard and close")) {
          if (clipAction({{"command", "close-animation"}, {"discard", true}})) {
            return;
          }
        }
      }

      float time = static_cast<float>(preview->player.playback.time);
      const bool compact = ImGui::GetContentRegionAvail().x >= 600;
      ImGui::SetNextItemWidth(compact ? ImGui::GetContentRegionAvail().x - 320 : -1);

      if (ImGui::SliderFloat("##clip-time", &time, 0, static_cast<float>(preview->player.duration),
                             "%.6f s")) {
        sessionAction({{"command", "control-animation-preview"},
                       {"session", std::to_string(m_animationSession)},
                       {"action", "seek"},
                       {"seconds", time}});
      }

      auto from = m_curveFrom;
      auto to = m_curveTo;

      if (compact) {
        ImGui::SameLine();
      }

      ImGui::BeginDisabled(m_clipMove.has_value());
      ImGui::SetNextItemWidth(80);
      bool rangeChanged = ImGui::InputDouble("From##curve", &from, 0, 0, "%.6g",
                                             ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80);
      rangeChanged |= ImGui::InputDouble("To##curve", &to, 0, 0, "%.6g",
                                         ImGuiInputTextFlags_EnterReturnsTrue);

      if (rangeChanged) {
        clipAction({{"command", "set-animation-curve-view"}, {"from", from}, {"to", to}});
      }

      if (nextButton("Fit time")) {
        clipAction({{"command", "set-animation-curve-view"},
                    {"from", 0.},
                    {"to", std::max(1., state->value.duration().value())}});
      }

      ImGui::EndDisabled();

      if (!m_clipMove) {
        ImGui::BeginDisabled(pending || m_clipSelection.empty());

        if (ImGui::Button("Move selected keys")) {
          clipAction({{"command", "begin-animation-key-move"}});
        }

        if (nextButton("Copy keys")) {
          clipAction({{"command", "copy-animation-keys"}});
        }

        ImGui::EndDisabled();
        ImGui::BeginDisabled(pending || !clipboard);

        if (nextButton("Paste keys...")) {
          m_clipPasteTime = static_cast<float>(preview->player.playback.time);
          ImGui::OpenPopup("Paste animation keys");
        }

        ImGui::EndDisabled();
        ImGui::BeginDisabled(pending);

        if (nextButton("Select all keys")) {
          clipAction({{"command", "select-all-animation-keys"}});
        }

        if (nextButton("Clear selection")) {
          clipAction({{"command", "select-animation-keys"}, {"keys", Json::array()}});
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%zu selected", m_clipSelection.size());
        ImGui::SetItemTooltip(
            "Ctrl/Cmd toggles keys across tracks; Shift selects a range. Last selected key anchors "
            "a move.");
      }

      if (ImGui::BeginPopup("Paste animation keys")) {
        if (clipboard) {
          const auto copied = clipboard->keys.description().value();
          ImGui::TextWrapped("%zu keys / %zu channels / %.9g to %.9g s", copied->keys.size(),
                             copied->channels, double(copied->start), double(copied->end));
          ImGui::TextWrapped("From %s", clipboard->source.path.c_str());
          ImGui::SetNextItemWidth(140);
          ImGui::InputFloat("Paste at (s)", &m_clipPasteTime, 0, 0, "%.9g");
          ImGui::Checkbox("Replace matching key times", &m_clipPasteOverwrite);
          const bool compatible = clipboardCompatible();

          if (!compatible) {
            ImGui::TextWrapped(
                "Copied keys belong to a different scene or animation player. Copy again from this "
                "player.");
          }

          ImGui::BeginDisabled(!compatible || m_clipMove.has_value());

          if (ImGui::Button("Paste")) {
            if (clipAction({{"command", "paste-animation-keys"},
                            {"clipboard", std::to_string(clipboard->id)},
                            {"time", m_clipPasteTime},
                            {"overwrite", m_clipPasteOverwrite}})) {
              ImGui::CloseCurrentPopup();
            }
          }

          ImGui::EndDisabled();

          if (!m_message.empty()) {
            ImGui::TextWrapped("%s", m_message.c_str());
          }

          if (ImGui::Button("Clear copied keys")) {
            sessionAction({{"command", "clear-animation-clipboard"},
                           {"clipboard", std::to_string(clipboard->id)}});
            ImGui::CloseCurrentPopup();
          }
        } else {
          ImGui::TextUnformatted("No copied keys.");
        }

        ImGui::EndPopup();
      }

      if (m_clipMove) {
        const auto move = m_clipMove->move.snapshot().value();
        const auto session = std::to_string(m_clipMove->session);
        float moveTime = move.time;
        ImGui::SetNextItemWidth(110);

        if (ImGui::InputFloat("Move time (s)", &moveTime, 0, 0, "%.9g",
                              ImGuiInputTextFlags_EnterReturnsTrue)) {
          sessionAction(
              {{"command", "update-animation-key-move"}, {"session", session}, {"time", moveTime}});
        }

        ImGui::BeginDisabled(move.occupied);

        if (nextButton("Apply move")) {
          sessionAction(
              {{"command", "finish-animation-key-move"}, {"session", session}, {"commit", true}});
        }

        ImGui::EndDisabled();

        if (nextButton("Cancel move")) {
          sessionAction(
              {{"command", "finish-animation-key-move"}, {"session", session}, {"commit", false}});
        }

        if (move.occupied) {
          ImGui::TextUnformatted("Destination time is occupied; choose another time or cancel.");
        }
      }

      if (!ImGui::BeginTable("Animation editor", 3,
                             ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        return;
      }

      ImGui::TableSetupColumn("Tracks", ImGuiTableColumnFlags_WidthStretch, .25F);
      ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthStretch, .15F);
      ImGui::TableSetupColumn("Curves", ImGuiTableColumnFlags_WidthStretch, .6F);
      ImGui::TableHeadersRow();
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const auto height = std::max(60.F, ImGui::GetContentRegionAvail().y);
      constexpr std::array paths{"translation", "rotation", "scale"};
      ImGui::BeginChild("Animation tracks", {0, height});

      if (m_clipTrackScroll && !m_clipForm.is_null()) {
        auto track = std::ranges::find_if(value->tracks, [&](const auto& item) {
          return m_clipForm["node"] == item.node
                 && m_clipForm["path"] == paths.at(static_cast<std::size_t>(item.path));
        });

        if (track != value->tracks.end()) {
          ImGui::SetScrollY(static_cast<float>(track - value->tracks.begin())
                            * ImGui::GetTextLineHeightWithSpacing());
        }

        m_clipTrackScroll = false;
      }

      ImGuiListClipper tracks;
      tracks.Begin(static_cast<int>(value->tracks.size()));

      while (tracks.Step()) {
        for (int i = tracks.DisplayStart; i < tracks.DisplayEnd; ++i) {
          const auto& track = value->tracks[static_cast<std::size_t>(i)];
          const auto* path = paths.at(static_cast<std::size_t>(track.path));
          const auto label = std::to_string(track.node) + " / "
                             + m_clipBindings.at(track.node).second + " / " + path;
          const bool selected = !m_clipForm.is_null() && m_clipForm["node"] == track.node
                                && m_clipForm["path"] == path;

          ImGui::PushID(i);

          ImGui::BeginDisabled(m_clipMove.has_value());

          if (ImGui::Selectable(label.c_str(), selected)) {
            chooseClipKey(track.node, track.path, track.keys.front().time);
          }

          ImGui::EndDisabled();
          ImGui::PopID();
        }
      }

      ImGui::EndChild();
      ImGui::TableSetColumnIndex(1);
      auto selected
          = m_clipForm.is_null()
                ? value->tracks.end()
                : std::ranges::find_if(value->tracks, [&](const auto& track) {
                    return m_clipForm["node"] == track.node
                           && m_clipForm["path"] == paths.at(static_cast<std::size_t>(track.path));
                  });
      ImGui::BeginChild("Animation keys", {0, height});

      if (selected != value->tracks.end()) {
        if (m_clipKeyScroll && !m_clipOriginal.is_null()) {
          auto key = std::ranges::find_if(selected->keys, [&](const auto& item) {
            return m_clipOriginal["key"]["time"] == item.time;
          });

          if (key != selected->keys.end()) {
            ImGui::SetScrollY(static_cast<float>(key - selected->keys.begin())
                              * ImGui::GetTextLineHeightWithSpacing());
          }

          m_clipKeyScroll = false;
        }

        ImGuiListClipper keys;
        keys.Begin(static_cast<int>(selected->keys.size()));

        while (keys.Step()) {
          for (int i = keys.DisplayStart; i < keys.DisplayEnd; ++i) {
            const auto& key = selected->keys[static_cast<std::size_t>(i)];
            char label[64];
            std::snprintf(label, sizeof(label), "%.6g s##%d", static_cast<double>(key.time), i);

            ImGui::BeginDisabled(m_clipMove.has_value());

            if (ImGui::Selectable(
                    label, m_clipSelected.contains({selected->node, selected->path, key.time}))) {
              chooseClipKey(selected->node, selected->path, key.time);
            }

            ImGui::EndDisabled();
          }
        }
      } else {
        ImGui::TextWrapped("Select a track to inspect its keys.");
      }

      ImGui::EndChild();
      ImGui::TableSetColumnIndex(2);

      if (selected != value->tracks.end()) {
        animationCurve(*state, *selected);
      } else {
        ImGui::TextWrapped(
            "Select a track. Click a curve key to select; drag horizontally to move.");
      }

      ImGui::EndTable();
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

      if (m_clipDocument) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
          focusedSave();
        }

        if (!ImGui::GetIO().WantTextInput
            && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
          focusedHistory(false);
        }

        if (!ImGui::GetIO().WantTextInput
            && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z,
                               ImGuiInputFlags_RouteGlobal)) {
          focusedHistory(true);
        }
      }

      ImGui::BeginDisabled(playing() || m_animationPreview.has_value());

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
        action({{"command", "delete-selection"}});
      }

      ImGui::SameLine();

      if ((ImGui::Button("Duplicate")
           || (!ImGui::GetIO().WantTextInput
               && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, ImGuiInputFlags_RouteGlobal)))
          && m_document->stats()->selected && !playing()) {
        action({{"command", "duplicate-selection"}});
      }

      if (!ImGui::GetIO().WantTextInput && !playing() && !m_animationPreview && !m_transform) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_G,
                            ImGuiInputFlags_RouteGlobal)) {
          action({{"command", "ungroup-selection"}});
        } else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G, ImGuiInputFlags_RouteGlobal)) {
          action({{"command", "group-selection"}, {"name", "Group"}});
        }
      }

      ImGui::EndDisabled();
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(m_animationPreview
                           || (!playing()
                               && (pendingInspector() || pendingEnvironment() || pendingOcclusion()
                                   || pendingText())));

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

      if (m_agentInput.stats().active) {
        ImGui::TextUnformatted("Agent control");
        ImGui::SameLine();

        if (ImGui::Button("Release control")) {
          sessionAction(
              {{"command", "cancel-play-input"}, {"session", std::to_string(m_playSession)}});
        }

        ImGui::SameLine();
      }

      ImGui::BeginDisabled(m_animationPreview.has_value());

      if (ImGui::Button("Export game")) {
        m_exportDialog = true;
        m_message.clear();
      }

      ImGui::EndDisabled();

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
            = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.34F, nullptr, &center);
        const auto hierarchy
            = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18F / 0.78F, nullptr, &center);
        ImGui::DockBuilderDockWindow("Hierarchy", hierarchy);
        ImGui::DockBuilderDockWindow("Scene", center);
        ImGui::DockBuilderDockWindow("Inspector", inspector);
        ImGui::DockBuilderDockWindow("Environment", inspector);
        ImGui::DockBuilderDockWindow("Rendering", inspector);
        ImGui::DockBuilderDockWindow("Skeleton", inspector);
        ImGui::DockBuilderDockWindow("Game state", inspector);
        ImGui::DockBuilderDockWindow("Assets", assets);
        ImGui::DockBuilderDockWindow("Console", assets);
        ImGui::DockBuilderDockWindow("Animation", assets);
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

      m_selectionIds.clear();

      for (const auto& id : m_snapshot["selection"]) {
        m_selectionIds.insert(id.get<std::string>());
      }

      m_visibleHierarchy = Json::array();
      m_hierarchyRows = Json::object();
      m_visibleResources = Json::array();

      if (beginPanel("Hierarchy", "hierarchy", "scene")) {
        ImGui::SetNextItemWidth(-1);
        searchField("##hierarchy-search", "Search hierarchy", "hierarchy", m_hierarchyFilter);
        ImGui::Separator();
        ImGui::BeginDisabled(playing() || m_animationPreview.has_value() || m_transform.has_value()
                             || m_pendingClose);
        std::optional<Json> selection;

        if (ImGui::Selectable("Scene root (drop to detach)", false)) {
          selection = Json{{"command", "select-many"}, {"entities", Json::array()}};
        }

        hierarchyDrop(nullptr, selection);
        tree(m_snapshot["scene"]["entities"], nullptr, selection);
        ImGui::EndDisabled();

        if (selection) {
          action(std::move(*selection));
        }
      }

      ImGui::End();

      if (rebuildingLayout) {
        ImGui::SetNextWindowDockID(initialViewportDock, ImGuiCond_Always);
        ImGui::SetNextWindowFocus();
      }

      m_viewVisible = beginPanel("Scene", "viewport", "viewport");

      if (m_viewVisible) {
        if (m_preparationStage != Graphics::ScenePreparationStage::Ready) {
          ImGui::TextUnformatted(m_preparationStage == Graphics::ScenePreparationStage::Environment
                                     ? "Preparing environment lighting..."
                                 : m_preparationStage == Graphics::ScenePreparationStage::Shadows
                                     ? "Preparing shadow shaders..."
                                 : m_preparationStage == Graphics::ScenePreparationStage::Occlusion
                                     ? "Preparing ambient occlusion shaders..."
                                 : m_preparationStage == Graphics::ScenePreparationStage::Skins
                                     ? "Preparing skinning shaders..."
                                 : m_preparationStage
                                         == Graphics::ScenePreparationStage::EnvironmentMap
                                     ? "Preparing HDR environment..."
                                     : "Preparing material textures...");
        }

        if (m_animationPreview) {
          const auto maxHeight = std::max(ImGui::GetFrameHeightWithSpacing() * 2,
                                          ImGui::GetContentRegionAvail().y * .6F);
          ImGui::SetNextWindowSizeConstraints({0, 0},
                                              {std::numeric_limits<float>::max(), maxHeight});
          ImGui::BeginChild("Viewport controls", {0, 0}, ImGuiChildFlags_AutoResizeY);
        } else {
          ImGui::BeginChild("Viewport controls", {0, ImGui::GetFrameHeightWithSpacing() * 5});
        }

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
                std::uint32_t height) -> Core::Result<std::optional<Graphics::GuiViewportFrame>> {
              auto ready = preview(width, height);

              if (!ready) {
                return std::unexpected(ready.error());
              }

              if (!m_previewReady) {
                return std::optional<Graphics::GuiViewportFrame>{};
              }

              auto scene = displayedScene();

              if (!scene) {
                return std::unexpected(scene.error());
              }

              auto overlay
                  = m_skeleton.render(**scene, m_project->files().value(),
                                      m_renderedFrame->viewProjection, m_renderedFrame->serial);
              return std::optional{Graphics::GuiViewportFrame{
                  *m_target, overlay ? std::move(*overlay) : Graphics::ViewportOverlay{}}};
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
        if (m_clipDocument) {
          if (m_clipSelection.size() > 1) {
            ImGui::TextWrapped(
                "%zu keys selected. Move the group in Animation; select one key to edit values.",
                m_clipSelection.size());
          }

          ImGui::BeginDisabled(m_clipMove.has_value() || m_clipSelection.size() > 1);
          animationKeyProperties();
          ImGui::EndDisabled();
        } else {
          ImGui::BeginDisabled(playing() || m_transform.has_value()
                               || m_animationPreview.has_value());
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

          sceneOverrideInspector();

          if (!playing() && !m_animationPreview && !m_transform && m_editEntity
              && m_edit.contains("animation") && !m_edit["animation"].is_null()) {
            ImGui::BeginDisabled(pendingInspector() || pendingText() || pendingEnvironment()
                                 || pendingOcclusion());

            if (ImGui::Button("Preview animation")) {
              action({{"command", "preview-animation"}, {"entity", std::to_string(*m_editEntity)}});
            }

            if (ImGui::Button("Edit animation clip")) {
              action({{"command", "open-animation"},
                      {"entity", std::to_string(*m_editEntity)},
                      {"clip", m_edit["animation"]["clip"]}});
            }

            ImGui::EndDisabled();
          }
        }
      }

      ImGui::End();

      if (beginPanel("Game state", "game-data", "game-data")) {
        gameDataPanel();
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

      if (beginPanel("Skeleton", "skeleton", "skeleton")) {
        skeletonPanel();
      }

      ImGui::End();

      if (beginPanel("Animation", "animation", "animation")) {
        animationPanel();
      }

      ImGui::End();

      if (beginPanel("Assets", "resources", "resources")) {
        ImGui::BeginDisabled(m_animationPreview.has_value());
        resourcesPanel();
        ImGui::EndDisabled();
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

      if (m_sceneDialogRequested) {
        ImGui::OpenPopup("Scene files");
        m_sceneDialogRequested = false;
      }

      if (m_sceneDialog || m_sceneDialogClose) {
        const auto sceneDialogSpace = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSizeConstraints({520, 0}, {std::min(700.F, sceneDialogSpace.x - 40),
                                                       std::min(600.F, sceneDialogSpace.y - 40)});
      }

      if (ImGui::BeginPopupModal("Scene files", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
          sessionAction({{"command", "scene-dialog"}, {"mode", "close"}});
        }

        if (m_sceneDialogClose) {
          ImGui::CloseCurrentPopup();
          m_sceneDialogClose = false;
        } else {
          ImGui::TextWrapped("Current scene: %s",
                             m_snapshot["scenePath"].get_ref<const std::string&>().c_str());
          ImGui::TextWrapped("%s", m_sceneDialogMode == 0 ? "Open an existing project scene."
                                   : m_sceneDialogMode == 1
                                       ? "Create an empty scene in an existing folder."
                                       : "Save a copy and continue editing the new file.");
          ImGui::SetNextItemWidth(420);
          ImGui::InputText("Scene path", &m_scenePath);

          if (m_sceneDialogMode == 1) {
            ImGui::InputText("Scene name", &m_sceneName);
          }

          if (m_sceneDialogMode != 2 && m_document->stats()->dirty) {
            ImGui::TextWrapped("The current scene has unsaved changes.");
            ImGui::Checkbox("Discard unsaved scene changes", &m_discardScene);
          }

          const auto label = m_sceneDialogMode == 0   ? "Open"
                             : m_sceneDialogMode == 1 ? "Create"
                                                      : "Save copy";

          if (ImGui::Button(label)) {
            if (action({{"command", "confirm-scene-dialog"}})) {
              ImGui::CloseCurrentPopup();
              m_sceneDialogClose = false;
            }
          }

          ImGui::SameLine();

          if (ImGui::Button("Cancel")) {
            sessionAction({{"command", "scene-dialog"}, {"mode", "close"}});
            ImGui::CloseCurrentPopup();
            m_sceneDialogClose = false;
          }

          if (!m_message.empty()) {
            ImGui::TextWrapped("%s", m_message.c_str());
          }
        }

        ImGui::EndPopup();
      }

      if (m_pendingClose) {
        ImGui::OpenPopup("Unsaved changes");
      }

      if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes before closing?");

        if (m_clipDocument) {
          ImGui::TextWrapped("Animation: %s", m_clipDocument->snapshot()->path.c_str());

          if (m_clipMove) {
            ImGui::TextWrapped("Cancel to finish or cancel the key move before saving.");
          } else if (m_clipForm != m_clipOriginal) {
            ImGui::TextWrapped("Cancel to apply or reset the key form before saving.");
          }
        }

        if (!m_message.empty()) {
          ImGui::TextWrapped("%s", m_message.c_str());
        }

        if (!m_pendingClose) {
          ImGui::CloseCurrentPopup();
        }

        ImGui::BeginDisabled(m_clipDocument && (m_clipForm != m_clipOriginal || m_clipMove));

        if (ImGui::Button("Save and close")) {
          action({{"command", "save-close"}});
        }

        ImGui::EndDisabled();

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
        if (m_captureDialog) {
          m_message = "Complete or cancel the native save dialog before closing";
          return false;
        }

        const bool wasPending = m_pendingClose;
        m_pendingClose = m_document->stats()->dirty || pendingInspector() || pendingEnvironment()
                         || pendingOcclusion() || pendingText() || m_transform.has_value();

        if (m_pendingClose && !wasPending) {
          m_message.clear();
        }

        if (!m_pendingClose) {
          auto stopped = stopPlay();
          auto previewsStopped = stopPreviews();

          if (!previewsStopped) {
            return std::unexpected(previewsStopped.error());
          }

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

        if (!m_application->running()) {
          return {};
        }

        auto materialPreview = synchronizeMaterialPreview();
        auto scene = displayedScene();
        auto prepared
            = !materialPreview ? Core::Result<Graphics::ScenePreparationStage>{std::unexpected(
                                     materialPreview.error())}
              : scene
                  ? m_drawing->prepare(**scene)
                  : Core::Result<Graphics::ScenePreparationStage>{std::unexpected(scene.error())};

        m_preparationStage = prepared ? *prepared : Graphics::ScenePreparationStage::Ready;

        if (!prepared || m_preparationStage != Graphics::ScenePreparationStage::Ready) {
          m_previewReady = false;
          m_previewError = prepared ? "" : prepared.error().message;
        }

        if (input.key(Platform::Key::Escape).pressed || m_pendingClose) {
          captureInput(false);
          m_agentInput.cancel();

          if (m_curveDrag) {
            cancelKeyMove();
          }
        }

        auto pointerChanged = synchronizePointerCapture();

        if (!pointerChanged) {
          return std::unexpected(pointerChanged.error());
        }

        if (m_resetGuiInput) {
          auto actualFocus = m_application->window().focused();

          if (!actualFocus) {
            return std::unexpected(actualFocus.error());
          }

          for (bool focused : {false, *actualFocus}) {
            auto forwarded
                = m_renderer->guiEvent({m_application->window().id(), 0, Platform::Focus{focused}});

            if (!forwarded) {
              return std::unexpected(forwarded.error());
            }
          }

          m_resetGuiInput = false;
        }

        auto agent = m_agentInput.next();

        if (!agent) {
          return std::unexpected(agent.error());
        }

        if (*agent) {
          m_gameInput = std::move(**agent);
          m_inputSource = "agent";
        } else if (m_captureInput) {
          m_gameInput = input;
          m_inputSource = "native";

          if (*pointerChanged) {
            m_gameInput.clearPointerDelta();
          }

          auto pointer = input.pointer();
          static_cast<void>(
              m_gameInput.consume({m_application->window().id(), 0,
                                   Platform::PointerMoved{pointer.x - m_viewOrigin.x,
                                                          pointer.y - m_viewOrigin.y, 0, 0}}));
        } else {
          m_inputSource = "none";
          m_gameInput.beginFrame();
          static_cast<void>(
              m_gameInput.consume({m_application->window().id(), 0, Platform::Focus{false}}));
        }

        return {};
      };
      callbacks.updateFrame = [&](const auto& frame) -> Core::Result<void> {
        if (playing() && m_play->stats()->state == PlayState::Running) {
          auto advanced = m_play->advance(m_gameInput, frame.elapsed, m_pointerCaptured);
          auto remembered = rememberPlayFrame();

          if (!remembered) {
            return remembered;
          }

          if (!advanced) {
            m_playError = advanced.error();
            diagnostic(errorJson(advanced.error()));
            m_agentInput.reset();
            m_inputSource = "none";
            captureInput(false);
          }
        }

        if (m_animationPreview) {
          auto state = m_animationPreview->stats();

          if (!state) {
            return std::unexpected(state.error());
          }

          if (!state->error) {
            auto advanced = m_animationPreview->advance(
                std::min(std::chrono::duration<double>(frame.elapsed).count(), 1.0));

            if (!advanced) {
              m_message = advanced.error().message;
            }
          }
        }

        return {};
      };
      callbacks.render = [&](const auto& frame) -> Core::Result<void> {
        if (m_assetRefresh) {
          auto cleared = m_drawing->clearCache();
          auto invalidated = m_skeleton.invalidateAssets();

          if (!cleared || !invalidated) {
            return !cleared ? cleared : invalidated;
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

        if (!shown) {
          return std::unexpected(shown.error());
        }

        resolveCapture();
        return {};
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

      impl->m_events
          = impl->m_application->events().subscribe([state = impl.get()](const auto& event) {
              if (event.windowId == state->m_application->window().id()) {
                if (const auto* button = std::get_if<Platform::ButtonChanged>(&event.data)) {
                  (button->down ? state->m_lastPointerPress : state->m_lastPointerRelease) = event;
                } else if (std::holds_alternative<Platform::PointerMoved>(event.data)) {
                  state->m_lastPointerMotion = event;
                }
              }

              if (std::holds_alternative<Platform::Focus>(event.data)
                  && !std::get<Platform::Focus>(event.data).focused) {
                state->captureInput(false);

                if (state->m_curveDrag) {
                  state->cancelKeyMove();
                }
              }

              if (state->m_transform && std::holds_alternative<Platform::Focus>(event.data)
                  && !std::get<Platform::Focus>(event.data).focused) {
                auto cancelled = state->finishTransform(false);

                if (!cancelled) {
                  state->m_transformError = cancelled.error().message;
                }
              }

              if (state->m_captureInput && !state->m_pointerCaptured
                  && std::holds_alternative<Platform::ButtonChanged>(event.data)) {
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

              if (state->m_pointerCaptured
                  && (std::holds_alternative<Platform::PointerMoved>(event.data)
                      || std::holds_alternative<Platform::ButtonChanged>(event.data)
                      || std::holds_alternative<Platform::Wheel>(event.data))) {
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

    return m_impl->readViewport();
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
