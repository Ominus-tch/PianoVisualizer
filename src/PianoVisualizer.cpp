#include "PianoVisualizer.h"

#include <algorithm>
#include <iostream>

#include "Audio/vst/VSTAudio.h"

#include <Psapi.h>

#pragma comment(lib, "Psapi.lib")

static BOOL CALLBACK EnumWindowsProc(
    HWND hwnd,
    LPARAM lParam
)
{
    if (!IsWindowVisible(hwnd))
        return TRUE;

    if (hwnd == GetShellWindow())
        return TRUE;

    int length = GetWindowTextLengthW(hwnd);

    if (length <= 0)
        return TRUE;

    std::wstring title(
        length,
        L'\0'
    );

    GetWindowTextW(
        hwnd,
        title.data(),
        length + 1
    );

    if (title.empty())
        return TRUE;

    auto* windows =
        reinterpret_cast<
        std::vector<gui::CaptureWindowEntry>*
        >(lParam);

    windows->push_back({
        hwnd,
        title
        });

    return TRUE;
}

static void RefreshCaptureWindows()
{
    gui::captureWindows.clear();

    EnumWindows(
        EnumWindowsProc,
        reinterpret_cast<LPARAM>(
            &gui::captureWindows
            )
    );
}

void SetFullscreen(HWND window, bool fullscreen)
{
    static WINDOWPLACEMENT previousPlacement{
        sizeof(WINDOWPLACEMENT)
    };

    if (fullscreen)
    {
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);

        if (!GetWindowPlacement(
            window,
            &previousPlacement
        ))
        {
            return;
        }

        HMONITOR monitor =
            MonitorFromWindow(
                window,
                MONITOR_DEFAULTTONEAREST
            );

        if (!GetMonitorInfo(
            monitor,
            &monitorInfo
        ))
        {
            return;
        }

        SetWindowLongPtr(
            window,
            GWL_STYLE,
            WS_POPUP | WS_VISIBLE
        );

        SetWindowPos(
            window,
            HWND_TOP,
            monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.top,
            monitorInfo.rcMonitor.right -
            monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.bottom -
            monitorInfo.rcMonitor.top,
            SWP_FRAMECHANGED |
            SWP_NOOWNERZORDER
        );
    }
    else
    {
        SetWindowLongPtr(
            window,
            GWL_STYLE,
            WS_OVERLAPPEDWINDOW | WS_VISIBLE
        );

        SetWindowPlacement(
            window,
            &previousPlacement
        );

        SetWindowPos(
            window,
            nullptr,
            0,
            0,
            0,
            0,
            SWP_NOMOVE |
            SWP_NOSIZE |
            SWP_NOZORDER |
            SWP_NOACTIVATE |
            SWP_FRAMECHANGED
        );
    }
}


PianoVisualizer::PianoVisualizer()
{}


PianoVisualizer::~PianoVisualizer()
{
    Shutdown();
}


// =========================================================
// Initialize
// =========================================================

bool PianoVisualizer::Initialize(
    HINSTANCE instance,
    HWND window,
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    IDXGISwapChain* swapChain,
    ID3D11RenderTargetView* renderTargetView
)
{
    m_instance = instance;
    m_window = window;

    m_device = device;
    m_context = context;
    m_swapChain = swapChain;
    m_renderTargetView = renderTargetView;

    Logger::Log(
        "Initializing Piano Visualizer...\n"
    );

    if (!InitializeRenderTarget())
    {
        Logger::Log(
            "Failed to initialize render target!\n"
        );

        return false;
    }

    // ---------------------------------------------------------
    // Audio
    // ---------------------------------------------------------

    if (!InitializeAudio())
    {
        Logger::Log(
            "Failed to initialize Audio Engine!\n"
        );

        return false;
    }

    // ---------------------------------------------------------
    // Camera
    // ---------------------------------------------------------

    if (!InitializeCamera())
    {
        Logger::Log(
            "Failed to initialize camera!\n"
        );

        return false;
    }

    // ---------------------------------------------------------
    // Window capture
    // ---------------------------------------------------------

    if (!InitializeWindowCapture())
    {
        Logger::Log(
            "Failed to initialize window capture!\n"
        );

        return false;
    }

    // ---------------------------------------------------------
    // MIDI Visualizer
    // ---------------------------------------------------------

    if (!InitializeMidiVisualizer())
    {
        Logger::Log(
            "Failed to create MIDI visualizer!\n"
        );

        return false;
    }

    // ---------------------------------------------------------
    // VST drag & drop
    // ---------------------------------------------------------

    if (!m_vstDropTarget.Register(m_window))
    {
        Logger::Log(
            "Failed to register VST drag-drop target!\n\n"
        );
    }

    // ---------------------------------------------------------
    // Configuration
    // ---------------------------------------------------------

    LoadPianoConfiguration();

    // ---------------------------------------------------------
    // Extensions
    // ---------------------------------------------------------

    if (!m_virtualCamera.Initialize(m_device))
    {
        Logger::Log(
            "Failed to initialize virtual camera extension!\n"
        );
    }

    // ---------------------------------------------------------
    // Timing
    // ---------------------------------------------------------

    m_startTime =
        std::chrono::steady_clock::now();

    m_prevTime =
        m_startTime;

    m_elapsedTime = 0.0f;

    m_initialized = true;

    // ---------------------------------------------------------
    // Start virtual camera
    // ---------------------------------------------------------

    Logger::Log(
        "[Piano Visualizer] Initialized!\n"
    );

    return true;
}


// =========================================================
// Initialize Render Target
// =========================================================

bool PianoVisualizer::InitializeRenderTarget()
{
    if (!m_device)
    {
        Logger::Log(
            "[Error] Cannot initialize render target: device is null!\n"
        );

        return false;
    }

    if (!m_context)
    {
        Logger::Log(
            "[Error] Cannot initialize render target: context is null!\n"
        );

        return false;
    }

    if (!m_swapChain)
    {
        Logger::Log(
            "[Error] Cannot initialize render target: swap chain is null!\n"
        );

        return false;
    }

    if (!m_renderTargetView)
    {
        Logger::Log(
            "[Error] Cannot initialize render target: RTV is null!\n"
        );

        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};

    if (SUCCEEDED(
        m_swapChain->GetDesc(&desc)
    ))
    {
        m_statistics.renderWidth =
            desc.BufferDesc.Width;

        m_statistics.renderHeight =
            desc.BufferDesc.Height;
    }

    return InitializeCameraOutput();
}


// =========================================================
// Initialize Camera Output
// =========================================================

bool PianoVisualizer::InitializeCameraOutput()
{
    if (!m_device)
    {
        Logger::Log(
            "[Error] Cannot initialize camera output: device is null!\n"
        );

        return false;
    }

    if (m_statistics.renderWidth == 0 ||
        m_statistics.renderHeight == 0)
    {
        Logger::Log(
            "[Error] Cannot initialize camera output: render size is invalid!\n"
        );

        return false;
    }

    D3D11_TEXTURE2D_DESC description{};

    description.Width =
        m_statistics.renderWidth;

    description.Height =
        m_statistics.renderHeight;

    description.MipLevels = 1;
    description.ArraySize = 1;

    description.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;

    description.SampleDesc.Count = 1;

    description.Usage =
        D3D11_USAGE_DEFAULT;

    description.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr =
        m_device->CreateTexture2D(
            &description,
            nullptr,
            &m_cameraOutputTexture
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Error] Failed to create camera output texture! HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    hr =
        m_device->CreateRenderTargetView(
            m_cameraOutputTexture.Get(),
            nullptr,
            &m_cameraOutputRTV
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Error] Failed to create camera output render target view! HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        m_cameraOutputTexture.Reset();

        return false;
    }

    hr =
        m_device->CreateShaderResourceView(
            m_cameraOutputTexture.Get(),
            nullptr,
            &m_cameraOutputSRV
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Error] Failed to create camera output shader resource view! HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        m_cameraOutputRTV.Reset();
        m_cameraOutputTexture.Reset();

        return false;
    }

    Logger::Log(
        "[Camera] Created %ux%u camera output texture.\n",
        m_statistics.renderWidth,
        m_statistics.renderHeight
    );

    return true;
}


// =========================================================
// Audio
// =========================================================

bool PianoVisualizer::InitializeAudio()
{
    Logger::Log(
        "[Piano Visualizer] Initializing Audio Engine...\n\n"
    );

    m_audioEngine =
        new audio::AudioEngine();

    if (!m_audioEngine->initialize())
    {
        Logger::Log(
            "[Error] Failed to initialize Audio Engine!\n"
        );

        delete m_audioEngine;

        m_audioEngine = nullptr;

        return false;
    }

    // ---------------------------------------------------------
    // Load previous VST configuration
    // ---------------------------------------------------------

    std::filesystem::path previousVstPath;
    std::filesystem::path folderPath;

    if (Config::LoadVSTConfig(
        previousVstPath,
        folderPath,
        m_recentVSTPlugins
    ))
    {
        if (!folderPath.empty())
        {
            m_vst3FolderPath = folderPath;
        }

        if (!previousVstPath.empty())
        {
            if (m_audioEngine->loadPlugin(
                previousVstPath.string()
            ))
            {
                if (!m_audioEngine->start())
                {
                    Logger::Log(
                        "[Error] Failed to start Audio Engine!\n"
                    );
                }

                m_currentPluginPath = previousVstPath;
            }
            else
            {
                Logger::Log(
                    "[Error] Failed to load plugin %s\n",
                    previousVstPath.string().c_str()
                );
            }
        }
    }

    return true;
}


// =========================================================
// Camera
// =========================================================

bool PianoVisualizer::InitializeCamera()
{
    if (!m_camera.Initialize(m_device, m_context))
        return false;

    const std::string cameraError =
        m_camera.GetLastError();

    if (!cameraError.empty())
    {
        m_showCameraError = true;
        m_cameraErrorMessage = cameraError;

        m_camera.ClearLastError();
    }

    if (!m_cameraVideoPlayer.Initialize(
        m_device,
        m_context
    ))
    {
        Logger::Log(
            "[Playback] Failed to initialize video player.\n"
        );

        m_camera.Shutdown();

        return false;
    }
    else
    {
        Logger::Log("[CameraVideoPlayer] Initialized.\n");
    }

    return true;
}


// =========================================================
// Window Capture
// =========================================================

bool PianoVisualizer::InitializeWindowCapture()
{
    return m_windowCapture.Initialize(
        m_device,
        m_context
    );
}


// =========================================================
// MIDI Visualizer
// =========================================================

bool PianoVisualizer::InitializeMidiVisualizer()
{
    // =========================================================
    // MidiVisualizer setup
    // =========================================================

    ResourcesManager::loadResources(
        m_device
    );

    D3D11Interface d3dInterface = {
        m_device,
        m_context
    };

    m_viewer =
        new Viewer(
            d3dInterface,
            1920,
            1080
        );

    m_viewer->setOnStartRecording(
        [this]()
        {
            onStartRecording();
        }
    );

    m_viewer->setOnStopRecording(
        [this]()
        {
            onStopRecording();
        }
    );

    m_viewer->setOnStartPlayback(
        [this](const std::string& path)
        {


            if (m_camera.IsOpen())
            {
                m_cameraSelectedIndex = m_camera.GetCameraIndex();
                m_camera.CloseCamera();
            }

            bool success = m_cameraVideoPlayer.Open(path);

            Config::LoadPianoConfigFromPath(
                m_viewer->selectedRecordingDirectory() / "recordingPreset.json",
                m_polygonPoints,
                m_horizontalFovDegrees,
                m_planeWidth,
                m_planeDepth,
                m_planePivot,
                m_surfaceXOffset,
                m_surfaceYOffset,
                m_surfaceZOffset
            );

            return success;
        }
    );


    m_viewer->setOnStopPlayback(
        [this]()
        {

            m_cameraVideoPlayer.Close();
            if (!m_camera.IsOpen() && m_cameraSelectedIndex != -1)
            {
                m_camera.OpenCamera(m_cameraSelectedIndex);
            }

            Config::LoadPianoConfig(
                m_polygonPoints,
                m_horizontalFovDegrees,
                m_planeWidth,
                m_planeDepth,
                m_planePivot,
                m_surfaceXOffset,
                m_surfaceYOffset,
                m_surfaceZOffset
            );
        }
    );

    m_viewer->setOnSeekChanged(
        [this](double time)
        {
            if (!m_cameraVideoPlayer.Seek(time))
            {
                Logger::Log("[CameraVideoPlayer] Seek to %.2f failed!\n", time);
            }
        }
    );

    return true;
}


// =========================================================
// Configuration
// =========================================================

void PianoVisualizer::LoadPianoConfiguration()
{
    std::call_once(
        gui::onceFlag,
        [&]()
        {
            if (!Config::LoadPianoConfig(
                m_polygonPoints,
                m_horizontalFovDegrees,
                m_planeWidth,
                m_planeDepth,
                m_planePivot,
                m_surfaceXOffset,
                m_surfaceYOffset,
                m_surfaceZOffset
            ))
            {
                Logger::Log(
                    "No piano config found. Using defaults.\n"
                );
            }
        }
    );
}


void PianoVisualizer::SavePianoConfiguration()
{
    if (Config::SavePianoConfig(
        m_polygonPoints,
        m_horizontalFovDegrees,
        m_planeWidth,
        m_planeDepth,
        m_planePivot,
        m_surfaceXOffset,
        m_surfaceYOffset,
        m_surfaceZOffset
    ))
    {
        m_configSaveMessage =
            "Configuration saved!";
    }
    else
    {
        m_configSaveMessage =
            "Failed to save configuration!";
    }
}


// =========================================================
// Update
// =========================================================

void PianoVisualizer::Update()
{
    if (!m_initialized)
        return;

    auto currentTime =
        std::chrono::steady_clock::now();

    // ---------------------------------------------------------
    // Timing
    // ---------------------------------------------------------

    const double deltaTime =
        std::chrono::duration<double>(
            currentTime - m_prevTime
        ).count();

    m_elapsedTime =
        static_cast<float>(
            std::chrono::duration<double>(
                currentTime - m_startTime
            ).count()
            );

    UpdateStatistics();

    m_prevTime =
        currentTime;

    // ---------------------------------------------------------
    // Keyboard shortcuts
    // ---------------------------------------------------------

    if (!m_viewer->isInputting()) {

        if (ImGui::IsKeyPressed(ImGuiKey_I))
        {
            gui::showSettings =
                !gui::showSettings;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_S))
        {
            m_showStatistics =
                !m_showStatistics;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_F))
        {
            gui::fullscreen =
                !gui::fullscreen;

            SetFullscreen(
                m_window,
                gui::fullscreen
            );
        }
    }

    // ---------------------------------------------------------
    // Camera
    // ---------------------------------------------------------

    double time = m_viewer->getTime();

    if (
        m_viewer &&
        m_viewer->isPlaybackLoaded()
        )
    {
        m_cameraVideoPlayer.Update(
            time
        );
    }
    else
    {
        m_camera.Update(time);
    }

    // ---------------------------------------------------------
    // MIDI Visualizer
    // ---------------------------------------------------------

    if (m_viewer)
    {
        m_viewer->setShowGUI(
            gui::showSettings
        );

        auto scene = m_viewer->scene();

        if (scene)
        {
            scene->setAudioEngine(
                m_audioEngine
            );
        }
    }

    // ---------------------------------------------------------
    // Window capture
    // ---------------------------------------------------------

    UpdateWindowCapture();

    // ---------------------------------------------------------
    // VST
    // ---------------------------------------------------------

    HandleVSTDrop();
}


