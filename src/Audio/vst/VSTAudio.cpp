#include "VSTAudio.h"

#include "../../../util/Logger.h"

#include "pluginterfaces/base/funknownimpl.h"

#include <algorithm>
#include <cmath>

namespace vst
{
    VSTAudio::VSTAudio(
        Steinberg::Vst::IAudioProcessor* processor,
        double sampleRate,
        int32_t maxBlockSize)
        : _processor(processor),
        _sampleRate(sampleRate),
        _maxBlockSize(maxBlockSize),
        _numSamples(0)
    {
        Logger::Log(
            "[VST] VSTAudio created\n\n");

        if (!_processor)
        {
            Logger::Log(
                "[VST] Cannot create VSTAudio: processor is null\n");

            return;
        }

        // =====================================================
        // OUTPUT BUS
        // =====================================================

        Steinberg::Vst::SpeakerArrangement busArrangement{};

        if (_processor->getBusArrangement(
            Steinberg::Vst::kOutput,
            0,
            busArrangement) != Steinberg::kResultOk)
        {
            Logger::Log(
                "[VST] Failed to get output bus arrangement\n");

            return;
        }

        const int32_t channelCount =
            Steinberg::Vst::SpeakerArr::getChannelCount(
                busArrangement);

        if (channelCount <= 0)
        {
            Logger::Log(
                "[VST] VST reports invalid output channel count: %d\n",
                channelCount);

            return;
        }

        Logger::Log(
            "[VST] Output channels: %d\n",
            channelCount);

        // =====================================================
        // AUDIO BUFFERS
        // =====================================================

        _channelData.resize(
            channelCount);

        _channelBuffers.resize(
            channelCount);

        for (int32_t i = 0;
            i < channelCount;
            ++i)
        {
            _channelData[i].resize(
                maxBlockSize,
                0.0f);

            _channelBuffers[i] =
                _channelData[i].data();
        }

        // =====================================================
        // OUTPUT BUS
        // =====================================================

        _outputBus = {};

        _outputBus.numChannels =
            static_cast<Steinberg::int32>(
                channelCount);

        _outputBus.silenceFlags =
            0;

        _outputBus.channelBuffers32 =
            _channelBuffers.data();

        // =====================================================
        // PROCESS CONTEXT
        // =====================================================

        _processContext = {};

        _processContext.sampleRate =
            sampleRate;

        _processContext.projectTimeSamples =
            0.0;

        _processContext.projectTimeMusic =
            0.0;

        _processContext.tempo =
            120.0;

        _processContext.timeSigNumerator =
            4;

        _processContext.timeSigDenominator =
            4;

        _processContext.state =
            Steinberg::Vst::ProcessContext::kPlaying;
    }


    VSTAudio::~VSTAudio()
    {
        Logger::Log(
            "[VST] VSTAudio destroyed\n");
    }


    // =========================================================
    // PROCESS
    // =========================================================

    bool VSTAudio::process()
    {
        std::lock_guard<std::mutex> lock(
            _processMutex);

        if (!_processor)
        {
            Logger::Log(
                "[VST] Cannot process: processor is null\n");

            return false;
        }

        if (_channelData.empty() ||
            _channelBuffers.empty() ||
            _outputBus.numChannels <= 0)
        {
            Logger::Log(
                "[VST] Cannot process: "
                "no output channels\n");

            return false;
        }

        _numSamples =
            _maxBlockSize;

        // -----------------------------------------------------
        // Clear all output channels
        // -----------------------------------------------------

        for (auto& buffer : _channelData)
        {
            std::fill(
                buffer.begin(),
                buffer.end(),
                0.0f);
        }

        _outputBus.silenceFlags =
            0;

        // -----------------------------------------------------
        // Process data
        // -----------------------------------------------------

        Steinberg::Vst::ProcessData data{};

        data.processMode =
            Steinberg::Vst::kRealtime;

        data.symbolicSampleSize =
            Steinberg::Vst::kSample32;

        data.numSamples =
            _numSamples;

        data.numInputs =
            0;

        data.inputs =
            nullptr;

        data.numOutputs =
            1;

        data.outputs =
            &_outputBus;

        data.inputEvents =
            &_eventList;

        data.outputEvents =
            nullptr;

        data.processContext =
            &_processContext;

        const auto result =
            _processor->process(data);

        // -----------------------------------------------------
        // Update process context
        // -----------------------------------------------------

        _processContext.projectTimeSamples +=
            _numSamples;

        _processContext.projectTimeMusic +=
            static_cast<double>(_numSamples) *
            120.0 /
            (60.0 * _sampleRate);

        // -----------------------------------------------------
        // Clear queued events
        // -----------------------------------------------------

        _eventList.clear();

        // -----------------------------------------------------
        // Check result
        // -----------------------------------------------------

        if (result != Steinberg::kResultOk)
        {
            Logger::Log(
                "[VST] process() failed\n");

            return false;
        }

        return true;
    }


    // =========================================================
    // CHANNEL ACCESS
    // =========================================================

    const float* VSTAudio::channel(
        int32_t index) const
    {
        if (index < 0 ||
            index >= static_cast<int32_t>(
                _channelData.size()))
        {
            return nullptr;
        }

        return _channelData[index].data();
    }

    const float* const* vst::VSTAudio::channels() const
    {
        if (_channelBuffers.empty())
            return nullptr;

        return _channelBuffers.data();
    }


    int32_t VSTAudio::numChannels() const
    {
        return static_cast<int32_t>(
            _channelData.size());
    }


