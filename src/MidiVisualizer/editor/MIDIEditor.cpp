#include "MIDIEditor.h"
#include "../midi/editor/MIDIEditorIO.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include <functional>

#include <imgui/imgui.h>

static std::string formatTime(
    double time
)
{
    time =
        (std::max)(
            0.0,
            time
            );

    const int totalSeconds =
        static_cast<int>(time);

    const int hours =
        totalSeconds / 3600;

    const int minutes =
        (totalSeconds % 3600) / 60;

    const int seconds =
        totalSeconds % 60;

    char buffer[32]{};

    if (hours > 0)
    {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%d:%02d:%02d",
            hours,
            minutes,
            seconds
        );
    }
    else
    {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%d:%02d",
            minutes,
            seconds
        );
    }

    return std::string(buffer);
}

// ============================================================
// Callbacks
// ============================================================

void MIDIEditor::setCallbacks(
    const MIDIEditorCallbacks& callbacks
)
{
    _callbacks = callbacks;
}

void MIDIEditor::setDocumentChangedCallback(
    std::function<void()> callback
)
{
    _onDocumentChanged =
        std::move(callback);
}

void MIDIEditor::notifyDocumentChanged()
{
    if (_onDocumentChanged)
    {
        _onDocumentChanged();
    }
}

bool MIDIEditor::isNoteSelected(
    uint64_t noteId
) const
{
    return std::find(
        _selectedNoteIds.begin(),
        _selectedNoteIds.end(),
        noteId
    ) != _selectedNoteIds.end();
}


void MIDIEditor::clearSelection()
{
    _selectedNoteIds.clear();

    _activeNoteId = 0;

    _editOriginalSelectionNotes.clear();

    _velocityUndoSaved = false;

    _velocityEditorValue = 100;
}


void MIDIEditor::setSingleSelection(
    uint64_t noteId
)
{
    _selectedNoteIds.clear();

    if (noteId != 0)
    {
        _selectedNoteIds.push_back(noteId);
    }

    _activeNoteId = noteId;

    _editOriginalSelectionNotes.clear();

    _velocityUndoSaved = false;

    refreshSelectionVelocity();
}


void MIDIEditor::appendSelection(
    uint64_t noteId
)
{
    if (noteId == 0)
    {
        return;
    }

    if (!isNoteSelected(noteId))
    {
        _selectedNoteIds.push_back(noteId);
    }

    _activeNoteId = noteId;

    _editOriginalSelectionNotes.clear();

    _velocityUndoSaved = false;

    refreshSelectionVelocity();
}


void MIDIEditor::refreshSelectionVelocity()
{
    if (_selectedNoteIds.empty())
    {
        _velocityEditorValue = 100;

        return;
    }

    const MIDIEditorNote* note =
        _document.findNote(
            _selectedNoteIds.front()
        );

    if (note)
    {
        _velocityEditorValue =
            (std::clamp)(
                note->velocity,
                1,
                127
                );
    }
    else
    {
        _velocityEditorValue = 100;
    }
}

void MIDIEditor::copySelection()
{
    _clipboardNotes.clear();

    if (_selectedNoteIds.empty())
    {
        return;
    }

    _clipboardNotes.reserve(
        _selectedNoteIds.size()
    );

    for (const uint64_t noteId : _selectedNoteIds)
    {
        const MIDIEditorNote* note =
            _document.findNote(noteId);

        if (!note)
        {
            continue;
        }

        const double noteOffsetTicks =
            static_cast<double>(
                note->startTick
                ) -
            static_cast<double>(
                _viewStartTick
                );

        const float xOffsetPixels =
            static_cast<float>(
                noteOffsetTicks *
                static_cast<double>(
                    _pixelsPerTick
                    )
                );

        ClipboardNote clipboardNote;

        clipboardNote.xOffsetPixels =
            xOffsetPixels;

        clipboardNote.pitch =
            note->pitch;

        clipboardNote.velocity =
            note->velocity;

        clipboardNote.channel =
            note->channel;

        clipboardNote.durationTick =
            note->durationTick;

        _clipboardNotes.push_back(
            clipboardNote
        );
    }
}

void MIDIEditor::cutSelection()
{
    if (_selectedNoteIds.empty())
    {
        return;
    }

    copySelection();

    saveUndoState();

    bool removedAny = false;

    for (const uint64_t noteId : _selectedNoteIds)
    {
        if (_document.removeNote(noteId))
        {
            removedAny = true;
        }
    }

    if (!removedAny)
    {
        if (!_undoHistory.empty())
        {
            _undoHistory.pop_back();
        }

        return;
    }

    _selectedNoteIds.clear();

    _activeNoteId = 0;

    _document.sortNotes();

    _document.dirty = true;

    notifyDocumentChanged();
}

void MIDIEditor::pasteSelection()
{
    if (_clipboardNotes.empty())
    {
        return;
    }

    if (_document.tracks.empty())
    {
        _document.tracks.push_back(
            MIDIEditorTrack{}
        );
    }

    const uint64_t PasteCollisionOffsetTicks =
        _document.ticksPerQuarterNote / 8;

    // ------------------------------------------------------------
    // Calculate the initial paste positions.
    // ------------------------------------------------------------

    std::vector<uint64_t> pasteStartTicks;

    pasteStartTicks.reserve(
        _clipboardNotes.size()
    );

    for (const ClipboardNote& clipboardNote :
        _clipboardNotes)
    {
        double tickOffset =
            static_cast<double>(
                clipboardNote.xOffsetPixels
                ) /
            static_cast<double>(
                _pixelsPerTick
                );

        double startTick =
            static_cast<double>(
                _viewStartTick
                ) +
            tickOffset;

        if (startTick < 0.0)
        {
            startTick = 0.0;
        }

        uint64_t newStartTick =
            static_cast<uint64_t>(
                std::llround(
                    startTick
                )
                );

        if (_snapEnabled)
        {
            const uint64_t snapTicks =
                gridTicks();

            if (snapTicks > 0)
            {
                newStartTick =
                    static_cast<uint64_t>(
                        std::llround(
                            static_cast<double>(
                                newStartTick
                                ) /
                            static_cast<double>(
                                snapTicks
                                )
                        )
                        ) *
                    snapTicks;
            }
        }

        pasteStartTicks.push_back(
            newStartTick
        );
    }

    // ------------------------------------------------------------
    // Find a collision-free offset.
    //
    // The whole pasted group moves together.
    // ------------------------------------------------------------

    uint64_t collisionOffset = 0;

    while (true)
    {
        bool collision = false;

        for (size_t i = 0;
            i < _clipboardNotes.size();
            ++i)
        {
            const uint64_t testStartTick =
                pasteStartTicks[i] +
                collisionOffset;

            const ClipboardNote& clipboardNote =
                _clipboardNotes[i];

            if (noteExistsAt(
                0,
                clipboardNote.pitch,
                clipboardNote.channel,
                testStartTick
            ))
            {
                collision = true;
                break;
            }
        }

        if (!collision)
        {
            break;
        }

        collisionOffset +=
            PasteCollisionOffsetTicks;
    }

    // ------------------------------------------------------------
    // Create the pasted notes.
    // ------------------------------------------------------------

    saveUndoState();

    std::vector<uint64_t> pastedNoteIds;

    pastedNoteIds.reserve(
        _clipboardNotes.size()
    );

    for (size_t i = 0;
        i < _clipboardNotes.size();
        ++i)
    {
        const ClipboardNote& clipboardNote =
            _clipboardNotes[i];

        const uint64_t newStartTick =
            pasteStartTicks[i] +
            collisionOffset;

        MIDIEditorNote* newNote =
            _document.addNote(
                0,
                clipboardNote.pitch,
                clipboardNote.velocity,
                clipboardNote.channel,
                newStartTick,
                clipboardNote.durationTick
            );

        if (newNote)
        {
            pastedNoteIds.push_back(
                newNote->id
            );
        }
    }

    if (pastedNoteIds.empty())
    {
        if (!_undoHistory.empty())
        {
            _undoHistory.pop_back();
        }

        return;
    }

    // ------------------------------------------------------------
    // Replace the previous selection with
    // the newly pasted notes.
    // ------------------------------------------------------------

    _selectedNoteIds =
        std::move(
            pastedNoteIds
        );

    _activeNoteId =
        _selectedNoteIds.empty()
        ? 0
        : _selectedNoteIds.front();

    _document.sortNotes();

    _document.dirty = true;

    _followPlayback = false;

    notifyDocumentChanged();
}

size_t MIDIEditor::findNoteTrackIndex(
    uint64_t noteId
) const
{
    for (
        size_t trackIndex = 0;
        trackIndex < _document.tracks.size();
        ++trackIndex
        )
    {
        const auto& track =
            _document.tracks[trackIndex];

        for (const auto& note : track.notes)
        {
            if (note.id == noteId)
            {
                return trackIndex;
            }
        }
    }

    return static_cast<size_t>(-1);
}