// =========================================================
// Window Capture
// =========================================================

void PianoVisualizer::UpdateWindowCapture()
{
    if (
        gui::renderer ==
        gui::VisualizerRenderer::OtherVisualizer
        )
    {
        HWND desiredWindow =
            gui::selectedCaptureWindow;

        if (
            desiredWindow !=
            gui::activeCaptureWindow
            )
        {
            StopWindowCapture();

            if (
                desiredWindow &&
                IsWindow(desiredWindow)
                )
            {
                StartWindowCapture(
                    desiredWindow
                );
            }
        }
    }
    else
    {
        StopWindowCapture();
    }
}


void PianoVisualizer::StopWindowCapture()
{
    if (!gui::windowCaptureRunning)
        return;

    m_windowCapture.Stop();

    gui::windowCaptureRunning =
        false;

    gui::activeCaptureWindow =
        nullptr;

    Logger::Log(
        "Window capture stopped.\n"
    );
}


void PianoVisualizer::StartWindowCapture(
    HWND window
)
{
    if (!window)
        return;

    if (gui::windowCaptureRunning)
        return;

    if (!m_windowCapture.Start(window))
    {
        Logger::Log(
            "Failed to start window capture!\n"
        );

        m_windowCapture.Stop();

        return;
    }

    gui::windowCaptureRunning =
        true;

    gui::activeCaptureWindow =
        window;

    Logger::Log(
        "Window capture started.\n"
    );
}

void PianoVisualizer::onStartRecording()
{
    if (!m_camera.StartRecording((m_viewer->recordingDirectory() / "cameraRecording.mp4").string()))
    {
        Logger::Log("Failed to start recording!\n");
    }
}

void PianoVisualizer::onStopRecording()
{
    if (!m_camera.StopRecording())
    {
        Logger::Log("Failed to stop recording!\n");
    }

    Config::SavePianoConfigToPath(
        m_viewer->recordingDirectory() / "recordingPreset.json",
        m_polygonPoints,
        m_horizontalFovDegrees,
        m_planeWidth,
        m_planeDepth,
        m_planePivot,
        m_surfaceXOffset,
        m_surfaceYOffset,
        m_surfaceZOffset
    );
}

// =========================================================
// VST Drop
// =========================================================

void PianoVisualizer::HandleVSTDrop()
{
    if (!m_audioEngine)
        return;

    RenderVSTDropTarget();

    auto droppedFiles =
        m_vstDropTarget.consumeDroppedFiles();

    if (droppedFiles.empty())
        return;

    const std::filesystem::path& vstPath =
        droppedFiles[0];

    if (vstPath == m_audioEngine->vstPath())
        return;

    LoadVST(vstPath);
}


void PianoVisualizer::RenderVSTDropTarget()
{
    if (!m_vstDropTarget.isDragging())
        return;

    ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    ImDrawList* drawList =
        ImGui::GetForegroundDrawList();

    drawList->AddRectFilled(
        viewport->WorkPos,
        ImVec2(
            viewport->WorkPos.x +
            viewport->WorkSize.x,
            viewport->WorkPos.y +
            viewport->WorkSize.y
        ),
        IM_COL32(
            255,
            255,
            255,
            20
        )
    );
}


void PianoVisualizer::LoadVST(
    const std::filesystem::path& path
)
{
    // IMPORTANT:
    // path may reference an element inside m_recentVSTPlugins.
    // We modify that vector below, so make our own copy first.
    const std::filesystem::path pluginPath = path;

    if (!m_audioEngine || pluginPath.empty())
    {
        Logger::Log(
            "[VST] LoadVST ABORT: audio engine or path invalid\n"
        );
        return;
    }

    // Remember the currently loaded plugin.
    const std::filesystem::path oldPluginPath =
        m_currentPluginPath;

    // Move the old current plugin into recents.
    if (!oldPluginPath.empty())
    {
        AddRecentVST(oldPluginPath);
    }

    // The old plugin is no longer the current plugin.
    m_currentPluginPath.clear();

    m_audioEngine->unLoadPlugin();

    if (!m_audioEngine->loadPlugin(pluginPath.string()))
    {

        Config::SaveVSTConfig(
            m_currentPluginPath,
            m_vst3FolderPath,
            m_recentVSTPlugins
        );

        return;
    }

    RemoveRecentVST(pluginPath);

    m_currentPluginPath = pluginPath;


    Config::SaveVSTConfig(
        m_currentPluginPath,
        m_vst3FolderPath,
        m_recentVSTPlugins
    );

    m_audioEngine->start();
}


void PianoVisualizer::UnloadVST()
{

    if (!m_audioEngine)
    {
        Logger::Log(
            "[VST] UnloadVST ABORT: audio engine is null\n"
        );
        return;
    }

    // Copy it first. This makes the operation independent
    // of any changes to the recent list.
    const std::filesystem::path pluginPath =
        m_currentPluginPath;

    if (!pluginPath.empty())
    {
        AddRecentVST(pluginPath);
    }

    m_audioEngine->unLoadPlugin();

    m_currentPluginPath.clear();

    Config::SaveVSTConfig(
        m_currentPluginPath,
        m_vst3FolderPath,
        m_recentVSTPlugins
    );
}


void PianoVisualizer::LoadRecentVST(
    const std::filesystem::path& path
)
{
    // Copy because path may be a reference to an element
    // inside m_recentVSTPlugins.
    const std::filesystem::path pluginPath = path;

    if (!std::filesystem::exists(pluginPath))
    {
        Logger::Log(
            "[VST] Recent VST no longer exists: %s\n",
            pluginPath.string().c_str()
        );

        m_recentVSTPlugins.erase(
            std::remove(
                m_recentVSTPlugins.begin(),
                m_recentVSTPlugins.end(),
                pluginPath
            ),
            m_recentVSTPlugins.end()
        );

        return;
    }

    LoadVST(pluginPath);
}


void PianoVisualizer::AddRecentVST(
    const std::filesystem::path& path
)
{
    if (path.empty())
        return;

    // Remove any existing occurrence first.
    m_recentVSTPlugins.erase(
        std::remove(
            m_recentVSTPlugins.begin(),
            m_recentVSTPlugins.end(),
            path
        ),
        m_recentVSTPlugins.end()
    );

    // Put it at the front.
    m_recentVSTPlugins.insert(
        m_recentVSTPlugins.begin(),
        path
    );

    // Keep only the most recent plugins.
    if (m_recentVSTPlugins.size() > MAX_RECENT_VST_PLUGINS)
        m_recentVSTPlugins.resize(MAX_RECENT_VST_PLUGINS);
}


void PianoVisualizer::RemoveRecentVST(
    const std::filesystem::path& path
)
{
    if (path.empty())
        return;

    m_recentVSTPlugins.erase(
        std::remove(
            m_recentVSTPlugins.begin(),
            m_recentVSTPlugins.end(),
            path
        ),
        m_recentVSTPlugins.end()
    );
}

// =========================================================
// Render
// =========================================================

void PianoVisualizer::Render()
{
    if (!m_initialized)
        return;

    // ---------------------------------------------------------
    // Main render viewport
    // ---------------------------------------------------------

    D3D11_VIEWPORT viewport{};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    viewport.Width =
        static_cast<float>(
            m_statistics.renderWidth
            );

    viewport.Height =
        static_cast<float>(
            m_statistics.renderHeight
            );

    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(
        1,
        &viewport
    );

    // ---------------------------------------------------------
    // Camera output
    // ---------------------------------------------------------

    RenderCameraOutput();

    // ---------------------------------------------------------
    // Virtual camera output
    //
    // Send the completed camera output into the shared
    // 1920x1080 texture.
    // ---------------------------------------------------------

    RenderVirtualCameraFrame();

    // ---------------------------------------------------------
    // Clear main render target
    // ---------------------------------------------------------

    constexpr float color[4]{
        0.0f,
        0.0f,
        0.0f,
        1.0f
    };

    m_context->OMSetRenderTargets(
        1,
        &m_renderTargetView,
        nullptr
    );

    m_context->ClearRenderTargetView(
        m_renderTargetView,
        color
    );

    // ---------------------------------------------------------
    // MIDI Visualizer
    // ---------------------------------------------------------

    RenderVisualizer();

    // ---------------------------------------------------------
    // Camera
    // ---------------------------------------------------------

    RenderCamera();

    std::string cameraError =
        m_camera.GetLastError();

    if (!cameraError.empty())
    {
        m_showCameraError = true;
        m_cameraErrorMessage = cameraError;

        m_camera.ClearLastError();
    }

    RenderCameraSettingsPanel();
    RenderCameraErrorDialog();

    // ---------------------------------------------------------
    // Audio
    // ---------------------------------------------------------

    RenderAudioPanel();

    // ---------------------------------------------------------
    // Settings
    // ---------------------------------------------------------

    RenderSettings();

    // ---------------------------------------------------------
    // Statistics
    // ---------------------------------------------------------

    RenderStatistics();
}


// =========================================================
// Render Camera Output
// =========================================================

void PianoVisualizer::RenderCameraOutput()
{
    if (!m_context)
        return;

    if (!m_cameraOutputRTV)
        return;

    const bool playback =
        m_viewer &&
        m_viewer->isPlaybackLoaded();

    ID3D11ShaderResourceView* cameraTexture = nullptr;

    float cameraWidth = 0.0f;
    float cameraHeight = 0.0f;

    if (playback)
    {
        if (!m_cameraVideoPlayer.IsOpen())
            return;

        cameraTexture =
            m_cameraVideoPlayer.GetTexture();

        cameraWidth =
            static_cast<float>(
                m_cameraVideoPlayer.GetWidth()
                );

        cameraHeight =
            static_cast<float>(
                m_cameraVideoPlayer.GetHeight()
                );
    }
    else
    {
        if (!m_camera.IsOpen())
            return;

        cameraTexture =
            m_camera.GetTexture();

        cameraWidth =
            static_cast<float>(
                m_camera.GetWidth()
                );

        cameraHeight =
            static_cast<float>(
                m_camera.GetHeight()
                );
    }

    if (!cameraTexture)
        return;

    const float outputWidth =
        static_cast<float>(
            m_statistics.renderWidth
            );

    const float outputHeight =
        static_cast<float>(
            m_statistics.renderHeight
            );

    if (
        outputWidth <= 0.0f ||
        outputHeight <= 0.0f ||
        cameraWidth <= 0.0f ||
        cameraHeight <= 0.0f
        )
    {
        return;
    }

    constexpr float clearColor[4]{
        0.0f,
        0.0f,
        0.0f,
        1.0f
    };

    ID3D11RenderTargetView* renderTarget =
        m_cameraOutputRTV.Get();

    m_context->OMSetRenderTargets(
        1,
        &renderTarget,
        nullptr
    );

    m_context->ClearRenderTargetView(
        renderTarget,
        clearColor
    );

    const float cameraAspect =
        cameraWidth / cameraHeight;

    float drawWidth =
        outputWidth;

    float drawHeight =
        drawWidth / cameraAspect;

    if (drawHeight > outputHeight)
    {
        drawHeight =
            outputHeight;

        drawWidth =
            drawHeight * cameraAspect;
    }

    D3D11_VIEWPORT cameraViewport{};

    cameraViewport.TopLeftX =
        (outputWidth - drawWidth) * 0.5f;

    cameraViewport.TopLeftY =
        (outputHeight - drawHeight) * 0.5f;

    cameraViewport.Width =
        drawWidth;

    cameraViewport.Height =
        drawHeight;

    cameraViewport.MinDepth = 0.0f;
    cameraViewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(
        1,
        &cameraViewport
    );

    m_viewer->getFullscreenQuad().draw(
        m_context,
        cameraTexture,
        0.0f
    );

    ID3D11ShaderResourceView* nullSRV =
        nullptr;

    m_context->PSSetShaderResources(
        0,
        1,
        &nullSRV
    );

    D3D11_VIEWPORT viewport{};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = outputWidth;
    viewport.Height = outputHeight;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(
        1,
        &viewport
    );
}


// =========================================================
// Render Visualizer
// =========================================================

void PianoVisualizer::RenderVisualizer()
{
    if (!m_drawVisualizer)
        return;

    if (!m_viewer)
        return;

    SystemAction action =
        m_viewer->draw(
            DEBUG_SPEED *
            float(System::time())
        );

    m_context->OMSetRenderTargets(
        1,
        &m_renderTargetView,
        nullptr
    );

    D3D11_VIEWPORT viewport{};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    viewport.Width =
        static_cast<float>(
            m_statistics.renderWidth
            );

    viewport.Height =
        static_cast<float>(
            m_statistics.renderHeight
            );

    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(
        1,
        &viewport
    );

    m_drawVisualizer = false;
}


// =========================================================
// Render Camera
// =========================================================

void PianoVisualizer::RenderCamera()
{
    if (!m_camera.IsOpen() && !m_cameraVideoPlayer.IsOpen())
        return;

    ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        viewport->WorkPos,
        ImGuiCond_Always
    );

    ImGui::SetNextWindowSize(
        viewport->WorkSize,
        ImGuiCond_Always
    );

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoMove;

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(0, 0)
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowBorderSize,
        0.0f
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowRounding,
        0.0f
    );

    ImGui::Begin(
        "Camera Feed",
        nullptr,
        flags
    );

    ImGuiWindow* camWindow =
        ImGui::GetCurrentWindow();

    if (m_choosingPolygonPoints)
    {
        ImGui::BringWindowToDisplayFront(
            camWindow
        );
    }
    else
    {
        ImGui::BringWindowToDisplayBack(
            camWindow
        );
    }

    RenderPolygonSelectionTooltip();

    RenderCameraFeed();

    ImGui::End();

    ImGui::PopStyleVar(3);
}


// =========================================================
// Polygon Selection Tooltip
// =========================================================

void PianoVisualizer::RenderPolygonSelectionTooltip()
{
    if (!m_choosingPolygonPoints)
        return;

    ImGuiIO& io =
        ImGui::GetIO();

    ImGui::SetNextWindowPos(
        ImVec2(
            io.MousePos.x + 12.0f,
            io.MousePos.y + 12.0f
        ),
        ImGuiCond_Always
    );

    ImGui::SetNextWindowBgAlpha(
        0.9f
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowRounding,
        8.0f
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(8.0f, 5.0f)
    );

    ImGuiWindowFlags ttFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    ImGui::Begin(
        "MouseTooltip",
        nullptr,
        ttFlags
    );

    ImGuiWindow* ttWindow =
        ImGui::GetCurrentWindow();

    ImGui::BringWindowToDisplayFront(
        ttWindow
    );

    switch (m_polygonClickCount)
    {
    case 0:
        ImGui::Text(
            "P1. Click    TOP-LEFT    corner of the piano (ESC to cancel)"
        );
        break;

    case 1:
        ImGui::Text(
            "P2: Click   BOTTOM-LEFT  corner of the piano (ESC to cancel)"
        );
        break;

    case 2:
        ImGui::Text(
            "P3: Click  BOTTOM-RIGHT  corner of the piano (ESC to cancel)"
        );
        break;

    case 3:
        ImGui::Text(
            "P4: Click    TOP-RIGHT   corner of the piano (ESC to cancel)"
        );
        break;
    }

    ImGui::End();

    ImGui::PopStyleVar(2);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        m_polygonPoints =
            m_savedPolygonPoints;

        m_choosingPolygonPoints =
            false;

        m_polygonClickCount =
            static_cast<int>(
                m_polygonPoints.size()
                );
    }
}


