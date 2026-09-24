#include "AudioOutput.h"

#include "../../util/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace audio
{

    AudioOutput::AudioOutput()
        : _device(nullptr),
        _audioClient(nullptr),
        _renderClient(nullptr),
        _format(nullptr),
        _bufferFrames(0),
        _sampleFormat(SampleFormat::Unknown),
        _configuration(),
        _initialized(false),
        _running(false),
        _comInitialized(false),
        _framesWritten(0),
        _writeCalls(0),
        _failedWrites(0),
        _statsStartTime()
    {
        Logger::Log(
            "[Audio] AudioOutput created\n");
    }


    AudioOutput::~AudioOutput()
    {
        shutdown();

        Logger::Log(
            "[Audio] AudioOutput destroyed\n");
    }


    // =========================================================
    // DEVICES
    // =========================================================

    std::vector<AudioOutput::Device>
        AudioOutput::enumerateDevices()
    {
        std::vector<Device> devices;

        bool comInitialized = false;

        HRESULT hr = CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED);

        if (SUCCEEDED(hr))
        {
            comInitialized = true;
        }
        else if (hr != RPC_E_CHANGED_MODE)
        {
            Logger::Log(
                "[Audio] CoInitializeEx failed while enumerating devices: 0x%08X\n",
                static_cast<unsigned>(hr));

            return devices;
        }

        IMMDeviceEnumerator* enumerator = nullptr;

        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator));

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] Failed to create device enumerator: 0x%08X\n",
                static_cast<unsigned>(hr));

            if (comInitialized)
                CoUninitialize();

            return devices;
        }

        IMMDevice* defaultDevice = nullptr;

        hr = enumerator->GetDefaultAudioEndpoint(
            eRender,
            eConsole,
            &defaultDevice);

        std::wstring defaultId;

        if (SUCCEEDED(hr) && defaultDevice)
        {
            LPWSTR id = nullptr;

            if (SUCCEEDED(defaultDevice->GetId(&id)) &&
                id)
            {
                defaultId = id;
                CoTaskMemFree(id);
            }

            defaultDevice->Release();
        }

        IMMDeviceCollection* collection = nullptr;

        hr = enumerator->EnumAudioEndpoints(
            eRender,
            DEVICE_STATE_ACTIVE,
            &collection);

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] Failed to enumerate audio endpoints: 0x%08X\n",
                static_cast<unsigned>(hr));

            enumerator->Release();

            if (comInitialized)
                CoUninitialize();

            return devices;
        }

        UINT count = 0;

        hr = collection->GetCount(
            &count);

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] Failed to get audio endpoint count: 0x%08X\n",
                static_cast<unsigned>(hr));

            collection->Release();
            enumerator->Release();

            if (comInitialized)
                CoUninitialize();

            return devices;
        }

        devices.reserve(count);

        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* device = nullptr;

            hr = collection->Item(
                i,
                &device);

            if (FAILED(hr) || !device)
                continue;

            LPWSTR id = nullptr;

            if (FAILED(device->GetId(&id)) ||
                !id)
            {
                device->Release();
                continue;
            }

            std::wstring deviceId = id;

            CoTaskMemFree(id);

            IPropertyStore* propertyStore = nullptr;

            std::wstring name;

            hr = device->OpenPropertyStore(
                STGM_READ,
                &propertyStore);

            if (SUCCEEDED(hr) &&
                propertyStore)
            {
                PROPVARIANT property;

                PropVariantInit(&property);

                hr = propertyStore->GetValue(
                    PKEY_Device_FriendlyName,
                    &property);

                if (SUCCEEDED(hr) &&
                    property.vt == VT_LPWSTR &&
                    property.pwszVal)
                {
                    name = property.pwszVal;
                }

                PropVariantClear(&property);

                propertyStore->Release();
            }

            Device info;

            info.id = deviceId;
            info.name = name;
            info.isDefault =
                !defaultId.empty() &&
                deviceId == defaultId;

            devices.push_back(
                std::move(info));

            device->Release();
        }

        collection->Release();
        enumerator->Release();

        if (comInitialized)
            CoUninitialize();

        return devices;
    }


    // =========================================================
    // INITIALIZATION
    // =========================================================

    bool AudioOutput::initialize(
        const Configuration& configuration)
    {
        if (_initialized)
        {
            Logger::Log(
                "[Audio] Already initialized\n");

            return true;
        }

        if (configuration.channels != 2)
        {
            Logger::Log(
                "[Audio] Only stereo output is currently supported\n");

            return false;
        }

        if (configuration.bufferDurationMs <= 0.0)
        {
            Logger::Log(
                "[Audio] Invalid buffer duration: %.2f ms\n",
                configuration.bufferDurationMs);

            return false;
        }

        if (configuration.volume < 0.0f ||
            configuration.volume > 4.0f)
        {
            Logger::Log(
                "[Audio] Invalid volume: %.3f\n",
                configuration.volume);

            return false;
        }

        Logger::Log(
            "[Audio] Initializing WASAPI\n");

        /*
         * Store configuration only after basic validation.
         */
        _configuration = configuration;

        /*
         * Initialize COM for this thread.
         */
        HRESULT hr = CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED);

        if (FAILED(hr) &&
            hr != RPC_E_CHANGED_MODE)
        {
            Logger::Log(
                "[Audio] CoInitializeEx failed: 0x%08X\n",
                static_cast<unsigned>(hr));

            return false;
        }

        if (hr == S_OK ||
            hr == S_FALSE)
        {
            _comInitialized = true;
        }

        /*
         * Create the WASAPI device enumerator.
         */
        IMMDeviceEnumerator* enumerator = nullptr;

        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator));

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] Failed to create device enumerator: 0x%08X\n",
                static_cast<unsigned>(hr));

            shutdown();
            return false;
        }

        /*
         * Acquire the requested device.
         *
         * Empty device ID means use the Windows default
         * playback device.
         */
        if (_configuration.deviceId.empty())
        {
            hr = enumerator->GetDefaultAudioEndpoint(
                eRender,
                eConsole,
                &_device);

            if (SUCCEEDED(hr))
            {
                Logger::Log(
                    "[Audio] Using default audio endpoint\n");
            }
        }
        else
        {
            hr = enumerator->GetDevice(
                _configuration.deviceId.c_str(),
                &_device);

            if (SUCCEEDED(hr))
            {
                Logger::Log(
                    "[Audio] Using configured audio endpoint\n");
            }
            else
            {
                Logger::Log(
                    "[Audio] Failed to find configured audio device: 0x%08X\n",
                    static_cast<unsigned>(hr));

                if (_configuration.fallbackToDefaultDevice)
                {
                    Logger::Log(
                        "[Audio] Falling back to default audio endpoint\n");

                    hr = enumerator->GetDefaultAudioEndpoint(
                        eRender,
                        eConsole,
                        &_device);
                }
            }
        }

        enumerator->Release();

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] Failed to acquire audio endpoint: 0x%08X\n",
                static_cast<unsigned>(hr));

            shutdown();
            return false;
        }

        /*
         * Retrieve device name for logging.
         */
        {
            IPropertyStore* propertyStore = nullptr;

            hr = _device->OpenPropertyStore(
                STGM_READ,
                &propertyStore);

            if (SUCCEEDED(hr) &&
                propertyStore)
            {
                PROPVARIANT property;

                PropVariantInit(&property);

                hr = propertyStore->GetValue(
                    PKEY_Device_FriendlyName,
                    &property);

                if (SUCCEEDED(hr) &&
                    property.vt == VT_LPWSTR &&
                    property.pwszVal)
                {
                    Logger::Log(
                        "[Audio] Output device: %ls\n",
                        property.pwszVal);
                }

                PropVariantClear(&property);

                propertyStore->Release();
            }
        }

        /*
         * Activate IAudioClient.
         */
        hr = _device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(&_audioClient));

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] Failed to activate IAudioClient: 0x%08X\n",
                static_cast<unsigned>(hr));

            shutdown();
            return false;
        }

        /*
         * Get the device's actual shared-mode mix format.
         */
        hr = _audioClient->GetMixFormat(
            &_format);

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] GetMixFormat failed: 0x%08X\n",
                static_cast<unsigned>(hr));

            shutdown();
            return false;
        }

        /*
         * If the user requested a specific sample rate, apply it
         * to the format before initialization.
         *
         * In shared mode Windows may reject this if the format
         * is not compatible with the device's shared-mode format.
         */
        if (_configuration.sampleRate > 0.0)
        {
            const auto requestedRate =
                static_cast<DWORD>(
                    std::llround(
                        _configuration.sampleRate));

            if (requestedRate == 0)
            {
                Logger::Log(
                    "[Audio] Invalid requested sample rate\n");

                shutdown();
                return false;
            }

            _format->nSamplesPerSec =
                requestedRate;

            if (_format->wFormatTag ==
                WAVE_FORMAT_EXTENSIBLE)
            {
                auto* extensible =
                    reinterpret_cast<WAVEFORMATEXTENSIBLE*>(
                        _format);

                extensible->Samples.wValidBitsPerSample =
                    _format->wBitsPerSample;
            }

            /*
             * Recalculate format-dependent fields.
             */
            _format->nAvgBytesPerSec =
                _format->nSamplesPerSec *
                _format->nBlockAlign;
        }

        //_format->nChannels = 2;

        Logger::Log(
            "[Audio] Device format: channels=%u, sampleRate=%u, bits=%u\n",
            _format->nChannels,
            _format->nSamplesPerSec,
            _format->wBitsPerSample);

        /*
         * Convert milliseconds to WASAPI 100-nanosecond units.
         */
        const REFERENCE_TIME bufferDuration =
            static_cast<REFERENCE_TIME>(
                _configuration.bufferDurationMs *
                10000.0);

        if (_configuration.mode ==
            Mode::Exclusive)
        {
            Logger::Log(
                "[Audio] Initializing WASAPI in exclusive mode\n");

            REFERENCE_TIME defaultPeriod = 0;
            REFERENCE_TIME minimumPeriod = 0;

            hr = _audioClient->GetDevicePeriod(
                &defaultPeriod,
                &minimumPeriod);

            if (FAILED(hr))
            {
                Logger::Log(
                    "[Audio] GetDevicePeriod failed: 0x%08X\n",
                    static_cast<unsigned>(hr));

                shutdown();
                return false;
            }

            const REFERENCE_TIME requestedPeriod =
                bufferDuration;

            Logger::Log(
                "[Audio] Device periods: default=%.3f ms, minimum=%.3f ms\n",
                static_cast<double>(defaultPeriod) / 10000.0,
                static_cast<double>(minimumPeriod) / 10000.0);

            // The device cannot normally run below its minimum period.
            const REFERENCE_TIME exclusivePeriod =
                (std::max)(
                    requestedPeriod,
                    minimumPeriod);

            bool formatSupported = false;

            if (_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
            {
                auto* extensible =
                    reinterpret_cast<WAVEFORMATEXTENSIBLE*>(
                        _format);

                const GUID originalSubFormat =
                    extensible->SubFormat;

                // Try the current format first.
                hr = _audioClient->IsFormatSupported(
                    AUDCLNT_SHAREMODE_EXCLUSIVE,
                    _format,
                    nullptr);

                if (hr == S_OK)
                {
                    formatSupported = true;
                }
                else
                {
                    // Try PCM32.
                    extensible->SubFormat =
                        KSDATAFORMAT_SUBTYPE_PCM;

                    extensible->Samples.wValidBitsPerSample =
                        32;

                    _format->wBitsPerSample = 32;
                    _format->nBlockAlign =
                        _format->nChannels *
                        sizeof(int32_t);

                    _format->nAvgBytesPerSec =
                        _format->nSamplesPerSec *
                        _format->nBlockAlign;

                    hr = _audioClient->IsFormatSupported(
                        AUDCLNT_SHAREMODE_EXCLUSIVE,
                        _format,
                        nullptr);

                    if (hr == S_OK)
                    {
                        Logger::Log(
                            "[Audio] Exclusive format: 32-bit PCM\n");

                        formatSupported = true;
                    }
                    else
                    {
                        // Try PCM24.
                        extensible->SubFormat =
                            KSDATAFORMAT_SUBTYPE_PCM;

                        extensible->Samples.wValidBitsPerSample =
                            24;

                        _format->wBitsPerSample = 24;
                        _format->nBlockAlign =
                            _format->nChannels * 3;

                        _format->nAvgBytesPerSec =
                            _format->nSamplesPerSec *
                            _format->nBlockAlign;

                        hr = _audioClient->IsFormatSupported(
                            AUDCLNT_SHAREMODE_EXCLUSIVE,
                            _format,
                            nullptr);

                        if (hr == S_OK)
                        {
                            Logger::Log(
                                "[Audio] Exclusive format: 24-bit PCM\n");

                            formatSupported = true;
                        }
                        else
                        {
                            // Try PCM16.
                            extensible->SubFormat =
                                KSDATAFORMAT_SUBTYPE_PCM;

                            extensible->Samples.wValidBitsPerSample =
                                16;

                            _format->wBitsPerSample = 16;
                            _format->nBlockAlign =
                                _format->nChannels * sizeof(int16_t);

                            _format->nAvgBytesPerSec =
                                _format->nSamplesPerSec *
                                _format->nBlockAlign;

                            hr = _audioClient->IsFormatSupported(
                                AUDCLNT_SHAREMODE_EXCLUSIVE,
                                _format,
                                nullptr);

                            if (hr == S_OK)
                            {
                                Logger::Log(
                                    "[Audio] Exclusive format: 16-bit PCM\n");

                                formatSupported = true;
                            }
                        }
                    }

                    if (!formatSupported)
                    {
                        extensible->SubFormat =
                            originalSubFormat;
                    }
                }
            }

            if (!formatSupported)
            {
                hr = _audioClient->IsFormatSupported(
                    AUDCLNT_SHAREMODE_EXCLUSIVE,
                    _format,
                    nullptr);

                if (hr != S_OK)
                {
                    Logger::Log(
                        "[Audio] No supported exclusive format found: 0x%08X\n",
                        static_cast<unsigned>(hr));

                    shutdown();
                    return false;
                }
            }

            hr = _audioClient->Initialize(
                AUDCLNT_SHAREMODE_EXCLUSIVE,
                0,
                exclusivePeriod,
                exclusivePeriod,
                _format,
                nullptr);

            if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
            {
                UINT32 alignedFrames = 0;

                hr = _audioClient->GetBufferSize(
                    &alignedFrames);

                if (FAILED(hr))
                {
                    Logger::Log(
                        "[Audio] GetBufferSize after alignment failure failed: 0x%08X\n",
                        static_cast<unsigned>(hr));

                    shutdown();
                    return false;
                }

                const REFERENCE_TIME alignedDuration =
                    static_cast<REFERENCE_TIME>(
                        (static_cast<double>(alignedFrames) /
                            static_cast<double>(_format->nSamplesPerSec)) *
                        10000000.0 + 0.5);

                Logger::Log(
                    "[Audio] Buffer alignment required: %u frames (%.3f ms)\n",
                    alignedFrames,
                    static_cast<double>(alignedDuration) / 10000.0);

                hr = _audioClient->Initialize(
                    AUDCLNT_SHAREMODE_EXCLUSIVE,
                    0,
                    alignedDuration,
                    alignedDuration,
                    _format,
                    nullptr);
            }

            if (FAILED(hr))
            {
                Logger::Log(
                    "[Audio] Exclusive IAudioClient::Initialize failed: 0x%08X\n",
                    static_cast<unsigned>(hr));

                shutdown();
                return false;
            }
        }
        else
        {
            hr = _audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                0,
                bufferDuration,
                0,
                _format,
                nullptr);

            if (FAILED(hr))
            {
                Logger::Log(
                    "[Audio] Shared IAudioClient::Initialize failed: 0x%08X\n",
                    static_cast<unsigned>(hr));

                shutdown();
                return false;
            }
        }

        /*
         * Determine the actual sample format from the final format.
         *
         * This must happen after exclusive-mode format negotiation,
         * because GetMixFormat() describes the shared-mode format and
         * exclusive mode may have selected a different format.
         */
        _sampleFormat =
            SampleFormat::Unknown;

        if (_format->wFormatTag ==
            WAVE_FORMAT_IEEE_FLOAT)
        {
            if (_format->wBitsPerSample == 32)
            {
                _sampleFormat =
                    SampleFormat::Float32;
            }
        }
        else if (_format->wFormatTag ==
            WAVE_FORMAT_PCM)
        {
            if (_format->wBitsPerSample == 16)
            {
                _sampleFormat =
                    SampleFormat::PCM16;
            }
            else if (_format->wBitsPerSample == 24)
            {
                _sampleFormat =
                    SampleFormat::PCM24;
            }
            else if (_format->wBitsPerSample == 32)
            {
                _sampleFormat =
                    SampleFormat::PCM32;
            }
        }
        else if (_format->wFormatTag ==
            WAVE_FORMAT_EXTENSIBLE)
        {
            auto* extensible =
                reinterpret_cast<WAVEFORMATEXTENSIBLE*>(
                    _format);

            if (extensible->SubFormat ==
                KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            {
                if (_format->wBitsPerSample == 32)
                {
                    _sampleFormat =
                        SampleFormat::Float32;
                }
            }
            else if (extensible->SubFormat ==
                KSDATAFORMAT_SUBTYPE_PCM)
            {
                if (_format->wBitsPerSample == 16)
                {
                    _sampleFormat =
                        SampleFormat::PCM16;
                }
                else if (_format->wBitsPerSample == 24)
                {
                    _sampleFormat =
                        SampleFormat::PCM24;
                }
                else if (_format->wBitsPerSample == 32)
                {
                    _sampleFormat =
                        SampleFormat::PCM32;
                }
            }
        }

        if (_sampleFormat ==
            SampleFormat::Unknown)
        {
            Logger::Log(
                "[Audio] Unsupported final WASAPI sample format\n");

            shutdown();
            return false;
        }

        const char* formatName =
            "Unknown";

        switch (_sampleFormat)
        {
        case SampleFormat::Float32:
            formatName =
                "32-bit float";
            break;

        case SampleFormat::PCM16:
            formatName =
                "16-bit PCM";
            break;

        case SampleFormat::PCM24:
            formatName =
                "24-bit PCM";
            break;

        case SampleFormat::PCM32:
            formatName =
                "32-bit PCM";
            break;

        default:
            break;
        }

        Logger::Log(
            "[Audio] Final sample format: %s\n",
            formatName);

        /*
         * Get actual WASAPI buffer size.
         */
        hr = _audioClient->GetBufferSize(
            &_bufferFrames);

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] GetBufferSize failed: 0x%08X\n",
                static_cast<unsigned>(hr));

            shutdown();
            return false;
        }

        Logger::Log(
            "[Audio] WASAPI buffer: %u frames\n",
            _bufferFrames);

        /*
         * Get render client.
         */
        hr = _audioClient->GetService(
            __uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(&_renderClient));

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] Failed to get IAudioRenderClient: 0x%08X\n",
                static_cast<unsigned>(hr));

            shutdown();
            return false;
        }

        /*
         * Reset statistics.
         */
        _framesWritten = 0;
        _writeCalls = 0;
        _failedWrites = 0;

        _statsStartTime =
            std::chrono::steady_clock::now();

        _initialized = true;

        Logger::Log(
            "[Audio] WASAPI initialized successfully\n");

        Logger::Log(
            "[Audio] Actual output sample rate: %u Hz\n",
            _format->nSamplesPerSec);

        Logger::Log(
            "[Audio] Actual output channels: %u\n",
            _format->nChannels);

        Logger::Log(
            "[Audio] Actual buffer duration: %.2f ms\n",
            static_cast<double>(_bufferFrames) /
            static_cast<double>(_format->nSamplesPerSec) *
            1000.0);

        return true;
    }


    // =========================================================
    // SHUTDOWN
    // =========================================================

    void AudioOutput::shutdown()
    {
        if (_running)
            stop();

        if (_renderClient)
        {
            _renderClient->Release();
            _renderClient = nullptr;
        }

        if (_audioClient)
        {
            _audioClient->Release();
            _audioClient = nullptr;
        }

        if (_device)
        {
            _device->Release();
            _device = nullptr;
        }

        if (_format)
        {
            CoTaskMemFree(_format);
            _format = nullptr;
        }

        _bufferFrames = 0;

        _sampleFormat =
            SampleFormat::Unknown;

        _initialized = false;

        if (_comInitialized)
        {
            CoUninitialize();
            _comInitialized = false;
        }

        Logger::Log(
            "[Audio] WASAPI shut down\n");
    }


    // =========================================================
    // PLAYBACK
    // =========================================================

    bool AudioOutput::start()
    {
        if (!_initialized)
        {
            Logger::Log(
                "[Audio] Cannot start: not initialized\n");

            return false;
        }

        if (_running)
            return true;

        HRESULT hr =
            _audioClient->Start();

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] AudioClient::Start failed: 0x%08X\n",
                static_cast<unsigned>(hr));

            return false;
        }

        _running = true;

        Logger::Log(
            "[Audio] Audio output started\n");

        return true;
    }


    void AudioOutput::stop()
    {
        if (!_running)
            return;

        HRESULT hr =
            _audioClient->Stop();

        if (FAILED(hr))
        {
            Logger::Log(
                "[Audio] AudioClient::Stop failed: 0x%08X\n",
                static_cast<unsigned>(hr));
        }

        _running = false;

        Logger::Log(
            "[Audio] Audio output stopped\n");
    }


    bool AudioOutput::waitForSpace(
        int32_t frames)
    {
        if (!_initialized ||
            !_running ||
            !_audioClient)
        {
            return false;
        }

        if (frames <= 0)
            return true;

        while (true)
        {
            UINT32 padding = 0;

            HRESULT hr =
                _audioClient->GetCurrentPadding(
                    &padding);

            if (FAILED(hr))
            {
                Logger::Log(
                    "[Audio] GetCurrentPadding failed: 0x%08X\n",
                    static_cast<unsigned>(hr));

                return false;
            }

            const UINT32 available =
                _bufferFrames > padding
                ? _bufferFrames - padding
                : 0;

            if (available >=
                static_cast<UINT32>(frames))
            {
                return true;
            }

            Sleep(1);
        }
    }


    bool audio::AudioOutput::write(
        const float* const* channels,
        int32_t numChannels,
        int32_t numSamples)
    {
        if (!_initialized ||
            !_running ||
            !_audioClient ||
            !_renderClient ||
            !_format ||
            !channels ||
            numChannels < 0 ||
            numSamples <= 0)
        {
            return false;
        }

        if (numChannels > 0)
        {
            for (int32_t channel = 0; channel < numChannels; ++channel)
            {
                if (!channels[channel])
                    return false;
            }
        }

        UINT32 currentPadding = 0;

        HRESULT hr = _audioClient->GetCurrentPadding(
            &currentPadding);

        if (FAILED(hr))
        {
            ++_failedWrites;
            return false;
        }

        if (currentPadding >= _bufferFrames)
        {
            return true;
        }

        UINT32 availableFrames =
            _bufferFrames - currentPadding;

        if (availableFrames <
            static_cast<UINT32>(numSamples))
        {
            ++_failedWrites;

            Logger::Log(
                "[Audio] Not enough WASAPI space: "
                "needed=%d available=%u\n",
                numSamples,
                availableFrames);

            return false;
        }

        const UINT32 framesToWrite =
            static_cast<UINT32>(numSamples);

        if (framesToWrite == 0)
            return true;

        BYTE* data = nullptr;

        hr = _renderClient->GetBuffer(
            framesToWrite,
            &data);

        if (FAILED(hr))
        {
            ++_failedWrites;
            return false;
        }

        const int32_t outputChannels =
            static_cast<int32_t>(_format->nChannels);

        const float volume =
            _configuration.muted
            ? 0.0f
            : _configuration.volume;

        // =========================================================
        // FLOAT32
        // =========================================================

        if (_sampleFormat == SampleFormat::Float32)
        {
            float* output =
                reinterpret_cast<float*>(data);

            for (UINT32 frame = 0;
                frame < framesToWrite;
                ++frame)
            {
                for (int32_t outputChannel = 0;
                    outputChannel < outputChannels;
                    ++outputChannel)
                {
                    float sample = 0.0f;

                    // Copy a source channel if one exists.
                    //
                    // If the VST provides fewer channels than
                    // the physical device, the remaining channels
                    // are explicitly silenced.
                    if (outputChannel < numChannels)
                    {
                        sample =
                            channels[outputChannel][frame];
                    }

                    output[
                        frame * outputChannels +
                            outputChannel
                    ] = sample * volume;
                }
            }
        }

        // =========================================================
        // PCM16
        // =========================================================

        else if (_sampleFormat == SampleFormat::PCM16)
        {
            int16_t* output =
                reinterpret_cast<int16_t*>(data);

            for (UINT32 frame = 0;
                frame < framesToWrite;
                ++frame)
            {
                for (int32_t outputChannel = 0;
                    outputChannel < outputChannels;
                    ++outputChannel)
                {
                    float sample = 0.0f;

                    if (outputChannel < numChannels)
                    {
                        sample =
                            channels[outputChannel][frame];
                    }

                    sample *= volume;

                    sample =
                        std::clamp(
                            sample,
                            -1.0f,
                            1.0f);

                    output[
                        frame * outputChannels +
                            outputChannel
                    ] =
                        static_cast<int16_t>(
                            std::lrintf(
                                sample * 32767.0f));
                }
            }
        }

        // =========================================================
        // PCM24
        // =========================================================

        else if (_sampleFormat == SampleFormat::PCM24)
        {
            for (UINT32 frame = 0;
                frame < framesToWrite;
                ++frame)
            {
                for (int32_t outputChannel = 0;
                    outputChannel < outputChannels;
                    ++outputChannel)
                {
                    float sample = 0.0f;

                    if (outputChannel < numChannels)
                    {
                        sample =
                            channels[outputChannel][frame];
                    }

                    sample *= volume;

                    sample =
                        std::clamp(
                            sample,
                            -1.0f,
                            1.0f);

                    const int32_t value =
                        static_cast<int32_t>(
                            std::lrintf(
                                sample * 8388607.0f));

                    const size_t byteOffset =
                        (
                            static_cast<size_t>(frame) *
                            static_cast<size_t>(outputChannels) +
                            static_cast<size_t>(outputChannel)
                            ) * 3;

                    data[byteOffset + 0] =
                        static_cast<BYTE>(
                            value & 0xFF);

                    data[byteOffset + 1] =
                        static_cast<BYTE>(
                            (value >> 8) & 0xFF);

                    data[byteOffset + 2] =
                        static_cast<BYTE>(
                            (value >> 16) & 0xFF);
                }
            }
        }

        // =========================================================
        // PCM32
        // =========================================================

        else if (_sampleFormat == SampleFormat::PCM32)
        {
            int32_t* output =
                reinterpret_cast<int32_t*>(data);

            for (UINT32 frame = 0;
                frame < framesToWrite;
                ++frame)
            {
                for (int32_t outputChannel = 0;
                    outputChannel < outputChannels;
                    ++outputChannel)
                {
                    float sample = 0.0f;

                    if (outputChannel < numChannels)
                    {
                        sample =
                            channels[outputChannel][frame];
                    }

                    sample *= volume;

                    sample =
                        std::clamp(
                            sample,
                            -1.0f,
                            1.0f);

                    output[
                        frame * outputChannels +
                            outputChannel
                    ] =
                        static_cast<int32_t>(
                            std::llround(
                                static_cast<double>(sample) *
                                2147483647.0));
                }
            }
        }

        else
        {
            _renderClient->ReleaseBuffer(
                framesToWrite,
                AUDCLNT_BUFFERFLAGS_SILENT);

            ++_failedWrites;
            return false;
        }

        hr = _renderClient->ReleaseBuffer(
            framesToWrite,
            0);

        if (FAILED(hr))
        {
            ++_failedWrites;
            return false;
        }

        _framesWritten += framesToWrite;
        ++_writeCalls;

        return true;
    }


    // =========================================================
    // STATE
    // =========================================================

    bool AudioOutput::isInitialized() const
    {
        return _initialized;
    }


    bool AudioOutput::isRunning() const
    {
        return _running;
    }


    // =========================================================
    // CONFIGURATION
    // =========================================================

    const AudioOutput::Configuration&
        AudioOutput::configuration() const
    {
        return _configuration;
    }


    void AudioOutput::setVolume(
        float volume)
    {
        volume =
            (std::max)(
                0.0f,
                (std::min)(
                    4.0f,
                    volume));

        _configuration.volume =
            volume;
    }


    float AudioOutput::volume() const
    {
        return _configuration.volume;
    }


    void AudioOutput::setMuted(
        bool muted)
    {
        _configuration.muted =
            muted;
    }


    bool AudioOutput::isMuted() const
    {
        return _configuration.muted;
    }


    // =========================================================
    // DEVICE / FORMAT INFORMATION
    // =========================================================

    std::wstring AudioOutput::deviceId() const
    {
        if (!_device)
            return {};

        LPWSTR id = nullptr;

        if (FAILED(_device->GetId(&id)) ||
            !id)
        {
            return {};
        }

        std::wstring result =
            id;

        CoTaskMemFree(id);

        return result;
    }


    std::wstring AudioOutput::deviceName() const
    {
        if (!_device)
            return {};

        IPropertyStore* propertyStore =
            nullptr;

        HRESULT hr =
            _device->OpenPropertyStore(
                STGM_READ,
                &propertyStore);

        if (FAILED(hr) ||
            !propertyStore)
        {
            return {};
        }

        PROPVARIANT property;

        PropVariantInit(&property);

        hr = propertyStore->GetValue(
            PKEY_Device_FriendlyName,
            &property);

        std::wstring name;

        if (SUCCEEDED(hr) &&
            property.vt == VT_LPWSTR &&
            property.pwszVal)
        {
            name =
                property.pwszVal;
        }

        PropVariantClear(&property);

        propertyStore->Release();

        return name;
    }


    double AudioOutput::sampleRate() const
    {
        if (!_format)
            return 0.0;

        return static_cast<double>(
            _format->nSamplesPerSec);
    }


    int32_t AudioOutput::channels() const
    {
        if (!_format)
            return 0;

        return static_cast<int32_t>(
            _format->nChannels);
    }


    // =========================================================
    // STATISTICS
    // =========================================================

    AudioOutput::AudioStatistics
        AudioOutput::getStatistics() const
    {
        AudioStatistics stats{};

        stats.initialized =
            _initialized;

        stats.running =
            _running;

        stats.sampleRate =
            sampleRate();

        stats.channels =
            channels();

        stats.bufferFrames =
            _bufferFrames;

        /*
         * Get the current amount of audio
         * currently queued in WASAPI.
         */
        if (_audioClient &&
            _initialized)
        {
            UINT32 padding = 0;

            const HRESULT hr =
                _audioClient->GetCurrentPadding(
                    &padding);

            if (SUCCEEDED(hr))
            {
                stats.currentPadding =
                    padding;

                stats.availableFrames =
                    _bufferFrames > padding
                    ? _bufferFrames - padding
                    : 0;

                if (_bufferFrames > 0)
                {
                    stats.bufferFillPercent =
                        static_cast<double>(
                            padding) /
                        static_cast<double>(
                            _bufferFrames) *
                        100.0;
                }
            }
        }

        /*
         * Total duration of the WASAPI buffer.
         */
        if (_format &&
            _format->nSamplesPerSec > 0)
        {
            stats.bufferDurationMs =
                static_cast<double>(
                    _bufferFrames) /
                static_cast<double>(
                    _format->nSamplesPerSec) *
                1000.0;
        }

        stats.framesWritten =
            _framesWritten.load(
                std::memory_order_relaxed);

        stats.writeCalls =
            _writeCalls.load(
                std::memory_order_relaxed);

        stats.failedWrites =
            _failedWrites.load(
                std::memory_order_relaxed);

        /*
         * Calculate actual audio throughput
         * since statistics were initialized.
         */
        if (_statsStartTime
            .time_since_epoch()
            .count() != 0)
        {
            const auto now =
                std::chrono::steady_clock::now();

            const double elapsed =
                std::chrono::duration<double>(
                    now - _statsStartTime
                ).count();

            if (elapsed > 0.0)
            {
                stats.outputFramesPerSecond =
                    static_cast<double>(
                        stats.framesWritten) /
                    elapsed;

                const double bytesPerFrame =
                    _format
                    ? static_cast<double>(
                        _format->nBlockAlign)
                    : 0.0;

                stats.throughputMBps =
                    (
                        static_cast<double>(
                            stats.framesWritten) *
                        bytesPerFrame
                        ) /
                    elapsed /
                    (1024.0 * 1024.0);
            }
        }

        return stats;
    }

}