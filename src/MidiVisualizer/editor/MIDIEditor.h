#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../midi/editor/MIDIEditorDocument.h"

enum class MIDIEditorGridSubdivision : uint8_t
{
    Whole = 1,
    Half = 2,
    Quarter = 4,
    Eighth = 8,
    Sixteenth = 16,
    ThirtySecond = 32
};

struct MIDIEditorCallbacks
{
    std::function<void()> playPause;
    std::function<void()> stop;
    std::function<void()> restart;
    std::function<void(double)> seek;
    std::function<bool()> isPlaying;
    std::function<void(bool)> setPlaying;

    std::function<void(
        int,
        int,
        float
        )> noteOn;

    std::function<void(
        int,
        int
        )> noteOff;
};

class MIDIEditor
{
public:
    MIDIEditor() = default;
    ~MIDIEditor() = default;

    void setCallbacks(
        const MIDIEditorCallbacks& callbacks
    );

    void setDocumentChangedCallback(
        std::function<void()> callback
    );

    bool openFile(
        const std::string& filePath
    );

    bool save();

    void close();

    bool draw(
        float currentTime
    );

    bool hasDocument() const
    {
        return _hasDocument;
    }

    const MIDIEditorDocument& document() const
    {
        return _document;
    }

    MIDIEditorDocument& document()
    {
        return _document;
    }

    const std::string& lastError() const
    {
        return _lastError;
    }

private:
    void drawPianoRoll(
        float currentTime
    );

    uint64_t secondsToTick(
        double seconds
    ) const;

    double tickToSeconds(
        uint64_t tick
    ) const;

    static const char* gridSubdivisionName(
        MIDIEditorGridSubdivision subdivision
    );

    uint64_t gridTicks() const;
    uint64_t beatTicks() const;
    uint64_t measureTicks() const;

    void saveUndoState();
    void undo();
    void redo();

    void notifyDocumentChanged();

    void startAudition(
        int pitch,
        int channel,
        float velocity
    );

    void stopAudition();

    uint64_t findHoveredNote(
        float mouseX,
        float mouseY,
        float originX,
        float originY,
        float gridX,
        float pixelsPerTick
    ) const;

    bool noteExistsAt(
        size_t trackIndex,
        int pitch,
        int channel,
        uint64_t startTick,
        uint64_t ignoreNoteId = 0
    ) const;

    MIDIEditorDocument _document;
    MIDIEditorCallbacks _callbacks;

    std::function<void()> _onDocumentChanged;

    std::string _lastError;
    bool _hasDocument = false;

    uint64_t _viewStartTick = 0;

    float _pixelsPerTick = 0.25f;

    bool _followPlayback = true;

    bool _playbackWasPlayingBeforeSeek = false;

    int _gridTimeSignatureNumerator = 4;
    int _gridTimeSignatureDenominator = 4;

    MIDIEditorGridSubdivision _gridSubdivision =
        MIDIEditorGridSubdivision::Sixteenth;

    bool _snapEnabled = true;

    bool _isDraggingNote = false;
    bool _isResizingNote = false;

    uint64_t _activeNoteId = 0;

    int _editOriginalPitch = 0;

    uint64_t _editOriginalStartTick = 0;
    uint64_t _editOriginalDurationTick = 0;

    double _dragMouseOffsetTicks = 0.0;
    float _dragMouseOffsetY = 0.0f;

    int _auditionPitch = -1;
    int _auditionChannel = 0;
    bool _auditioning = false;

    bool _editUndoSaved = false;

    bool _pianoRollHovered = false;


    static constexpr float NoteResizeHandleWidth = 7.0f;

    static constexpr float MinPixelsPerTick = 0.02f;
    static constexpr float MaxPixelsPerTick = 4.0f;

    static constexpr size_t MaxHistorySize = 256;

    std::vector<MIDIEditorDocument> _undoHistory;
    std::vector<MIDIEditorDocument> _redoHistory;
};