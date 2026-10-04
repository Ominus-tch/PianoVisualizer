#include <stdio.h>

#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>

#include <glm/gtc/matrix_transform.hpp>

#include "../../helpers/ProgramUtilities.h"
#include "../../helpers/ResourcesManager.h"

#include "../../../../util/Logger.h"

#include "MIDISceneFile.h"

#ifdef _WIN32
#undef MIN
#undef MAX
#endif


namespace
{
	/*
	 * This is the same MIDI-pitch -> renderer-note mapping
	 * used by MIDITrack::getNotes().
	 *
	 * IMPORTANT:
	 *
	 * This mapping is only used for GPUNote.note.
	 *
	 * Particles and active keys use the ORIGINAL MIDI
	 * pitch (0..127), because their shaders index the
	 * shifts[] table using the actual MIDI pitch.
	 */
	const bool noteIsMinor[12] =
	{
		false,
		true,
		false,
		true,
		false,
		false,
		true,
		false,
		true,
		false,
		true,
		false
	};


	const short noteShift[12] =
	{
		0,
		0,
		1,
		1,
		2,
		3,
		3,
		4,
		4,
		5,
		5,
		6
	};


	int clampMIDIPitch(
		int pitch
	)
	{
		return
			(std::max)(
				0,
				(std::min)(
					127,
					pitch
					)
				);
	}


	int pitchToRendererNote(
		int pitch
	)
	{
		pitch =
			clampMIDIPitch(
				pitch
			);

		return
			(pitch / 12) * 7 +
			noteShift[pitch % 12];
	}


	bool pitchIsMinor(
		int pitch
	)
	{
		pitch =
			clampMIDIPitch(
				pitch
			);

		return
			noteIsMinor[
				pitch % 12
			];
	}
}


MIDISceneFile::~MIDISceneFile()
{
}


MIDISceneFile::MIDISceneFile(
	const std::string& midiFilePath,
	const SetOptions& options,
	const FilterOptions& filter
)
	: MIDIScene()
{
	Logger::Log(
		"[MIDI Scene File]: Loading MIDI file %s\n",
		midiFilePath.c_str()
	);

	_filePath =
		midiFilePath;

	_setOptions =
		options;

	_midiFile =
		MIDIFile(
			_filePath
		);

	updateSetsAndVisibleNotes(
		options,
		filter
	);

	Logger::Log(
		"[MIDI Scene File]: Final track duration %f sec.\n",
		_midiFile.duration()
	);
}


void MIDISceneFile::updateSetsAndVisibleNotes(
	const SetOptions& options,
	const FilterOptions& filter
)
{
	_setOptions =
		options;

	/*
	 * Once the editor has taken ownership of the scene,
	 * visible notes are rebuilt from the editor document.
	 */
	if (_usingEditorDocument)
	{
		updateVisibleNotes(
			filter
		);

		return;
	}


	_midiFile.updateSets(
		options
	);

	updateVisibleNotes(
		filter
	);
}