// =========================================================
// Camera Feed
// =========================================================

void PianoVisualizer::RenderCameraFeed()
{
    ImVec2 imagePos =
        ImGui::GetCursorScreenPos();

    ImVec2 available =
        ImGui::GetContentRegionAvail();

    ID3D11ShaderResourceView* texture = nullptr;

    float cameraWidth = 0.0f;
    float cameraHeight = 0.0f;

    // ---------------------------------------------------------
    // Draw camera output
    // ---------------------------------------------------------

    if (
        m_viewer &&
        m_viewer->isPlaybackLoaded()
        )
    {
        texture =
            m_cameraVideoPlayer.GetTexture();

        cameraWidth =
            static_cast<float>(
                m_cameraVideoPlayer.GetWidth()
                );

        cameraHeight =
            static_cast<float>(
                m_cameraVideoPlayer.GetHeight()
                );

        //Logger::Log("%fx%f\n", cameraWidth, cameraHeight);
    }
    else
    {
        texture =
            m_cameraOutputSRV.Get();

        cameraWidth =
            static_cast<float>(
                m_camera.GetWidth()
                );

        cameraHeight =
            static_cast<float>(
                m_camera.GetHeight()
                );
    }

    if (
        cameraWidth <= 0.0f ||
        cameraHeight <= 0.0f
        )
    {
        return;
    }

    float aspect =
        cameraWidth /
        cameraHeight;

    float width =
        available.x;

    float height =
        width /
        aspect;

    // Keep aspect ratio
    if (height > available.y)
    {
        height =
            available.y;

        width =
            height *
            aspect;
    }
    //);

    ImGui::Image(
        (ImTextureID)texture,
        ImVec2(
            width,
            height
        )
    );



    // ---------------------------------------------------------
    // Camera -> screen conversion
    // ---------------------------------------------------------

    auto CameraToScreen =
        [&](float cameraX, float cameraY)
        {
            float screenX =
                imagePos.x +
                cameraX *
                (width / cameraWidth);

            float screenY =
                imagePos.y +
                cameraY *
                (height / cameraHeight);

            return ImVec2(
                screenX,
                screenY
            );
        };

    // ---------------------------------------------------------
    // Screen -> camera conversion
    // ---------------------------------------------------------

    auto ScreenToCamera =
        [&](float screenX, float screenY)
        {
            float cameraX =
                (screenX - imagePos.x) *
                (cameraWidth / width);

            float cameraY =
                (screenY - imagePos.y) *
                (cameraHeight / height);

            return ImVec2(
                cameraX,
                cameraY
            );
        };

    // ---------------------------------------------------------
    // Piano overlay
    // ---------------------------------------------------------

    RenderPianoOverlay();

    // ---------------------------------------------------------
    // Selected polygon
    // ---------------------------------------------------------

    ImDrawList* drawList =
        ImGui::GetWindowDrawList();

    DrawSelectedPolygon(
        drawList,
        CameraToScreen,
        ScreenToCamera
    );

    // ---------------------------------------------------------
    // Polygon point selection
    // ---------------------------------------------------------

    HandlePolygonPointSelection(
        imagePos,
        width,
        height,
        cameraWidth,
        cameraHeight
    );
}

void PianoVisualizer::RenderVirtualCameraVisualizer()
{
    if (!m_context)
        return;

    if (!m_viewer)
        return;

    if (!m_virtualCamera.IsActive())
        return;

    ID3D11RenderTargetView* frameRTV =
        m_virtualCamera.GetFrameRTV();

    if (!frameRTV)
        return;

    ID3D11ShaderResourceView* visualizerTexture =
        m_viewer->getTexture();

    if (!visualizerTexture)
        return;

    if (!m_virtualCamera.AcquireFrame())
        return;

    constexpr UINT frameWidth = 1920;
    constexpr UINT frameHeight = 1080;

    // ---------------------------------------------------------
    // Calculate piano projection
    // ---------------------------------------------------------

    if (m_polygonPoints.size() != 4)
    {
        m_virtualCamera.ReleaseFrame();
        return;
    }

    ImVec2 P1 =
        m_polygonPoints[0];

    ImVec2 P2 =
        m_polygonPoints[1];

    ImVec2 P3 =
        m_polygonPoints[2];

    ImVec2 P4 =
        m_polygonPoints[3];

    const float cameraWidth =
        static_cast<float>(
            m_camera.GetWidth()
            );

    const float cameraHeight =
        static_cast<float>(
            m_camera.GetHeight()
            );

    PianoCameraPose pose =
        CalculatePianoCameraPose(
            P1,
            P2,
            P3,
            P4,
            cameraWidth,
            cameraHeight,
            m_horizontalFovDegrees
        );

    if (!pose.valid)
    {
        m_virtualCamera.ReleaseFrame();
        return;
    }

    float surfaceHeight =
        m_planeDepth *
        m_heightScale;

    float angleRadians =
        m_planePivot *
        3.14159265358979323846f /
        180.0f;

    // ---------------------------------------------------------
    // Rotate the virtual surface around the X axis.
    //
    // 90° = current vertical orientation
    //  0° = flat along the piano
    //
    // The surface extends in local +Y / -Z.
    // ---------------------------------------------------------

    float rotatedY =
        surfaceHeight *
        std::cos(angleRadians);

    float rotatedZ =
        -surfaceHeight *
        std::sin(angleRadians);

    float x1, y1;
    float x2, y2;
    float x3, y3;
    float x4, y4;

    bool valid1 =
        ProjectPianoPoint(
            pose,
            m_surfaceXOffset,
            m_surfaceYOffset,
            m_surfaceZOffset,
            x1,
            y1
        );

    bool valid2 =
        ProjectPianoPoint(
            pose,
            m_planeWidth +
            m_surfaceXOffset,
            m_surfaceYOffset,
            m_surfaceZOffset,
            x2,
            y2
        );

    bool valid3 =
        ProjectPianoPoint(
            pose,
            m_planeWidth +
            m_surfaceXOffset,
            m_surfaceYOffset +
            rotatedY,
            m_surfaceZOffset +
            rotatedZ,
            x3,
            y3
        );

    bool valid4 =
        ProjectPianoPoint(
            pose,
            m_surfaceXOffset,
            m_surfaceYOffset +
            rotatedY,
            m_surfaceZOffset +
            rotatedZ,
            x4,
            y4
        );

    if (!valid1 ||
        !valid2 ||
        !valid3 ||
        !valid4)
    {
        m_virtualCamera.ReleaseFrame();
        return;
    }

    // ---------------------------------------------------------
    // Convert camera coordinates to virtual-camera coordinates
    // ---------------------------------------------------------

    auto CameraToFrame =
        [&](float cameraX, float cameraY)
        {
            return ImVec2(
                cameraX *
                (
                    static_cast<float>(frameWidth) /
                    cameraWidth
                    ),

                cameraY *
                (
                    static_cast<float>(frameHeight) /
                    cameraHeight
                    )
            );
        };

    const ImVec2 bottomLeft =
        CameraToFrame(
            x1,
            y1
        );

    const ImVec2 bottomRight =
        CameraToFrame(
            x2,
            y2
        );

    const ImVec2 topRight =
        CameraToFrame(
            x3,
            y3
        );

    const ImVec2 topLeft =
        CameraToFrame(
            x4,
            y4
        );

    // ---------------------------------------------------------
    // Render visualizer into the shared frame
    // ---------------------------------------------------------

    // IMPORTANT:
    //
    // This is NOT the ImGui VirtualWindowRenderer.
    //
    // The shared frame is an actual D3D11 texture, so the
    // perspective transformation needs to happen through D3D11.
    //
    // We'll implement this as a GPU grid/homography renderer.
    //

    m_virtualWindowRenderer.RenderD3D11(
        m_context,
        frameRTV,
        visualizerTexture,

        topLeft,
        topRight,
        bottomRight,
        bottomLeft
    );

    // ---------------------------------------------------------
    // Unbind visualizer SRV
    // ---------------------------------------------------------

    ID3D11ShaderResourceView* nullSRV =
        nullptr;

    m_context->PSSetShaderResources(
        0,
        1,
        &nullSRV
    );

    // ---------------------------------------------------------
    // Release shared frame
    // ---------------------------------------------------------

    m_virtualCamera.ReleaseFrame();

    // ---------------------------------------------------------
    // Restore normal viewport
    // ---------------------------------------------------------

    D3D11_VIEWPORT viewport{};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    viewport.Width =
        static_cast<float>(
            m_statistics.renderWidth
            );

    viewport.Height =
        static_cast<float>(
            m_statistics.renderHeight
            );

    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(
        1,
        &viewport
    );
}

// =========================================================
// Render Virtual Camera Frame
// =========================================================

void PianoVisualizer::RenderVirtualCameraFrame()
{
    if (!m_context)
        return;

    if (!m_viewer)
        return;

    if (!m_cameraOutputSRV)
        return;

    if (!m_virtualCamera.IsActive())
        return;

    ID3D11RenderTargetView* frameRTV =
        m_virtualCamera.GetFrameRTV();

    if (!frameRTV)
        return;

    if (!m_virtualCamera.AcquireFrame())
        return;

    constexpr UINT frameWidth = 1920;
    constexpr UINT frameHeight = 1080;

    // ---------------------------------------------------------
    // Viewport
    // ---------------------------------------------------------

    D3D11_VIEWPORT viewport{};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    viewport.Width =
        static_cast<float>(
            frameWidth
            );

    viewport.Height =
        static_cast<float>(
            frameHeight
            );

    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(
        1,
        &viewport
    );

    // ---------------------------------------------------------
    // Shared frame render target
    // ---------------------------------------------------------

    constexpr float clearColor[4]{
        0.0f,
        0.0f,
        0.0f,
        1.0f
    };

    m_context->OMSetRenderTargets(
        1,
        &frameRTV,
        nullptr
    );

    m_context->ClearRenderTargetView(
        frameRTV,
        clearColor
    );

    // ---------------------------------------------------------
    // Camera
    // ---------------------------------------------------------

    m_viewer->getFullscreenQuad().draw(
        m_context,
        m_cameraOutputSRV.Get(),
        0.0f
    );

    // ---------------------------------------------------------
    // Visualizer overlay
    // ---------------------------------------------------------

    RenderVirtualCameraVisualizer();

    // ---------------------------------------------------------
    // Unbind SRV
    // ---------------------------------------------------------

    ID3D11ShaderResourceView* nullSRV =
        nullptr;

    m_context->PSSetShaderResources(
        0,
        1,
        &nullSRV
    );

    // ---------------------------------------------------------
    // Release shared frame
    //
    // Producer:
    //     key 0 -> write
    //     key 1 -> consumer may read
    // ---------------------------------------------------------

    m_virtualCamera.ReleaseFrame();

    // ---------------------------------------------------------
    // Restore normal viewport
    // ---------------------------------------------------------

    D3D11_VIEWPORT mainViewport{};

    mainViewport.TopLeftX = 0.0f;
    mainViewport.TopLeftY = 0.0f;

    mainViewport.Width =
        static_cast<float>(
            m_statistics.renderWidth
            );

    mainViewport.Height =
        static_cast<float>(
            m_statistics.renderHeight
            );

    mainViewport.MinDepth = 0.0f;
    mainViewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(
        1,
        &mainViewport
    );
}

