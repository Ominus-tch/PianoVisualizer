#include "gui.h"

#include <shobjidl.h>

namespace gui
{
    // =========================================================
    // ImGui / Application
    // =========================================================

    bool running = true;
    bool fullscreen = false;

    // =========================================================
    // Program
    // =========================================================

    int targetFrameRate = 240;

    bool debug = false;
    const char* debugtext = "";
    FILE* file = nullptr;

    bool bIsOpen = true;
    bool opening = true;
    bool closing = false;

    ImVec2 ImWindowNeededSize{};
    ImVec2 ImWindowSize{};
    ImVec2 ImWindowSizeCached{};

    bool showSettings = true;
    bool showDebugLines = true;

    // =========================================================
    // Piano configuration
    // =========================================================

    bool choosingPoints = false;

    int pointsSelected = 0;

    std::array<std::pair<int, int>, 4> selectedPoints{};

    std::once_flag onceFlag;

    // =========================================================
    // Visualizer
    // =========================================================

    VisualizerRenderer renderer =
        VisualizerRenderer::BuiltInVisualizer;

    // =========================================================
    // Window capture
    // =========================================================

    HWND selectedCaptureWindow = nullptr;

    int selectedCaptureWindowIndex = -1;

    std::vector<CaptureWindowEntry> captureWindows;

    bool windowCaptureRunning = false;

    HWND activeCaptureWindow = nullptr;

    // =========================================================
    // File dialogs
    // =========================================================

    std::filesystem::path OpenFileDialog()
    {
        IFileOpenDialog* dialog = nullptr;

        HRESULT hr = CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&dialog)
        );

        if (FAILED(hr))
            return {};

        hr = dialog->Show(nullptr);

        if (FAILED(hr))
        {
            dialog->Release();
            return {};
        }

        IShellItem* item = nullptr;

        hr = dialog->GetResult(&item);

        if (FAILED(hr))
        {
            dialog->Release();
            return {};
        }

        PWSTR filePath = nullptr;

        hr = item->GetDisplayName(
            SIGDN_FILESYSPATH,
            &filePath
        );

        std::filesystem::path result;

        if (SUCCEEDED(hr))
        {
            result = filePath;
            CoTaskMemFree(filePath);
        }

        item->Release();
        dialog->Release();

        return result;
    }

    std::filesystem::path OpenFolderDialog()
    {
        IFileOpenDialog* dialog = nullptr;

        HRESULT hr = CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&dialog)
        );

        if (FAILED(hr))
            return {};

        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options)))
        {
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }

        hr = dialog->Show(nullptr);
        if (FAILED(hr))
        {
            dialog->Release();
            return {};
        }

        IShellItem* item = nullptr;
        hr = dialog->GetResult(&item);
        if (FAILED(hr))
        {
            dialog->Release();
            return {};
        }

        PWSTR folderPath = nullptr;
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &folderPath);

        std::filesystem::path result;
        if (SUCCEEDED(hr) && folderPath)
        {
            result = folderPath;
            CoTaskMemFree(folderPath);
        }

        item->Release();
        dialog->Release();
        return result;
    }
}
