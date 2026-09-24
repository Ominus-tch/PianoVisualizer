#pragma once

#include <cstdint>
#include <chrono>
#include <atomic>
#include <string>
#include <vector>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

namespace audio
{

    class AudioOutput
    {
    public:

        // =====================================================
        // DEVICE
        // =====================================================

        struct Device
        {
            std::wstring id;
            std::wstring name;

            bool isDefault = false;
        };


        // =====================================================
        // CONFIGURATION
        // =====================================================

        enum class Mode
        {
            Shared,
            Exclusive
        };


        struct Configuration
        {
            // Empty = use Windows default output device.
            std::wstring deviceId;

            // WASAPI shared or exclusive mode.
            Mode mode = Mode::Shared;

            // Number of output channels.
            int32_t channels = 2;

            // Requested sample rate.
            //
            // 0.0 = use the device's default/mix format.
            double sampleRate = 0.0;

            // Requested WASAPI buffer duration.
            double bufferDurationMs = 20.0;

            // Application output volume.
            //
            // 0.0 = silent
            // 1.0 = 100%
            float volume = 1.0f;

            bool muted = false;

            // If the selected device disappears, allow
            // AudioOutput to fall back to the Windows
            // default output device.
            bool fallbackToDefaultDevice = true;
        };


        // =====================================================
        // STATISTICS
        // =====================================================

        struct AudioStatistics
        {
            bool initialized = false;
            bool running = false;

            double sampleRate = 0.0;
            int32_t channels = 0;

            uint32_t bufferFrames = 0;
            uint32_t currentPadding = 0;
            uint32_t availableFrames = 0;

            double bufferFillPercent = 0.0;
            double bufferDurationMs = 0.0;

            uint64_t framesWritten = 0;
            uint64_t writeCalls = 0;

            double outputFramesPerSecond = 0.0;
            double throughputMBps = 0.0;

            uint64_t failedWrites = 0;
        };


        // =====================================================
        // LIFETIME
        // =====================================================

        AudioOutput();
        ~AudioOutput();


        // =====================================================
        // DEVICES
        // =====================================================

        static std::vector<Device> enumerateDevices();


        // =====================================================
        // INITIALIZATION
        // =====================================================

        bool initialize(
            const Configuration& configuration);

        void shutdown();


        // =====================================================
        // PLAYBACK
        // =====================================================

        bool start();
        void stop();

        bool waitForSpace(
            int32_t frames);

        bool write(
            const float* const* channels,
            int32_t numChannels,
            int32_t numSamples);


        // =====================================================
        // STATE
        // =====================================================

        bool isInitialized() const;
        bool isRunning() const;


        // =====================================================
        // CONFIGURATION
        // =====================================================

        const Configuration& configuration() const;


        // =====================================================
        // OUTPUT SETTINGS
        // =====================================================

        void setVolume(
            float volume);

        float volume() const;

        void setMuted(
            bool muted);

        bool isMuted() const;


        // =====================================================
        // DEVICE / FORMAT INFORMATION
        // =====================================================

        std::wstring deviceId() const;
        std::wstring deviceName() const;

        double sampleRate() const;
        int32_t channels() const;


        // =====================================================
        // STATISTICS
        // =====================================================

        AudioStatistics getStatistics() const;


    private:

        enum class SampleFormat
        {
            Float32,
            PCM16,
            PCM24,
            PCM32,
            Unknown
        };


        // =====================================================
        // WASAPI
        // =====================================================

        IMMDevice* _device;
        IAudioClient* _audioClient;
        IAudioRenderClient* _renderClient;

        WAVEFORMATEX* _format;

        UINT32 _bufferFrames;

        SampleFormat _sampleFormat;

        HANDLE _audioEvent;

        // =====================================================
        // CONFIGURATION
        // =====================================================

        Configuration _configuration;


        // =====================================================
        // STATE
        // =====================================================

        bool _initialized;
        bool _running;
        bool _comInitialized;


        // =====================================================
        // STATISTICS
        // =====================================================

        std::atomic<uint64_t> _framesWritten;
        std::atomic<uint64_t> _writeCalls;
        std::atomic<uint64_t> _failedWrites;

        std::chrono::steady_clock::time_point
            _statsStartTime;
    };

}