#include "MIDIEditorDocument.h"

#include <algorithm>

void MIDIEditorDocument::clear()
{
    format =
        MIDIEditorFormat::SingleTrack;

    ticksPerQuarterNote = 960;

    tracks.clear();

    tempos.clear();

    timeSignatures.clear();

    keySignatures.clear();

    filePath.clear();

    fileName.clear();

    dirty = false;

    nextNoteId = 1;
}

size_t MIDIEditorDocument::notesCount() const
{
    size_t count = 0;

    for (const auto& track : tracks)
    {
        count += track.notes.size();
    }

    return count;
}

uint64_t MIDIEditorDocument::durationTicks() const
{
    uint64_t duration = 0;

    for (const auto& track : tracks)
    {
        for (const auto& note : track.notes)
        {
            const uint64_t end =
                note.startTick +
                note.durationTick;

            if (end > duration)
            {
                duration = end;
            }
        }

        for (const auto& event : track.events)
        {
            if (event.tick > duration)
            {
                duration = event.tick;
            }
        }
    }

    return duration;
}

MIDIEditorNote* MIDIEditorDocument::findNote(
    uint64_t id
)
{
    for (auto& track : tracks)
    {
        for (auto& note : track.notes)
        {
            if (note.id == id)
            {
                return &note;
            }
        }
    }

    return nullptr;
}

const MIDIEditorNote* MIDIEditorDocument::findNote(
    uint64_t id
) const
{
    for (const auto& track : tracks)
    {
        for (const auto& note : track.notes)
        {
            if (note.id == id)
            {
                return &note;
            }
        }
    }

    return nullptr;
}

MIDIEditorNote* MIDIEditorDocument::addNote(
    size_t trackIndex,
    int pitch,
    int velocity,
    int channel,
    uint64_t startTick,
    uint64_t durationTick
)
{
    if (trackIndex >= tracks.size())
    {
        return nullptr;
    }

    if (pitch < 0 || pitch > 127)
    {
        return nullptr;
    }

    if (velocity < 0 || velocity > 127)
    {
        return nullptr;
    }

    if (channel < 0 || channel > 15)
    {
        return nullptr;
    }

    MIDIEditorNote note;

    note.id =
        nextNoteId++;

    note.pitch =
        pitch;

    note.velocity =
        velocity;

    note.channel =
        channel;

    note.startTick =
        startTick;

    note.durationTick =
        durationTick;

    tracks[trackIndex].notes.push_back(
        note
    );

    dirty = true;

    return &tracks[trackIndex].notes.back();
}

bool MIDIEditorDocument::removeNote(
    uint64_t id
)
{
    for (auto& track : tracks)
    {
        for (auto it =
            track.notes.begin();
            it != track.notes.end();
            ++it)
        {
            if (it->id == id)
            {
                track.notes.erase(it);

                dirty = true;

                return true;
            }
        }
    }

    return false;
}

void MIDIEditorDocument::sortNotes()
{
    for (auto& track : tracks)
    {
        std::sort(
            track.notes.begin(),
            track.notes.end(),
            [](const MIDIEditorNote& a,
                const MIDIEditorNote& b)
            {
                if (a.startTick != b.startTick)
                {
                    return
                        a.startTick <
                        b.startTick;
                }

                if (a.pitch != b.pitch)
                {
                    return
                        a.pitch <
                        b.pitch;
                }

                return
                    a.id <
                    b.id;
            }
        );

        std::sort(
            track.events.begin(),
            track.events.end(),
            [](const MIDIEditorEvent& a,
                const MIDIEditorEvent& b)
            {
                return a.tick < b.tick;
            }
        );
    }

    std::sort(
        tempos.begin(),
        tempos.end(),
        [](const MIDIEditorTempo& a,
            const MIDIEditorTempo& b)
        {
            return a.tick < b.tick;
        }
    );

    std::sort(
        timeSignatures.begin(),
        timeSignatures.end(),
        [](const MIDIEditorTimeSignature& a,
            const MIDIEditorTimeSignature& b)
        {
            return a.tick < b.tick;
        }
    );

    std::sort(
        keySignatures.begin(),
        keySignatures.end(),
        [](const MIDIEditorKeySignature& a,
            const MIDIEditorKeySignature& b)
        {
            return a.tick < b.tick;
        }
    );
}