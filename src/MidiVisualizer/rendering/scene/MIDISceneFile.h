#ifndef MIDISceneFile_h
#define MIDISceneFile_h

#include <glm/glm.hpp>

#include "../../midi/MIDIFile.h"
#include "../State.h"
#include "MIDIScene.h"

#include "../../midi/editor/MIDIEditorDocument.h"

#include <vector>
#include <cstdint>
#include <string>

class MIDISceneFile : public MIDIScene
{
public:

	MIDISceneFile(
		const std::string& midiFilePath,
		const SetOptions& options,
		const FilterOptions& filter
	);

	~MIDISceneFile();

	virtual void updateSetsAndVisibleNotes(
		const SetOptions& options,
		const FilterOptions& filter
	) override;

	virtual void updateVisibleNotes(
		const FilterOptions& filter
	) override;

	void updatesActiveNotes(
		double time,
		double speed,
		const FilterOptions& filter
	) override;

	double duration() const override;

	double secondsPerMeasure() const override;

	int notesCount() const override;

	int tracksCount() const override;

	void print() const override;

	void save(std::ofstream& file) const override;

	const std::string& filePath() const;

	void setNoteTravelTime(double noteTravelTime)
	{
		_noteTravelTime = noteTravelTime;
	}

	/*
	 * Synchronize the scene with the current MIDI editor
	 * document.
	 *
	 * currentTime is the Viewer playback time, so the
	 * synchronization does not replay the entire MIDI file
	 * from the beginning.
	 */
	void syncFromEditor(
		const MIDIEditorDocument& document,
		const FilterOptions& filter,
		double currentTime
	);

	void resetPlaybackState(double time = 0.0);

private:

	struct EditorPlaybackEvent
	{
		double time = 0.0;

		MIDIEventType type = noteOn;

		uint8_t channel = 0;
		uint8_t data1 = 0;
		uint8_t data2 = 0;
	};

	struct EditorNote
	{
		int pitch = 60;
		int velocity = 100;
		int channel = 0;
		int track = 0;

		double start = 0.0;
		double duration = 0.0;

		int set = 0;
	};

	double tickToSeconds(
		const MIDIEditorDocument& document,
		uint64_t tick
	) const;

	int calculateEditorSet(
		const MIDIEditorNote& note,
		double startTime
	) const;

	void rebuildEditorVisibleNotes(
		const MIDIEditorDocument& document
	);

	void rebuildEditorPlaybackEvents(
		const MIDIEditorDocument& document
	);

	void resetScenePlaybackState();

private:

	MIDIFile _midiFile;

	std::string _filePath;

	SetOptions _setOptions;

	double _previousTime = 0.0;

	double _noteTravelTime = 0.0;

	bool _usingEditorDocument = false;

	std::vector<EditorNote>
		_editorNotes;

	std::vector<EditorPlaybackEvent>
		_editorPlaybackEvents;

	size_t _nextPlaybackEvent = 0;

	size_t _nextEditorPlaybackEvent = 0;

	int _editorTrackCount = 0;
};

#endif