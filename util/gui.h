#pragma once

#ifndef GUI_H
#define GUI_H

#include <imgui/imgui.h>

#include <Windows.h>

#include <filesystem>
#include <chrono>
#include <array>
#include <mutex>
#include <utility>
#include <vector>
#include <string>

namespace gui
{
    // =========================================================
    // ImGui / Application
    // =========================================================

    extern bool running;
    extern bool fullscreen;

    // =========================================================
    // Program
    // =========================================================

    extern int targetFrameRate;

    extern bool debug;
    extern const char* debugtext;
    extern FILE* file;

    extern bool bIsOpen;
    extern bool opening;
    extern bool closing;

    extern ImVec2 ImWindowNeededSize;
    extern ImVec2 ImWindowSize;
    extern ImVec2 ImWindowSizeCached;

    extern bool showSettings;
    extern bool showDebugLines;

    // =========================================================
    // Piano configuration
    // =========================================================

    extern bool choosingPoints;

    extern int pointsSelected;

    extern std::array<std::pair<int, int>, 4> selectedPoints;

    extern std::once_flag onceFlag;

    // =========================================================
    // Visualizer
    // =========================================================

    enum class VisualizerRenderer
    {
        BuiltInVisualizer,
        OtherVisualizer
    };

    extern VisualizerRenderer renderer;

    // =========================================================
    // Window capture
    // =========================================================

    extern HWND selectedCaptureWindow;

    extern int selectedCaptureWindowIndex;

    struct CaptureWindowEntry
    {
        HWND hwnd;
        std::wstring title;
    };

    extern std::vector<CaptureWindowEntry> captureWindows;

    extern bool windowCaptureRunning;

    extern HWND activeCaptureWindow;

    // =========================================================
    // File dialogs
    // =========================================================

    std::filesystem::path OpenFileDialog();

    // Opens the Windows folder picker. Kept separate from OpenFileDialog so
    // callers cannot accidentally persist a file path where a directory is
    // required.
    std::filesystem::path OpenFolderDialog();
}

#endif