void PianoVisualizer::RenderCameraErrorDialog()
{
    if (!m_showCameraError)
        return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::OpenPopup("Camera Error");

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + viewport->WorkSize.y * 0.5f
        ),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f)
    );

    if (ImGui::BeginPopupModal(
        "Camera Error",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar
    ))
    {
        ImGui::TextWrapped(
            "%s",
            m_cameraErrorMessage.c_str()
        );

        ImGui::Spacing();

        const float buttonWidth = 120.0f;

        ImGui::SetCursorPosX(
            (ImGui::GetWindowWidth() - buttonWidth) * 0.5f
        );

        if (ImGui::Button(
            "OK",
            ImVec2(buttonWidth, 32.0f)
        ))
        {
            m_showCameraError = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// =========================================================
// Piano Overlay
// =========================================================

void PianoVisualizer::RenderPianoOverlay()
{
    if (m_polygonPoints.size() != 4)
        return;

    ImVec2 P1 =
        m_polygonPoints[0];

    ImVec2 P2 =
        m_polygonPoints[1];

    ImVec2 P3 =
        m_polygonPoints[2];

    ImVec2 P4 =
        m_polygonPoints[3];

    float cameraWidth = 0.0f;
    float cameraHeight = 0.0f;

    // ---------------------------------------------------------
    // Draw camera output
    // ---------------------------------------------------------

    if (
        m_viewer &&
        m_viewer->isPlaybackLoaded()
        )
    {
        cameraWidth =
            static_cast<float>(
                m_cameraVideoPlayer.GetWidth()
                );

        cameraHeight =
            static_cast<float>(
                m_cameraVideoPlayer.GetHeight()
                );
    }
    else
    {
        cameraWidth =
            static_cast<float>(
                m_camera.GetWidth()
                );

        cameraHeight =
            static_cast<float>(
                m_camera.GetHeight()
                );
    }

    PianoCameraPose pose =
        CalculatePianoCameraPose(
            P1,
            P2,
            P3,
            P4,
            cameraWidth,
            cameraHeight,
            m_horizontalFovDegrees
        );

    if (!pose.valid)
        return;

    float surfaceHeight =
        m_planeDepth *
        m_heightScale;

    float angleRadians =
        m_planePivot *
        3.14159265358979323846f /
        180.0f;

    // ---------------------------------------------------------
    // Rotate the virtual surface around the X axis.
    //
    // 90° = current vertical orientation
    //  0° = flat along the piano
    //
    // The surface extends in local +Y / -Z.
    // ---------------------------------------------------------

    float rotatedY =
        surfaceHeight *
        std::cos(angleRadians);

    float rotatedZ =
        -surfaceHeight *
        std::sin(angleRadians);

    float x1, y1;
    float x2, y2;
    float x3, y3;
    float x4, y4;

    bool valid1 =
        ProjectPianoPoint(
            pose,
            m_surfaceXOffset,
            m_surfaceYOffset,
            m_surfaceZOffset,
            x1,
            y1
        );

    bool valid2 =
        ProjectPianoPoint(
            pose,
            m_planeWidth +
            m_surfaceXOffset,
            m_surfaceYOffset,
            m_surfaceZOffset,
            x2,
            y2
        );

    bool valid3 =
        ProjectPianoPoint(
            pose,
            m_planeWidth +
            m_surfaceXOffset,
            m_surfaceYOffset +
            rotatedY,
            m_surfaceZOffset +
            rotatedZ,
            x3,
            y3
        );

    bool valid4 =
        ProjectPianoPoint(
            pose,
            m_surfaceXOffset,
            m_surfaceYOffset +
            rotatedY,
            m_surfaceZOffset +
            rotatedZ,
            x4,
            y4
        );

    if (
        !valid1 ||
        !valid2 ||
        !valid3 ||
        !valid4
        )
    {
        return;
    }

    // ---------------------------------------------------------
    // Convert to screen coordinates
    // ---------------------------------------------------------

    ImVec2 imagePos =
        ImGui::GetItemRectMin();

    ImVec2 imageSize =
        ImGui::GetItemRectSize();

    auto CameraToScreen =
        [&](float cameraX, float cameraY)
        {
            return ImVec2(
                imagePos.x +
                cameraX *
                (imageSize.x / cameraWidth),

                imagePos.y +
                cameraY *
                (imageSize.y / cameraHeight)
            );
        };

    ImVec2 bottomLeft =
        CameraToScreen(
            x1,
            y1
        );

    ImVec2 bottomRight =
        CameraToScreen(
            x2,
            y2
        );

    ImVec2 topRight =
        CameraToScreen(
            x3,
            y3
        );

    ImVec2 topLeft =
        CameraToScreen(
            x4,
            y4
        );

    // ---------------------------------------------------------
    // Select texture
    // ---------------------------------------------------------

    ID3D11ShaderResourceView* texture =
        nullptr;

    if (
        gui::renderer ==
        gui::VisualizerRenderer::OtherVisualizer
        )
    {
        m_windowCapture.Update();

        texture =
            m_windowCapture.GetTexture();
    }
    else if (
        gui::renderer ==
        gui::VisualizerRenderer::BuiltInVisualizer
        )
    {
        m_drawVisualizer = true;

        if (m_viewer)
        {
            texture =
                m_viewer->getTexture();
        }
    }

    if (!texture)
        return;

    ImTextureID textureId =
        reinterpret_cast<ImTextureID>(
            texture
            );

    VirtualWindowRenderer::Settings settings;

    settings.gridX = 64;
    settings.gridY = 36;

    settings.drawDebugLines =
        gui::showDebugLines;

    ImDrawList* drawList =
        ImGui::GetWindowDrawList();

    m_virtualWindowRenderer.Render(
        drawList,
        textureId,

        topLeft,
        topRight,
        bottomRight,
        bottomLeft,

        settings
    );

    // ---------------------------------------------------------
    // Debug outline
    // ---------------------------------------------------------

    if (!gui::showDebugLines)
        return;

    ImVec2 virtualWindow[4] =
    {
        topLeft,
        topRight,
        bottomRight,
        bottomLeft
    };

    for (int i = 0; i < 4; ++i)
    {
        int next =
            (i + 1) % 4;

        drawList->AddLine(
            virtualWindow[i],
            virtualWindow[next],
            IM_COL32(
                0,
                150,
                255,
                230
            ),
            2.0f
        );
    }
}


// =========================================================
// Draw Selected Polygon
// =========================================================

void PianoVisualizer::DrawSelectedPolygon(
    ImDrawList* drawList,
    const std::function<ImVec2(float, float)>& cameraToScreen,
    const std::function<ImVec2(float, float)>& screenToCamera
)
{
    if (!gui::showDebugLines && !gui::choosingPoints)
        return;

    if (m_polygonPoints.size() < 2)
        return;

    ImVec2 points[4];

    for (
        size_t i = 0;
        i < m_polygonPoints.size() &&
        i < 4;
        ++i
        )
    {
        points[i] =
            cameraToScreen(
                m_polygonPoints[i].x,
                m_polygonPoints[i].y
            );
    }

    // ---------------------------------------------------------
    // Handle point interaction
    // ---------------------------------------------------------

    ImGuiIO& io = ImGui::GetIO();

    static int draggingPoint = -1;

    for (
        size_t i = 0;
        i < m_polygonPoints.size() &&
        i < 4;
        ++i
        )
    {
        const float dx =
            io.MousePos.x - points[i].x;

        const float dy =
            io.MousePos.y - points[i].y;

        const float distanceSquared =
            dx * dx +
            dy * dy;

        const float hoverRadius = 10.0f;

        const bool hovered =
            distanceSquared <=
            hoverRadius * hoverRadius;

        // -----------------------------------------------------
        // Start dragging
        // -----------------------------------------------------

        if (
            hovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            )
        {
            draggingPoint =
                static_cast<int>(i);
        }

        // -----------------------------------------------------
        // Move point while dragging
        // -----------------------------------------------------

        if (
            draggingPoint ==
            static_cast<int>(i)
            )
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                ImVec2 cameraPosition =
                    screenToCamera(
                        io.MousePos.x,
                        io.MousePos.y
                    );

                m_polygonPoints[i].x =
                    cameraPosition.x;

                m_polygonPoints[i].y =
                    cameraPosition.y;

                // Recalculate screen position
                points[i] =
                    cameraToScreen(
                        m_polygonPoints[i].x,
                        m_polygonPoints[i].y
                    );
            }
            else
            {
                draggingPoint = -1;
            }
        }
    }

    // ---------------------------------------------------------
    // Filled polygon
    // ---------------------------------------------------------

    if (m_polygonPoints.size() >= 3)
    {
        drawList->AddConvexPolyFilled(
            points,
            static_cast<int>(
                m_polygonPoints.size()
                ),
            IM_COL32(
                0,
                255,
                0,
                80
            )
        );
    }

    // ---------------------------------------------------------
    // Polygon outline
    // ---------------------------------------------------------

    for (
        size_t i = 0;
        i < m_polygonPoints.size();
        ++i
        )
    {
        size_t next =
            (i + 1) %
            m_polygonPoints.size();

        drawList->AddLine(
            points[i],
            points[next],
            IM_COL32(
                0,
                255,
                0,
                255
            ),
            2.0f
        );
    }

    // ---------------------------------------------------------
    // Polygon points
    // ---------------------------------------------------------

    for (
        size_t i = 0;
        i < m_polygonPoints.size();
        ++i
        )
    {
        const float dx =
            io.MousePos.x - points[i].x;

        const float dy =
            io.MousePos.y - points[i].y;

        const bool hovered =
            dx * dx + dy * dy <=
            10.0f * 10.0f;

        const bool dragging =
            draggingPoint ==
            static_cast<int>(i);

        ImU32 pointColor;

        if (dragging)
        {
            pointColor =
                IM_COL32(
                    100,
                    200,
                    255,
                    255
                );
        }
        else if (hovered)
        {
            pointColor =
                IM_COL32(
                    200,
                    230,
                    255,
                    255
                );
        }
        else
        {
            pointColor =
                IM_COL32(
                    255,
                    255,
                    255,
                    255
                );
        }

        drawList->AddCircleFilled(
            points[i],
            7.0f,
            pointColor
        );

        drawList->AddCircle(
            points[i],
            8.0f,
            IM_COL32(
                0,
                0,
                0,
                255
            ),
            16,
            1.5f
        );

        // -----------------------------------------------------
        // Point label
        // -----------------------------------------------------

        const char* label = "";

        switch (i)
        {
        case 0:
            label = "P1 - Top Left";
            break;

        case 1:
            label = "P2 - Bottom Left";
            break;

        case 2:
            label = "P3 - Bottom Right";
            break;

        case 3:
            label = "P4 - Top Right";
            break;
        }

        drawList->AddText(
            ImVec2(
                points[i].x + 10.0f,
                points[i].y - 10.0f
            ),
            IM_COL32(
                255,
                255,
                255,
                255
            ),
            label
        );
    }
}


// =========================================================
// Polygon Point Selection
// =========================================================

void PianoVisualizer::HandlePolygonPointSelection(
    const ImVec2& imagePos,
    float width,
    float height,
    float cameraWidth,
    float cameraHeight
)
{
    if (!m_choosingPolygonPoints)
        return;

    if (!ImGui::IsItemHovered())
        return;

    if (!ImGui::IsMouseClicked(
        ImGuiMouseButton_Left
    ))
    {
        return;
    }

    ImVec2 mousePos =
        ImGui::GetMousePos();

    // ---------------------------------------------------------
    // Screen -> displayed image coordinates
    // ---------------------------------------------------------

    float imageX =
        mousePos.x -
        imagePos.x;

    float imageY =
        mousePos.y -
        imagePos.y;

    // ---------------------------------------------------------
    // Displayed image -> camera coordinates
    // ---------------------------------------------------------

    float scaleX =
        cameraWidth /
        width;

    float scaleY =
        cameraHeight /
        height;

    float cameraX =
        imageX *
        scaleX;

    float cameraY =
        imageY *
        scaleY;

    // ---------------------------------------------------------
    // Clamp
    // ---------------------------------------------------------

    cameraX =
        std::clamp(
            cameraX,
            0.0f,
            cameraWidth - 1.0f
        );

    cameraY =
        std::clamp(
            cameraY,
            0.0f,
            cameraHeight - 1.0f
        );

    // ---------------------------------------------------------
    // Add point
    // ---------------------------------------------------------

    if (m_polygonPoints.size() >= 4)
        return;

    m_polygonPoints.push_back(
        ImVec2(
            cameraX,
            cameraY
        )
    );

    m_polygonClickCount++;

    const char* pointName = "";

    switch (m_polygonClickCount)
    {
    case 1:
        pointName = "Top-left";
        break;

    case 2:
        pointName = "Bottom-left";
        break;

    case 3:
        pointName = "Bottom-right";
        break;

    case 4:
        pointName = "Top-right";
        break;
    }

    Logger::Log(
        "P" +
        std::to_string(
            m_polygonClickCount
        ) +
        " (" +
        std::string(pointName) +
        "): (" +
        std::to_string(
            static_cast<int>(cameraX)
        ) +
        ", " +
        std::to_string(
            static_cast<int>(cameraY)
        ) +
        ")\n"
    );

    // ---------------------------------------------------------
    // Finished
    // ---------------------------------------------------------

    if (m_polygonPoints.size() >= 4)
    {
        m_choosingPolygonPoints =
            false;

        Logger::Log(
            "Piano corner selection complete.\n"
        );
    }
}


// =========================================================
// Settings
// =========================================================