bool MIDIEditor::noteExistsIgnoringSelection(
    size_t trackIndex,
    int pitch,
    int channel,
    uint64_t startTick
) const
{
    if (trackIndex >= _document.tracks.size())
    {
        return false;
    }

    const auto& track =
        _document.tracks[trackIndex];

    for (const auto& note : track.notes)
    {
        if (
            note.pitch != pitch ||
            note.channel != channel ||
            note.startTick != startTick
            )
        {
            continue;
        }

        if (isNoteSelected(note.id))
        {
            continue;
        }

        return true;
    }

    return false;
}


void MIDIEditor::startAudition(
    int pitch,
    int channel,
    float velocity
)
{
    pitch =
        (std::max)(
            0,
            (std::min)(
                127,
                pitch
                )
            );

    channel =
        (std::max)(
            0,
            (std::min)(
                15,
                channel
                )
            );

    velocity =
        (std::max)(
            1.0f / 127.0f,
            (std::min)(
                1.0f,
                velocity
                )
            );

    // Already playing exactly this pitch.
    // Do not retrigger it every frame.
    if (
        _auditioning &&
        _auditionPitch == pitch &&
        _auditionChannel == channel
        )
    {
        return;
    }

    // Release the previous preview note.
    stopAudition();

    _auditionPitch =
        pitch;

    _auditionChannel =
        channel;

    _auditioning =
        true;

    if (_callbacks.noteOn)
    {
        _callbacks.noteOn(
            channel,
            pitch,
            velocity
        );
    }
}


void MIDIEditor::stopAudition()
{
    if (!_auditioning)
    {
        return;
    }

    if (_callbacks.noteOff)
    {
        _callbacks.noteOff(
            _auditionChannel,
            _auditionPitch
        );
    }

    _auditionPitch =
        -1;

    _auditionChannel =
        0;

    _auditioning =
        false;

    _auditionFromKeyboard =
        false;
}

// ============================================================
// File handling
// ============================================================

bool MIDIEditor::openFile(
    const std::string& filePath
)
{
    MIDIEditorDocument loaded;

    std::string error;

    if (!MIDIEditorIO::load(
        filePath,
        loaded,
        error
    ))
    {
        _lastError =
            error;

        return false;
    }

    _document =
        std::move(
            loaded
        );

    _lastError.clear();

    _hasDocument = true;

    // ------------------------------------------------------------
    // Reset view state
    // ------------------------------------------------------------

    _viewStartTick = 0;

    _followPlayback = true;

    _playbackWasPlayingBeforeSeek =
        false;

    // ------------------------------------------------------------
    // Reset note editing state
    // ------------------------------------------------------------

    _isDraggingNote = false;

    _isResizingNote = false;

    _activeNoteId = 0;

    _editOriginalPitch = 0;

    _editOriginalStartTick = 0;

    _editOriginalDurationTick = 0;

    _dragMouseOffsetTicks = 0.0;

    _dragMouseOffsetY = 0.0f;

    _editUndoSaved = false;

    stopAudition();

    clearSelection();

    // ------------------------------------------------------------
    // Clear edit history
    // ------------------------------------------------------------

    _undoHistory.clear();

    _redoHistory.clear();

    _undoHistory.reserve(
        MaxHistorySize
    );

    _redoHistory.reserve(
        MaxHistorySize
    );

    // ------------------------------------------------------------
    // Initialize editor grid format from MIDI.
    //
    // This does NOT modify the MIDI document.
    // ------------------------------------------------------------

    _gridTimeSignatureNumerator =
        4;

    _gridTimeSignatureDenominator =
        4;

    if (!_document.timeSignatures.empty())
    {
        const MIDIEditorTimeSignature& signature =
            _document.timeSignatures.front();

        _gridTimeSignatureNumerator =
            static_cast<int>(
                signature.numerator
                );

        _gridTimeSignatureDenominator =
            static_cast<int>(
                signature.denominator()
                );
    }

    if (_gridTimeSignatureNumerator < 1)
    {
        _gridTimeSignatureNumerator = 1;
    }

    if (_gridTimeSignatureNumerator > 32)
    {
        _gridTimeSignatureNumerator = 32;
    }

    if (_gridTimeSignatureDenominator < 1)
    {
        _gridTimeSignatureDenominator = 4;
    }

    return true;
}

bool MIDIEditor::save()
{
    if (!_hasDocument)
    {
        _lastError =
            "No MIDI document is open.";

        return false;
    }

    if (_document.filePath.empty())
    {
        _lastError =
            "The MIDI document has no file path.";

        return false;
    }

    std::string error;

    if (!MIDIEditorIO::save(
        _document.filePath,
        _document,
        error
    ))
    {
        _lastError =
            error;

        return false;
    }

    _document.dirty = false;

    _lastError.clear();

    return true;
}

void MIDIEditor::close()
{
    _document.clear();

    _lastError.clear();

    _hasDocument = false;

    _viewStartTick = 0;

    _followPlayback = true;

    _playbackWasPlayingBeforeSeek =
        false;

    _gridTimeSignatureNumerator =
        4;

    _gridTimeSignatureDenominator =
        4;

    _gridSubdivision =
        MIDIEditorGridSubdivision::Sixteenth;

    _snapEnabled = false;

    // ------------------------------------------------------------
    // Reset note editing state
    // ------------------------------------------------------------

    _isDraggingNote = false;

    _isResizingNote = false;

    _activeNoteId = 0;

    _editOriginalPitch = 0;

    _editOriginalStartTick = 0;

    _editOriginalDurationTick = 0;

    _dragMouseOffsetTicks = 0.0;

    _dragMouseOffsetY = 0.0f;

    _editUndoSaved = false;

    stopAudition();

    clearSelection();

    // ------------------------------------------------------------
    // Clear history
    // ------------------------------------------------------------

    _undoHistory.clear();

    _redoHistory.clear();
}

// ============================================================
// Undo / redo
// ============================================================

void MIDIEditor::saveUndoState()
{
    _undoHistory.push_back(
        _document
    );

    if (
        _undoHistory.size() >
        MaxHistorySize
        )
    {
        _undoHistory.erase(
            _undoHistory.begin()
        );
    }

    _redoHistory.clear();
}

void MIDIEditor::undo()
{
    if (_undoHistory.empty())
    {
        return;
    }

    _redoHistory.push_back(
        _document
    );

    if (
        _redoHistory.size() >
        MaxHistorySize
        )
    {
        _redoHistory.erase(
            _redoHistory.begin()
        );
    }

    _document =
        std::move(
            _undoHistory.back()
        );

    _undoHistory.pop_back();

    _document.sortNotes();

    clearSelection();

    _document.dirty = true;

    notifyDocumentChanged();
}

void MIDIEditor::redo()
{
    if (_redoHistory.empty())
    {
        return;
    }

    _undoHistory.push_back(
        _document
    );

    if (
        _undoHistory.size() >
        MaxHistorySize
        )
    {
        _undoHistory.erase(
            _undoHistory.begin()
        );
    }

    _document =
        std::move(
            _redoHistory.back()
        );

    _redoHistory.pop_back();

    _document.sortNotes();

    clearSelection();

    _document.dirty = true;

    notifyDocumentChanged();
}

// ============================================================
// Grid helpers
// ============================================================

const char* MIDIEditor::gridSubdivisionName(
    MIDIEditorGridSubdivision subdivision
)
{
    switch (subdivision)
    {
    case MIDIEditorGridSubdivision::Whole:
        return "1/1";

    case MIDIEditorGridSubdivision::Half:
        return "1/2";

    case MIDIEditorGridSubdivision::Quarter:
        return "1/4";

    case MIDIEditorGridSubdivision::Eighth:
        return "1/8";

    case MIDIEditorGridSubdivision::Sixteenth:
        return "1/16";

    case MIDIEditorGridSubdivision::ThirtySecond:
        return "1/32";

    default:
        return "1/16";
    }
}

uint64_t MIDIEditor::gridTicks() const
{
    const uint64_t ppq =
        _document.ticksPerQuarterNote;

    if (ppq == 0)
    {
        return 1;
    }

    const uint64_t subdivision =
        static_cast<uint64_t>(
            _gridSubdivision
            );

    if (subdivision == 0)
    {
        return ppq / 4;
    }

    /*
     * Whole        = 4 quarter notes
     * Half         = 2 quarter notes
     * Quarter      = 1 quarter note
     * Eighth       = 1/2 quarter note
     * Sixteenth    = 1/4 quarter note
     * Thirtysecond = 1/8 quarter note
     */

    const double ticks =
        static_cast<double>(ppq) *
        4.0 /
        static_cast<double>(
            subdivision
            );

    uint64_t result =
        static_cast<uint64_t>(
            std::llround(
                ticks
            )
            );

    if (result == 0)
    {
        result = 1;
    }

    return result;
}

uint64_t MIDIEditor::beatTicks() const
{
    const uint64_t ppq =
        _document.ticksPerQuarterNote;

    if (ppq == 0)
    {
        return 1;
    }

    if (_gridTimeSignatureDenominator <= 0)
    {
        return ppq;
    }

    const double ticks =
        static_cast<double>(ppq) *
        4.0 /
        static_cast<double>(
            _gridTimeSignatureDenominator
            );

    uint64_t result =
        static_cast<uint64_t>(
            std::llround(
                ticks
            )
            );

    if (result == 0)
    {
        result = 1;
    }

    return result;
}

