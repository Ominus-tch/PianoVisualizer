#pragma once

#include <Windows.h>
#include <ShlObj.h>

#include <imgui/imgui.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <cmath>
#include <vector>
#include <string>

#include "Logger.h"

using json = nlohmann::json;

// ============================================================
// PIANO CONFIGURATION
// ============================================================

namespace Config {

    inline constexpr int APPLICATION_CONFIG_VERSION = 1;

    inline bool WriteJsonAtomically(
        const std::filesystem::path& configPath,
        const json& config)
    {
        if (configPath.empty())
            return false;

        std::filesystem::path temporaryPath = configPath;
        temporaryPath += ".tmp";

        {
            std::ofstream file(temporaryPath, std::ios::trunc);

            if (!file.is_open())
                return false;

            file << config.dump(4);

            if (!file.good())
                return false;
        }

        std::error_code error;
        std::filesystem::rename(temporaryPath, configPath, error);

        if (!error)
            return true;

        // Windows does not replace an existing file with rename(). Use the
        // native replace operation so a valid previous configuration remains
        // intact until the new file has been written successfully.
        if (MoveFileExW(
            temporaryPath.c_str(),
            configPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            return true;
        }

        Logger::Log(
            "Failed to replace configuration file: %s\n",
            configPath.string().c_str());

        return false;
    }

    // ============================================================
    // CONFIG DIRECTORY
    // ============================================================

    inline std::filesystem::path GetConfigDirectory()
    {
        static std::filesystem::path path;

        if (!path.empty())
            return path;

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

        path = configDirectory;
        return path;
    }

    // ============================================================
    // CONFIG FILE
    // ============================================================

    inline std::filesystem::path GetPianoConfigPath()
    {
        static std::filesystem::path path;

        if (!path.empty())
            return path;

        const auto directory = GetConfigDirectory();

        if (directory.empty())
            return {};

        path = directory / "piano_config.json";
        return path;
    }

    inline std::filesystem::path GetVisualizerConfigPath()
    {
        static std::filesystem::path path;

        if (!path.empty())
            return path;

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

        path = presetsDirectory;
        return path;
    }

    inline std::filesystem::path GetVisualizerConfigurationPath()
    {
        static std::filesystem::path path;

        if (!path.empty())
            return path;

        const auto directory = GetConfigDirectory();

        if (directory.empty())
            return {};

        path = directory / "visualizer_configuration.settings";
        return path;
    }

    inline std::filesystem::path GetVSTConfigPath()
    {
        static std::filesystem::path path;

        if (!path.empty())
            return path;

        const auto directory = GetConfigDirectory();

        if (directory.empty())
            return {};

        path = directory / "vst_config.json";
        return path;
    }

    inline std::filesystem::path GetVSTStateDirectory()
    {
        static std::filesystem::path path;

        if (!path.empty())
            return path;

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

        path = stateDirectory;
        return path;
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

        config["version"] = APPLICATION_CONFIG_VERSION;

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

        return WriteJsonAtomically(configPath, config);
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


    inline bool SavePianoConfigToPath(
        const std::filesystem::path& configPath,
        const std::vector<ImVec2>& polygonPoints,
        int scene,

        // Perspective Scene
        float planePivot,

        // PianoRoll Scene
        float pianoRollVisualizerHeight,
        float pianoRollCameraSourceScale
    )
    {
        if (configPath.empty())
            return false;

        json config;

        config["version"] = APPLICATION_CONFIG_VERSION;

        // --------------------------------------------------------
        // Selected polygon points
        // --------------------------------------------------------

        if (polygonPoints.size() == 4) {

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

        }

        // --------------------------------------------------------
        // Camera / plane settings
        // --------------------------------------------------------

        config["settings"]["scene"] =
            scene;

        config["settings"]["planePivot"] =
            planePivot;

        config["settings"]["pianoRollVisualizerHeight"] =
            pianoRollVisualizerHeight;

        config["settings"]["pianoRollCameraSourceScale"] =
            pianoRollCameraSourceScale;

        // --------------------------------------------------------
        // Save
        // --------------------------------------------------------

        return WriteJsonAtomically(configPath, config);
    }

    inline bool SavePianoConfig(
        const std::vector<ImVec2>& polygonPoints,
        int scene,

        // Perspective Scene
        float planePivot,

        // PianoRoll Scene
        float pianoRollVisualizerHeight,
        float pianoRollCameraSourceScale
    )
    {
        return SavePianoConfigToPath(
            GetPianoConfigPath(),
            polygonPoints,
            scene,

            // Perspective Scene
            planePivot,

            pianoRollVisualizerHeight,
            pianoRollCameraSourceScale);
    }

    // ============================================================
    // LOAD PIANO CONFIGURATION
    // ============================================================

    inline bool LoadPianoConfigFromPath(
        const std::filesystem::path& configPath,
        std::vector<ImVec2>& polygonPoints,
        int& scene,

        // Perspective Scene
        float& planePivot,

        // PianoRoll Scene
        float& pianoRollVisualizerHeight,
        float& pianoRollCameraSourceScale
    )
    {
        Logger::Log("Loading piano config from %s\n", configPath.string().c_str());

        if (configPath.empty())
            return false;

        std::ifstream file(configPath);

        if (!file.is_open())
            return false;

        try
        {
            json config;
            file >> config;

            if (config.contains("points")) {

                if (config["points"].contains("P1") &&
                    config["points"].contains("P2") &&
                    config["points"].contains("P3") &&
                    config["points"].contains("P4")) {

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
                }
            }

            // ----------------------------------------------------
            // Load settings
            // ----------------------------------------------------

            if (config.contains("settings"))
            {
                auto& settings =
                    config["settings"];

                if (settings.contains("scene"))
                {
                    scene =
                        settings["scene"].get<int>();

                    if (scene < 0 || scene > 1)
                    {
                        Logger::Log("Invalid piano scene in configuration.\n");
                        return false;
                    }
                }

                if (settings.contains("planePivot"))
                {
                    planePivot =
                        settings["planePivot"]
                        .get<float>();

                    if (!std::isfinite(planePivot))
                        return false;
                }

                if (settings.contains("pianoRollVisualizerHeight"))
                {
                    pianoRollVisualizerHeight =
                        settings["pianoRollVisualizerHeight"].get<float>();

                    if (!std::isfinite(pianoRollVisualizerHeight))
                        return false;
                }

                if (settings.contains("pianoRollCameraSourceScale"))
                {
                    pianoRollCameraSourceScale =
                        settings["pianoRollCameraSourceScale"].get<float>();

                    if (!std::isfinite(pianoRollCameraSourceScale))
                        return false;
                }
            }
            else { return false; }

            return true;
        }
        catch (const std::exception& e)
        {
            Logger::Log(
                "Failed to load piano config: %s\n",
                e.what()
            );

            return false;
        }
    }

    inline bool LoadPianoConfig(
        std::vector<ImVec2>& polygonPoints,
        int& scene,

        // Perspective Scene
        float& planePivot,

        // PianoRoll Scene
        float& pianoRollVisualizerHeight,
        float& pianoRollCameraSourceScale
    )
    {
        return LoadPianoConfigFromPath(
            GetPianoConfigPath(),
            polygonPoints,
            scene,

            // Perspective Scene
            planePivot,

            pianoRollVisualizerHeight,
            pianoRollCameraSourceScale);
    }
}