void MIDISceneFile::updateVisibleNotes(
	const FilterOptions& filter
)
{
	/*
	 * --------------------------------------------------------
	 * Normal MIDIFile-backed path
	 * --------------------------------------------------------
	 */
	if (!_usingEditorDocument)
	{
		std::vector<MIDINote>
			notesM;

		_midiFile.getNotes(
			notesM,
			NoteType::MAJOR,
			filter,
			0
		);

		std::vector<MIDINote>
			notesm;

		_midiFile.getNotes(
			notesm,
			NoteType::MINOR,
			filter,
			0
		);


		const size_t majorCount =
			notesM.size();

		const size_t minorCount =
			notesm.size();

		const size_t totalCount =
			majorCount +
			minorCount;


		_notes.resize(
			totalCount
		);


		for (size_t i = 0;
			i < majorCount;
			++i)
		{
			const MIDINote& note =
				notesM[i];

			GPUNote& data =
				_notes[i];

			data.note =
				float(note.note);

			data.start =
				float(note.start);

			data.duration =
				float(note.duration);

			data.isMinor =
				0.0f;

			data.set =
				float(note.set);
		}


		for (size_t i = 0;
			i < minorCount;
			++i)
		{
			const MIDINote& note =
				notesm[i];

			GPUNote& data =
				_notes[
					i + majorCount
				];

			data.note =
				float(note.note);

			data.start =
				float(note.start);

			data.duration =
				float(note.duration);

			data.isMinor =
				1.0f;

			data.set =
				float(note.set);
		}


		assert(
			totalCount <
			(1 << 31)
		);

		_dirtyNotes =
			true;

		_effectiveNotesCount =
			static_cast<int>(
				totalCount
				);

		_dirtyNotesRange =
		{
			0,
			0
		};

		return;
	}


	/*
	 * --------------------------------------------------------
	 * Editor-backed path
	 * --------------------------------------------------------
	 *
	 * The editor stores real MIDI pitches.
	 *
	 * GPUNote.note must be converted to the renderer's
	 * diatonic numbering here.
	 */
	std::vector<const EditorNote*>
		majorNotes;

	std::vector<const EditorNote*>
		minorNotes;


	for (const EditorNote& note :
		_editorNotes)
	{
		if (!filter.accepts(
			note.track,
			note.channel
		))
		{
			continue;
		}


		if (pitchIsMinor(
			note.pitch
		))
		{
			minorNotes.push_back(
				&note
			);
		}
		else
		{
			majorNotes.push_back(
				&note
			);
		}
	}


	const size_t majorCount =
		majorNotes.size();

	const size_t minorCount =
		minorNotes.size();

	const size_t totalCount =
		majorCount +
		minorCount;


	_notes.resize(
		totalCount
	);


	for (size_t i = 0;
		i < majorCount;
		++i)
	{
		const EditorNote& note =
			*majorNotes[i];

		GPUNote& data =
			_notes[i];


		/*
		 * GPUNote uses the renderer note index.
		 */
		data.note =
			static_cast<float>(
				pitchToRendererNote(
					note.pitch
				)
				);

		data.start =
			static_cast<float>(
				note.start
				);

		data.duration =
			static_cast<float>(
				note.duration
				);

		data.isMinor =
			0.0f;

		data.set =
			static_cast<float>(
				note.set
				);
	}


	for (size_t i = 0;
		i < minorCount;
		++i)
	{
		const EditorNote& note =
			*minorNotes[i];

		GPUNote& data =
			_notes[
				i + majorCount
			];


		data.note =
			static_cast<float>(
				pitchToRendererNote(
					note.pitch
				)
				);

		data.start =
			static_cast<float>(
				note.start
				);

		data.duration =
			static_cast<float>(
				note.duration
				);

		data.isMinor =
			1.0f;

		data.set =
			static_cast<float>(
				note.set
				);
	}


	assert(
		totalCount <
		(1 << 31)
	);


	_dirtyNotes =
		true;

	_effectiveNotesCount =
		static_cast<int>(
			totalCount
			);

	_dirtyNotesRange =
	{
		0,
		0
	};
}


