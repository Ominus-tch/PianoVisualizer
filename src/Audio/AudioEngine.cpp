#include "AudioEngine.h"

#include "AudioOutput.h"
#include "vst/VSTPlugin.h"
#include "vst/VSTAudio.h"

#include "../../util/Logger.h"

#include <avrt.h>

#pragma comment(lib, "Avrt.lib")

namespace audio
{

    // =========================================================
    // CONSTRUCTOR / DESTRUCTOR
    // =========================================================

    AudioEngine::AudioEngine()
        : _running(false),
        _stopRequested(false)
    {
        Logger::Log(
            "[Audio] AudioEngine created\n");
    }


    AudioEngine::~AudioEngine()
    {
        shutdown();

        Logger::Log(
            "[Audio] AudioEngine destroyed\n");
    }


    // =========================================================
    // INITIALIZATION
    // =========================================================

    bool AudioEngine::initialize()
    {
        if (_initialized)
        {
            Logger::Log(
                "[Audio] AudioEngine is already initialized\n");

            return true;
        }

        AudioOutput::Configuration configuration;

        _output = new AudioOutput();

        if (!_output->initialize(configuration))
        {
            Logger::Log(
                "[Audio] Failed to initialize AudioOutput\n");

            delete _output;
            _output = nullptr;

            return false;
        }

        if (!_output->start())
        {
            Logger::Log(
                "[Audio] Failed to start AudioOutput\n");

            _output->shutdown();

            delete _output;
            _output = nullptr;

            return false;
        }

        _initialized = true;

        Logger::Log(
            "[Audio] AudioEngine initialized\n");

        Logger::Log(
            "[Audio] Output device: %ls\n",
            _output->deviceName().c_str());

        Logger::Log(
            "[Audio] Sample rate: %.0f Hz\n",
            _output->sampleRate());

        Logger::Log(
            "[Audio] Channels: %d\n",
            _output->channels());

        Logger::Log(
            "[Audio] Output buffer: %.2f ms\n\n",
            _output->configuration().bufferDurationMs);

        return true;
    }


    // =========================================================
    // VST
    // =========================================================

    bool AudioEngine::loadPlugin(
        const std::string& path)
    {
        if (!_initialized)
        {
            Logger::Log(
                "[VST] Cannot load plugin: "
                "AudioEngine is not initialized\n");

            return false;
        }

        if (!_output)
        {
            Logger::Log(
                "[VST] Cannot load plugin: "
                "AudioOutput is null\n");

            return false;
        }

        if (_vstPlugin || _vstAudio)
        {
            Logger::Log(
                "[VST] Unloading existing plugin "
                "before loading new plugin\n");

            unLoadPlugin();
        }

        _vstPlugin = new vst::VSTPlugin();

        if (!_vstPlugin->load(
            path,
            _output->sampleRate()))
        {
            Logger::Log(
                "[VST] Failed to load plugin: %s\n",
                path.c_str());

            delete _vstPlugin;
            _vstPlugin = nullptr;

            return false;
        }

        _vstPath = path;

        _vstAudio = new vst::VSTAudio(
            _vstPlugin->processor(),
            _output->sampleRate(),
            512);

        if (!_vstAudio)
        {
            Logger::Log(
                "[VST] Failed to create VSTAudio\n");

            _vstPlugin->unload();

            delete _vstPlugin;
            _vstPlugin = nullptr;

            _vstPath.clear();

            return false;
        }

        return true;
    }


    void AudioEngine::unLoadPlugin()
    {
        stop();

        if (_vstAudio)
        {
            delete _vstAudio;
            _vstAudio = nullptr;
        }

        if (_vstPlugin)
        {
            _vstPlugin->unload();

            delete _vstPlugin;
            _vstPlugin = nullptr;
        }

        _vstPath.clear();

        Logger::Log(
            "[VST] Plugin unloaded\n");
    }


    // =========================================================
    // START
    // =========================================================

    bool AudioEngine::start()
    {
        if (_running)
            return true;

        if (!_initialized)
        {
            Logger::Log(
                "[Audio] Cannot start engine: "
                "AudioEngine is not initialized\n");

            return false;
        }

        if (!_vstAudio)
        {
            Logger::Log(
                "[Audio] Cannot start engine: "
                "VSTAudio is null\n");

            return false;
        }

        if (!_output)
        {
            Logger::Log(
                "[Audio] Cannot start engine: "
                "AudioOutput is null\n");

            return false;
        }

        if (!_output->isInitialized())
        {
            Logger::Log(
                "[Audio] Cannot start engine: "
                "output is not initialized\n");

            return false;
        }

        if (!_output->isRunning())
        {
            Logger::Log(
                "[Audio] Cannot start engine: "
                "output is not running\n");

            return false;
        }

        _stopRequested = false;
        _running = true;

        _thread =
            std::thread(
                &AudioEngine::threadMain,
                this);

        Logger::Log(
            "[Audio] Audio engine started\n");

        return true;
    }


