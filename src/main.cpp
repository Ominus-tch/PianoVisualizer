#define _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
#define _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS

#include <d3d11.h>

#include <iostream>
#include <Windows.h>
#include <Shellapi.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_internal.h>

#include <atomic>
#include <cstdio>

#include "../util/Logger.h"

#include "../util/helpers.h"
#include "../util/config.h"

#include "PianoVisualizer.h"

// Icon
#include "../resources/resource1.h"


// =========================================================
// Forward declarations
// =========================================================

void DebugSetup();
void DebugEnd();

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);

LRESULT CALLBACK window_procedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
);

// =========================================================
// Globals used by Win32 callbacks
// =========================================================

static PianoVisualizer* gPianoVisualizer = nullptr;

static HWND gWindow = nullptr;
static WNDCLASSEX gWndclass;

// =========================================================
// Console handler
// =========================================================

BOOL WINAPI ConsoleHandler(
    DWORD signal
)
{
    switch (signal)
    {
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
    {
        if (gWindow)
        {
            PostMessage(
                gWindow,
                WM_CLOSE,
                0,
                0
            );
        }

        return TRUE;
    }

    default:
        break;
    }

    return FALSE;
}

static int RunMessageLoop()
{
    MSG msg{};

    while (PeekMessage(
        &msg,
        nullptr,
        0U,
        0U,
        PM_REMOVE))
    {

        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT)
            return -1;
    }

    return 0;
}


// =========================================================
// WinMain
// =========================================================