void PianoVisualizer::RenderSettings()
{
    if (!gui::showSettings)
        return;

    // =========================================================
    // Window
    // =========================================================

    ImGui::SetNextWindowSize(
        ImVec2(520.0f, 0.0f),
        ImGuiCond_FirstUseEver
    );

    if (
        ImGui::Begin(
            "Settings",
            &gui::showSettings,
            ImGuiWindowFlags_NoCollapse
        )
        )
    {
        // =====================================================
        // Header
        // =====================================================

        ImGui::Text(
            "Piano Visualizer"
        );

        ImGui::SameLine();

        ImGui::TextDisabled(
            "  •  Press I to hide"
        );

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();


        // =====================================================
        // Display
        // =====================================================

        ImGui::Text(
            "DISPLAY"
        );

        ImGui::Spacing();

        ImGui::Checkbox(
            "Fullscreen (F)",
            &gui::fullscreen
        );

        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            SetFullscreen(
                m_window,
                gui::fullscreen
            );
        }

        ImGui::Spacing();


        // =====================================================
        // Setup
        // =====================================================

        if (ImGui::CollapsingHeader(
            "SETUP"
        ))
        {
            ImGui::Spacing();

            ImGui::Checkbox(
                "Show Debug Lines",
                &gui::showDebugLines
            );

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // =================================================
            // Piano Surface
            // =================================================

            ImGui::Text(
                "PIANO SURFACE"
            );

            ImGui::Spacing();

            if (!m_choosingPolygonPoints)
            {
                if (ImGui::Button(
                    "Choose Piano Corners",
                    ImVec2(-1.0f, 36.0f)
                ))
                {
                    m_savedPolygonPoints =
                        m_polygonPoints;

                    m_polygonPoints.clear();

                    m_polygonClickCount = 0;

                    m_choosingPolygonPoints =
                        true;

                    Logger::Log(
                        "Waiting for 4 piano corner points...\n"
                    );
                }
            }
            else
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImVec4(0.20f, 0.45f, 0.75f, 1.0f)
                );

                ImGui::Button(
                    "Click 4 piano corners...",
                    ImVec2(-1.0f, 36.0f)
                );

                ImGui::PopStyleColor();
            }


            // =================================================
            // Projection & Transform
            // =================================================

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextDisabled(
                "PROJECTION & TRANSFORM"
            );

            ImGui::Spacing();

            ImGui::SliderFloat(
                "FOV",
                &m_horizontalFovDegrees,
                0.1f,
                90.0f,
                "%.3f"
            );

            ImGui::SliderFloat(
                "Plane Width",
                &m_planeWidth,
                0.1f,
                2.0f,
                "%.3f"
            );

            ImGui::SliderFloat(
                "Plane Depth",
                &m_planeDepth,
                0.1f,
                5.0f,
                "%.3f"
            );

            ImGui::SliderFloat(
                "Plane Pivot",
                &m_planePivot,
                0.0f,
                180.0f,
                "%.1f°"
            );

            ImGui::Spacing();

            ImGui::TextDisabled(
                "Surface Offset"
            );

            ImGui::SliderFloat(
                "X##SurfaceOffset",
                &m_surfaceXOffset,
                -1.0f,
                1.0f,
                "%.3f"
            );

            ImGui::SliderFloat(
                "Y##SurfaceOffset",
                &m_surfaceYOffset,
                -1.0f,
                1.0f,
                "%.3f"
            );

            ImGui::SliderFloat(
                "Z##SurfaceOffset",
                &m_surfaceZOffset,
                -1.0f,
                1.0f,
                "%.3f"
            );
        }


        // =====================================================
        // Renderer
        // =====================================================

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text(
            "VISUALIZER"
        );

        ImGui::Spacing();

        const char* rendererName =
            nullptr;

        switch (gui::renderer)
        {
        case gui::VisualizerRenderer::BuiltInVisualizer:
            rendererName =
                "Built-in visualizer";
            break;

        case gui::VisualizerRenderer::OtherVisualizer:
            rendererName =
                "Capture other visualizer";
            break;

        default:
            rendererName =
                "Unknown";
            break;
        }

        if (ImGui::BeginCombo(
            "Renderer",
            rendererName
        ))
        {
            if (ImGui::Selectable(
                "Built-in visualizer",
                gui::renderer ==
                gui::VisualizerRenderer::BuiltInVisualizer
            ))
            {
                gui::renderer =
                    gui::VisualizerRenderer::BuiltInVisualizer;
            }

            if (ImGui::Selectable(
                "Capture other visualizer",
                gui::renderer ==
                gui::VisualizerRenderer::OtherVisualizer
            ))
            {
                gui::renderer =
                    gui::VisualizerRenderer::OtherVisualizer;
            }

            ImGui::EndCombo();
        }


        // =====================================================
        // Window Capture
        // =====================================================

        if (
            gui::renderer ==
            gui::VisualizerRenderer::OtherVisualizer
            )
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text(
                "CAPTURE WINDOW"
            );

            ImGui::Spacing();

            if (ImGui::Button(
                "Refresh Windows",
                ImVec2(-1.0f, 32.0f)
            ))
            {
                RefreshCaptureWindows();
            }

            ImGui::Spacing();

            const char* selectedName =
                "Select a window";

            int selectedWindowIndex =
                gui::selectedCaptureWindowIndex;

            std::string selectedTitle;

            if (
                selectedWindowIndex >= 0 &&
                selectedWindowIndex <
                static_cast<int>(
                    gui::captureWindows.size()
                    )
                )
            {
                const std::wstring& ws =
                    gui::captureWindows[
                        selectedWindowIndex
                    ].title;

                if (!ws.empty())
                {
                    int size =
                        WideCharToMultiByte(
                            CP_UTF8,
                            0,
                            ws.data(),
                            static_cast<int>(
                                ws.size()
                                ),
                            nullptr,
                            0,
                            nullptr,
                            nullptr
                        );

                    selectedTitle.resize(
                        size
                    );

                    WideCharToMultiByte(
                        CP_UTF8,
                        0,
                        ws.data(),
                        static_cast<int>(
                            ws.size()
                            ),
                        selectedTitle.data(),
                        size,
                        nullptr,
                        nullptr
                    );
                }

                selectedName =
                    selectedTitle.c_str();
            }

            if (ImGui::BeginCombo(
                "Window",
                selectedName
            ))
            {
                for (
                    int i = 0;
                    i <
                    static_cast<int>(
                        gui::captureWindows.size()
                        );
                        ++i
                    )
                {
                    std::string title(
                        gui::captureWindows[i]
                        .title
                        .begin(),
                        gui::captureWindows[i]
                        .title
                        .end()
                    );

                    bool selected =
                        selectedWindowIndex == i;

                    if (ImGui::Selectable(
                        title.c_str(),
                        selected
                    ))
                    {
                        gui::selectedCaptureWindowIndex =
                            i;

                        gui::selectedCaptureWindow =
                            gui::captureWindows[i]
                            .hwnd;
                    }

                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }
        }


        // =====================================================
        // Camera / Audio Settings
        // =====================================================

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(
            "Camera Settings",
            ImVec2(-1.0f, 32.0f)
        ))
        {
            m_showCameraSettings = true;
        }

        ImGui::Spacing();

        if (ImGui::Button(
            "Audio Settings",
            ImVec2(-1.0f, 32.0f)
        ))
        {
            m_showAudioSettings = true;
        }


        // =====================================================
        // Plugin Settings
        // =====================================================

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text(
            "PLUGINS"
        );

        ImGui::Spacing();


        // -----------------------------------------------------
        // Plugin status card
        // -----------------------------------------------------

        ImGui::BeginChild(
            "PluginStatus",
            ImVec2(
                0.0f,
                72.0f
            ),
            true
        );

        if (m_audioEngine)
        {
            const std::string& pluginName =
                m_audioEngine->pluginName();

            if (!pluginName.empty())
            {
                ImGui::Text(
                    "Plugin loaded"
                );

                ImGui::TextDisabled(
                    "%s",
                    pluginName.c_str()
                );
            }
            else
            {
                ImGui::Text(
                    "No plugin loaded"
                );

                ImGui::TextDisabled(
                    "Drop a .vst3 plugin into the window"
                );
            }
        }
        else
        {
            ImGui::Text(
                "Audio engine unavailable"
            );
        }

        ImGui::EndChild();

        ImGui::Spacing();


        // -----------------------------------------------------
        // Recent plugins
        // -----------------------------------------------------

        if (!m_recentVSTPlugins.empty())
        {
            ImGui::Text(
                "Recent Plugins"
            );

            ImGui::Spacing();

            for (const auto& path : m_recentVSTPlugins)
            {
                if (!std::filesystem::exists(path))
                    continue;

                std::string filename =
                    path.filename().string();

                // Remove .vst3 from display name.
                if (
                    filename.size() > 5 &&
                    filename.ends_with(".vst3")
                    )
                {
                    filename.erase(
                        filename.size() - 5
                    );
                }

                if (
                    m_audioEngine &&
                    m_audioEngine->pluginName() != filename
                    )
                {
                    std::string strPath =
                        path.string();

                    if (ImGui::Button(
                        filename.c_str(),
                        ImVec2(-1.0f, 32.0f)
                    ))
                    {
                        LoadRecentVST(path);
                    }

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip(
                            "%s",
                            strPath.c_str()
                        );
                    }

                    ImGui::Spacing();
                }
            }
        }

        ImGui::Separator();
        ImGui::Spacing();


        // -----------------------------------------------------
        // Plugin editor
        // -----------------------------------------------------

        if (
            m_audioEngine &&
            m_audioEngine->plugin()
            )
        {
            if (
                !m_audioEngine->plugin()
                ->hasEditor()
                )
            {
                if (ImGui::Button(
                    "Open Plugin Editor",
                    ImVec2(-1.0f, 32.0f)
                ))
                {
                    if (
                        !m_audioEngine->plugin()
                        ->createEditor(m_instance)
                        )
                    {
                        Logger::Log(
                            "Failed to create plugin editor!\n"
                        );
                    }
                }
            }
            else
            {
                if (ImGui::Button(
                    "Close Plugin Editor",
                    ImVec2(-1.0f, 32.0f)
                ))
                {
                    m_audioEngine->plugin()
                        ->destroyEditor();
                }
            }

            ImGui::Spacing();

            if (ImGui::Button(
                "Remove Plugin",
                ImVec2(-1.0f, 32.0f)
            ))
            {
                UnloadVST();
            }
        }

        ImGui::Spacing();

        if (
            std::filesystem::is_directory(
                m_vst3FolderPath
            )
            )
        {
            if (ImGui::Button(
                "Open VST3 Folder",
                ImVec2(-1.0f, 32.0f)
            ))
            {
                ShellExecuteW(
                    nullptr,
                    L"open",
                    m_vst3FolderPath.c_str(),
                    nullptr,
                    nullptr,
                    SW_SHOWNORMAL
                );
            }
        }
        else
        {
            if (ImGui::Button(
                "Select VST3 Folder",
                ImVec2(-1.0f, 32.0f)
            ))
            {
                auto fldr =
                    gui::OpenFileDialog();

                if (!fldr.empty())
                {
                    m_vst3FolderPath =
                        fldr;
                }
            }
        }


        // =====================================================
        // Configuration
        // =====================================================

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text(
            "CONFIGURATION"
        );

        ImGui::Spacing();

        const float buttonWidth =
            (ImGui::GetContentRegionAvail().x - 8.0f) *
            0.5f;

        if (ImGui::Button(
            "Save Configuration",
            ImVec2(buttonWidth, 36.0f)
        ))
        {
            SavePianoConfiguration();
        }

        ImGui::SameLine();

        if (ImGui::Button(
            "Load Configuration",
            ImVec2(buttonWidth, 36.0f)
        ))
        {
            if (
                Config::LoadPianoConfig(
                    m_polygonPoints,
                    m_horizontalFovDegrees,
                    m_planeWidth,
                    m_planeDepth,
                    m_planePivot,
                    m_surfaceXOffset,
                    m_surfaceYOffset,
                    m_surfaceZOffset
                )
                )
            {
                m_polygonClickCount = 4;

                m_configSaveMessage =
                    "Configuration loaded!";
            }
            else
            {
                m_configSaveMessage =
                    "No valid configuration found!";
            }
        }

        if (!m_configSaveMessage.empty())
        {
            ImGui::Spacing();

            ImGui::TextDisabled(
                "%s",
                m_configSaveMessage.c_str()
            );
        }
    }

    ImGui::End();
}



// =========================================================
// Resize
// =========================================================

void PianoVisualizer::Resize(
    UINT width,
    UINT height
)
{
    if (!m_device)
        return;

    if (!m_swapChain)
        return;

    if (width == 0 || height == 0)
        return;

    Logger::Log(
        "Resizing renderer to %ux%u.\n",
        width,
        height
    );

    m_statistics.renderWidth =
        width;

    m_statistics.renderHeight =
        height;

    CleanupRenderTarget();

    HRESULT hr =
        m_swapChain->ResizeBuffers(
            0,
            width,
            height,
            DXGI_FORMAT_UNKNOWN,
            0
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "Failed to resize swap chain! HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return;
    }

    CreateRenderTarget();

    // ---------------------------------------------------------
    // Set viewport to new render resolution
    // ---------------------------------------------------------

    D3D11_VIEWPORT viewport{};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    viewport.Width =
        static_cast<float>(width);

    viewport.Height =
        static_cast<float>(height);

    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(
        1,
        &viewport
    );

    // ---------------------------------------------------------
    // Resize MIDI visualizer
    // ---------------------------------------------------------

    if (m_viewer)
    {
        m_viewer->resize(
            width,
            height
        );
    }
}


// =========================================================
// Keyboard
// =========================================================

void PianoVisualizer::KeyPressed(
    int key,
    int state
)
{
    if (m_viewer)
    {
        m_viewer->keyPressed(
            key,
            state
        );
    }
}


// =========================================================
// Render Target
// =========================================================

void PianoVisualizer::CleanupRenderTarget()
{
    m_cameraOutputSRV.Reset();
    m_cameraOutputRTV.Reset();
    m_cameraOutputTexture.Reset();

    if (m_renderTargetView)
    {
        m_renderTargetView->Release();

        m_renderTargetView =
            nullptr;
    }
}


void PianoVisualizer::CreateRenderTarget()
{
    if (!m_swapChain)
        return;

    if (!m_device)
        return;

    ID3D11Texture2D* backBuffer =
        nullptr;

    HRESULT hr =
        m_swapChain->GetBuffer(
            0,
            IID_PPV_ARGS(&backBuffer)
        );

    if (FAILED(hr) || !backBuffer)
    {
        Logger::Log(
            "Failed to get swap chain back buffer!\n"
        );

        return;
    }

    hr =
        m_device->CreateRenderTargetView(
            backBuffer,
            nullptr,
            &m_renderTargetView
        );

    backBuffer->Release();

    if (FAILED(hr))
    {
        Logger::Log(
            "Failed to create render target view!\n"
        );

        m_renderTargetView =
            nullptr;

        return;
    }

    if (!InitializeCameraOutput())
    {
        Logger::Log(
            "Failed to initialize camera output after render target creation!\n"
        );
    }
}

// =========================================================
// Statistics
// =========================================================

void PianoVisualizer::UpdateStatistics()
{
    if (!m_initialized)
        return;

    const auto now =
        std::chrono::steady_clock::now();

    const double deltaTime =
        std::chrono::duration<double>(
            now - m_prevTime
        ).count();

    m_statistics.frameTimeMs =
        static_cast<float>(
            deltaTime * 1000.0
            );

    m_statistics.frameCount++;

    m_statistics.fpsAccumulator +=
        deltaTime;

    m_statistics.fpsFrameCount++;

    m_statistics.cameraAccumulator +=
        deltaTime;

    m_statistics.cameraUpdateCount++;

    m_statistics.uptimeSeconds =
        std::chrono::duration<double>(
            now - m_startTime
        ).count();

    // ---------------------------------------------------------
    // Calculate FPS once per second
    // ---------------------------------------------------------

    if (m_statistics.fpsAccumulator >= 1.0)
    {
        m_statistics.fps =
            static_cast<float>(
                m_statistics.fpsFrameCount /
                m_statistics.fpsAccumulator
                );

        m_statistics.fpsFrameCount = 0;
        m_statistics.fpsAccumulator = 0.0;
    }

    // ---------------------------------------------------------
    // Camera update rate
    // ---------------------------------------------------------

    if (m_statistics.cameraAccumulator >= 1.0)
    {
        m_statistics.cameraUpdateFps =
            static_cast<float>(
                m_statistics.cameraUpdateCount /
                m_statistics.cameraAccumulator
                );

        m_statistics.cameraUpdateCount = 0;
        m_statistics.cameraAccumulator = 0.0;
    }

    // ---------------------------------------------------------
    // Frame time extremes
    // ---------------------------------------------------------

    if (m_statistics.minFrameTimeMs <= 0.0f)
    {
        m_statistics.minFrameTimeMs =
            m_statistics.frameTimeMs;
    }
    else
    {
        m_statistics.minFrameTimeMs =
            std::min(
                m_statistics.minFrameTimeMs,
                m_statistics.frameTimeMs
            );
    }

    m_statistics.maxFrameTimeMs =
        std::max(
            m_statistics.maxFrameTimeMs,
            m_statistics.frameTimeMs
        );

    // ---------------------------------------------------------
    // Camera
    // ---------------------------------------------------------

    m_statistics.cameraWidth =
        m_camera.GetWidth();

    m_statistics.cameraHeight =
        m_camera.GetHeight();

    // ---------------------------------------------------------
    // Process stats
    // ---------------------------------------------------------

    PROCESS_MEMORY_COUNTERS pmc{};

    pmc.cb =
        sizeof(PROCESS_MEMORY_COUNTERS);

    if (GetProcessMemoryInfo(
        GetCurrentProcess(),
        &pmc,
        sizeof(pmc)
    ))
    {
        m_statistics.processMemoryMB =
            static_cast<uint64_t>(
                pmc.WorkingSetSize /
                (1024ull * 1024ull)
                );
    }


    //PROCESS_MEMORY_COUNTERS_EX pmc{};
    //pmc.cb = sizeof(pmc);

    //if (GetProcessMemoryInfo(
    //    GetCurrentProcess(),
    //    reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
    //    sizeof(pmc)))
    //{
    //    Logger::Log(
    //        "Memory: WorkingSet=%llu MB, PrivateUsage=%llu MB, "
    //        "PeakWorkingSet=%llu MB, PagefileUsage=%llu MB\n",
    //        pmc.WorkingSetSize / (1024ull * 1024ull),
    //        pmc.PrivateUsage / (1024ull * 1024ull),
    //        pmc.PeakWorkingSetSize / (1024ull * 1024ull),
    //        pmc.PagefileUsage / (1024ull * 1024ull)
    //    );
    //}
}