    // =========================================================
    // STOP
    // =========================================================

    void AudioEngine::stop()
    {
        if (!_running &&
            !_thread.joinable())
        {
            return;
        }

        Logger::Log(
            "[Audio] Stopping Audio thread...\n");

        _stopRequested = true;

        if (_thread.joinable())
            _thread.join();

        _running = false;

        Logger::Log(
            "[Audio] Audio thread stopped\n");
    }


    // =========================================================
    // SHUTDOWN
    // =========================================================

    void AudioEngine::shutdown()
    {
        stop();

        if (_output)
        {
            _output->stop();
            _output->shutdown();
        }

        delete _vstAudio;
        _vstAudio = nullptr;

        if (_vstPlugin)
        {
            _vstPlugin->unload();

            delete _vstPlugin;
            _vstPlugin = nullptr;
        }

        delete _output;
        _output = nullptr;

        _vstPath.clear();

        _initialized = false;

        Logger::Log(
            "[Audio] AudioEngine shutdown\n");
    }


    // =========================================================
    // STATE
    // =========================================================

    bool AudioEngine::isRunning() const
    {
        return _running;
    }


    // =========================================================
    // MIDI
    // =========================================================

    bool AudioEngine::noteOn(
        int16_t channel,
        int16_t pitch,
        float velocity)
    {
        if (!_vstAudio)
            return false;

        return _vstAudio->noteOn(
            channel,
            pitch,
            velocity);
    }


    bool AudioEngine::noteOff(
        int16_t channel,
        int16_t pitch,
        float velocity)
    {
        if (!_vstAudio)
            return false;

        return _vstAudio->noteOff(
            channel,
            pitch,
            velocity);
    }


    bool AudioEngine::controlChange(
        int16_t channel,
        int16_t controller,
        int16_t value)
    {
        if (!_vstAudio)
            return false;

        return _vstAudio->controlChange(
            channel,
            controller,
            value);
    }


    // =========================================================
    // PLUGIN INFORMATION
    // =========================================================

    const std::string& AudioEngine::pluginName() const
    {
        if (!_vstPlugin || !_initialized)
        {
            static const std::string empty;

            return empty;
        }

        return _vstPlugin->editorName();
    }


    // =========================================================
    // OUTPUT CONFIGURATION
    // =========================================================

    AudioOutput::Configuration
        AudioEngine::outputConfiguration() const
    {
        if (!_output)
        {
            return AudioOutput::Configuration{};
        }

        return _output->configuration();
    }


    bool AudioEngine::setOutputDevice(
        const std::wstring& deviceId)
    {
        if (!_output)
        {
            Logger::Log(
                "[Audio] Cannot change output device: "
                "AudioOutput is null\n");

            return false;
        }

        AudioOutput::Configuration configuration =
            _output->configuration();

        configuration.deviceId = deviceId;

        Logger::Log(
            "[Audio] Changing output device...\n");

        return reconfigureOutput(configuration);
    }


    bool AudioEngine::setOutputMode(
        AudioOutput::Mode mode)
    {
        if (!_output)
        {
            Logger::Log(
                "[Audio] Cannot change output mode: "
                "AudioOutput is null\n");

            return false;
        }

        AudioOutput::Configuration configuration =
            _output->configuration();

        configuration.mode = mode;

        Logger::Log(
            "[Audio] Changing output mode...\n");

        return reconfigureOutput(configuration);
    }


    bool AudioEngine::setOutputSampleRate(
        double sampleRate)
    {
        if (!_output)
        {
            Logger::Log(
                "[Audio] Cannot change sample rate: "
                "AudioOutput is null\n");

            return false;
        }

        if (sampleRate < 0.0)
        {
            Logger::Log(
                "[Audio] Invalid sample rate: %.2f\n",
                sampleRate);

            return false;
        }

        AudioOutput::Configuration configuration =
            _output->configuration();

        configuration.sampleRate = sampleRate;

        Logger::Log(
            "[Audio] Changing output sample rate to %.2f Hz...\n",
            sampleRate);

        return reconfigureOutput(configuration);
    }


    bool AudioEngine::setOutputBufferDuration(
        double milliseconds)
    {
        if (!_output)
        {
            Logger::Log(
                "[Audio] Cannot change buffer duration: "
                "AudioOutput is null\n");

            return false;
        }

        if (milliseconds <= 0.0)
        {
            Logger::Log(
                "[Audio] Invalid buffer duration: %.2f ms\n",
                milliseconds);

            return false;
        }

        AudioOutput::Configuration configuration =
            _output->configuration();

        configuration.bufferDurationMs = milliseconds;

        Logger::Log(
            "[Audio] Changing output buffer duration "
            "to %.2f ms...\n",
            milliseconds);

        return reconfigureOutput(configuration);
    }


    // =========================================================
    // LIVE VOLUME / MUTE
    // =========================================================

    void AudioEngine::setOutputVolume(
        float volume)
    {
        if (!_output)
            return;

        _output->setVolume(volume);
    }