double MIDISceneFile::tickToSeconds(
	const MIDIEditorDocument& document,
	uint64_t tick
) const
{
	const uint64_t ppq =
		document.ticksPerQuarterNote;


	if (ppq == 0)
	{
		return 0.0;
	}


	/*
	 * Default to 120 BPM if the document does not contain
	 * an explicit tempo.
	 */
	if (document.tempos.empty())
	{
		return
			static_cast<double>(
				tick
				) /
			static_cast<double>(
				ppq
				) *
			0.5;
	}


	double seconds =
		0.0;

	uint64_t previousTick =
		0;

	uint32_t microsecondsPerQuarterNote =
		500000;


	for (const auto& tempo :
		document.tempos)
	{
		if (tempo.tick > tick)
		{
			break;
		}


		if (tempo.tick > previousTick)
		{
			const uint64_t deltaTicks =
				tempo.tick -
				previousTick;

			seconds +=
				static_cast<double>(
					deltaTicks
					) /
				static_cast<double>(
					ppq
					) *
				(
					static_cast<double>(
						microsecondsPerQuarterNote
						) /
					1'000'000.0
					);
		}


		previousTick =
			tempo.tick;

		microsecondsPerQuarterNote =
			tempo.microsecondsPerQuarterNote;
	}


	if (tick > previousTick)
	{
		const uint64_t deltaTicks =
			tick -
			previousTick;

		seconds +=
			static_cast<double>(
				deltaTicks
				) /
			static_cast<double>(
				ppq
				) *
			(
				static_cast<double>(
					microsecondsPerQuarterNote
					) /
				1'000'000.0
				);
	}


	return seconds;
}


int MIDISceneFile::calculateEditorSet(
	const MIDIEditorNote& note,
	double startTime
) const
{
	/*
	 * This matches MIDITrack::updateSets():
	 *
	 *     options.apply(
	 *         note,
	 *         channel,
	 *         track,
	 *         time
	 *     );
	 */
	return _setOptions.apply(
		note.pitch,
		note.channel,
		0,
		startTime
	);
}


void MIDISceneFile::rebuildEditorVisibleNotes(
	const MIDIEditorDocument& document
)
{
	_editorNotes.clear();

	_editorTrackCount =
		static_cast<int>(
			document.tracks.size()
			);


	for (
		size_t trackIndex = 0;
		trackIndex < document.tracks.size();
		++trackIndex
		)
	{
		const auto& track =
			document.tracks[
				trackIndex
			];


		for (const auto& note :
			track.notes)
		{
			EditorNote editorNote;


			editorNote.pitch =
				clampMIDIPitch(
					note.pitch
				);

			editorNote.velocity =
				(std::max)(
					0,
					(std::min)(
						127,
						note.velocity
						)
					);

			editorNote.channel =
				(std::max)(
					0,
					(std::min)(
						15,
						note.channel
						)
					);

			editorNote.track =
				static_cast<int>(
					trackIndex
					);


			editorNote.start =
				tickToSeconds(
					document,
					note.startTick
				);


			const double endTime =
				tickToSeconds(
					document,
					note.startTick +
					note.durationTick
				);


			editorNote.duration =
				(std::max)(
					0.0,
					endTime -
					editorNote.start
					);


			/*
			 * The renderer's set assignment uses the
			 * original MIDI track number.
			 */
			editorNote.set =
				_setOptions.apply(
					editorNote.pitch,
					editorNote.channel,
					editorNote.track,
					editorNote.start
				);


			_editorNotes.push_back(
				editorNote
			);
		}
	}


	std::sort(
		_editorNotes.begin(),
		_editorNotes.end(),
		[](
			const EditorNote& a,
			const EditorNote& b
			)
		{
			if (a.start != b.start)
				return a.start < b.start;

			if (a.pitch != b.pitch)
				return a.pitch < b.pitch;

			if (a.channel != b.channel)
				return a.channel < b.channel;

			return a.track < b.track;
		}
	);
}