void PianoVisualizer::RenderStatistics()
{
    if (!m_showStatistics)
        return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // ---------------------------------------------------------
    // Position near bottom-right
    // ---------------------------------------------------------

    const float margin = 16.0f;

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x - margin,
            viewport->WorkPos.y + viewport->WorkSize.y - margin
        ),
        ImGuiCond_FirstUseEver,
        ImVec2(1.0f, 1.0f)
    );

    if (ImGui::Begin(
        "Statistics",
        &m_showStatistics,
        ImGuiWindowFlags_AlwaysAutoResize
    ))
    {
        // Compact 4-column layout:
        // Label | Value | Label | Value
        //
        // This puts two statistics on each row, greatly reducing
        // the vertical size of the window.

        auto BeginStatsTable = []()
            {
                return ImGui::BeginTable(
                    "##StatsTable",
                    4,
                    ImGuiTableFlags_SizingFixedFit |
                    ImGuiTableFlags_NoSavedSettings
                );
            };

        auto EndStatsTable = []()
            {
                ImGui::EndTable();
            };

        auto Stat = [](const char* label, const char* format, auto... args)
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", label);

                ImGui::TableNextColumn();
                ImGui::Text(format, args...);
            };

        // =====================================================
        // PERFORMANCE
        // =====================================================

        ImGui::SeparatorText("PERFORMANCE");

        if (BeginStatsTable())
        {
            ImGui::TableNextRow();

            Stat("FPS", "%.1f", m_statistics.fps);
            Stat("Frame time", "%.3f ms", m_statistics.frameTimeMs);

            ImGui::TableNextRow();

            Stat("Min frame", "%.3f ms", m_statistics.minFrameTimeMs);
            Stat("Max frame", "%.3f ms", m_statistics.maxFrameTimeMs);

            ImGui::TableNextRow();

            Stat(
                "Frames",
                "%llu",
                static_cast<unsigned long long>(
                    m_statistics.frameCount
                    )
            );

            Stat("Uptime", "%.1f s", m_statistics.uptimeSeconds);

            EndStatsTable();
        }

        // =====================================================
        // DISPLAY
        // =====================================================

        ImGui::SeparatorText("DISPLAY");

        if (BeginStatsTable())
        {
            ImGui::TableNextRow();

            Stat(
                "Resolution",
                "%u x %u",
                m_statistics.renderWidth,
                m_statistics.renderHeight
            );

            Stat(
                "Fullscreen",
                "%s",
                gui::fullscreen ? "Yes" : "No"
            );

            EndStatsTable();
        }

        // =====================================================
        // CAMERA / VIDEO PLAYER
        // =====================================================

        const bool playback =
            m_viewer &&
            m_viewer->isPlaybackLoaded();

        ImGui::SeparatorText(
            playback
            ? "VIDEO PLAYER"
            : "CAMERA"
        );

        if (playback)
        {
            const CameraVideoPlayerStatistics videoStats =
                m_cameraVideoPlayer.GetStatistics();

            const double frameSizeMB =
                static_cast<double>(videoStats.frameBytes) /
                (1024.0 * 1024.0);

            if (BeginStatsTable())
            {
                ImGui::TableNextRow();

                Stat(
                    "Status",
                    "%s",
                    videoStats.open ? "Open" : "Closed"
                );

                Stat(
                    "Thread",
                    "%s",
                    videoStats.threadRunning
                    ? "Running"
                    : "Stopped"
                );

                ImGui::TableNextRow();

                Stat(
                    "Resolution",
                    "%d x %d",
                    videoStats.width,
                    videoStats.height
                );

                Stat(
                    "Video FPS",
                    "%.2f FPS",
                    videoStats.videoFps
                );

                ImGui::TableNextRow();

                Stat(
                    "Decoded FPS",
                    "%.2f FPS",
                    videoStats.decodeFps
                );

                Stat(
                    "Uploaded FPS",
                    "%.2f FPS",
                    videoStats.uploadFps
                );

                ImGui::TableNextRow();

                Stat(
                    "Decode",
                    "%.3f ms",
                    videoStats.decodeFrameTimeMs
                );

                Stat(
                    "D3D11 upload",
                    "%.3f ms",
                    videoStats.uploadFrameTimeMs
                );

                ImGui::TableNextRow();

                Stat(
                    "Frames decoded",
                    "%llu",
                    static_cast<unsigned long long>(
                        videoStats.framesDecoded
                        )
                );

                Stat(
                    "Frames uploaded",
                    "%llu",
                    static_cast<unsigned long long>(
                        videoStats.framesUploaded
                        )
                );

                ImGui::TableNextRow();

                Stat(
                    "Frames dropped",
                    "%llu",
                    static_cast<unsigned long long>(
                        videoStats.framesDropped
                        )
                );

                Stat(
                    "Queued frames",
                    "%zu",
                    videoStats.queuedFrames
                );

                ImGui::TableNextRow();

                Stat(
                    "Buffered",
                    "%.1f ms",
                    videoStats.bufferedTimeMs
                );

                Stat(
                    "Decoder lead",
                    "%.1f ms",
                    videoStats.decoderLeadMs
                );

                ImGui::TableNextRow();

                Stat(
                    "Current time",
                    "%.3f s",
                    videoStats.currentFrameTime
                );

                Stat(
                    "Target time",
                    "%.3f s",
                    videoStats.targetTime
                );

                ImGui::TableNextRow();

                Stat(
                    "Frame size",
                    "%.2f MB",
                    frameSizeMB
                );

                Stat(
                    "End of stream",
                    "%s",
                    videoStats.endOfStream
                    ? "Yes"
                    : "No"
                );

                EndStatsTable();
            }
        }
        else
        {
            const Camera::CameraStatistics cameraStats =
                m_camera.GetStatistics();

            double configuredCameraFps = 0.0;

            if (cameraStats.fpsDenominator != 0)
            {
                configuredCameraFps =
                    static_cast<double>(
                        cameraStats.fpsNumerator
                        ) /
                    static_cast<double>(
                        cameraStats.fpsDenominator
                        );
            }

            const double frameSizeMB =
                static_cast<double>(cameraStats.frameBytes) /
                (1024.0 * 1024.0);

            if (BeginStatsTable())
            {
                ImGui::TableNextRow();

                Stat(
                    "Status",
                    "%s",
                    cameraStats.open
                    ? "Open"
                    : "Closed"
                );

                Stat(
                    "Thread",
                    "%s",
                    cameraStats.threadRunning
                    ? "Running"
                    : "Stopped"
                );

                ImGui::TableNextRow();

                Stat(
                    "Resolution",
                    "%d x %d",
                    cameraStats.width,
                    cameraStats.height
                );

                Stat(
                    "Configured FPS",
                    "%.2f FPS",
                    configuredCameraFps
                );

                ImGui::TableNextRow();

                Stat(
                    "Captured FPS",
                    "%.2f FPS",
                    cameraStats.captureFps
                );

                Stat(
                    "Uploaded FPS",
                    "%.2f FPS",
                    cameraStats.uploadFps
                );

                ImGui::TableNextRow();

                Stat(
                    "Decode",
                    "%.3f ms",
                    cameraStats.captureFrameTimeMs
                );

                Stat(
                    "D3D11 upload",
                    "%.3f ms",
                    cameraStats.uploadFrameTimeMs
                );

                ImGui::TableNextRow();

                Stat(
                    "Frames captured",
                    "%llu",
                    static_cast<unsigned long long>(
                        cameraStats.framesCaptured
                        )
                );

                Stat(
                    "Frames uploaded",
                    "%llu",
                    static_cast<unsigned long long>(
                        cameraStats.framesUploaded
                        )
                );

                ImGui::TableNextRow();

                Stat(
                    "Frames dropped",
                    "%llu",
                    static_cast<unsigned long long>(
                        cameraStats.framesDropped
                        )
                );

                Stat(
                    "Frame size",
                    "%.2f MB",
                    frameSizeMB
                );

                EndStatsTable();
            }
        }

        

        // =====================================================
        // AUDIO
        // =====================================================

        ImGui::SeparatorText("AUDIO");

        if (m_audioEngine)
        {
            const auto* output = m_audioEngine->output();
            const auto* audio = m_audioEngine->audio();

            if (output)
            {
                const auto audioStats = output->getStatistics();

                static double latencyAccumulator = 0.0;
                static double latencyAccumulatorTime = 0.0;
                static uint64_t latencySampleCount = 0;
                static double averageLatencyMs = 0.0;

                double pluginLatencyMs = 0.0;
                double queuedAudioLatencyMs = 0.0;

                if (audio && audioStats.sampleRate > 0.0)
                {
                    pluginLatencyMs =
                        static_cast<double>(
                            audio->getLatencySamples()
                            ) /
                        audioStats.sampleRate *
                        1000.0;

                    queuedAudioLatencyMs =
                        static_cast<double>(
                            audioStats.currentPadding
                            ) /
                        audioStats.sampleRate *
                        1000.0;
                }

                const double estimatedLatencyMs =
                    pluginLatencyMs +
                    queuedAudioLatencyMs;

                latencyAccumulator += estimatedLatencyMs;
                latencyAccumulatorTime +=
                    m_statistics.frameTimeMs / 1000.0;
                latencySampleCount++;

                if (latencyAccumulatorTime >= 2.0 &&
                    latencySampleCount > 0)
                {
                    averageLatencyMs =
                        latencyAccumulator /
                        static_cast<double>(latencySampleCount);

                    latencyAccumulator = 0.0;
                    latencyAccumulatorTime = 0.0;
                    latencySampleCount = 0;
                }

                if (BeginStatsTable())
                {
                    ImGui::TableNextRow();

                    Stat(
                        "Output",
                        "%s",
                        audioStats.initialized
                        ? (audioStats.running ? "Running" : "Stopped")
                        : "Unavailable"
                    );

                    Stat(
                        "Sample rate",
                        "%.0f Hz",
                        audioStats.sampleRate
                    );

                    ImGui::TableNextRow();

                    Stat(
                        "Channels",
                        "%d",
                        audioStats.channels
                    );

                    Stat(
                        "Buffer",
                        "%u frames",
                        audioStats.bufferFrames
                    );

                    ImGui::TableNextRow();

                    Stat(
                        "Buffer duration",
                        "%.2f ms",
                        audioStats.bufferDurationMs
                    );

                    Stat(
                        "Output rate",
                        "%.0f frames/s",
                        audioStats.outputFramesPerSecond
                    );

                    ImGui::TableNextRow();

                    Stat(
                        "Throughput",
                        "%.2f MB/s",
                        audioStats.throughputMBps
                    );

                    Stat(
                        "Failed writes",
                        "%llu",
                        static_cast<unsigned long long>(
                            audioStats.failedWrites
                            )
                    );

                    ImGui::TableNextRow();

                    Stat(
                        "Average latency",
                        "%.3f ms",
                        averageLatencyMs
                    );

                    EndStatsTable();
                }
            }
            else
            {
                ImGui::TextDisabled("Audio output: Unavailable");
            }
        }
        else
        {
            ImGui::TextDisabled("Audio engine: Unavailable");
        }

        // -----------------------------------------------------
        // VST3
        // -----------------------------------------------------

        const bool pluginLoaded =
            m_audioEngine &&
            !m_audioEngine->pluginName().empty();

        if (BeginStatsTable())
        {
            ImGui::TableNextRow();

            Stat(
                "VST3",
                "%s",
                pluginLoaded ? "Loaded" : "None"
            );

            if (pluginLoaded)
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("Plugin");

                ImGui::TableNextColumn();
                ImGui::Text(
                    "%s",
                    m_audioEngine->pluginName().c_str()
                );
            }

            EndStatsTable();
        }

        // =====================================================
        // VISUALIZER
        // =====================================================

        ImGui::SeparatorText("VISUALIZER");

        if (BeginStatsTable())
        {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextDisabled("Renderer");

            ImGui::TableNextColumn();

            switch (gui::renderer)
            {
            case gui::VisualizerRenderer::BuiltInVisualizer:
                ImGui::Text("Built-in");
                break;

            case gui::VisualizerRenderer::OtherVisualizer:
                ImGui::Text("Window capture");
                break;

            default:
                ImGui::Text("Unknown");
                break;
            }

            Stat(
                "Visualizer",
                "%s",
                m_viewer ? "Loaded" : "Unavailable"
            );

            ImGui::TableNextRow();

            Stat(
                "Window capture",
                "%s",
                gui::windowCaptureRunning
                ? "Running"
                : "Stopped"
            );

            EndStatsTable();
        }

        // =====================================================
        // SYSTEM
        // =====================================================

        ImGui::SeparatorText("SYSTEM");

        if (BeginStatsTable())
        {
            ImGui::TableNextRow();

            //Stat(
            //    "CPU Usage",
            //    "%.2f%%",
            //    m_statistics.processCpuPercent
            //);

            Stat(
                "Process memory",
                "%llu MB",
                static_cast<unsigned long long>(
                    m_statistics.processMemoryMB
                    )
            );

        

            EndStatsTable();
        }
    }

    ImGui::End();
}


