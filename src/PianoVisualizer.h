#pragma once

#define _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
#define _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS

#include <Windows.h>
#include <Shellapi.h>
#include <d3d11.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <functional>
#include <algorithm>
#include <vector>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_internal.h>

#include "../util/Logger.h"
#include "../util/camera/Camera.h"
#include "../util/camera/CameraVideoPlayer.h"
#include "../util/capture/WindowCapture.h"
#include "../util/renderer/VirtualWindowRenderer.h"
#include "../util/DragDrop/FileDropTarget.h"

#include "../util/helpers.h"
#include "../util/gui.h"
#include "../util/config.h"

// MidiVisualizer
#include "MidiVisualizer/helpers/ProgramUtilities.h"
#include "MidiVisualizer/helpers/Configuration.h"
#include "MidiVisualizer/helpers/ResourcesManager.h"
#include "MidiVisualizer/helpers/ImGuiStyle.h"
#include "MidiVisualizer/helpers/System.h"
#include "MidiVisualizer/rendering/scene/MIDISceneLive.h"
#include "MidiVisualizer/rendering/Viewer.h"
#include "MidiVisualizer/rendering/ScreenQuad.h"

// Audio
#include "Audio/AudioEngine.h"
#include "Audio/AudioOutput.h"
#include "Audio/vst/VSTPlugin.h"

// Extensions
#include "VirtualCameraExtension/VirtualCameraExtension.h"


class PianoVisualizer
{
public:

    enum class PianoScene
    {
        Perspective,
        PianoRoll
    };

    PianoVisualizer();
    ~PianoVisualizer();

    // Non-copyable
    PianoVisualizer(const PianoVisualizer&) = delete;
    PianoVisualizer& operator=(const PianoVisualizer&) = delete;

    // ---------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------

    bool Initialize(
        HINSTANCE instance,
        HWND window,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        IDXGISwapChain* swapChain,
        ID3D11RenderTargetView* renderTargetView
    );

    void Shutdown();

    void Update();

    void Render();

    // ---------------------------------------------------------
    // Window handling
    // ---------------------------------------------------------

    void Resize(
        UINT width,
        UINT height
    );

    void KeyPressed(
        int key,
        int state
    );

    // ---------------------------------------------------------
    // Accessors
    // ---------------------------------------------------------

    ID3D11Device* GetDevice() const
    {
        return m_device;
    }

    ID3D11DeviceContext* GetContext() const
    {
        return m_context;
    }

    IDXGISwapChain* GetSwapChain() const
    {
        return m_swapChain;
    }

    ID3D11RenderTargetView* GetRenderTargetView() const
    {
        return m_renderTargetView;
    }

    HWND GetWindow() const
    {
        return m_window;
    }

    Viewer* GetViewer() const
    {
        return m_viewer;
    }

    audio::AudioEngine* GetAudioEngine() const
    {
        return m_audioEngine;
    }

    Camera& GetCamera()
    {
        return m_camera;
    }

    WindowCapture& GetWindowCapture()
    {
        return m_windowCapture;
    }

    VirtualWindowRenderer& GetVirtualWindowRenderer()
    {
        return m_virtualWindowRenderer;
    }

    bool test = false;

private:

    // =========================================================
    // Initialization
    // =========================================================

    bool InitializeMidiVisualizer();

    bool InitializeAudio();

    bool InitializeCamera();

    bool InitializeWindowCapture();

    bool InitializeRenderTarget();
    bool InitializeCameraOutput();

    // =========================================================
    // Shutdown
    // =========================================================

    void ShutdownAudio();

    void ShutdownCamera();

    void ShutdownMidiVisualizer();

    void ShutdownRenderTarget();

    // =========================================================
    // Rendering
    // =========================================================

    void RenderCamera();

    void RenderCameraOutput();

    void RenderVisualizer();

    void RenderPianoRoll();

    void RenderPianoRollVisualizer();

    void RenderSettings();

    void RenderVSTDropTarget();

    void RenderStatistics();

    void RenderCameraSettingsPanel();

    void RenderAudioPanel();

    // =========================================================
    // Camera / Piano
    // =========================================================

    void RenderCameraFeed();

    void RenderVirtualCameraVisualizer();

    void RenderVirtualCameraFrame();

    void RenderCameraErrorDialog();

    void RenderPianoOverlay();

    void RenderPolygonSelectionTooltip();

    bool CalculatePianoRollGeometry(
        float outputWidth,
        float outputHeight,
        float cameraWidth,
        float cameraHeight,
        ImVec2& visualizerTopLeft,
        ImVec2& visualizerTopRight,
        ImVec2& boundaryLeft,
        ImVec2& boundaryRight,
        ImVec2& cameraBottomLeft,
        ImVec2& cameraBottomRight
    );

    void HandlePolygonPointSelection(
        const ImVec2& imagePos,
        float width,
        float height,
        float cameraWidth,
        float cameraHeight
    );

    void DrawSelectedPolygon(
        ImDrawList* drawList,
        const std::function<ImVec2(float, float)>& cameraToScreen,
        const std::function<ImVec2(float, float)>& screenToCamera
    );

    // =========================================================
    // VST
    // =========================================================

    void HandleVSTDrop();

    void LoadVST(
        const std::filesystem::path& path
    );

    void UnloadVST();

    void LoadRecentVST(
        const std::filesystem::path& path
    );

    void AddRecentVST(
        const std::filesystem::path& path
    );

