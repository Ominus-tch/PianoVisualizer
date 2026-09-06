#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "VSTEventList.h"

namespace vst
{
    class VSTAudio
    {
    public:
        VSTAudio(
            Steinberg::Vst::IAudioProcessor* processor,
            double sampleRate,
            int32_t maxBlockSize);

        ~VSTAudio();

        bool process();

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
            int16_t value,
            int16_t value2 = 0);

        // =====================================================
        // AUDIO CHANNELS
        // =====================================================

        const float* channel(
            int32_t index) const;

        const float* const* channels() const;

        int32_t numChannels() const;

        const float* leftChannel() const;
        const float* rightChannel() const;

        // =====================================================
        // AUDIO INFORMATION
        // =====================================================

        int32_t numSamples() const;

        double sampleRate() const;
        int32_t maxBlockSize() const;

        int32_t getLatencySamples() const
        {
            if (_processor)
                return _processor->getLatencySamples();

            return -1;
        }

    private:
        Steinberg::Vst::ProcessContext _processContext;

        Steinberg::Vst::IAudioProcessor* _processor;

        double _sampleRate;
        int32_t _maxBlockSize;

        // One audio buffer for every VST output channel.
        std::vector<std::vector<float>> _channelData;

        // Pointers passed to the VST.
        std::vector<float*> _channelBuffers;

        Steinberg::Vst::AudioBusBuffers _outputBus;

        VSTEventList _eventList;

        int32_t _numSamples;

        std::mutex _processMutex;
    };

}