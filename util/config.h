#pragma once

#include <Windows.h>
#include <ShlObj.h>

#include <imgui/imgui.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <vector>
#include <string>

#include "Logger.h"

using json = nlohmann::json;

// ============================================================
// PIANO CONFIGURATION
// ============================================================

namespace Config {

    // ============================================================
    // CONFIG DIRECTORY
    // ============================================================

    inline std::filesystem::path GetConfigDirectory()
    {
        PWSTR appDataPath = nullptr;

        HRESULT result = SHGetKnownFolderPath(
            FOLDERID_RoamingAppData,
            0,
            nullptr,
            &appDataPath
        );

        if (FAILED(result) || !appDataPath)
            return {};

        std::filesystem::path configDirectory =
            std::filesystem::path(appDataPath) /
            "PianoVisualizer";

        CoTaskMemFree(appDataPath);

        std::error_code error;

        std::filesystem::create_directories(
            configDirectory,
            error
        );

        if (error)
            return {};

        return configDirectory;
    }

    // ============================================================
    // CONFIG FILE
    // ============================================================

    inline std::filesystem::path GetPianoConfigPath()
    {
        const auto directory = GetConfigDirectory();

        if (directory.empty())
            return {};

        return directory / "piano_config.json";
    }

    inline std::filesystem::path GetVisualizerConfigPath()
    {
        const auto directory = GetConfigDirectory();

        if (directory.empty())
            return {};

        std::error_code error;
        std::filesystem::path presetsDirectory = directory / "presets";

        std::filesystem::create_directories(
            presetsDirectory,
            error
        );

        if (error)
            return {};

        return presetsDirectory;
    }

    inline std::filesystem::path GetVisualizerConfigurationPath()
    {
        const auto directory = GetConfigDirectory();

        if (directory.empty())
            return {};

        return directory / "visualizer_configuration.settings";
    }

    inline std::filesystem::path GetVSTConfigPath()
    {
        const auto directory = GetConfigDirectory();

        if (directory.empty())
            return {};

        return directory / "vst_config.json";
    }

    inline std::filesystem::path GetVSTStateDirectory()
    {
        const auto directory =
            GetConfigDirectory();

        if (directory.empty())
            return {};

        const auto stateDirectory =
            directory / "vst_states";

        std::error_code error;

        std::filesystem::create_directories(
            stateDirectory,
            error
        );

        if (error)
            return {};

        return stateDirectory;
    }

    inline std::filesystem::path GetVSTStatePath(
        const std::string& pluginName)
    {
        const auto directory =
            GetVSTStateDirectory();

        if (directory.empty())
            return {};

        return directory /
            (pluginName + ".state");
    }

    inline bool SaveVSTConfig(
        std::filesystem::path currentPluginPath,
        std::filesystem::path vst3folderPath,
        const std::vector<std::filesystem::path>& recentPlugins
    )
    {
        const auto configPath =
            GetVSTConfigPath();

        if (configPath.empty())
            return false;

        json config;

        config["currentPluginPath"] =
            currentPluginPath.string();

        config["vst3folderPath"] =
            vst3folderPath.string();

        config["recentPlugins"] =
            json::array();

        for (const auto& plugin : recentPlugins)
        {
            if (!plugin.empty())
            {
                config["recentPlugins"].push_back(
                    plugin.string()
                );
            }
        }

        std::ofstream file(configPath);

        if (!file.is_open())
            return false;

        file << config.dump(4);

        return true;
    }