    const float* VSTAudio::leftChannel() const
    {
        return channel(0);
    }


    const float* VSTAudio::rightChannel() const
    {
        return channel(1);
    }


    // =========================================================
    // MIDI - NOTE ON
    // =========================================================

    bool VSTAudio::noteOn(
        int16_t channel,
        int16_t pitch,
        float velocity)
    {
        std::lock_guard<std::mutex> lock(
            _processMutex);

        if (!_processor)
        {
            Logger::Log(
                "[VST] Cannot send note on: "
                "processor is null\n");

            return false;
        }

        if (channel < 0 ||
            channel > 15)
        {
            Logger::Log(
                "[VST] Invalid MIDI channel: %d\n",
                channel);

            return false;
        }

        if (pitch < 0 ||
            pitch > 127)
        {
            Logger::Log(
                "[VST] Invalid MIDI pitch: %d\n",
                pitch);

            return false;
        }

        if (velocity < 0.0f ||
            velocity > 1.0f)
        {
            Logger::Log(
                "[VST] Invalid MIDI velocity: %f\n",
                velocity);

            return false;
        }

        Steinberg::Vst::Event event{};

        event.type =
            Steinberg::Vst::Event::kNoteOnEvent;

        event.busIndex =
            0;

        event.sampleOffset =
            0;

        event.flags =
            Steinberg::Vst::Event::kIsLive;

        event.noteOn.channel =
            channel;

        event.noteOn.pitch =
            pitch;

        event.noteOn.tuning =
            0.0f;

        event.noteOn.velocity =
            velocity;

        event.noteOn.length =
            0;

        event.noteOn.noteId =
            pitch;

        if (_eventList.addEvent(event) !=
            Steinberg::kResultTrue)
        {
            Logger::Log(
                "[VST] Failed to queue note on\n");

            return false;
        }

        /*Logger::Log(
            "[VST] Queued Note On: "
            "channel=%d pitch=%d velocity=%f\n",
            channel,
            pitch,
            velocity);*/

        return true;
    }


    // =========================================================
    // MIDI - NOTE OFF
    // =========================================================

    bool VSTAudio::noteOff(
        int16_t channel,
        int16_t pitch,
        float velocity)
    {
        std::lock_guard<std::mutex> lock(
            _processMutex);

        if (!_processor)
        {
            Logger::Log(
                "[VST] Cannot send note off: "
                "processor is null\n");

            return false;
        }

        if (channel < 0 ||
            channel > 15)
        {
            Logger::Log(
                "[VST] Invalid MIDI channel: %d\n",
                channel);

            return false;
        }

        if (pitch < 0 ||
            pitch > 127)
        {
            Logger::Log(
                "[VST] Invalid MIDI pitch: %d\n",
                pitch);

            return false;
        }

        if (velocity < 0.0f ||
            velocity > 1.0f)
        {
            Logger::Log(
                "[VST] Invalid MIDI velocity: %f\n",
                velocity);

            return false;
        }

        Steinberg::Vst::Event event{};

        event.type =
            Steinberg::Vst::Event::kNoteOffEvent;

        event.busIndex =
            0;

        event.sampleOffset =
            0;

        event.flags =
            Steinberg::Vst::Event::kIsLive;

        event.noteOff.channel =
            channel;

        event.noteOff.pitch =
            pitch;

        event.noteOff.tuning =
            0.0f;

        event.noteOff.velocity =
            velocity;

        event.noteOff.noteId =
            pitch;

        if (_eventList.addEvent(event) !=
            Steinberg::kResultTrue)
        {
            Logger::Log(
                "[VST] Failed to queue note off\n");

            return false;
        }

        /*Logger::Log(
            "[VST] Queued Note Off: "
            "channel=%d pitch=%d velocity=%f\n",
            channel,
            pitch,
            velocity);*/

        return true;
    }


    // =========================================================
    // MIDI - CONTROL CHANGE
    // =========================================================

    bool VSTAudio::controlChange(
        int16_t channel,
        int16_t controller,
        int16_t value,
        int16_t value2)
    {
        if (!_processor)
            return false;

        if (channel < 0 ||
            channel > 15 ||
            controller < 0 ||
            controller > 127 ||
            value < 0 ||
            value > 127)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(
            _processMutex);

        Steinberg::Vst::Event event{};

        event.type =
            Steinberg::Vst::Event::kLegacyMIDICCOutEvent;

        event.busIndex =
            0;

        event.sampleOffset =
            0;

        event.flags =
            Steinberg::Vst::Event::kIsLive;

        event.midiCCOut.channel =
            static_cast<Steinberg::int8>(
                channel);

        event.midiCCOut.controlNumber =
            static_cast<Steinberg::uint8>(
                controller);

        event.midiCCOut.value =
            static_cast<Steinberg::int8>(
                value);

        event.midiCCOut.value2 =
            static_cast<Steinberg::int8>(
                value2);

        if (_eventList.addEvent(event) !=
            Steinberg::kResultTrue)
        {
            Logger::Log(
                "[VST] Failed to queue "
                "control change\n");

            return false;
        }

        return true;
    }


    // =========================================================
    // AUDIO INFORMATION
    // =========================================================

    int32_t VSTAudio::numSamples() const
    {
        return _numSamples;
    }


    double VSTAudio::sampleRate() const
    {
        return _sampleRate;
    }


    int32_t VSTAudio::maxBlockSize() const
    {
        return _maxBlockSize;
    }

}