void MIDISceneFile::rebuildEditorPlaybackEvents(
	const MIDIEditorDocument& document
)
{
	_editorPlaybackEvents.clear();


	/*
	 * Keep controller events from the original MIDI document.
	 *
	 * Note-on/off events are regenerated from MIDIEditorNote,
	 * so moving/resizing/deleting a note cannot leave its old
	 * playback event around.
	 */
	for (const auto& track :
		document.tracks)
	{
		for (const auto& event :
			track.events)
		{
			if (
				event.kind !=
				MIDIEditorEvent::Kind::MIDI
				)
			{
				continue;
			}


			const uint8_t eventType =
				event.type;


			if (
				eventType ==
				static_cast<uint8_t>(
					noteOn
					) ||
				eventType ==
				static_cast<uint8_t>(
					noteOff
					)
				)
			{
				continue;
			}


			/*
			 * Keep the same event classes as the existing
			 * MIDIFile playback path.
			 */
			if (
				eventType !=
				static_cast<uint8_t>(
					controllerChange
					)
				)
			{
				continue;
			}


			EditorPlaybackEvent playbackEvent;


			playbackEvent.time =
				tickToSeconds(
					document,
					event.tick
				);


			playbackEvent.type =
				static_cast<MIDIEventType>(
					eventType
					);


			playbackEvent.channel =
				event.channel;


			/*
			 * MIDIEditorIO stores controller data as:
			 *
			 * data[0] = controller
			 * data[1] = value
			 */
			playbackEvent.data1 =
				event.data.size() > 0
				? event.data[0]
				: 0;

			playbackEvent.data2 =
				event.data.size() > 1
				? event.data[1]
				: 0;


			_editorPlaybackEvents.push_back(
				playbackEvent
			);
		}
	}


	/*
	 * Regenerate all note-on / note-off events from the
	 * current editor notes.
	 */
	for (const auto& track :
		document.tracks)
	{
		for (const auto& note :
			track.notes)
		{
			const int channel =
				(std::max)(
					0,
					(std::min)(
						15,
						note.channel
						)
					);

			const int pitch =
				clampMIDIPitch(
					note.pitch
				);

			const int velocity =
				(std::max)(
					0,
					(std::min)(
						127,
						note.velocity
						)
					);


			const double start =
				tickToSeconds(
					document,
					note.startTick
				);


			const double end =
				tickToSeconds(
					document,
					note.startTick +
					note.durationTick
				);


			/*
			 * Note ON
			 */
			EditorPlaybackEvent noteOnEvent;

			noteOnEvent.time =
				start;

			noteOnEvent.type =
				noteOn;

			noteOnEvent.channel =
				static_cast<uint8_t>(
					channel
					);

			noteOnEvent.data1 =
				static_cast<uint8_t>(
					pitch
					);

			noteOnEvent.data2 =
				static_cast<uint8_t>(
					velocity
					);


			_editorPlaybackEvents.push_back(
				noteOnEvent
			);


			/*
			 * Note OFF
			 */
			EditorPlaybackEvent noteOffEvent;

			noteOffEvent.time =
				(std::max)(
					start,
					end
					);

			noteOffEvent.type =
				noteOff;

			noteOffEvent.channel =
				static_cast<uint8_t>(
					channel
					);

			noteOffEvent.data1 =
				static_cast<uint8_t>(
					pitch
					);

			noteOffEvent.data2 =
				0;


			_editorPlaybackEvents.push_back(
				noteOffEvent
			);
		}
	}


	/*
	 * Sort chronologically.
	 *
	 * At an identical timestamp, note-offs go before note-ons.
	 */
	std::stable_sort(
		_editorPlaybackEvents.begin(),
		_editorPlaybackEvents.end(),
		[](
			const EditorPlaybackEvent& a,
			const EditorPlaybackEvent& b
			)
		{
			if (a.time != b.time)
				return a.time < b.time;


			const bool aIsNoteOff =
				a.type == noteOff;

			const bool bIsNoteOff =
				b.type == noteOff;


			if (aIsNoteOff != bIsNoteOff)
				return aIsNoteOff;


			return a.channel < b.channel;
		}
	);
}


void MIDISceneFile::resetScenePlaybackState()
{
	/*
	 * Release any delayed/particle state.
	 */
	for (auto& particle :
		_particles)
	{
		particle.note = -1;
		particle.set = -1;
		particle.duration = 0.0f;
		particle.start = 0.0f;
		particle.elapsed = 0.0f;
	}


	_actives.fill(
		-1
	);


	_pedals.damper =
		0.0f;

	_pedals.sostenuto =
		0.0f;

	_pedals.soft =
		0.0f;

	_pedals.expression =
		0.0f;
}