    float AudioEngine::outputVolume() const
    {
        if (!_output)
            return 1.0f;

        return _output->volume();
    }


    void AudioEngine::setOutputMuted(
        bool muted)
    {
        if (!_output)
            return;

        _output->setMuted(muted);
    }


    bool AudioEngine::outputMuted() const
    {
        if (!_output)
            return false;

        return _output->isMuted();
    }


    // =========================================================
    // DEVICE ENUMERATION
    // =========================================================

    std::vector<AudioOutput::Device>
        AudioEngine::enumerateOutputDevices()
    {
        return AudioOutput::enumerateDevices();
    }


    // =========================================================
    // OUTPUT RECONFIGURATION
    // =========================================================

    bool AudioEngine::reconfigureOutput(
        const AudioOutput::Configuration& configuration)
    {
        if (!_output)
        {
            Logger::Log(
                "[Audio] Cannot reconfigure output: "
                "AudioOutput is null\n");

            return false;
        }

        const std::string previousVSTPath =
            _vstPath;

        const bool hadPlugin =
            (_vstPlugin != nullptr);

        Logger::Log(
            "[Audio] Reconfiguring audio output...\n");

        // -----------------------------------------------------
        // Stop audio thread
        // -----------------------------------------------------

        stop();

        // -----------------------------------------------------
        // Remove VST audio processor
        // -----------------------------------------------------

        if (_vstAudio)
        {
            delete _vstAudio;
            _vstAudio = nullptr;
        }

        // -----------------------------------------------------
        // Unload VST
        //
        // This is required because a sample-rate change means
        // the VST needs to be recreated with the new rate.
        // -----------------------------------------------------

        if (_vstPlugin)
        {
            _vstPlugin->unload();

            delete _vstPlugin;
            _vstPlugin = nullptr;
        }

        _vstPath.clear();

        // -----------------------------------------------------
        // Reinitialize AudioOutput
        // -----------------------------------------------------

        _output->stop();
        _output->shutdown();

        if (!_output->initialize(configuration))
        {
            Logger::Log(
                "[Audio] Failed to reinitialize AudioOutput\n");

            return false;
        }

        if (!_output->start())
        {
            Logger::Log(
                "[Audio] Failed to restart AudioOutput\n");

            _output->shutdown();

            return false;
        }

        Logger::Log(
            "[Audio] Audio output reconfigured successfully\n");

        Logger::Log(
            "[Audio] Device: %ls\n",
            _output->deviceName().c_str());

        Logger::Log(
            "[Audio] Sample rate: %.0f Hz\n",
            _output->sampleRate());

        Logger::Log(
            "[Audio] Buffer: %.2f ms\n",
            _output->configuration().bufferDurationMs);

        // -----------------------------------------------------
        // Reload previous VST
        // -----------------------------------------------------

        if (hadPlugin && !previousVSTPath.empty())
        {
            Logger::Log(
                "[VST] Reloading plugin after output "
                "reconfiguration: %s\n",
                previousVSTPath.c_str());

            if (!loadPlugin(previousVSTPath))
            {
                Logger::Log(
                    "[VST] Failed to reload plugin after "
                    "output reconfiguration\n");

                return false;
            }
        }

        start();

        return true;
    }


    // =========================================================
    // AUDIO THREAD
    // =========================================================

    void AudioEngine::threadMain()
    {
        Logger::Log(
            "[Audio] Audio thread started\n");

        if (!_vstAudio || !_output)
        {
            Logger::Log(
                "[Audio] Audio thread cannot start: "
                "missing VSTAudio or AudioOutput\n");

            _running = false;
            return;
        }

        DWORD taskIndex = 0;

        HANDLE mmcssHandle =
            AvSetMmThreadCharacteristicsW(
                L"Pro Audio",
                &taskIndex);

        if (!mmcssHandle)
        {
            Logger::Log(
                "[Audio] Failed to register audio thread with MMCSS\n");
        }
        else
        {
            Logger::Log(
                "[Audio] Audio thread registered with MMCSS (Pro Audio)\n");
        }

        const int32_t blockSize =
            _vstAudio->maxBlockSize();

        Logger::Log(
            "[Audio] VST block size: %d\n\n",
            blockSize);

        while (!_stopRequested)
        {
            if (!_output->waitForSpace(blockSize))
            {
                Logger::Log(
                    "[Audio] Failed waiting for output space\n");

                break;
            }

            if (_stopRequested)
                break;

            if (!_vstAudio->process())
            {
                Logger::Log(
                    "[Audio] VST processing failed\n");

                break;
            }

            if (!_output->write(
                _vstAudio->channels(),
                _vstAudio->numChannels(),
                _vstAudio->numSamples()))
            {
                Logger::Log(
                    "[Audio] Failed writing VST audio\n");

                break;
            }
        }

        if (mmcssHandle)
        {
            AvRevertMmThreadCharacteristics(
                mmcssHandle);
        }

        _running = false;

        Logger::Log(
            "[Audio] Audio thread stopped\n");
    }

}