INT APIENTRY WinMain(
    HINSTANCE instance,
    HINSTANCE,
    PSTR,
    INT cmd_show
)
{
    // =========================================================
    // Debug
    // =========================================================

    DebugSetup();


    // =========================================================
    // Logger
    // =========================================================

    if (!Logger::Init())
    {
        AllocConsole();

        freopen_s(
            &gui::file,
            "CONOUT$",
            "w",
            stdout
        );

        std::cout
            << "Unable to initialize logger!\n";

        system("pause");

        fclose(gui::file);
        FreeConsole();

        return 1;
    }

    Logger::Log(
        "[main] Logger initialized!\n"
    );


    // =========================================================
    // OLE
    // =========================================================

    HRESULT oleResult =
        OleInitialize(nullptr);

    if (FAILED(oleResult))
    {
        Logger::Log(
            "[main] Failed to initialize OLE!\n"
        );

        Logger::Remove();

        if (gui::debug)
            DebugEnd();

        return 1;
    }


    // =========================================================
    // Window class
    // =========================================================

    gWndclass.cbSize =
        sizeof(WNDCLASSEX);

    gWndclass.style =
        CS_HREDRAW |
        CS_VREDRAW;

    gWndclass.lpfnWndProc =
        window_procedure;

    gWndclass.hInstance =
        instance;

    gWndclass.hIcon =
        LoadIcon(
            instance,
            MAKEINTRESOURCE(IDI_ICON1)
        );

    gWndclass.hIconSm =
        LoadIcon(
            instance,
            MAKEINTRESOURCE(IDI_ICON1)
        );

    gWndclass.lpszClassName =
        "Overlay class";


    if (!RegisterClassEx(&gWndclass))
    {
        Logger::Log(
            "[main] Failed to register window class!\n"
        );

        OleUninitialize();
        Logger::Remove();

        if (gui::debug)
            DebugEnd();

        return 1;
    }


    // =========================================================
    // Create window
    // =========================================================

    const HWND window =
        CreateWindowEx(
            0,
            gWndclass.lpszClassName,
            "Piano Visualizer",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1280,
            720,
            nullptr,
            nullptr,
            gWndclass.hInstance,
            nullptr
        );

    if (!window)
    {
        Logger::Log(
            "[main] Failed to create window!\n"
        );

        UnregisterClass(
            gWndclass.lpszClassName,
            gWndclass.hInstance
        );

        OleUninitialize();
        Logger::Remove();

        if (gui::debug)
            DebugEnd();

        return 1;
    }

    gWindow = window;


    // =========================================================
    // D3D11
    // =========================================================

    DXGI_SWAP_CHAIN_DESC swapChainDesc{};

    swapChainDesc.BufferDesc.RefreshRate.Numerator =
        60U;

    swapChainDesc.BufferDesc.RefreshRate.Denominator =
        1U;

    swapChainDesc.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;

    swapChainDesc.SampleDesc.Count =
        1U;

    swapChainDesc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;

    swapChainDesc.BufferCount =
        2U;

    swapChainDesc.OutputWindow =
        window;

    swapChainDesc.Windowed =
        TRUE;

    swapChainDesc.SwapEffect =
        DXGI_SWAP_EFFECT_DISCARD;

    swapChainDesc.Flags =
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;


    constexpr D3D_FEATURE_LEVEL featureLevels[2]
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };


    ID3D11Device* device =
        nullptr;

    ID3D11DeviceContext* context =
        nullptr;

    IDXGISwapChain* swapChain =
        nullptr;

    ID3D11RenderTargetView* renderTargetView =
        nullptr;

    D3D_FEATURE_LEVEL featureLevel{};


    UINT deviceFlags =
        D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    deviceFlags |=
        D3D11_CREATE_DEVICE_DEBUG;

    HRESULT hr =
        D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            deviceFlags,
            featureLevels,
            2U,
            D3D11_SDK_VERSION,
            &swapChainDesc,
            &swapChain,
            &device,
            &featureLevel,
            &context
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[main] Failed to create D3D11 device and swap chain!\n"
        );

        DestroyWindow(window);

        UnregisterClass(
            gWndclass.lpszClassName,
            gWndclass.hInstance
        );

        OleUninitialize();
        Logger::Remove();

        if (gui::debug)
            DebugEnd();

        return 1;
    }


    // =========================================================
    // Create render target
    // =========================================================

    ID3D11Texture2D* backBuffer =
        nullptr;

    hr =
        swapChain->GetBuffer(
            0U,
            IID_PPV_ARGS(&backBuffer)
        );


    if (FAILED(hr) || !backBuffer)
    {
        Logger::Log(
            "[main] Failed to acquire back buffer!\n"
        );

        if (swapChain)
            swapChain->Release();

        if (context)
            context->Release();

        if (device)
            device->Release();

        DestroyWindow(window);

        UnregisterClass(
            gWndclass.lpszClassName,
            gWndclass.hInstance
        );

        OleUninitialize();
        Logger::Remove();

        if (gui::debug)
            DebugEnd();

        return 1;
    }


    hr =
        device->CreateRenderTargetView(
            backBuffer,
            nullptr,
            &renderTargetView
        );

    backBuffer->Release();


    if (FAILED(hr) || !renderTargetView)
    {
        Logger::Log(
            "[main] Failed to create render target view!\n"
        );

        swapChain->Release();
        context->Release();
        device->Release();

        DestroyWindow(window);

        UnregisterClass(
            gWndclass.lpszClassName,
            gWndclass.hInstance
        );

        OleUninitialize();
        Logger::Remove();

        if (gui::debug)
            DebugEnd();

        return 1;
    }


    Logger::Log(
        "[main] D3D11 initialized!\n"
    );


    // =========================================================
    // Show window
    // =========================================================

    ShowWindow(
        window,
        cmd_show
    );

    UpdateWindow(window);

    Logger::Log(
        "[main] Window created!\n"
    );


    // =========================================================
    // ImGui
    // =========================================================

    Logger::Log(
        "[main] Setting up ImGui...\n"
    );

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io =
        ImGui::GetIO();

    io.ConfigFlags |=
        ImGuiConfigFlags_DockingEnable;

    io.IniFilename =
        nullptr;


    ImFontConfig font;

    ImGui::configureFont(
        font
    );

    ImGui::configureStyle();


    if (!ImGui_ImplWin32_Init(window))
    {
        Logger::Log(
            "[main] Failed to initialize ImGui Win32 backend!\n"
        );

        ImGui::DestroyContext();

        renderTargetView->Release();
        swapChain->Release();
        context->Release();
        device->Release();

        DestroyWindow(window);

        UnregisterClass(
            gWndclass.lpszClassName,
            gWndclass.hInstance
        );

        OleUninitialize();
        Logger::Remove();

        if (gui::debug)
            DebugEnd();

        return 1;
    }


    if (!ImGui_ImplDX11_Init(
        device,
        context))
    {
        Logger::Log(
            "[main] Failed to initialize ImGui DX11 backend!\n"
        );

        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        renderTargetView->Release();
        swapChain->Release();
        context->Release();
        device->Release();

        DestroyWindow(window);

        UnregisterClass(
            gWndclass.lpszClassName,
            gWndclass.hInstance
        );

        OleUninitialize();
        Logger::Remove();

        if (gui::debug)
            DebugEnd();

        return 1;
    }


    Logger::Log(
        "[main] ImGui initialized!\n"
    );


    // =========================================================
    // PianoVisualizer
    // =========================================================

    PianoVisualizer pianoVisualizer;

    gPianoVisualizer =
        &pianoVisualizer;


    if (!pianoVisualizer.Initialize(
        instance,
        window,
        device,
        context,
        swapChain,
        renderTargetView
    ))
    {
        Logger::Log(
            "[main] Failed to initialize PianoVisualizer!\n"
        );

        pianoVisualizer.Shutdown();

        gPianoVisualizer =
            nullptr;

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();

        ImGui::DestroyContext();

        renderTargetView->Release();
        swapChain->Release();
        context->Release();
        device->Release();

        DestroyWindow(window);

        UnregisterClass(
            gWndclass.lpszClassName,
            gWndclass.hInstance
        );

        OleUninitialize();
        Logger::Remove();

        if (gui::debug)
            DebugEnd();

        return 1;
    }

    // =========================================================
    // Console shutdown handler
    // =========================================================

    SetConsoleCtrlHandler(
        ConsoleHandler,
        TRUE
    );


    // =========================================================
    // Main loop
    // =========================================================

    Logger::Log(
        "[main] Loop starting...\n"
    );


    while (gui::running)
    {
        // -----------------------------------------------------
        // Windows messages
        // -----------------------------------------------------

        const int result = RunMessageLoop();

        if (result != 0)
        {
            gui::running = false;
        }


        if (!gui::running)
            break;

        // -----------------------------------------------------
        // ImGui frame
        // -----------------------------------------------------

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();

        ImGui::NewFrame();


        // -----------------------------------------------------
        // PianoVisualizer update
        // -----------------------------------------------------

        pianoVisualizer.Update();


        // -----------------------------------------------------
        // Rendering
        // -----------------------------------------------------

        pianoVisualizer.Render();


        // -----------------------------------------------------
        // ImGui rendering
        // -----------------------------------------------------

        ImGui::Render();

        ImGui_ImplDX11_RenderDrawData(
            ImGui::GetDrawData()
        );


        // -----------------------------------------------------
        // Present
        // -----------------------------------------------------

        HRESULT presentResult =
            swapChain->Present(
                1U,
                0U
            );


        if (FAILED(presentResult))
        {
            if (presentResult ==
                DXGI_ERROR_DEVICE_REMOVED ||
                presentResult ==
                DXGI_ERROR_DEVICE_RESET)
            {
                Logger::Log(
                    "D3D11 device was removed or reset!\n"
                );

                gui::running = false;
            }
        }
    }


    // =========================================================
    // Shutdown
    // =========================================================

    Logger::Log(
        "\n"
    );

    Logger::Log(
        "Exit procedure...\n"
    );


    SetConsoleCtrlHandler(
        ConsoleHandler,
        FALSE
    );


    // =========================================================
    // PianoVisualizer
    // =========================================================

    Logger::Log(
        "Shutting down PianoVisualizer...\n"
    );

    pianoVisualizer.Shutdown();

    gPianoVisualizer =
        nullptr;


    // =========================================================
    // ImGui
    // =========================================================

    Logger::Log(
        "Shutting down ImGui...\n"
    );

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();

    ImGui::DestroyContext();


    // =========================================================
    // D3D11
    // =========================================================

    Logger::Log(
        "Shutting down D3D11...\n"
    );

    //if (renderTargetView)
    //{
    //    renderTargetView->Release();
    //    renderTargetView = nullptr;
    //}

    if (swapChain)
    {
        swapChain->Release();
        swapChain = nullptr;
    }

    if (context)
    {
        context->Release();
        context = nullptr;
    }

    if (device)
    {
        device->Release();
        device = nullptr;
    }


    // =========================================================
    // Win32
    // =========================================================

    Logger::Log(
        "Destroying window...\n"
    );

    DestroyWindow(window);

    UnregisterClass(
        gWndclass.lpszClassName,
        gWndclass.hInstance
    );

    gWindow =
        nullptr;


    // =========================================================
    // OLE
    // =========================================================

    OleUninitialize();


    // =========================================================
    // Logger
    // =========================================================

    Logger::Log(
        "Exiting...\n"
    );

    Logger::Remove();


    // =========================================================
    // Debug
    // =========================================================

    if (gui::debug)
        DebugEnd();


    return 0;
}