void MIDISceneFile::syncFromEditor(
	const MIDIEditorDocument& document,
	const FilterOptions& filter,
	double currentTime
)
{
	/*
	 * If the previous editor version had notes currently
	 * sounding, release them before replacing the document.
	 *
	 * This prevents stuck notes while dragging a note around.
	 */
	if (
		_usingEditorDocument &&
		_audioEngine
		)
	{
		for (const auto& oldNote :
			_editorNotes)
		{
			if (
				currentTime >= oldNote.start &&
				currentTime <
				oldNote.start +
				oldNote.duration
				)
			{
				_audioEngine->noteOff(
					static_cast<int16_t>(
						oldNote.channel
						),
					static_cast<int16_t>(
						oldNote.pitch
						),
					0.0f
				);
			}
		}
	}


	_usingEditorDocument =
		true;


	/*
	 * Rebuild BOTH:
	 *
	 *   1. _editorNotes
	 *   2. _notes
	 *
	 * Previously we rebuilt only _editorNotes, which is why
	 * newly-created notes appeared in the editor/particles
	 * but were missing from the actual falling-note renderer.
	 */
	rebuildEditorVisibleNotes(
		document
	);

	updateVisibleNotes(
		filter
	);


	rebuildEditorPlaybackEvents(
		document
	);


	/*
	 * Do NOT restart playback from zero after a live edit.
	 *
	 * Set the event cursor to the current playback position.
	 * Events before the current position have already happened.
	 */
	_nextEditorPlaybackEvent =
		0;

	while (
		_nextEditorPlaybackEvent <
		_editorPlaybackEvents.size() &&
		_editorPlaybackEvents[
			_nextEditorPlaybackEvent
		].time < currentTime
		)
	{
		++_nextEditorPlaybackEvent;
	}


	/*
	 * Synchronization happened at currentTime, so we must not
	 * treat every old note as newly-triggered.
	 */
	_previousTime =
		currentTime;


	resetScenePlaybackState();
}