    inline bool LoadVSTConfig(
        std::filesystem::path& currentPluginPath,
        std::filesystem::path& vst3folderPath,
        std::vector<std::filesystem::path>& recentPlugins
    )
    {
        const auto configPath =
            GetVSTConfigPath();

        if (configPath.empty())
            return false;

        std::ifstream file(configPath);

        if (!file.is_open())
            return false;

        try
        {
            json config;
            file >> config;

            if (config.contains("currentPluginPath"))
            {
                currentPluginPath =
                    config["currentPluginPath"].get<std::string>();
            }

            if (config.contains("vst3folderPath"))
            {
                vst3folderPath =
                    config["vst3folderPath"].get<std::string>();
            }

            recentPlugins.clear();

            if (config.contains("recentPlugins") &&
                config["recentPlugins"].is_array())
            {
                for (const auto& plugin :
                    config["recentPlugins"])
                {
                    if (plugin.is_string())
                    {
                        recentPlugins.emplace_back(
                            plugin.get<std::string>()
                        );
                    }
                }
            }

            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    // ============================================================
    // SAVE PIANO CONFIGURATION
    // ============================================================

    inline bool SavePianoConfig(
        const std::vector<ImVec2>& polygonPoints,
        float horizontalFovDegrees,
        float planeWidth,
        float planeDepth,
        float surfaceXOffset,
        float surfaceYOffset,
        float surfaceZOffset
    )
    {
        if (polygonPoints.size() != 4)
            return false;

        const auto configPath = GetPianoConfigPath();

        if (configPath.empty())
            return false;

        json config;

        // --------------------------------------------------------
        // Selected polygon points
        // --------------------------------------------------------

        config["points"]["P1"]["x"] =
            polygonPoints[0].x;

        config["points"]["P1"]["y"] =
            polygonPoints[0].y;

        config["points"]["P2"]["x"] =
            polygonPoints[1].x;

        config["points"]["P2"]["y"] =
            polygonPoints[1].y;

        config["points"]["P3"]["x"] =
            polygonPoints[2].x;

        config["points"]["P3"]["y"] =
            polygonPoints[2].y;

        config["points"]["P4"]["x"] =
            polygonPoints[3].x;

        config["points"]["P4"]["y"] =
            polygonPoints[3].y;

        // --------------------------------------------------------
        // Camera / plane settings
        // --------------------------------------------------------

        config["settings"]["horizontalFovDegrees"] =
            horizontalFovDegrees;

        config["settings"]["planeWidth"] =
            planeWidth;

        config["settings"]["planeDepth"] =
            planeDepth;

        config["settings"]["surfaceXOffset"] =
            surfaceXOffset;

        config["settings"]["surfaceYOffset"] =
            surfaceYOffset;

        config["settings"]["surfaceZOffset"] =
            surfaceZOffset;

        // --------------------------------------------------------
        // Save
        // --------------------------------------------------------

        std::ofstream file(configPath);

        if (!file.is_open())
            return false;

        file << config.dump(4);

        file.close();

        return true;
    }

    // ============================================================
    // LOAD PIANO CONFIGURATION
    // ============================================================

    inline bool LoadPianoConfig(
        std::vector<ImVec2>& polygonPoints,
        float& horizontalFovDegrees,
        float& planeWidth,
        float& planeDepth,
        float& surfaceXOffset,
        float& surfaceYOffset,
        float& surfaceZOffset
    )
    {
        const auto configPath = GetPianoConfigPath();

        if (configPath.empty())
            return false;

        std::ifstream file(configPath);

        if (!file.is_open())
            return false;

        try
        {
            json config;
            file >> config;

            // ----------------------------------------------------
            // Make sure all points exist
            // ----------------------------------------------------

            if (!config.contains("points"))
                return false;

            if (!config["points"].contains("P1") ||
                !config["points"].contains("P2") ||
                !config["points"].contains("P3") ||
                !config["points"].contains("P4"))
            {
                return false;
            }

            // ----------------------------------------------------
            // Load points
            // ----------------------------------------------------

            polygonPoints.resize(4);

            polygonPoints[0] = ImVec2(
                config["points"]["P1"]["x"].get<float>(),
                config["points"]["P1"]["y"].get<float>()
            );

            polygonPoints[1] = ImVec2(
                config["points"]["P2"]["x"].get<float>(),
                config["points"]["P2"]["y"].get<float>()
            );

            polygonPoints[2] = ImVec2(
                config["points"]["P3"]["x"].get<float>(),
                config["points"]["P3"]["y"].get<float>()
            );

            polygonPoints[3] = ImVec2(
                config["points"]["P4"]["x"].get<float>(),
                config["points"]["P4"]["y"].get<float>()
            );

            // ----------------------------------------------------
            // Load settings
            // ----------------------------------------------------

            if (config.contains("settings"))
            {
                auto& settings =
                    config["settings"];

                if (settings.contains(
                    "horizontalFovDegrees"))
                {
                    horizontalFovDegrees =
                        settings["horizontalFovDegrees"]
                        .get<float>();
                }

                if (settings.contains("planeWidth"))
                {
                    planeWidth =
                        settings["planeWidth"]
                        .get<float>();
                }

                if (settings.contains("planeDepth"))
                {
                    planeDepth =
                        settings["planeDepth"]
                        .get<float>();
                }

                if (settings.contains("surfaceXOffset"))
                {
                    surfaceXOffset =
                        settings["surfaceXOffset"]
                        .get<float>();
                }

                if (settings.contains("surfaceYOffset"))
                {
                    surfaceYOffset =
                        settings["surfaceYOffset"]
                        .get<float>();
                }

                if (settings.contains("surfaceZOffset"))
                {
                    surfaceZOffset =
                        settings["surfaceZOffset"]
                        .get<float>();
                }
            }

            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
}
