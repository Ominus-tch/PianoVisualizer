#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class MIDIEditorFormat : uint16_t
{
    SingleTrack = 0,
    MultiTrack = 1
};

struct MIDIEditorNote
{
    uint64_t id = 0;

    int pitch = 60;
    int velocity = 100;
    int channel = 0;

    uint64_t startTick = 0;
    uint64_t durationTick = 0;
};

struct MIDIEditorEvent
{
    enum class Kind : uint8_t
    {
        MIDI,
        META,
        SYSEX
    };

    uint64_t tick = 0;

    Kind kind = Kind::MIDI;

    uint8_t status = 0;
    uint8_t channel = 0;
    uint8_t type = 0;

    std::vector<uint8_t> data;
};

struct MIDIEditorTrack
{
    std::string name;
    std::string instrument;

    std::vector<MIDIEditorNote> notes;
    std::vector<MIDIEditorEvent> events;
};

struct MIDIEditorTempo
{
    uint64_t tick = 0;

    uint32_t microsecondsPerQuarterNote = 500000;
};

struct MIDIEditorTimeSignature
{
    uint64_t tick = 0;

    uint8_t numerator = 4;
    uint8_t denominatorPower = 2;

    uint32_t denominator() const
    {
        return 1u << denominatorPower;
    }
};

struct MIDIEditorKeySignature
{
    uint64_t tick = 0;

    int8_t sharpsFlats = 0;
    bool minor = false;
};

struct MIDIEditorDocument
{
    MIDIEditorFormat format =
        MIDIEditorFormat::SingleTrack;

    uint16_t ticksPerQuarterNote = 960;

    std::vector<MIDIEditorTrack> tracks;

    std::vector<MIDIEditorTempo> tempos;

    std::vector<MIDIEditorTimeSignature>
        timeSignatures;

    std::vector<MIDIEditorKeySignature>
        keySignatures;

    std::string filePath;
    std::string fileName;

    bool dirty = false;

    uint64_t nextNoteId = 1;

    void clear();

    bool empty() const
    {
        return tracks.empty();
    }

    size_t notesCount() const;

    uint64_t durationTicks() const;

    MIDIEditorNote* findNote(
        uint64_t id
    );

    const MIDIEditorNote* findNote(
        uint64_t id
    ) const;

    MIDIEditorNote* addNote(
        size_t trackIndex,
        int pitch,
        int velocity,
        int channel,
        uint64_t startTick,
        uint64_t durationTick
    );

    bool removeNote(
        uint64_t id
    );

    void sortNotes();
};