void MIDISceneFile::updatesActiveNotes(
	double time,
	double speed,
	const FilterOptions& filter
)
{
	/*
	 * --------------------------------------------------------
	 * AUDIO
	 * --------------------------------------------------------
	 *
	 * IMPORTANT:
	 *
	 * This now uses the MIDIScene::_audioEngine member.
	 *
	 * MIDISceneFile must NOT have its own second AudioEngine
	 * pointer because Viewer/PianoVisualizer sets the base
	 * class pointer.
	 */
	if (_audioEngine)
	{
		if (_usingEditorDocument)
		{
			while (
				_nextEditorPlaybackEvent <
				_editorPlaybackEvents.size() &&
				_editorPlaybackEvents[
					_nextEditorPlaybackEvent
				].time <= time
				)
			{
				const EditorPlaybackEvent& event =
					_editorPlaybackEvents[
						_nextEditorPlaybackEvent
					];


				if (event.type == noteOn)
				{
					_audioEngine->noteOn(
						static_cast<int16_t>(
							event.channel
							),
						static_cast<int16_t>(
							event.data1
							),
						static_cast<float>(
							event.data2
							) / 127.0f
					);
				}
				else if (event.type == noteOff)
				{
					_audioEngine->noteOff(
						static_cast<int16_t>(
							event.channel
							),
						static_cast<int16_t>(
							event.data1
							),
						static_cast<float>(
							event.data2
							) / 127.0f
					);
				}
				else if (
					event.type ==
					controllerChange
					)
				{
					_audioEngine->controlChange(
						static_cast<int16_t>(
							event.channel
							),
						static_cast<int16_t>(
							event.data1
							),
						static_cast<int16_t>(
							event.data2
							)
					);
				}


				++_nextEditorPlaybackEvent;
			}
		}
		else
		{
			const auto& events =
				_midiFile.playbackEvents();


			while (
				_nextPlaybackEvent <
				events.size() &&
				events[
					_nextPlaybackEvent
				].time <= time
				)
			{
				const auto& event =
					events[
						_nextPlaybackEvent
					];


				if (event.type == noteOn)
				{
					_audioEngine->noteOn(
						event.channel,
						event.data1,
						float(event.data2) /
						127.0f
					);
				}
				else if (event.type == noteOff)
				{
					_audioEngine->noteOff(
						event.channel,
						event.data1,
						float(event.data2) /
						127.0f
					);
				}
				else if (
					event.type ==
					controllerChange
					)
				{
					_audioEngine->controlChange(
						static_cast<int16_t>(
							event.channel
							),
						static_cast<int16_t>(
							event.data1
							),
						event.data2
					);
				}


				++_nextPlaybackEvent;
			}
		}
	}


	/*
	 * --------------------------------------------------------
	 * PARTICLES
	 * --------------------------------------------------------
	 */
	for (auto& particle :
		_particles)
	{
		if (particle.duration > 0.0f)
		{
			particle.elapsed =
				(
					float(time) -
					particle.start +
					0.25f
					) /
				(
					float(speed) *
					particle.duration
					);
		}
		else
		{
			particle.elapsed =
				0.0f;
		}


		if (
			float(time) >=
			particle.start +
			particle.duration ||
			float(time) <
			particle.start
			)
		{
			particle.note =
				-1;

			particle.set =
				-1;

			particle.duration =
				0.0f;

			particle.start =
				0.0f;

			particle.elapsed =
				0.0f;
		}
	}


	/*
	 * --------------------------------------------------------
	 * EDITOR-BACKED ACTIVE NOTES
	 * --------------------------------------------------------
	 */
	if (_usingEditorDocument)
	{
		/*
		 * Active keys are indexed by REAL MIDI PITCH.
		 *
		 * Do NOT put pitchToRendererNote() here.
		 *
		 * Renderer shaders use:
		 *
		 *     shifts[globalId]
		 *
		 * where globalId is the MIDI pitch.
		 */
		_actives.fill(
			-1
		);


		for (const auto& note :
			_editorNotes)
		{
			if (!filter.accepts(
				note.track,
				note.channel
			))
			{
				continue;
			}


			/*
			 * ------------------------------------------------
			 * Active key
			 * ------------------------------------------------
			 */
			if (
				time >= note.start &&
				time <
				note.start +
				note.duration
				)
			{
				const int midiPitch =
					clampMIDIPitch(
						note.pitch
					);


				_actives[
					midiPitch
				] =
					note.set;
			}


			/*
			 * ------------------------------------------------
			 * Particle trigger
			 * ------------------------------------------------
			 *
			 * IMPORTANT:
			 *
			 * particle.note must contain the real MIDI pitch,
			 * not the renderer's diatonic note index.
			 */
			if (
				note.start >
				_previousTime &&
				note.start <= time
				)
			{
				for (auto& particle :
					_particles)
				{
					if (particle.note < 0)
					{
						particle.duration =
							static_cast<float>(
								(std::max)(
									note.duration * 2.0,
									note.duration + 1.2
									)
								);


						particle.start =
							static_cast<float>(
								note.start
								);


						/*
						 * REAL MIDI PITCH.
						 */
						particle.note =
							clampMIDIPitch(
								note.pitch
							);


						particle.set =
							note.set;


						particle.elapsed =
							0.0f;


						break;
					}
				}
			}
		}
	}
	else
	{
		/*
		 * ------------------------------------------------
		 * Original MIDIFile-backed active notes
		 * ------------------------------------------------
		 */
		auto actives =
			ActiveNotesArray();


		_midiFile.getNotesActive(
			actives,
			time,
			filter,
			0
		);


		for (int i = 0;
			i < 128;
			++i)
		{
			const auto& note =
				actives[i];


			_actives[i] =
				note.enabled
				? note.set
				: -1;


			if (
				note.start >
				_previousTime &&
				note.start <= time
				)
			{
				for (auto& particle :
					_particles)
				{
					if (particle.note < 0)
					{
						particle.duration =
							(
								std::max
								)(
									note.duration *
									2.0,
									note.duration +
									1.2
									);

						particle.start =
							note.start;

						/*
						 * REAL MIDI PITCH.
						 */
						particle.note =
							i;

						particle.set =
							note.set;

						particle.elapsed =
							0.0f;

						break;
					}
				}
			}
		}
	}


	_previousTime =
		time;


	/*
	 * --------------------------------------------------------
	 * PEDALS
	 * --------------------------------------------------------
	 *
	 * Keep the original MIDIFile pedal implementation for now.
	 * BPM/time-signature editing in the editor is display-only,
	 * so this remains synchronized with the source MIDI timing.
	 */
	if (!_usingEditorDocument)
	{
		_pedals.damper =
			0.0f;

		_pedals.sostenuto =
			0.0f;

		_pedals.soft =
			0.0f;

		_pedals.expression =
			0.0f;


		_midiFile.getPedalsActive(
			_pedals.damper,
			_pedals.sostenuto,
			_pedals.soft,
			_pedals.expression,
			time,
			0
		);
	}
}