// =========================================================
// Debug
// =========================================================

void DebugSetup()
{
    gui::debug = true;

    AllocConsole();

    freopen_s(
        &gui::file,
        "CONOUT$",
        "w",
        stdout
    );

    SetConsoleCtrlHandler(
        ConsoleHandler,
        TRUE
    );

    std::cout
        << "Debug Mode Started!\n";
}


void DebugEnd()
{
    SetConsoleCtrlHandler(
        ConsoleHandler,
        FALSE
    );

    gui::debug = false;

    if (gui::file)
    {
        fclose(gui::file);
        gui::file = nullptr;
    }

    FreeConsole();
}


// =========================================================
// Window procedure
// =========================================================

LRESULT CALLBACK window_procedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
)
{

    const bool imguiHandled =
        ImGui_ImplWin32_WndProcHandler(
            window,
            message,
            wParam,
            lParam
        );

    if (imguiHandled)
    {
        return 0;
    }

    switch (message)
    {
    case WM_SIZE:
    {
        if (gPianoVisualizer &&
            wParam != SIZE_MINIMIZED)
        {
            const UINT width =
                static_cast<UINT>(LOWORD(lParam));

            const UINT height =
                static_cast<UINT>(HIWORD(lParam));

            gPianoVisualizer->Resize(
                width,
                height
            );
        }

        return 0;
    }

    case WM_KEYDOWN:
    {
        if (gPianoVisualizer)
        {
            gPianoVisualizer->KeyPressed(
                static_cast<int>(wParam),
                1
            );
        }

        break;
    }

    case WM_DESTROY:
    {
        PostQuitMessage(EXIT_SUCCESS);
        return EXIT_SUCCESS;
    }
    }

    LRESULT result = DefWindowProc(
        window,
        message,
        wParam,
        lParam);

    return result;
}