    void RemoveRecentVST(
        const std::filesystem::path& path
    );

    // =========================================================
    // Capture
    // =========================================================

    void UpdateWindowCapture();

    void StopWindowCapture();

    void StartWindowCapture(
        HWND window
    );

    void onStartRecording();

    void onStopRecording();

    // =========================================================
    // Configuration
    // =========================================================

    void LoadPianoConfiguration();

    void SavePianoConfiguration();

    // =========================================================
    // Rendering target
    // =========================================================

    void CleanupRenderTarget();

    void CreateRenderTarget();

    // =========================================================
    // Statistics
    // =========================================================

    void UpdateStatistics();


private:

    // =========================================================
    // Windows / DirectX
    // =========================================================

    HINSTANCE m_instance = nullptr;

    HWND m_window = nullptr;

    ID3D11Device* m_device = nullptr;

    ID3D11DeviceContext* m_context = nullptr;

    IDXGISwapChain* m_swapChain = nullptr;

    ID3D11RenderTargetView* m_renderTargetView = nullptr;

    ComPtr<ID3D11Texture2D> m_cameraOutputTexture;

    ComPtr<ID3D11RenderTargetView> m_cameraOutputRTV;

    ComPtr<ID3D11ShaderResourceView> m_cameraOutputSRV;

    // =========================================================
    // Camera
    // =========================================================

    Camera m_camera;

    CameraVideoPlayer m_cameraVideoPlayer;

    bool m_showCameraError = false;

    std::string m_cameraErrorMessage;

    bool m_showCameraSettings = false;

    bool m_showAudioSettings = false;

    int m_cameraSelectedIndex = -1;

    int m_cameraSettingsWidth = 0;

    int m_cameraSettingsHeight = 0;

    UINT32 m_cameraSettingsFPSNumerator = 0;

    UINT32 m_cameraSettingsFPSDenominator = 1;

    WindowCapture m_windowCapture;

    VirtualWindowRenderer m_virtualWindowRenderer;

    // =========================================================
    // MidiVisualizer
    // =========================================================

    Viewer* m_viewer = nullptr;

    // =========================================================
    // Extensions
    // =========================================================

    VirtualCameraExtension m_virtualCamera;

    // =========================================================
    // Audio / VST
    // =========================================================

    audio::AudioEngine* m_audioEngine = nullptr;

    HWND m_vstEditorWindow = nullptr;

    std::filesystem::path m_vst3FolderPath =
        "C:\\Program Files\\Common Files\\VST3\\";

    FileDropTarget m_vstDropTarget{
        { L".vst3" }
    };

    std::filesystem::path m_currentPluginPath;

    std::vector<std::filesystem::path> m_recentVSTPlugins;

    static constexpr size_t MAX_RECENT_VST_PLUGINS = 8;

    // =========================================================
    // Piano configuration
    // =========================================================

    PianoScene m_pianoScene =
        PianoScene::Perspective;

    float m_pianoRollVisualizerHeight = 0.5f;
    float m_pianoRollCameraSourceScale = 1.0f;

    bool m_choosingPolygonPoints = false;

    int m_polygonClickCount = 0;

    // Previously selected points, used when ESC cancels selection.
    std::vector<ImVec2> m_savedPolygonPoints;

    // 0 = Top-left
    // 1 = Bottom-left
    // 2 = Bottom-right
    // 3 = Top-right
    std::vector<ImVec2> m_polygonPoints;

    // =========================================================
    // Perspective scene configuration
    // =========================================================

    float m_heightScale = 1.0f;

    float m_virtualWidth = 1920.0f;

    float m_virtualDepth = 1080.0f;

    float m_planeWidth = 16.0f / 9.0f;

    float m_planeDepth = 1.0f;

    float m_planePivot = 90.0f;

    float m_horizontalFovDegrees = 90.0f;

    float m_surfaceXOffset = 0.0f;

    float m_surfaceYOffset = 0.0f;

    float m_surfaceZOffset = 0.0f;

    // =========================================================
    // State
    // =========================================================

    bool m_drawVisualizer = false;

    bool m_initialized = false;

    std::string m_configSaveMessage;

    // =========================================================
    // Timing
    // =========================================================

    std::chrono::steady_clock::time_point m_startTime;

    std::chrono::steady_clock::time_point m_prevTime;

    float m_elapsedTime = 0.0f;

    // =========================================================
    // Statistics
    // =========================================================

    struct Statistics
    {
        float fps = 0.0f;

        float frameTimeMs = 0.0f;

        float minFrameTimeMs = 0.0f;

        float maxFrameTimeMs = 0.0f;

        float cameraUpdateFps = 0.0f;

        uint64_t frameCount = 0;

        double uptimeSeconds = 0.0;

        UINT renderWidth = 0;

        UINT renderHeight = 0;

        UINT cameraWidth = 0;

        UINT cameraHeight = 0;

        uint64_t processMemoryMB = 0;

        double processCpuPercent = 0.0;

        double lastUpdateTime = 0.0;

        double fpsAccumulator = 0.0;

        uint32_t fpsFrameCount = 0;

        double cameraAccumulator = 0.0;

        uint32_t cameraUpdateCount = 0;
    };

    Statistics m_statistics;

    bool m_showStatistics = false;

    // =========================================================
    // Application shutdown
    // =========================================================

    std::atomic<bool> m_shouldExit = false;
};