double MIDISceneFile::duration() const
{
	if (_usingEditorDocument)
	{
		double maxDuration =
			0.0;


		for (const auto& note :
			_editorNotes)
		{
			maxDuration =
				(std::max)(
					maxDuration,
					note.start +
					note.duration
					);
		}


		for (const auto& event :
			_editorPlaybackEvents)
		{
			maxDuration =
				(std::max)(
					maxDuration,
					event.time
					);
		}


		return maxDuration;
	}


	return _midiFile.duration();
}


double MIDISceneFile::secondsPerMeasure() const
{
	return _midiFile.secondsPerMeasure();
}


int MIDISceneFile::notesCount() const
{
	if (_usingEditorDocument)
	{
		return static_cast<int>(
			_editorNotes.size()
			);
	}


	return _midiFile.notesCount();
}


int MIDISceneFile::tracksCount() const
{
	if (_usingEditorDocument)
	{
		return _editorTrackCount;
	}


	return _midiFile.tracksCount();
}


void MIDISceneFile::print() const
{
	_midiFile.print();
}


void MIDISceneFile::save(
	std::ofstream& file
) const
{
	/*
	 * MIDIEditor::save() is responsible for saving the
	 * edited document.
	 *
	 * Keep this scene method as the original file-backed
	 * implementation.
	 */
	std::ifstream input(
		_filePath
	);

	if (
		input.is_open() &&
		file.is_open()
		)
	{
		std::copy(
			std::istreambuf_iterator<char>(
				input
			),
			std::istreambuf_iterator<char>(),
			std::ostream_iterator<char>(
				file
			)
		);
	}

	input.close();
}


const std::string& MIDISceneFile::filePath() const
{
	return _filePath;
}


void MIDISceneFile::resetPlaybackState(
	double time
)
{
	/*
	 * Reset both possible playback cursors.
	 */
	_nextPlaybackEvent =
		0;

	_nextEditorPlaybackEvent =
		0;


	/*
	 * Find the first editor event at or after the requested
	 * playback time.
	 */
	if (_usingEditorDocument)
	{
		while (
			_nextEditorPlaybackEvent <
			_editorPlaybackEvents.size() &&
			_editorPlaybackEvents[
				_nextEditorPlaybackEvent
			].time < time
			)
		{
			++_nextEditorPlaybackEvent;
		}
	}
	else
	{
		const auto& events =
			_midiFile.playbackEvents();


		while (
			_nextPlaybackEvent <
			events.size() &&
			events[
				_nextPlaybackEvent
			].time < time
			)
		{
			++_nextPlaybackEvent;
		}
	}


	resetScenePlaybackState();


	_previousTime =
		time;
}