void PianoVisualizer::RenderCameraSettingsPanel()
{
    if (!m_showCameraSettings)
        return;

    ImGui::Begin(
        "Camera Settings",
        &m_showCameraSettings,
        ImGuiWindowFlags_AlwaysAutoResize
    );

    // =====================================================
    // Camera
    // =====================================================

    const std::vector<Camera::CameraDevice>& cameras =
        m_camera.GetAvailableCameras();

    if (!cameras.empty())
    {
        int selectedCamera =
            m_camera.GetCameraIndex();

        const char* preview =
            selectedCamera >= 0 &&
            selectedCamera <
            static_cast<int>(cameras.size())
            ? cameras[selectedCamera].name.c_str()
            : "Select camera";

        ImGui::Text(
            "Camera"
        );

        if (ImGui::BeginCombo(
            "##CameraDevice",
            preview
        ))
        {
            for (
                int i = 0;
                i < static_cast<int>(cameras.size());
                ++i
                )
            {
                const bool selected =
                    i == selectedCamera;

                if (ImGui::Selectable(
                    cameras[i].name.c_str(),
                    selected
                ))
                {
                    if (i != selectedCamera)
                    {
                        if (m_camera.OpenCamera(i))
                        {
                            m_cameraSettingsWidth = 0;
                            m_cameraSettingsHeight = 0;
                            m_cameraSettingsFPSNumerator = 0;
                            m_cameraSettingsFPSDenominator = 1;
                        }
                        else
                        {
                            const std::string cameraError =
                                m_camera.GetLastError();

                            if (!cameraError.empty())
                            {
                                const std::string message =
                                    "Error: Failed to initialize camera \"" +
                                    cameraError +
                                    "\"";

                                MessageBoxA(
                                    nullptr,
                                    message.c_str(),
                                    "Piano Visualizer",
                                    MB_OK | MB_ICONWARNING
                                );

                                m_camera.ClearLastError();
                            }
                        }
                    }
                }

                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }


        ImGui::SameLine();
    }

    if (ImGui::Button(
        "Refresh",
        ImVec2(-1.0f, 32.0f)
    ))
    {
        m_camera.EnumerateCameras();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // =====================================================
    // Camera not currently open
    // =====================================================

    if (!m_camera.IsOpen())
    {
        ImGui::TextDisabled(
            cameras.empty()
            ? "No camera available."
            : "Select a camera to continue."
        );


        ImGui::End();

        return;
    }

    // =====================================================
    // Format
    // =====================================================

    ImGui::Text(
        "Format"
    );

    Camera::CameraFormat currentFormat =
        m_camera.GetFormat();

    const char* formatName =
        currentFormat == Camera::CameraFormat::NV12
        ? "NV12"
        : currentFormat == Camera::CameraFormat::MJPG
        ? "MJPG"
        : "None";

    if (ImGui::BeginCombo(
        "##CameraFormat",
        formatName
    ))
    {
        bool isNV12 =
            currentFormat ==
            Camera::CameraFormat::NV12;

        if (ImGui::Selectable(
            "NV12",
            isNV12
        ))
        {
            if (!isNV12)
            {
                m_camera.SetFormat(
                    Camera::CameraFormat::NV12
                );

                m_cameraSettingsWidth = 0;
                m_cameraSettingsHeight = 0;
                m_cameraSettingsFPSNumerator = 0;
                m_cameraSettingsFPSDenominator = 1;
            }
        }

        if (isNV12)
        {
            ImGui::SetItemDefaultFocus();
        }

        bool isMJPG =
            currentFormat ==
            Camera::CameraFormat::MJPG;

        if (ImGui::Selectable(
            "MJPG",
            isMJPG
        ))
        {
            if (!isMJPG)
            {
                m_camera.SetFormat(
                    Camera::CameraFormat::MJPG
                );

                m_cameraSettingsWidth = 0;
                m_cameraSettingsHeight = 0;
                m_cameraSettingsFPSNumerator = 0;
                m_cameraSettingsFPSDenominator = 1;
            }
        }

        if (isMJPG)
        {
            ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // =====================================================
    // Resolution / FPS
    // =====================================================

    const std::vector<Camera::CameraMode>& modes =
        m_camera.GetAvailableModes();

    if (!modes.empty())
    {
        const int currentWidth =
            m_camera.GetWidth();

        const int currentHeight =
            m_camera.GetHeight();

        const Camera::CameraStatistics statistics =
            m_camera.GetStatistics();

        const UINT32 currentFPSNumerator =
            statistics.fpsNumerator;

        const UINT32 currentFPSDenominator =
            statistics.fpsDenominator;

        // -------------------------------------------------
        // Synchronize UI state with actual camera state
        // -------------------------------------------------

        if (
            m_cameraSettingsWidth != currentWidth ||
            m_cameraSettingsHeight != currentHeight
            )
        {
            m_cameraSettingsWidth =
                currentWidth;

            m_cameraSettingsHeight =
                currentHeight;
        }

        if (
            m_cameraSettingsFPSNumerator !=
            currentFPSNumerator ||
            m_cameraSettingsFPSDenominator !=
            currentFPSDenominator
            )
        {
            m_cameraSettingsFPSNumerator =
                currentFPSNumerator;

            m_cameraSettingsFPSDenominator =
                currentFPSDenominator;
        }

        const std::vector<Camera::CameraMode>& modes =
            m_camera.GetAvailableModes();

        if (!modes.empty())
        {
            const int currentWidth =
                m_camera.GetWidth();

            const int currentHeight =
                m_camera.GetHeight();

            const Camera::CameraStatistics statistics =
                m_camera.GetStatistics();

            const UINT32 currentFPSNumerator =
                statistics.fpsNumerator;

            const UINT32 currentFPSDenominator =
                statistics.fpsDenominator;

            // -------------------------------------------------
            // Synchronize UI state with actual camera state
            // -------------------------------------------------

            if (
                m_cameraSettingsWidth != currentWidth ||
                m_cameraSettingsHeight != currentHeight
                )
            {
                m_cameraSettingsWidth =
                    currentWidth;

                m_cameraSettingsHeight =
                    currentHeight;
            }

            if (
                m_cameraSettingsFPSNumerator !=
                currentFPSNumerator ||
                m_cameraSettingsFPSDenominator !=
                currentFPSDenominator
                )
            {
                m_cameraSettingsFPSNumerator =
                    currentFPSNumerator;

                m_cameraSettingsFPSDenominator =
                    currentFPSDenominator;
            }

            // -------------------------------------------------
            // Build unique resolutions
            // -------------------------------------------------

            std::vector<std::pair<int, int>> resolutions;

            for (const Camera::CameraMode& mode : modes)
            {
                bool alreadyExists = false;

                for (
                    const auto& resolution :
                    resolutions
                    )
                {
                    if (
                        resolution.first ==
                        mode.width &&
                        resolution.second ==
                        mode.height
                        )
                    {
                        alreadyExists = true;
                        break;
                    }
                }

                if (!alreadyExists)
                {
                    resolutions.emplace_back(
                        mode.width,
                        mode.height
                    );
                }
            }

            // -------------------------------------------------
            // Build unique FPS values
            // -------------------------------------------------

            std::vector<Camera::CameraMode> uniqueFPSModes;

            for (const Camera::CameraMode& mode : modes)
            {
                bool alreadyExists = false;

                for (
                    const Camera::CameraMode& existing :
                    uniqueFPSModes
                    )
                {
                    if (
                        static_cast<UINT64>(
                            mode.fpsNumerator
                            ) *
                        static_cast<UINT64>(
                            existing.fpsDenominator
                            ) ==
                        static_cast<UINT64>(
                            existing.fpsNumerator
                            ) *
                        static_cast<UINT64>(
                            mode.fpsDenominator
                            )
                        )
                    {
                        alreadyExists = true;
                        break;
                    }
                }

                if (!alreadyExists)
                {
                    uniqueFPSModes.push_back(
                        mode
                    );
                }
            }

            // -------------------------------------------------
            // Two-column layout
            // -------------------------------------------------

            ImGui::Columns(
                2,
                "##CameraResolutionFPS",
                false
            );

            // =================================================
            // Resolution
            // =================================================

            ImGui::Text(
                "Resolution"
            );

            int selectedResolutionIndex = -1;

            for (
                int i = 0;
                i < static_cast<int>(resolutions.size());
                ++i
                )
            {
                if (
                    resolutions[i].first ==
                    m_cameraSettingsWidth &&
                    resolutions[i].second ==
                    m_cameraSettingsHeight
                    )
                {
                    selectedResolutionIndex = i;
                    break;
                }
            }

            char resolutionPreview[64];

            if (selectedResolutionIndex >= 0)
            {
                std::snprintf(
                    resolutionPreview,
                    sizeof(resolutionPreview),
                    "%d x %d",
                    resolutions[
                        selectedResolutionIndex
                    ].first,
                    resolutions[
                        selectedResolutionIndex
                    ].second
                            );
            }
            else
            {
                std::snprintf(
                    resolutionPreview,
                    sizeof(resolutionPreview),
                    "%d x %d",
                    currentWidth,
                    currentHeight
                );
            }

            if (ImGui::BeginCombo(
                "##CameraResolution",
                resolutionPreview
            ))
            {
                for (
                    int i = 0;
                    i < static_cast<int>(resolutions.size());
                    ++i
                    )
                {
                    bool selected =
                        i == selectedResolutionIndex;

                    char label[64];

                    std::snprintf(
                        label,
                        sizeof(label),
                        "%d x %d",
                        resolutions[i].first,
                        resolutions[i].second
                    );

                    if (ImGui::Selectable(
                        label,
                        selected
                    ))
                    {
                        const int newWidth =
                            resolutions[i].first;

                        const int newHeight =
                            resolutions[i].second;

                        // -------------------------------------
                        // Keep current FPS if possible.
                        // -------------------------------------

                        bool currentFPSAvailable = false;

                        for (
                            const Camera::CameraMode& mode :
                            modes
                            )
                        {
                            if (
                                mode.width ==
                                newWidth &&
                                mode.height ==
                                newHeight &&
                                mode.fpsNumerator ==
                                m_cameraSettingsFPSNumerator &&
                                mode.fpsDenominator ==
                                m_cameraSettingsFPSDenominator
                                )
                            {
                                currentFPSAvailable = true;
                                break;
                            }
                        }

                        UINT32 newFPSNumerator =
                            m_cameraSettingsFPSNumerator;

                        UINT32 newFPSDenominator =
                            m_cameraSettingsFPSDenominator;

                        // -------------------------------------
                        // If unavailable, use highest FPS.
                        // -------------------------------------

                        if (!currentFPSAvailable)
                        {
                            bool foundMode = false;

                            Camera::CameraMode bestMode;

                            for (
                                const Camera::CameraMode& mode :
                                modes
                                )
                            {
                                if (
                                    mode.width != newWidth ||
                                    mode.height != newHeight
                                    )
                                {
                                    continue;
                                }

                                if (!foundMode)
                                {
                                    bestMode = mode;
                                    foundMode = true;
                                    continue;
                                }

                                if (
                                    static_cast<UINT64>(
                                        mode.fpsNumerator
                                        ) *
                                    static_cast<UINT64>(
                                        bestMode.fpsDenominator
                                        ) >
                                    static_cast<UINT64>(
                                        bestMode.fpsNumerator
                                        ) *
                                    static_cast<UINT64>(
                                        mode.fpsDenominator
                                        )
                                    )
                                {
                                    bestMode = mode;
                                }
                            }

                            if (foundMode)
                            {
                                newFPSNumerator =
                                    bestMode.fpsNumerator;

                                newFPSDenominator =
                                    bestMode.fpsDenominator;
                            }
                        }

                        if (
                            m_camera.SetMode(
                                newWidth,
                                newHeight,
                                newFPSNumerator,
                                newFPSDenominator
                            )
                            )
                        {
                            m_cameraSettingsWidth =
                                newWidth;

                            m_cameraSettingsHeight =
                                newHeight;

                            m_cameraSettingsFPSNumerator =
                                newFPSNumerator;

                            m_cameraSettingsFPSDenominator =
                                newFPSDenominator;
                        }
                    }

                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }

            // =================================================
            // FPS
            // =================================================

            ImGui::NextColumn();

            ImGui::Text(
                "Frame Rate"
            );

            const Camera::CameraMode* currentMode =
                nullptr;

            for (
                const Camera::CameraMode& mode :
                modes
                )
            {
                if (
                    mode.width ==
                    m_cameraSettingsWidth &&
                    mode.height ==
                    m_cameraSettingsHeight &&
                    mode.fpsNumerator ==
                    m_cameraSettingsFPSNumerator &&
                    mode.fpsDenominator ==
                    m_cameraSettingsFPSDenominator
                    )
                {
                    currentMode = &mode;
                    break;
                }
            }

            char fpsPreview[64];

            if (currentMode != nullptr)
            {
                std::snprintf(
                    fpsPreview,
                    sizeof(fpsPreview),
                    "%.3g FPS",
                    currentMode->GetFPS()
                );
            }
            else
            {
                std::snprintf(
                    fpsPreview,
                    sizeof(fpsPreview),
                    "Unknown"
                );
            }

            if (ImGui::BeginCombo(
                "##CameraFPS",
                fpsPreview
            ))
            {
                for (
                    const Camera::CameraMode& fpsMode :
                    uniqueFPSModes
                    )
                {
                    bool available = false;

                    Camera::CameraMode availableMode;

                    for (
                        const Camera::CameraMode& mode :
                        modes
                        )
                    {
                        if (
                            mode.width !=
                            m_cameraSettingsWidth ||
                            mode.height !=
                            m_cameraSettingsHeight
                            )
                        {
                            continue;
                        }

                        if (
                            static_cast<UINT64>(
                                mode.fpsNumerator
                                ) *
                            static_cast<UINT64>(
                                fpsMode.fpsDenominator
                                ) ==
                            static_cast<UINT64>(
                                fpsMode.fpsNumerator
                                ) *
                            static_cast<UINT64>(
                                mode.fpsDenominator
                                )
                            )
                        {
                            available = true;
                            availableMode = mode;
                            break;
                        }
                    }

                    bool selected =
                        fpsMode.fpsNumerator ==
                        m_cameraSettingsFPSNumerator &&
                        fpsMode.fpsDenominator ==
                        m_cameraSettingsFPSDenominator;

                    char label[64];

                    std::snprintf(
                        label,
                        sizeof(label),
                        "%.3g FPS",
                        fpsMode.GetFPS()
                    );

                    if (!available)
                    {
                        ImGui::BeginDisabled();
                    }

                    if (ImGui::Selectable(
                        label,
                        selected
                    ))
                    {
                        if (
                            m_camera.SetMode(
                                availableMode.width,
                                availableMode.height,
                                availableMode.fpsNumerator,
                                availableMode.fpsDenominator
                            )
                            )
                        {
                            m_cameraSettingsFPSNumerator =
                                availableMode.fpsNumerator;

                            m_cameraSettingsFPSDenominator =
                                availableMode.fpsDenominator;
                        }
                    }

                    if (!available)
                    {
                        ImGui::EndDisabled();
                    }

                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::Columns(
                1
            );
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // =====================================================
    // Exposure
    // =====================================================

    bool autoExposure = false;

    if (
        m_camera.Controls()
        .GetAutoExposure(
            autoExposure
        )
        )
    {
        if (
            ImGui::Checkbox(
                "Auto Exposure",
                &autoExposure
            )
            )
        {
            m_camera.Controls()
                .SetAutoExposure(
                    autoExposure
                );
        }

        if (!autoExposure)
        {
            int exposureMin = 0;
            int exposureMax = 0;
            int exposureStep = 1;

            if (
                m_camera.Controls()
                .GetExposureRange(
                    exposureMin,
                    exposureMax,
                    exposureStep
                )
                )
            {
                int exposure = 0;

                if (
                    m_camera.Controls()
                    .GetExposure(
                        exposure
                    )
                    )
                {
                    if (
                        ImGui::SliderInt(
                            "Exposure",
                            &exposure,
                            exposureMin,
                            exposureMax
                        )
                        )
                    {
                        if (exposureStep > 1)
                        {
                            exposure =
                                exposureMin +
                                (
                                    (exposure - exposureMin + exposureStep / 2) /
                                    exposureStep
                                    ) *
                                exposureStep;

                            exposure =
                                std::clamp(
                                    exposure,
                                    exposureMin,
                                    exposureMax
                                );
                        }

                        m_camera.Controls()
                            .SetExposure(
                                exposure
                            );
                    }
                }
            }
        }
    }

    // =====================================================
    // Low Light Compensation
    // =====================================================

    bool lowLightCompensation = false;

    if (
        m_camera.Controls()
        .GetLowLightCompensation(
            lowLightCompensation
        )
        )
    {
        if (
            ImGui::Checkbox(
                "Low Light Compensation",
                &lowLightCompensation
            )
            )
        {
            m_camera.Controls()
                .SetLowLightCompensation(
                    lowLightCompensation
                );
        }
    }

    // =====================================================
    // Focus
    // =====================================================

    bool autoFocus = false;

    if (
        m_camera.Controls()
        .GetAutoFocus(
            autoFocus
        )
        )
    {
        if (
            ImGui::Checkbox(
                "Auto Focus",
                &autoFocus
            )
            )
        {
            m_camera.Controls()
                .SetAutoFocus(
                    autoFocus
                );
        }

        if (!autoFocus)
        {
            int focusMin = 0;
            int focusMax = 0;
            int focusStep = 1;

            if (
                m_camera.Controls()
                .GetFocusRange(
                    focusMin,
                    focusMax,
                    focusStep
                )
                )
            {
                int focus = 0;

                if (
                    m_camera.Controls()
                    .GetFocus(
                        focus
                    )
                    )
                {
                    if (
                        ImGui::SliderInt(
                            "Focus",
                            &focus,
                            focusMin,
                            focusMax
                        )
                        )
                    {
                        if (focusStep > 1)
                        {
                            focus =
                                focusMin +
                                (
                                    (focus - focusMin + focusStep / 2) /
                                    focusStep
                                    ) *
                                focusStep;

                            focus =
                                std::clamp(
                                    focus,
                                    focusMin,
                                    focusMax
                                );
                        }

                        m_camera.Controls()
                            .SetFocus(focus);
                    }
                }
            }
        }
    }

    // =====================================================
    // White Balance
    // =====================================================

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("IMAGE");

    // ---------------------------------------------------------
    // Brightness
    // ---------------------------------------------------------

    {
        int minimum = 0;
        int maximum = 0;
        int step = 1;

        if (m_camera.Controls().GetBrightnessRange(
            minimum,
            maximum,
            step
        ))
        {
            int value = 0;

            if (m_camera.Controls().GetBrightness(value))
            {
                if (ImGui::SliderInt(
                    "Brightness",
                    &value,
                    minimum,
                    maximum
                ))
                {
                    if (step > 1)
                    {
                        value =
                            minimum +
                            ((value - minimum + step / 2) / step) * step;

                        value =
                            std::clamp(
                                value,
                                minimum,
                                maximum
                            );
                    }

                    m_camera.Controls().SetBrightness(
                        value
                    );
                }
            }
        }
    }


    // ---------------------------------------------------------
    // Contrast
    // ---------------------------------------------------------

    {
        int minimum = 0;
        int maximum = 0;
        int step = 1;

        if (m_camera.Controls().GetContrastRange(
            minimum,
            maximum,
            step
        ))
        {
            int value = 0;

            if (m_camera.Controls().GetContrast(value))
            {
                if (ImGui::SliderInt(
                    "Contrast",
                    &value,
                    minimum,
                    maximum
                ))
                {
                    if (step > 1)
                    {
                        value =
                            minimum +
                            ((value - minimum + step / 2) / step) * step;

                        value =
                            std::clamp(
                                value,
                                minimum,
                                maximum
                            );
                    }

                    m_camera.Controls().SetContrast(
                        value
                    );
                }
            }
        }
    }


    // ---------------------------------------------------------
    // Saturation
    // ---------------------------------------------------------

    {
        int minimum = 0;
        int maximum = 0;
        int step = 1;

        if (m_camera.Controls().GetSaturationRange(
            minimum,
            maximum,
            step
        ))
        {
            int value = 0;

            if (m_camera.Controls().GetSaturation(value))
            {
                if (ImGui::SliderInt(
                    "Saturation",
                    &value,
                    minimum,
                    maximum
                ))
                {
                    if (step > 1)
                    {
                        value =
                            minimum +
                            ((value - minimum + step / 2) / step) * step;

                        value =
                            std::clamp(
                                value,
                                minimum,
                                maximum
                            );
                    }

                    m_camera.Controls().SetSaturation(
                        value
                    );
                }
            }
        }
    }


    // ---------------------------------------------------------
    // Sharpness
    // ---------------------------------------------------------

    {
        int minimum = 0;
        int maximum = 0;
        int step = 1;

        if (m_camera.Controls().GetSharpnessRange(
            minimum,
            maximum,
            step
        ))
        {
            int value = 0;

            if (m_camera.Controls().GetSharpness(value))
            {
                if (ImGui::SliderInt(
                    "Sharpness",
                    &value,
                    minimum,
                    maximum
                ))
                {
                    if (step > 1)
                    {
                        value =
                            minimum +
                            ((value - minimum + step / 2) / step) * step;

                        value =
                            std::clamp(
                                value,
                                minimum,
                                maximum
                            );
                    }

                    m_camera.Controls().SetSharpness(
                        value
                    );
                }
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // =====================================================
    // Virtual Camera
    // =====================================================

    ImGui::Text(
        "Virtual Camera"
    );

    const bool virtualCameraAvailable =
        m_virtualCamera.IsAvailable();

    const bool virtualCameraActive =
        m_virtualCamera.IsActive();

    if (!virtualCameraAvailable)
    {
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha * 0.5f
        );
    }

    bool clicked =
        ImGui::Button(
            virtualCameraAvailable
            ? (virtualCameraActive
                ? "Stop Virtual Camera"
                : "Start Virtual Camera")
            : "Start Virtual Camera (Extension Required)"
        );

    if (!virtualCameraAvailable)
    {
        ImGui::PopStyleVar();
    }

    // ---------------------------------------------------------
    // Button action
    // ---------------------------------------------------------

    if (clicked)
    {
        if (virtualCameraAvailable)
        {
            if (!virtualCameraActive)
            {
                if (m_virtualCamera.RegisterExtension())
                {
                    if (!m_virtualCamera.StartVirtualCamera())
                    {
                        m_virtualCamera.UnregisterExtension();
                    }
                }
            }
            else
            {
                if (m_virtualCamera.StopVirtualCamera())
                {
                    m_virtualCamera.UnregisterExtension();
                }
            }
        }
        else
        {
            ShellExecuteA(
                nullptr,
                "open",
                "https://github.com/Ominus-tch/Piano-Visualizer",
                nullptr,
                nullptr,
                SW_SHOWNORMAL
            );
        }
    }

    // ---------------------------------------------------------
    // Tooltip
    // ---------------------------------------------------------

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(virtualCameraAvailable ? 
			"The virtual camera outputs the visualizer to other applications.\n"
			"Click to start or stop the virtual camera."
            :
            "Virtual Camera extension not found.\n"
            "Download the extension to enable this feature.\n"
            "Click to visit the download page."
        );
    }

    ImGui::End();
}

void PianoVisualizer::RenderAudioPanel()
{
    if (!m_showAudioSettings)
        return;

    ImGui::Begin(
        "Audio Settings",
        &m_showAudioSettings,
        ImGuiWindowFlags_AlwaysAutoResize
    );

    if (!m_audioEngine)
    {
        ImGui::TextDisabled(
            "Audio engine is not available."
        );

        ImGui::End();
        return;
    }

    audio::AudioOutput* output =
        m_audioEngine->output();

    if (!output)
    {
        ImGui::TextDisabled(
            "Audio output is not initialized."
        );

        ImGui::End();
        return;
    }

    // =========================================================
    // OUTPUT DEVICE
    // =========================================================

    ImGui::SeparatorText("Output");

    const auto devices =
        m_audioEngine->enumerateOutputDevices();

    const auto configuration =
        m_audioEngine->outputConfiguration();

    int selectedDevice = -1;

    for (int i = 0;
        i < static_cast<int>(devices.size());
        ++i)
    {
        if (devices[i].id == configuration.deviceId)
        {
            selectedDevice = i;
            break;
        }
    }

    // Empty device ID means Windows default device.
    if (configuration.deviceId.empty())
    {
        for (int i = 0;
            i < static_cast<int>(devices.size());
            ++i)
        {
            if (devices[i].isDefault)
            {
                selectedDevice = i;
                break;
            }
        }
    }

    std::string selectedDeviceName =
        "Default";

    if (selectedDevice >= 0 &&
        selectedDevice < static_cast<int>(devices.size()))
    {
        selectedDeviceName =
            std::string(
                devices[selectedDevice].name.begin(),
                devices[selectedDevice].name.end()
            );
    }

    if (ImGui::BeginCombo(
        "Device",
        selectedDeviceName.c_str()))
    {
        // -----------------------------------------------------
        // Default device
        // -----------------------------------------------------

        const bool isDefaultSelected =
            configuration.deviceId.empty();

        if (ImGui::Selectable(
            "Default",
            isDefaultSelected))
        {
            if (!isDefaultSelected)
            {
                m_audioEngine->setOutputDevice(
                    L""
                );
            }
        }

        if (isDefaultSelected)
            ImGui::SetItemDefaultFocus();

        // -----------------------------------------------------
        // Enumerated devices
        // -----------------------------------------------------

        for (int i = 0;
            i < static_cast<int>(devices.size());
            ++i)
        {
            const std::string deviceName(
                devices[i].name.begin(),
                devices[i].name.end()
            );

            const bool selected =
                selectedDevice == i;

            if (ImGui::Selectable(
                deviceName.c_str(),
                selected))
            {
                m_audioEngine->setOutputDevice(
                    devices[i].id
                );
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }

    // ---------------------------------------------------------
    // Current device information
    // ---------------------------------------------------------

    ImGui::Text(
        "Active device: %ls",
        output->deviceName().c_str()
    );

    ImGui::Text(
        "Channels: %d",
        output->channels()
    );

    ImGui::Text(
        "Sample rate: %.0f Hz",
        output->sampleRate()
    );


    // =========================================================
    // MODE
    // =========================================================

    ImGui::SeparatorText("Mode");

    const char* modeNames[] =
    {
        "Shared",
        "Exclusive"
    };

    int mode =
        configuration.mode ==
        audio::AudioOutput::Mode::Exclusive
        ? 1
        : 0;

    if (ImGui::Combo(
        "Mode",
        &mode,
        modeNames,
        IM_ARRAYSIZE(modeNames)))
    {
        const auto newMode =
            mode == 0
            ? audio::AudioOutput::Mode::Shared
            : audio::AudioOutput::Mode::Exclusive;

        if (newMode != configuration.mode)
        {
            m_audioEngine->setOutputMode(
                newMode
            );
        }
    }


    // =========================================================
    // SAMPLE RATE
    // =========================================================

    ImGui::SeparatorText("Format");

    const double currentSampleRate =
        configuration.sampleRate;

    int sampleRateIndex = 0;

    if (currentSampleRate == 0.0)
        sampleRateIndex = 0;
    else if (currentSampleRate == 44100.0)
        sampleRateIndex = 1;
    else if (currentSampleRate == 48000.0)
        sampleRateIndex = 2;
    else if (currentSampleRate == 88200.0)
        sampleRateIndex = 3;
    else if (currentSampleRate == 96000.0)
        sampleRateIndex = 4;
    else
        sampleRateIndex = 5;

    const char* sampleRateNames[] =
    {
        "Device default",
        "44.1 kHz",
        "48 kHz",
        "88.2 kHz",
        "96 kHz",
        "Custom"
    };

    if (ImGui::Combo(
        "Sample rate",
        &sampleRateIndex,
        sampleRateNames,
        IM_ARRAYSIZE(sampleRateNames)))
    {
        double newSampleRate = 0.0;

        switch (sampleRateIndex)
        {
        case 0:
            newSampleRate = 0.0;
            break;

        case 1:
            newSampleRate = 44100.0;
            break;

        case 2:
            newSampleRate = 48000.0;
            break;

        case 3:
            newSampleRate = 88200.0;
            break;

        case 4:
            newSampleRate = 96000.0;
            break;

        default:
            newSampleRate = currentSampleRate;
            break;
        }

        if (sampleRateIndex != 5 &&
            newSampleRate != currentSampleRate)
        {
            m_audioEngine->setOutputSampleRate(
                newSampleRate
            );
        }
    }

    // Custom sample rate
    if (sampleRateIndex == 5)
    {
        double customSampleRate =
            currentSampleRate > 0.0
            ? currentSampleRate
            : 48000.0;

        if (ImGui::InputDouble(
            "Custom sample rate",
            &customSampleRate,
            100.0,
            1000.0,
            "%.0f"))
        {
            if (customSampleRate >= 8000.0)
            {
                m_audioEngine->setOutputSampleRate(
                    customSampleRate
                );
            }
        }
    }


    // =========================================================
    // BUFFER
    // =========================================================

    double bufferDuration =
        configuration.bufferDurationMs;

    constexpr double minBufferDuration = 2.0;
    constexpr double maxBufferDuration = 100.0;

    if (ImGui::SliderScalar(
        "Buffer duration",
        ImGuiDataType_Double,
        &bufferDuration,
        &minBufferDuration,
        &maxBufferDuration,
        "%.1f ms"))
    {
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            m_audioEngine->setOutputBufferDuration(
                bufferDuration
            );
        }
    }

    ImGui::TextDisabled(
        "Lower values reduce latency but increase CPU usage."
    );


    // =========================================================
    // VOLUME
    // =========================================================

    ImGui::SeparatorText("Volume");

    float volume =
        m_audioEngine->outputVolume();

    float volumeNormalized = volume * 100.f;

    if (ImGui::SliderFloat(
        "Volume",
        &volumeNormalized,
        0.0f,
        100.0f,
        "%.0f%%",
        ImGuiSliderFlags_AlwaysClamp))
    {
        m_audioEngine->setOutputVolume(
            volumeNormalized / 100.f
        );
    }

    bool muted =
        m_audioEngine->outputMuted();

    if (ImGui::Checkbox(
        "Mute",
        &muted))
    {
        m_audioEngine->setOutputMuted(
            muted
        );
    }


    // =========================================================
    // DEVICE FALLBACK
    // =========================================================

    ImGui::SeparatorText("Behavior");

    bool fallback =
        configuration.fallbackToDefaultDevice;

    if (ImGui::Checkbox(
        "Fallback to default device",
        &fallback))
    {
        // This currently requires updating the whole
        // configuration because AudioOutput exposes the
        // setting through Configuration.
        //
        // We don't currently have a dedicated setter for it.
        auto newConfiguration =
            m_audioEngine->outputConfiguration();

        newConfiguration.fallbackToDefaultDevice =
            fallback;

        // The current AudioEngine API doesn't expose a generic
        // configuration setter, so this cannot be applied yet.
        //
        // Keep the UI here disabled until that setter exists.
    }

    ImGui::TextDisabled(
        "Used if the selected output device disappears."
    );


    // =========================================================
    // STATUS
    // =========================================================

    ImGui::SeparatorText("Status");

    ImGui::Text(
        "Output: %s",
        output->isRunning()
        ? "Running"
        : "Stopped"
    );

    ImGui::Text(
        "Engine: %s",
        m_audioEngine->isRunning()
        ? "Running"
        : "Stopped"
    );

    ImGui::End();
}


// =========================================================
// Shutdown
// =========================================================

void PianoVisualizer::Shutdown()
{
    if (!m_initialized)
        return;

    Logger::Log(
        "Shutting down Piano Visualizer...\n"
    );

    StopWindowCapture();

    m_vstDropTarget.Unregister();

    ShutdownAudio();

    ShutdownCamera();

    ShutdownMidiVisualizer();

    ShutdownRenderTarget();

    m_virtualCamera.Shutdown();

    m_initialized =
        false;

    Logger::Log(
        "Piano Visualizer shut down.\n"
    );
}


void PianoVisualizer::ShutdownAudio()
{
    if (!m_audioEngine)
        return;

    m_audioEngine->stop();

    m_audioEngine->shutdown();

    delete m_audioEngine;

    m_audioEngine =
        nullptr;
}


void PianoVisualizer::ShutdownCamera()
{
    m_cameraVideoPlayer.Shutdown();

    m_camera.Shutdown();
}


void PianoVisualizer::ShutdownMidiVisualizer()
{
    if (!m_viewer)
        return;

    delete m_viewer;

    m_viewer =
        nullptr;
}


void PianoVisualizer::ShutdownRenderTarget()
{
    CleanupRenderTarget();
}