uint64_t MIDIEditor::measureTicks() const
{
    const uint64_t beats =
        static_cast<uint64_t>(
            _gridTimeSignatureNumerator
            );

    const uint64_t ticksPerBeat =
        beatTicks();

    if (beats == 0)
    {
        return ticksPerBeat;
    }

    return
        ticksPerBeat *
        beats;
}

// ============================================================
// Main draw
// ============================================================

bool MIDIEditor::draw(
    float currentTime
)
{
    if (!_hasDocument)
    {
        return false;
    }

    bool open = true;

    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_None;

    if (_pianoRollHovered)
    {
        windowFlags |=
            ImGuiWindowFlags_NoMove;
    }

    if (!ImGui::Begin(
        "MIDI Editor",
        &open,
        windowFlags
    ))
    {
        ImGui::End();

        return false;
    }

    if (!open)
    {
        ImGui::End();

        return false;
    }

    // ============================================================
    // Undo / redo / save keyboard shortcuts
    // ============================================================

    if (
        ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows
        ) &&
        !ImGui::IsAnyItemActive() &&
        !_isDraggingNote &&
        !_isResizingNote
        )
    {
        ImGuiIO& io =
            ImGui::GetIO();

        if (
            io.KeyCtrl &&
            ImGui::IsKeyPressed(
                ImGuiKey_S,
                false
            )
            )
        {
            save();
        }

        if (
            io.KeyCtrl &&
            ImGui::IsKeyPressed(
                ImGuiKey_Z,
                false
            )
            )
        {
            undo();
        }

        if (
            io.KeyCtrl &&
            ImGui::IsKeyPressed(
                ImGuiKey_Y,
                false
            )
            )
        {
            redo();
        }

        if (
            io.KeyCtrl &&
            ImGui::IsKeyPressed(
                ImGuiKey_C,
                false
            )
            )
        {
            copySelection();
        }

        if (
            io.KeyCtrl &&
            ImGui::IsKeyPressed(
                ImGuiKey_X,
                false
            )
            )
        {
            cutSelection();
        }

        if (
            io.KeyCtrl &&
            ImGui::IsKeyPressed(
                ImGuiKey_V,
                false
            )
            )
        {
            pasteSelection();
        }
    }

    // ------------------------------------------------------------
    // Header
    // ------------------------------------------------------------

    ImGui::Text(
        "%s",
        _document.fileName.empty()
        ? "Untitled MIDI"
        : _document.fileName.c_str()
    );

    ImGui::SameLine();

    if (ImGui::Button("Save"))
    {
        save();
    }

    if (_document.dirty)
    {
        ImGui::SameLine();

        ImGui::TextDisabled(
            "Modified"
        );
    }

    ImGui::SameLine();

    ImGui::TextDisabled(
        "%zu notes | %u PPQ | %zu tracks",
        _document.notesCount(),
        static_cast<unsigned>(
            _document.ticksPerQuarterNote
            ),
        _document.tracks.size()
    );

    ImGui::SameLine();

    if (!_undoHistory.empty())
    {
        ImGui::TextDisabled(
            "| Undo: %zu",
            _undoHistory.size()
        );
    }

    if (!_redoHistory.empty())
    {
        ImGui::SameLine();

        ImGui::TextDisabled(
            "| Redo: %zu",
            _redoHistory.size()
        );
    }

    ImGui::Separator();

    // ------------------------------------------------------------
    // Transport
    // ------------------------------------------------------------

    if (ImGui::Button(
        "Play / Pause"
    ))
    {
        if (_callbacks.playPause)
        {
            _callbacks.playPause();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button(
        "Stop"
    ))
    {
        if (_callbacks.stop)
        {
            _callbacks.stop();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button(
        "Restart"
    ))
    {
        if (_callbacks.restart)
        {
            _callbacks.restart();
        }
    }

    // ============================================================
    // Grid / musical format
    // ============================================================

    ImGui::Spacing();

    ImGui::Text(
        "Format"
    );

    ImGui::SameLine();

    if (ImGui::Button(
        " - "
    ))
    {
        --_gridTimeSignatureNumerator;

        if (_gridTimeSignatureNumerator < 1)
        {
            _gridTimeSignatureNumerator = 1;
        }
    }

    ImGui::SameLine();

    ImGui::Text(
        "%d",
        _gridTimeSignatureNumerator
    );

    ImGui::SameLine();

    if (ImGui::Button(
        " + "
    ))
    {
        ++_gridTimeSignatureNumerator;

        if (_gridTimeSignatureNumerator > 32)
        {
            _gridTimeSignatureNumerator = 32;
        }
    }

    ImGui::SameLine();

    ImGui::Text(
        "/"
    );

    ImGui::SameLine();

    ImGui::SetNextItemWidth(
        40.0f
    );

    char denominatorPreview[16]{};

    std::snprintf(
        denominatorPreview,
        sizeof(denominatorPreview),
        "%d",
        _gridTimeSignatureDenominator
    );

    if (ImGui::BeginCombo(
        "##GridDenominator",
        denominatorPreview
    ))
    {
        constexpr int denominators[] =
        {
            1,
            2,
            4,
            8,
            16,
            32
        };

        for (int value : denominators)
        {
            const bool selected =
                _gridTimeSignatureDenominator ==
                value;

            char label[16]{};

            std::snprintf(
                label,
                sizeof(label),
                "%d",
                value
            );

            if (ImGui::Selectable(
                label,
                selected
            ))
            {
                _gridTimeSignatureDenominator =
                    value;
            }

            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SameLine();

    ImGui::Text(
        "Grid"
    );

    ImGui::SameLine();

    ImGui::SetNextItemWidth(
        75.0f
    );

    if (ImGui::BeginCombo(
        "##GridSubdivision",
        gridSubdivisionName(
            _gridSubdivision
        )
    ))
    {
        constexpr MIDIEditorGridSubdivision subdivisions[] =
        {
            MIDIEditorGridSubdivision::Whole,
            MIDIEditorGridSubdivision::Half,
            MIDIEditorGridSubdivision::Quarter,
            MIDIEditorGridSubdivision::Eighth,
            MIDIEditorGridSubdivision::Sixteenth,
            MIDIEditorGridSubdivision::ThirtySecond
        };

        for (
            const auto subdivision :
            subdivisions
            )
        {
            const bool selected =
                _gridSubdivision ==
                subdivision;

            if (ImGui::Selectable(
                gridSubdivisionName(
                    subdivision
                ),
                selected
            ))
            {
                _gridSubdivision =
                    subdivision;
            }

            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SameLine();

    ImGui::Checkbox(
        "Snap",
        &_snapEnabled
    );

    ImGui::SameLine();

    if (!_followPlayback)
    {
        if (ImGui::Button(
            "Sync View"
        ))
        {
            _followPlayback = true;
        }
    }

    // ============================================================
    // Selected note velocity
    // ============================================================

    if (!_selectedNoteIds.empty())
    {
        ImGui::SameLine();

        ImGui::Text(
            "Selected: %zu",
            _selectedNoteIds.size()
        );

        ImGui::SameLine();

        ImGui::Text(
            "Velocity"
        );

        ImGui::SameLine();

        ImGui::SetNextItemWidth(
            180.0f
        );

        int velocity =
            _velocityEditorValue;

        if (ImGui::SliderInt(
            "##SelectedVelocity",
            &velocity,
            1,
            127,
            "%d"
        ))
        {
            velocity =
                (std::clamp)(
                    velocity,
                    1,
                    127
                    );

            _velocityEditorValue =
                velocity;

            if (!_velocityUndoSaved)
            {
                saveUndoState();

                _velocityUndoSaved =
                    true;
            }

            bool changed = false;

            for (const uint64_t noteId :
            _selectedNoteIds)
            {
                MIDIEditorNote* note =
                    _document.findNote(
                        noteId
                    );

                if (!note)
                {
                    continue;
                }

                if (note->velocity != velocity)
                {
                    note->velocity =
                        velocity;

                    changed = true;
                }
            }

            if (changed)
            {
                _document.dirty =
                    true;

                notifyDocumentChanged();
            }
        }

        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            _velocityUndoSaved =
                false;
        }
    }

    // ============================================================
    // MIDI timeline
    // ============================================================

    const double durationSeconds =
        tickToSeconds(
            _document.durationTicks()
        );

    const float duration =
        durationSeconds > 0.0
        ? static_cast<float>(
            durationSeconds
            )
        : 0.0f;

    float seekTime =
        static_cast<float>(
            (std::clamp)(
                static_cast<double>(
                    currentTime
                    ),
                0.0,
                durationSeconds
                )
            );

    ImGui::Spacing();

    ImGui::Separator();

    ImGui::Spacing();

    // ---------------------------------------------------------
    // Time labels
    // ---------------------------------------------------------

    const std::string currentTimeString =
        formatTime(
            seekTime
        );

    const std::string totalTimeString =
        formatTime(
            duration
        );

    ImGui::Text(
        "%s",
        currentTimeString.c_str()
    );

    ImGui::SameLine();

    ImGui::TextDisabled(
        "/ %s",
        totalTimeString.c_str()
    );

    // ---------------------------------------------------------
    // Seek bar
    // ---------------------------------------------------------

    ImGui::PushItemWidth(
        -1.0f
    );

    static float previousSeekTime =
        -1.0f;

    if (previousSeekTime < 0.0f)
    {
        previousSeekTime =
            seekTime;
    }

    float newSeekTime =
        seekTime;

    const bool shouldPlayDupe =
        _callbacks.isPlaying
        ? _callbacks.isPlaying()
        : false;

    if (ImGui::SliderFloat(
        "##MIDITimeline",
        &newSeekTime,
        0.0f,
        duration,
        ""
    ))
    {
        newSeekTime =
            (std::clamp)(
                newSeekTime,
                0.0f,
                duration
                );

        if (newSeekTime != previousSeekTime)
        {
            if (_callbacks.seek)
            {
                _callbacks.seek(
                    static_cast<double>(
                        newSeekTime
                        )
                );
            }

            previousSeekTime =
                newSeekTime;
        }
    }

    if (ImGui::IsItemActivated())
    {
        _playbackWasPlayingBeforeSeek =
            shouldPlayDupe;
    }

    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        if (_callbacks.setPlaying)
        {
            _callbacks.setPlaying(
                _playbackWasPlayingBeforeSeek
            );
        }
    }

    ImGui::PopItemWidth();

    ImGui::Separator();

    // ------------------------------------------------------------
    // Piano roll
    // ------------------------------------------------------------

    drawPianoRoll(
        currentTime
    );

    ImGui::End();

    return true;
}

// ============================================================
// Find note under mouse cursor
// ============================================================

uint64_t MIDIEditor::findHoveredNote(
    float mouseX,
    float mouseY,
    float originX,
    float originY,
    float gridX,
    float pixelsPerTick
) const
{
    constexpr float rowHeight = 18.0f;

    constexpr int minPitch = 21;
    constexpr int maxPitch = 108;

    if (mouseX < gridX)
    {
        return 0;
    }

    const int row =
        static_cast<int>(
            (
                mouseY -
                originY
                ) /
            rowHeight
            );

    if (
        row < 0 ||
        row > maxPitch - minPitch
        )
    {
        return 0;
    }

    const int pitch =
        maxPitch -
        row;

    const float mouseTick =
        static_cast<float>(
            _viewStartTick
            ) +
        (
            mouseX -
            gridX
            ) /
        pixelsPerTick;

    for (
        const auto& track :
        _document.tracks
        )
    {
        for (
            const auto& note :
            track.notes
            )
        {
            if (note.pitch != pitch)
            {
                continue;
            }

            const float noteStart =
                static_cast<float>(
                    note.startTick
                    );

            const float noteEnd =
                static_cast<float>(
                    note.startTick +
                    note.durationTick
                    );

            if (
                mouseTick >= noteStart &&
                mouseTick <= noteEnd
                )
            {
                return note.id;
            }
        }
    }

    return 0;
}

// ============================================================
// Check if an identical note already exists
// ============================================================

bool MIDIEditor::noteExistsAt(
    size_t trackIndex,
    int pitch,
    int channel,
    uint64_t startTick,
    uint64_t ignoreNoteId
) const
{
    if (
        trackIndex >=
        _document.tracks.size()
        )
    {
        return false;
    }

    const auto& track =
        _document.tracks[
            trackIndex
        ];

    for (
        const auto& note :
        track.notes
        )
    {
        if (
            note.id != ignoreNoteId &&
            note.pitch == pitch &&
            note.channel == channel &&
            note.startTick == startTick
            )
        {
            return true;
        }
    }

    return false;
}

// ============================================================
// Piano roll
// ============================================================

void MIDIEditor::drawPianoRoll(
    float currentTime
)
{
    constexpr float keyWidth = 60.0f;

    constexpr float rowHeight = 18.0f;

    constexpr int minPitch = 21;

    constexpr int maxPitch = 108;

    const ImVec2 available =
        ImGui::GetContentRegionAvail();

    // ------------------------------------------------------------
    // Piano roll scrolling region
    // ------------------------------------------------------------

    ImGui::BeginChild(
        "PianoRoll",
        ImVec2(
            available.x,
            available.y
        ),
        true,
        ImGuiWindowFlags_None
    );

    const ImVec2 origin =
        ImGui::GetCursorScreenPos();

    const uint64_t ppq =
        _document.ticksPerQuarterNote;

    if (ppq == 0)
    {
        ImGui::EndChild();

        return;
    }

    // ------------------------------------------------------------
    // Convert playback time -> MIDI tick
    // ------------------------------------------------------------

    uint64_t currentTick =
        secondsToTick(
            currentTime
        );

    const uint64_t durationTicks =
        _document.durationTicks();

    if (
        durationTicks > 0 &&
        currentTick > durationTicks
        )
    {
        currentTick =
            durationTicks;
    }

    // ------------------------------------------------------------
    // Zoom / coordinate conversion
    // ------------------------------------------------------------

    const float pixelsPerTick =
        _pixelsPerTick;

    const float ticksPerPixel =
        1.0f /
        pixelsPerTick;

    const float visibleWidth =
        available.x -
        keyWidth;

    const uint64_t visibleTicks =
        static_cast<uint64_t>(
            visibleWidth *
            ticksPerPixel
            );

    // ------------------------------------------------------------
    // Follow playback horizontally
    // ------------------------------------------------------------

    const float playheadScreenOffset =
        visibleWidth *
        0.25f;

    const uint64_t playheadOffsetTicks =
        static_cast<uint64_t>(
            playheadScreenOffset *
            ticksPerPixel
            );

    uint64_t viewStartTick = 0;

    if (_followPlayback)
    {
        if (
            currentTick >
            playheadOffsetTicks
            )
        {
            viewStartTick =
                currentTick -
                playheadOffsetTicks;
        }
    }
    else
    {
        viewStartTick =
            _viewStartTick;
    }

    if (
        durationTicks >
        visibleTicks
        )
    {
        const uint64_t maxViewStart =
            durationTicks -
            visibleTicks;

        if (
            viewStartTick >
            maxViewStart
            )
        {
            viewStartTick =
                maxViewStart;
        }
    }
    else
    {
        viewStartTick = 0;
    }

    _viewStartTick =
        viewStartTick;

    // ------------------------------------------------------------
    // Make entire piano range vertically scrollable
    // ------------------------------------------------------------

    const float pianoRollHeight =
        static_cast<float>(
            maxPitch -
            minPitch +
            1
            ) *
        rowHeight;

    ImGui::Dummy(
        ImVec2(
            available.x,
            pianoRollHeight
        )
    );

    const ImVec2 drawOrigin =
        ImVec2(
            origin.x,
            origin.y
        );

    // ------------------------------------------------------------
    // Middle-mouse manual panning
    // ------------------------------------------------------------

    if (
        ImGui::IsWindowHovered() &&
        ImGui::IsMouseDown(
            ImGuiMouseButton_Middle
        )
        )
    {
        const ImVec2 mouseDelta =
            ImGui::GetIO().MouseDelta;

        if (
            mouseDelta.x != 0.0f ||
            mouseDelta.y != 0.0f
            )
        {
            _followPlayback = false;

            if (pixelsPerTick > 0.0f)
            {
                const double tickDelta =
                    static_cast<double>(
                        mouseDelta.x
                        ) /
                    static_cast<double>(
                        pixelsPerTick
                        );

                double newViewStart =
                    static_cast<double>(
                        _viewStartTick
                        ) -
                    tickDelta;

                if (newViewStart < 0.0)
                {
                    newViewStart = 0.0;
                }

                if (durationTicks > visibleTicks)
                {
                    const double maxViewStart =
                        static_cast<double>(
                            durationTicks -
                            visibleTicks
                            );

                    newViewStart =
                        (std::min)(
                            newViewStart,
                            maxViewStart
                            );
                }
                else
                {
                    newViewStart = 0.0;
                }

                _viewStartTick =
                    static_cast<uint64_t>(
                        std::llround(
                            newViewStart
                        )
                        );

                viewStartTick =
                    _viewStartTick;
            }

            const float scrollMaxY =
                ImGui::GetScrollMaxY();

            float newScrollY =
                ImGui::GetScrollY() -
                mouseDelta.y;

            newScrollY =
                (std::clamp)(
                    newScrollY,
                    0.0f,
                    scrollMaxY
                    );

            ImGui::SetScrollY(
                newScrollY
            );
        }
    }

    ImDrawList* drawList =
        ImGui::GetWindowDrawList();

    // ------------------------------------------------------------
    // Piano roll focus / hover state
    // ------------------------------------------------------------

    _pianoRollHovered =
        ImGui::IsWindowHovered();

    const bool pianoRollFocused =
        ImGui::IsWindowFocused();

    const bool pianoRollCanEdit =
        _pianoRollHovered &&
        pianoRollFocused;

    const ImVec2 mousePos =
        ImGui::GetMousePos();

    const ImVec2 childWindowPos =
        ImGui::GetWindowPos();

    const ImVec2 childWindowSize =
        ImGui::GetWindowSize();

    const bool mouseInPianoRoll =
        pianoRollCanEdit &&
        mousePos.x >=
        drawOrigin.x +
        keyWidth &&
        mousePos.x <=
        childWindowPos.x +
        childWindowSize.x &&
        mousePos.y >=
        childWindowPos.y &&
        mousePos.y <=
        childWindowPos.y +
        childWindowSize.y;

    const bool mouseInPianoKeyboard =
        pianoRollCanEdit &&
        mousePos.x >=
        childWindowPos.x &&
        mousePos.x <
        childWindowPos.x +
        keyWidth &&
        mousePos.y >=
        childWindowPos.y &&
        mousePos.y <
        childWindowPos.y +
        childWindowSize.y;

    const float gridX =
        drawOrigin.x +
        keyWidth;

    // ============================================================
    // Horizontal Zoom
    // ============================================================

    if (
        mouseInPianoRoll &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::GetIO().MouseWheel != 0.0f
        )
    {
        const float oldPixelsPerTick =
            _pixelsPerTick;

        const double mouseTick =
            static_cast<double>(
                viewStartTick
                ) +
            (
                static_cast<double>(
                    mousePos.x
                    ) -
                static_cast<double>(
                    drawOrigin.x +
                    keyWidth
                    )
                ) /
            static_cast<double>(
                oldPixelsPerTick
                );

        const float zoomFactor =
            std::pow(
                1.15f,
                ImGui::GetIO().MouseWheel
            );

        _pixelsPerTick =
            std::clamp(
                _pixelsPerTick *
                zoomFactor,
                MinPixelsPerTick,
                MaxPixelsPerTick
            );

        const double newViewStart =
            mouseTick -
            (
                static_cast<double>(
                    mousePos.x
                    ) -
                static_cast<double>(
                    drawOrigin.x +
                    keyWidth
                    )
                ) /
            static_cast<double>(
                _pixelsPerTick
                );

        _viewStartTick =
            newViewStart <= 0.0
            ? 0
            : static_cast<uint64_t>(
                newViewStart
                );

        _followPlayback = false;
    }

    // ------------------------------------------------------------
    // Determine hovered note
    // ------------------------------------------------------------

    uint64_t hoveredNoteId = 0;

    if (mouseInPianoRoll)
    {
        hoveredNoteId =
            findHoveredNote(
                mousePos.x,
                mousePos.y,
                drawOrigin.x,
                drawOrigin.y,
                drawOrigin.x +
                keyWidth,
                pixelsPerTick
            );
    }

    // ------------------------------------------------------------
    // Determine if cursor is on the right resize handle
    // ------------------------------------------------------------

    bool hoveredResizeHandle = false;

    if (hoveredNoteId != 0)
    {
        const MIDIEditorNote* hoveredNote =
            _document.findNote(
                hoveredNoteId
            );

        if (hoveredNote)
        {
            const float noteX =
                drawOrigin.x +
                keyWidth +
                (
                    static_cast<float>(
                        hoveredNote->startTick -
                        viewStartTick
                        ) *
                    pixelsPerTick
                    );

            const float noteWidth =
                static_cast<float>(
                    hoveredNote->durationTick
                    ) *
                pixelsPerTick;

            const float noteRight =
                noteX +
                noteWidth;

            const float handleStart =
                (
                    noteRight -
                    NoteResizeHandleWidth
                    ) >
                noteX
                ? (
                    noteRight -
                    NoteResizeHandleWidth
                    )
                : noteX;

            hoveredResizeHandle =
                mousePos.x >= handleStart &&
                mousePos.x <= noteRight + 1.0f;
        }
    }

    // ------------------------------------------------------------
    // Convert mouse position to tick
    // ------------------------------------------------------------

    auto mouseXToTick =
        [&](float x)
        {
            const double tick =
                static_cast<double>(
                    viewStartTick
                    ) +
                (
                    static_cast<double>(
                        x
                        ) -
                    static_cast<double>(
                        drawOrigin.x +
                        keyWidth
                        )
                    ) /
                static_cast<double>(
                    pixelsPerTick
                    );

            return tick < 0.0
                ? 0.0
                : tick;
        };

    // ------------------------------------------------------------
    // Create a new note from a screen-space position.
    // ------------------------------------------------------------

    auto createNoteAt =
        [&](float x, float y)
        {
            double tickValue =
                mouseXToTick(x);

            if (tickValue < 0.0)
            {
                tickValue = 0.0;
            }

            uint64_t startTick =
                static_cast<uint64_t>(
                    std::llround(
                        tickValue
                    )
                    );

            // Snap to the start of the grid cell.
            if (_snapEnabled)
            {
                const uint64_t snapTicks =
                    gridTicks();

                if (snapTicks > 0)
                {
                    startTick =
                        (
                            startTick /
                            snapTicks
                            ) *
                        snapTicks;
                }
            }

            const float noteY =
                y -
                drawOrigin.y;

            int pitch =
                maxPitch -
                static_cast<int>(
                    noteY /
                    rowHeight
                    );

            pitch =
                (std::clamp)(
                    pitch,
                    minPitch,
                    maxPitch
                    );

            const uint64_t noteDuration =
                gridTicks() * 2;

            if (noteDuration == 0)
            {
                return;
            }

            if (_document.tracks.empty())
            {
                _document.tracks.push_back(
                    MIDIEditorTrack{}
                );
            }

            constexpr size_t editTrack = 0;

            const bool duplicate =
                noteExistsAt(
                    editTrack,
                    pitch,
                    0,
                    startTick
                );

            if (duplicate)
            {
                return;
            }

            saveUndoState();

            _document.addNote(
                editTrack,
                pitch,
                100,
                0,
                startTick,
                noteDuration
            );

            _document.sortNotes();

            _document.dirty =
                true;

            notifyDocumentChanged();
        };

    // ============================================================
    // Selection rectangle helpers
    // ============================================================

    auto updateSelectionFromRectangle =
        [&](float x0, float y0, float x1, float y1)
        {
            const float selectionMinX =
                (std::min)(x0, x1);

            const float selectionMaxX =
                (std::max)(x0, x1);

            const float selectionMinY =
                (std::min)(y0, y1);

            const float selectionMaxY =
                (std::max)(y0, y1);

            _selectedNoteIds.clear();

            for (
                size_t trackIndex = 0;
                trackIndex < _document.tracks.size();
                ++trackIndex
                )
            {
                const auto& track =
                    _document.tracks[trackIndex];

                for (const auto& note : track.notes)
                {
                    if (
                        note.pitch < minPitch ||
                        note.pitch > maxPitch
                        )
                    {
                        continue;
                    }

                    const float noteX =
                        gridX +
                        static_cast<float>(
                            static_cast<double>(
                                note.startTick
                                ) -
                            static_cast<double>(
                                viewStartTick
                                )
                            ) *
                        pixelsPerTick;

                    const float noteWidth =
                        (std::max)(
                            0.0f,
                            static_cast<float>(
                                note.durationTick
                                ) *
                            pixelsPerTick
                            );

                    const float noteY =
                        drawOrigin.y +
                        static_cast<float>(
                            maxPitch -
                            note.pitch
                            ) *
                        rowHeight;

                    const float noteRight =
                        noteX +
                        noteWidth;

                    const float noteBottom =
                        noteY +
                        rowHeight;

                    const bool touchesSelection =
                        noteRight >= selectionMinX &&
                        noteX <= selectionMaxX &&
                        noteBottom >= selectionMinY &&
                        noteY <= selectionMaxY;

                    if (!touchesSelection)
                    {
                        continue;
                    }

                    if (!isNoteSelected(note.id))
                    {
                        _selectedNoteIds.push_back(
                            note.id
                        );
                    }
                }
            }

            if (!_selectedNoteIds.empty())
            {
                _activeNoteId =
                    _selectedNoteIds.back();

                refreshSelectionVelocity();
            }
            else
            {
                _activeNoteId = 0;

                _velocityEditorValue = 100;
            }

            _editOriginalSelectionNotes.clear();
            _velocityUndoSaved = false;
        };

    // ============================================================
    // Active note dragging / resizing
    // ============================================================

    if (
        _isDraggingNote ||
        _isResizingNote
        )
    {
        MIDIEditorNote* activeNote =
            _document.findNote(
                _activeNoteId
            );

        if (!activeNote)
        {
            _isDraggingNote = false;
            _isResizingNote = false;
            _activeNoteId = 0;
            _editUndoSaved = false;
            _editOriginalSelectionNotes.clear();
            stopAudition();
        }
        else
        {
            // ----------------------------------------------------
            // Move selected notes together
            // ----------------------------------------------------

            if (_isDraggingNote)
            {
                const MIDIEditorNote* originalActiveNote =
                    nullptr;

                for (const auto& snapshot :
                    _editOriginalSelectionNotes)
                {
                    if (snapshot.id == _activeNoteId)
                    {
                        originalActiveNote =
                            &snapshot.note;

                        break;
                    }
                }

                if (originalActiveNote)
                {
                    const double mouseTick =
                        mouseXToTick(mousePos.x);

                    double candidateStart =
                        mouseTick -
                        _dragMouseOffsetTicks;

                    if (candidateStart < 0.0)
                    {
                        candidateStart = 0.0;
                    }

                    uint64_t candidateStartTick =
                        static_cast<uint64_t>(
                            std::llround(
                                candidateStart
                            )
                            );

                    if (_snapEnabled)
                    {
                        const uint64_t snapTicks =
                            gridTicks();

                        if (snapTicks > 0)
                        {
                            candidateStartTick =
                                static_cast<uint64_t>(
                                    std::llround(
                                        static_cast<double>(
                                            candidateStartTick
                                            ) /
                                        static_cast<double>(
                                            snapTicks
                                            )
                                    )
                                    ) *
                                snapTicks;
                        }
                    }

                    int newPitch =
                        maxPitch -
                        static_cast<int>(
                            (
                                mousePos.y -
                                drawOrigin.y -
                                _dragMouseOffsetY
                                ) /
                            rowHeight
                            );

                    newPitch =
                        (std::clamp)(
                            newPitch,
                            minPitch,
                            maxPitch
                            );

                    int pitchDelta =
                        newPitch -
                        originalActiveNote->pitch;

                    int minOriginalPitch =
                        maxPitch;

                    int maxOriginalPitch =
                        minPitch;

                    uint64_t minOriginalStartTick =
                        UINT64_MAX;

                    for (const auto& snapshot :
                        _editOriginalSelectionNotes)
                    {
                        minOriginalPitch =
                            (std::min)(
                                minOriginalPitch,
                                snapshot.note.pitch
                                );

                        maxOriginalPitch =
                            (std::max)(
                                maxOriginalPitch,
                                snapshot.note.pitch
                                );

                        minOriginalStartTick =
                            (std::min)(
                                minOriginalStartTick,
                                snapshot.note.startTick
                                );
                    }

                    pitchDelta =
                        (std::max)(
                            pitchDelta,
                            minPitch -
                            minOriginalPitch
                            );

                    pitchDelta =
                        (std::min)(
                            pitchDelta,
                            maxPitch -
                            maxOriginalPitch
                            );

                    int64_t requestedTickDelta =
                        static_cast<int64_t>(
                            candidateStartTick
                            ) -
                        static_cast<int64_t>(
                            originalActiveNote->startTick
                            );

                    const int64_t minimumTickDelta =
                        minOriginalStartTick == UINT64_MAX
                        ? 0
                        : -static_cast<int64_t>(
                            minOriginalStartTick
                            );

                    requestedTickDelta =
                        (std::max)(
                            requestedTickDelta,
                            minimumTickDelta
                            );

                    bool duplicate = false;

                    for (const auto& snapshot :
                        _editOriginalSelectionNotes)
                    {
                        int targetPitch =
                            snapshot.note.pitch +
                            pitchDelta;

                        targetPitch =
                            (std::clamp)(
                                targetPitch,
                                minPitch,
                                maxPitch
                                );

                        int64_t targetStartSigned =
                            static_cast<int64_t>(
                                snapshot.note.startTick
                                ) +
                            requestedTickDelta;

                        if (targetStartSigned < 0)
                        {
                            targetStartSigned = 0;
                        }

                        const uint64_t targetStartTick =
                            static_cast<uint64_t>(
                                targetStartSigned
                                );

                        if (
                            noteExistsIgnoringSelection(
                                snapshot.trackIndex,
                                targetPitch,
                                snapshot.note.channel,
                                targetStartTick
                            )
                            )
                        {
                            duplicate = true;
                            break;
                        }
                    }

                    const int auditionPitch =
                        (std::clamp)(
                            originalActiveNote->pitch +
                            pitchDelta,
                            minPitch,
                            maxPitch
                            );

                    _auditionFromKeyboard =
                        false;

                    startAudition(
                        auditionPitch,
                        activeNote->channel,
                        static_cast<float>(
                            activeNote->velocity
                            ) / 127.0f
                    );

                    if (!duplicate)
                    {
                        const bool moved =
                            pitchDelta != 0 ||
                            requestedTickDelta != 0;

                        if (moved)
                        {
                            if (!_editUndoSaved)
                            {
                                saveUndoState();

                                _editUndoSaved =
                                    true;
                            }

                            for (const auto& snapshot :
                                _editOriginalSelectionNotes)
                            {
                                MIDIEditorNote* note =
                                    _document.findNote(
                                        snapshot.id
                                    );

                                if (!note)
                                {
                                    continue;
                                }

                                int targetPitch =
                                    snapshot.note.pitch +
                                    pitchDelta;

                                targetPitch =
                                    (std::clamp)(
                                        targetPitch,
                                        minPitch,
                                        maxPitch
                                        );

                                int64_t targetStartSigned =
                                    static_cast<int64_t>(
                                        snapshot.note.startTick
                                        ) +
                                    requestedTickDelta;

                                if (targetStartSigned < 0)
                                {
                                    targetStartSigned = 0;
                                }

                                note->pitch =
                                    targetPitch;

                                note->startTick =
                                    static_cast<uint64_t>(
                                        targetStartSigned
                                        );
                            }

                            _document.dirty =
                                true;

                            notifyDocumentChanged();
                        }
                    }
                }
            }

            // ----------------------------------------------------
            // Resize selected notes together
            // ----------------------------------------------------

            if (_isResizingNote)
            {
                const MIDIEditorNote* originalNote = nullptr;

                for (const auto& snapshot :
                    _editOriginalSelectionNotes)
                {
                    if (snapshot.id == _activeNoteId)
                    {
                        originalNote =
                            &snapshot.note;

                        break;
                    }
                }

                if (originalNote)
                {
                    const double mouseTick =
                        mouseXToTick(mousePos.x);

                    // The right edge follows the mouse.
                    double newEndTick =
                        mouseTick -
                        _dragMouseOffsetTicks;

                    const double minimumEndTick =
                        static_cast<double>(
                            originalNote->startTick
                            ) + 1.0;

                    if (newEndTick < minimumEndTick)
                    {
                        newEndTick =
                            minimumEndTick;
                    }

                    // Snap only when explicitly enabled.
                    if (_snapEnabled)
                    {
                        const uint64_t snapTicks =
                            gridTicks();

                        if (snapTicks > 0)
                        {
                            newEndTick =
                                std::round(
                                    newEndTick /
                                    static_cast<double>(
                                        snapTicks
                                        )
                                ) *
                                static_cast<double>(
                                    snapTicks
                                    );

                            newEndTick =
                                (std::max)(
                                    newEndTick,
                                    minimumEndTick
                                    );
                        }
                    }

                    const uint64_t newDuration =
                        static_cast<uint64_t>(
                            (std::max)(
                                1.0,
                                newEndTick -
                                static_cast<double>(
                                    originalNote->startTick
                                    )
                                )
                            );

                    if (newDuration !=
                        originalNote->durationTick)
                    {
                        if (!_editUndoSaved)
                        {
                            saveUndoState();
                            _editUndoSaved = true;
                        }

                        activeNote->durationTick =
                            newDuration;

                        _document.dirty = true;

                        notifyDocumentChanged();
                    }
                }
            }

            // ----------------------------------------------------
            // End drag / resize
            // ----------------------------------------------------

            if (!ImGui::IsMouseDown(
                ImGuiMouseButton_Left
            ))
            {
                stopAudition();

                if (_editUndoSaved)
                {
                    _document.sortNotes();
                }

                _isDraggingNote = false;

                _isResizingNote = false;

                _activeNoteId = 0;

                _editUndoSaved = false;

                _editOriginalSelectionNotes.clear();

                _dragMouseOffsetTicks = 0.0;

                _dragMouseOffsetY = 0.0f;
            }
        }
    }

    // ============================================================
    // Start a new note edit or selection gesture
    // ============================================================

    if (
        !_isDraggingNote &&
        !_isResizingNote &&
        !_isSelectingNotes &&
        mouseInPianoRoll &&
        ImGui::IsMouseClicked(
            ImGuiMouseButton_Left
        )
        )
    {
        const bool ctrlDown =
            ImGui::GetIO().KeyCtrl;

        if (hoveredNoteId != 0)
        {
            MIDIEditorNote* note =
                _document.findNote(
                    hoveredNoteId
                );

            if (note)
            {
                _followPlayback = false;

                _auditionFromKeyboard =
                    false;

                startAudition(
                    note->pitch,
                    note->channel,
                    static_cast<float>(
                        note->velocity
                        ) / 127.0f
                );

                if (ctrlDown)
                {
                    appendSelection(
                        note->id
                    );

                    // Ctrl-click is selection-only.
                    _activeNoteId =
                        note->id;
                }
                else
                {
                    // Clicking an already-selected note keeps the
                    // current multi-selection so it can be moved
                    // or resized as a group. Clicking an unselected
                    // note starts a new single-note selection.
                    if (!isNoteSelected(note->id))
                    {
                        setSingleSelection(
                            note->id
                        );
                    }
                    else
                    {
                        _activeNoteId =
                            note->id;
                    }

                    _editOriginalPitch =
                        note->pitch;

                    _editOriginalStartTick =
                        note->startTick;

                    _editOriginalDurationTick =
                        note->durationTick;

                    _editUndoSaved =
                        false;

                    _editOriginalSelectionNotes.clear();

                    for (const uint64_t selectedId :
                    _selectedNoteIds)
                    {
                        const size_t trackIndex =
                            findNoteTrackIndex(
                                selectedId
                            );

                        const MIDIEditorNote* selectedNote =
                            _document.findNote(
                                selectedId
                            );

                        if (
                            selectedNote &&
                            trackIndex != static_cast<size_t>(-1)
                            )
                        {
                            NoteEditSnapshot snapshot;

                            snapshot.id =
                                selectedId;

                            snapshot.trackIndex =
                                trackIndex;

                            snapshot.note =
                                *selectedNote;

                            _editOriginalSelectionNotes.push_back(
                                snapshot
                            );
                        }
                    }

                    if (hoveredResizeHandle)
                    {
                        const double mouseTick =
                            mouseXToTick(
                                mousePos.x
                            );

                        _dragMouseOffsetTicks =
                            mouseTick -
                            (
                                static_cast<double>(
                                    note->startTick
                                    ) +
                                static_cast<double>(
                                    note->durationTick
                                    )
                                );

                        _isResizingNote =
                            true;

                        _isDraggingNote =
                            false;
                    }
                    else
                    {
                        _isDraggingNote =
                            true;

                        _isResizingNote =
                            false;

                        const double mouseTick =
                            mouseXToTick(
                                mousePos.x
                            );

                        _dragMouseOffsetTicks =
                            mouseTick -
                            static_cast<double>(
                                note->startTick
                                );

                        _dragMouseOffsetY =
                            mousePos.y -
                            (
                                drawOrigin.y +
                                static_cast<float>(
                                    maxPitch -
                                    note->pitch
                                    ) *
                                rowHeight
                                );
                    }
                }
            }
        }
        else
        {
            // Empty-space clicks start either a deselection click,
            // a selection rectangle, or (when nothing was selected
            // beforehand) a new note. Keep the existing selection
            // until we know whether the gesture is a click or drag.
            stopAudition();

            _followPlayback = false;

            _isSelectingNotes =
                true;

            _selectionHasMoved =
                false;

            _selectionStartX =
                mousePos.x;

            _selectionStartY =
                mousePos.y;

            _selectionCurrentX =
                mousePos.x;

            _selectionCurrentY =
                mousePos.y;
        }
    }

    // ============================================================
    // Selection gesture update / completion
    // ============================================================

    if (_isSelectingNotes)
    {
        _selectionCurrentX =
            mousePos.x;

        _selectionCurrentY =
            mousePos.y;

        const float dx =
            mousePos.x -
            _selectionStartX;

        const float dy =
            mousePos.y -
            _selectionStartY;

        const float distanceSquared =
            dx * dx +
            dy * dy;

        if (
            !_selectionHasMoved &&
            distanceSquared >=
            SelectionDragThreshold *
            SelectionDragThreshold
            )
        {
            _selectionHasMoved =
                true;
        }

        if (
            _selectionHasMoved &&
            ImGui::IsMouseDown(
                ImGuiMouseButton_Left
            )
            )
        {
            updateSelectionFromRectangle(
                _selectionStartX,
                _selectionStartY,
                _selectionCurrentX,
                _selectionCurrentY
            );
        }

        if (!ImGui::IsMouseDown(
            ImGuiMouseButton_Left
        ))
        {
            if (!_selectionHasMoved)
            {
                if (_selectedNoteIds.empty())
                {
                    createNoteAt(
                        _selectionStartX,
                        _selectionStartY
                    );
                }
                else
                {
                    // A plain click in empty space while notes are
                    // selected is a deselection action, not note
                    // creation.
                    clearSelection();
                }
            }

            _isSelectingNotes =
                false;

            _selectionHasMoved =
                false;

            _selectionStartX =
                0.0f;

            _selectionStartY =
                0.0f;

            _selectionCurrentX =
                0.0f;

            _selectionCurrentY =
                0.0f;
        }
    }

    // ============================================================
    // Right click -> remove hovered note
    // ============================================================

    if (
        mouseInPianoRoll &&
        hoveredNoteId != 0 &&
        ImGui::IsMouseClicked(
            ImGuiMouseButton_Right
        )
        )
    {
        saveUndoState();

        if (!_document.removeNote(
            hoveredNoteId
        ))
        {
            if (!_undoHistory.empty())
            {
                _undoHistory.pop_back();
            }
        }
        else
        {
            _selectedNoteIds.erase(
                std::remove(
                    _selectedNoteIds.begin(),
                    _selectedNoteIds.end(),
                    hoveredNoteId
                ),
                _selectedNoteIds.end()
            );

            if (_selectedNoteIds.empty())
            {
                _activeNoteId = 0;
            }
            else if (_activeNoteId == hoveredNoteId)
            {
                _activeNoteId =
                    _selectedNoteIds.back();
            }

            refreshSelectionVelocity();

            _document.dirty =
                true;

            notifyDocumentChanged();
        }
    }

    // ============================================================
    // Piano keyboard
    // ============================================================

    for (
        int pitch = minPitch;
        pitch <= maxPitch;
        ++pitch
        )
    {
        const float y =
            drawOrigin.y +
            static_cast<float>(
                maxPitch -
                pitch
                ) *
            rowHeight;

        const int pitchClass =
            pitch % 12;

        const bool blackKey =
            pitchClass == 1 ||
            pitchClass == 3 ||
            pitchClass == 6 ||
            pitchClass == 8 ||
            pitchClass == 10;

        const bool auditioned =
            _auditioning &&
            _auditionPitch == pitch;

        // --------------------------------------------------------
        // Normal key
        // --------------------------------------------------------

        drawList->AddRectFilled(
            ImVec2(
                drawOrigin.x,
                y
            ),
            ImVec2(
                drawOrigin.x +
                keyWidth,
                y + rowHeight
            ),
            ImGui::ColorConvertFloat4ToU32(
                blackKey
                ? ImVec4(
                    0.08f,
                    0.08f,
                    0.08f,
                    1.0f
                )
                : ImVec4(
                    0.18f,
                    0.18f,
                    0.18f,
                    1.0f
                )
            )
        );

        // --------------------------------------------------------
        // Audition highlight
        // --------------------------------------------------------

        if (auditioned)
        {
            drawList->AddRectFilled(
                ImVec2(
                    drawOrigin.x,
                    y
                ),
                ImVec2(
                    drawOrigin.x +
                    keyWidth,
                    y + rowHeight
                ),
                IM_COL32(
                    100,
                    180,
                    255,
                    220
                )
            );
        }

        // --------------------------------------------------------
        // Key border
        // --------------------------------------------------------

        drawList->AddRect(
            ImVec2(
                drawOrigin.x,
                y
            ),
            ImVec2(
                drawOrigin.x +
                keyWidth,
                y + rowHeight
            ),
            IM_COL32(
                60,
                60,
                60,
                255
            )
        );
    }

    // ============================================================
    // Piano keyboard audition
    // ============================================================

    // While the left mouse button is held, the keyboard behaves
    // like a piano strip: moving into a different key triggers
    // that key exactly once.
    //
    // Note dragging has priority, so moving a MIDI note over the
    // keyboard does not accidentally switch to keyboard audition.
    if (
        mouseInPianoKeyboard &&
        !_isDraggingNote &&
        !_isResizingNote &&
        ImGui::IsMouseDown(
            ImGuiMouseButton_Left
        )
        )
    {
        const int keyboardPitch =
            (std::clamp)(
                maxPitch -
                static_cast<int>(
                    (
                        mousePos.y -
                        drawOrigin.y
                        ) /
                    rowHeight
                    ),
                minPitch,
                maxPitch
                );

        _auditionFromKeyboard =
            true;

        startAudition(
            keyboardPitch,
            0,
            100.0f / 127.0f
        );
    }
    else if (
        _auditionFromKeyboard &&
        ImGui::IsMouseDown(
            ImGuiMouseButton_Left
        )
        )
    {
        // We are holding the mouse somewhere other than the
        // keyboard, so release any keyboard audition.
        stopAudition();
    }

    // ============================================================
    // Horizontal pitch grid
    // ============================================================

    for (
        int pitch = minPitch;
        pitch <= maxPitch;
        ++pitch
        )
    {
        const float y =
            drawOrigin.y +
            static_cast<float>(
                maxPitch -
                pitch
                ) *
            rowHeight;

        drawList->AddLine(
            ImVec2(
                gridX,
                y
            ),
            ImVec2(
                drawOrigin.x +
                available.x,
                y
            ),
            IM_COL32(
                45,
                45,
                45,
                255
            )
        );
    }

    // ============================================================
    // Musical grid
    // ============================================================

    const uint64_t subdivisionTicks =
        gridTicks();

    const uint64_t currentBeatTicks =
        beatTicks();

    const uint64_t currentMeasureTicks =
        measureTicks();

    if (
        subdivisionTicks > 0 &&
        currentBeatTicks > 0 &&
        currentMeasureTicks > 0
        )
    {
        uint64_t firstGridTick =
            (
                viewStartTick /
                subdivisionTicks
                ) *
            subdivisionTicks;

        if (
            firstGridTick >
            viewStartTick &&
            firstGridTick >=
            subdivisionTicks
            )
        {
            firstGridTick -=
                subdivisionTicks;
        }

        for (
            uint64_t tick =
            firstGridTick;

            tick <=
            durationTicks +
            subdivisionTicks;

            tick +=
            subdivisionTicks
            )
        {
            if (tick < viewStartTick)
            {
                continue;
            }

            const uint64_t relativeTick =
                tick -
                viewStartTick;

            const float x =
                gridX +
                static_cast<float>(
                    relativeTick
                    ) *
                pixelsPerTick;

            if (
                x >
                drawOrigin.x +
                available.x
                )
            {
                break;
            }

            const bool isMeasure =
                (
                    tick %
                    currentMeasureTicks
                    ) == 0;

            const bool isBeat =
                (
                    tick %
                    currentBeatTicks
                    ) == 0;

            ImU32 lineColor =
                IM_COL32(
                    55,
                    55,
                    55,
                    255
                );

            float lineThickness =
                1.0f;

            if (isBeat)
            {
                lineColor =
                    IM_COL32(
                        70,
                        70,
                        70,
                        255
                    );
            }

            if (isMeasure)
            {
                lineColor =
                    IM_COL32(
                        100,
                        100,
                        100,
                        255
                    );

                lineThickness =
                    2.0f;
            }

            drawList->AddLine(
                ImVec2(
                    x,
                    drawOrigin.y
                ),
                ImVec2(
                    x,
                    drawOrigin.y +
                    pianoRollHeight
                ),
                lineColor,
                lineThickness
            );
        }
    }

    // ============================================================
    // Selection rectangle
    // ============================================================

    if (
        _isSelectingNotes &&
        _selectionHasMoved
        )
    {
        const ImVec2 selectionMin =
            ImVec2(
                (std::min)(
                    _selectionStartX,
                    _selectionCurrentX
                    ),
                (std::min)(
                    _selectionStartY,
                    _selectionCurrentY
                    )
            );

        const ImVec2 selectionMax =
            ImVec2(
                (std::max)(
                    _selectionStartX,
                    _selectionCurrentX
                    ),
                (std::max)(
                    _selectionStartY,
                    _selectionCurrentY
                    )
            );

        drawList->AddRectFilled(
            selectionMin,
            selectionMax,
            IM_COL32(
                100,
                180,
                255,
                45
            )
        );

        drawList->AddRect(
            selectionMin,
            selectionMax,
            IM_COL32(
                140,
                200,
                255,
                180
            )
        );
    }

    // ============================================================
    // Notes
    // ============================================================

    for (
        const auto& track :
        _document.tracks
        )
    {
        for (
            const auto& note :
            track.notes
            )
        {
            if (
                note.pitch < minPitch ||
                note.pitch > maxPitch
                )
            {
                continue;
            }

            const uint64_t noteEndTick =
                note.startTick +
                note.durationTick;

            if (
                noteEndTick <
                viewStartTick ||
                note.startTick >
                viewStartTick +
                visibleTicks
                )
            {
                continue;
            }

            float x =
                gridX +
                static_cast<float>(
                    note.startTick -
                    viewStartTick
                    ) *
                pixelsPerTick;

            float width =
                static_cast<float>(
                    note.durationTick
                    ) *
                pixelsPerTick;

            if (
                note.startTick <
                viewStartTick
                )
            {
                const uint64_t clippedTicks =
                    viewStartTick -
                    note.startTick;

                x =
                    gridX;

                width -=
                    static_cast<float>(
                        clippedTicks
                        ) *
                    pixelsPerTick;
            }

            if (width < 2.0f)
            {
                width = 2.0f;
            }

            const float y =
                drawOrigin.y +
                static_cast<float>(
                    maxPitch -
                    note.pitch
                    ) *
                rowHeight;

            const bool hovered =
                note.id ==
                hoveredNoteId;

            const bool selected =
                isNoteSelected(
                    note.id
                );

            const bool active =
                note.id ==
                _activeNoteId;

            const bool resizeHovered =
                hovered &&
                hoveredResizeHandle;

            ImU32 fillColor =
                IM_COL32(
                    70,
                    150,
                    230,
                    255
                );

            ImU32 borderColor =
                IM_COL32(
                    120,
                    190,
                    255,
                    255
                );

            if (hovered)
            {
                fillColor =
                    IM_COL32(
                        100,
                        180,
                        245,
                        255
                    );

                borderColor =
                    IM_COL32(
                        180,
                        220,
                        255,
                        255
                    );
            }

            if (selected)
            {
                borderColor =
                    IM_COL32(
                        255,
                        220,
                        120,
                        255
                    );
            }

            if (active)
            {
                borderColor =
                    IM_COL32(
                        255,
                        245,
                        170,
                        255
                    );
            }

            drawList->AddRectFilled(
                ImVec2(
                    x,
                    y + 1.0f
                ),
                ImVec2(
                    x + width,
                    y + rowHeight - 1.0f
                ),
                fillColor
            );

            drawList->AddRect(
                ImVec2(
                    x,
                    y + 1.0f
                ),
                ImVec2(
                    x + width,
                    y + rowHeight - 1.0f
                ),
                borderColor
            );

            // ----------------------------------------------------
            // Right resize handle
            // ----------------------------------------------------

            if (
                resizeHovered ||
                (
                    _isResizingNote &&
                    active
                    )
                )
            {
                const float handleX =
                    x +
                    width -
                    NoteResizeHandleWidth;

                drawList->AddRectFilled(
                    ImVec2(
                        handleX,
                        y + 2.0f
                    ),
                    ImVec2(
                        x + width,
                        y + rowHeight - 2.0f
                    ),
                    IM_COL32(
                        180,
                        220,
                        255,
                        120
                    )
                );
            }
        }
    }

    // ============================================================
    // Playhead
    // ============================================================

    float playheadX =
        gridX +
        static_cast<float>(
            currentTick -
            viewStartTick
            ) *
        pixelsPerTick;

    const float minPlayheadX =
        gridX;

    const float maxPlayheadX =
        drawOrigin.x +
        available.x;

    if (
        playheadX <
        minPlayheadX
        )
    {
        playheadX =
            minPlayheadX;
    }

    if (
        playheadX >
        maxPlayheadX
        )
    {
        playheadX =
            maxPlayheadX;
    }

    drawList->AddLine(
        ImVec2(
            playheadX,
            drawOrigin.y
        ),
        ImVec2(
            playheadX,
            drawOrigin.y +
            pianoRollHeight
        ),
        IM_COL32(
            255,
            80,
            80,
            255
        ),
        2.0f
    );

    if (
        _auditioning &&
        !ImGui::IsMouseDown(
            ImGuiMouseButton_Left
        )
        )
    {
        stopAudition();
    }

    ImGui::EndChild();
}

// ============================================================
// Seconds -> MIDI ticks
// ============================================================

uint64_t MIDIEditor::secondsToTick(
    double seconds
) const
{
    if (seconds <= 0.0)
    {
        return 0;
    }

    const uint64_t ppq =
        _document.ticksPerQuarterNote;

    if (ppq == 0)
    {
        return 0;
    }

    if (_document.tempos.empty())
    {
        return 0;
    }

    const double secondsPerQuarterNote =
        static_cast<double>(
            _document
            .tempos[0]
            .microsecondsPerQuarterNote
            ) /
        1'000'000.0;

    if (secondsPerQuarterNote <= 0.0)
    {
        return 0;
    }

    return static_cast<uint64_t>(
        std::llround(
            seconds /
            secondsPerQuarterNote *
            static_cast<double>(
                ppq
                )
        )
        );
}

// ============================================================
// MIDI ticks -> seconds
// ============================================================

double MIDIEditor::tickToSeconds(
    uint64_t tick
) const
{
    if (
        _document.ticksPerQuarterNote ==
        0
        )
    {
        return 0.0;
    }

    if (_document.tempos.empty())
    {
        return 0.0;
    }

    const double secondsPerQuarterNote =
        static_cast<double>(
            _document
            .tempos[0]
            .microsecondsPerQuarterNote
            ) /
        1'000'000.0;

    return
        static_cast<double>(
            tick
            ) /
        static_cast<double>(
            _document.ticksPerQuarterNote
            ) *
        secondsPerQuarterNote;
}