#pragma once

#include "AudioOutput.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace vst
{
    class VSTPlugin;
    class VSTAudio;
}

namespace audio
{

    class AudioEngine
    {
    public:
        AudioEngine();

        ~AudioEngine();

        // =====================================================
        // LIFECYCLE
        // =====================================================

        bool initialize();

        bool loadPlugin(
            const std::string& path);

        void unLoadPlugin();

        bool start();
        void stop();

        void shutdown();

        bool isRunning() const;

        // =====================================================
        // MIDI / VST
        // =====================================================

        bool noteOn(
            int16_t channel,
            int16_t pitch,
            float velocity);

        bool noteOff(
            int16_t channel,
            int16_t pitch,
            float velocity = 0.0f);

        bool controlChange(
            int16_t channel,
            int16_t controller,
            int16_t value);

        vst::VSTAudio* audio() const
        {
            return _vstAudio;
        }

        vst::VSTPlugin* plugin() const
        {
            return _vstPlugin;
        }

        AudioOutput* output() const
        {
            return _output;
        }

        std::string vstPath() const
        {
            return _vstPath;
        }

        const std::string& pluginName() const;

        // =====================================================
        // OUTPUT CONFIGURATION
        // =====================================================

        AudioOutput::Configuration outputConfiguration() const;

        bool setOutputDevice(
            const std::wstring& deviceId);

        bool setOutputSampleRate(
            double sampleRate);

        bool setOutputBufferDuration(
            double milliseconds);

        void setOutputVolume(
            float volume);

        float outputVolume() const;

        void setOutputMuted(
            bool muted);

        bool outputMuted() const;

        // =====================================================
        // OUTPUT DEVICES
        // =====================================================

        static std::vector<AudioOutput::Device>
            enumerateOutputDevices();

    private:

        // =====================================================
        // AUDIO THREAD
        // =====================================================

        void threadMain();

        // =====================================================
        // OUTPUT RECONFIGURATION
        // =====================================================

        bool reconfigureOutput(
            const AudioOutput::Configuration& configuration);

    private:

        // =====================================================
        // VST
        // =====================================================

        vst::VSTAudio* _vstAudio = nullptr;
        vst::VSTPlugin* _vstPlugin = nullptr;

        // =====================================================
        // AUDIO OUTPUT
        // =====================================================

        AudioOutput* _output = nullptr;

        // =====================================================
        // THREAD
        // =====================================================

        std::thread _thread;

        std::atomic<bool> _running;
        std::atomic<bool> _stopRequested;

        // =====================================================
        // STATE
        // =====================================================

        bool _initialized = false;

        std::string _vstPath = "";
    };

}