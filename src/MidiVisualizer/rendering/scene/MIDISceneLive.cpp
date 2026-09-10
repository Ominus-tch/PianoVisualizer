#include <stdio.h> 
#include <iostream> 
#include <vector> 
#include <algorithm> 
#include <cmath>

#include <glm/gtc/matrix_transform.hpp> 

#include "../../helpers/ProgramUtilities.h" 
#include "../../helpers/ResourcesManager.h" 
#include "../../midi/MIDIUtils.h" 

#include "MIDISceneLive.h" 
#include <libremidi/writer.hpp> 

#include "../../../../util/Logger.h"

#ifdef _WIN32
#undef MIN
#undef MAX
#endif

#define MAX_NOTES_IN_FLIGHT 8192


MIDISceneLive::~MIDISceneLive()
{
	Logger::Log(
		"[MIDI] Destroying MIDI scene: %s\n",
		_deviceName.c_str()
	);

	if (_midiInputInstance != _sharedMIDIIn)
	{
		Logger::Log(
			"[MIDI] Scene no longer owns the current MIDI input; "
			"skipping close.\n"
		);

		return;
	}

	if (_midiInputStale.load())
	{
		Logger::Log(
			"[MIDI] MIDI input is stale; leaving it untouched.\n"
		);

		return;
	}

	if (_sharedMIDIIn != nullptr &&
		_sharedMIDIIn->is_port_open())
	{
		Logger::Log(
			"[MIDI] Closing MIDI input.\n"
		);

		_sharedMIDIIn->close_port();
	}
}


MIDISceneLive::MIDISceneLive(int port, bool verbose)
	: MIDIScene()
{
	_verbose = verbose;

	/*
	 * Resolve the requested device first.
	 */
	if (port >= 0)
	{
		const auto& ports = availablePorts(true);

		if (port >= static_cast<int>(ports.size()))
		{
			Logger::Log(
				"[MIDI] Invalid MIDI port index: %d\n",
				port
			);

			return;
		}

		_deviceName = ports[port];
	}
	else
	{
		_deviceName = VIRTUAL_DEVICE_NAME;
	}


	/*
	 * If the previous MIDI connection was physically disconnected,
	 * the old WinMM midi_in object is no longer safe to close.
	 *
	 * Do this BEFORE checking is_port_open().
	 */
	if (_midiInputStale.load())
	{
		Logger::Log(
			"[MIDI] Previous MIDI connection was physically disconnected. "
			"Creating a fresh MIDI input.\n"
		);

		recreateMIDIInput();

		_midiInputStale.store(false);

		{
			std::lock_guard<std::mutex> lock(_deviceMutex);
			_removedDeviceName.clear();
		}

		_deviceDisconnected.store(false);
	}


	libremidi::midi_in& midiIn = shared();


	/*
	 * If we have a normal, still-valid connection, close it before
	 * opening the newly selected device.
	 *
	 * IMPORTANT:
	 * This is only reached when _deviceDisconnected == false.
	 */
	if (midiIn.is_port_open())
	{
		Logger::Log(
			"[MIDI] Closing previous MIDI connection.\n"
		);

		midiIn.close_port();
	}


	/*
	 * Open the requested MIDI device.
	 */
	if (port >= 0)
	{
		midiIn.open_port(
			static_cast<unsigned int>(port),
			"PianoVisualizer input"
		);

		Logger::Log(
			"[MIDI] Opened input: %s\n\n",
			_deviceName.c_str()
		);
	}
	else
	{
		midiIn.open_virtual_port(
			"PianoVisualizer virtual input"
		);

		Logger::Log(
			"[MIDI] Opened virtual MIDI input.\n"
		);
	}


	midiIn.ignore_types(
		true,
		true,
		true
	);

	/*
	 * Remember the exact midi_in instance used by this scene.
	 */
	_midiInputInstance = _sharedMIDIIn;


	_activeIds.fill(-1);
	_activeRecording.fill(false);

	_notes.resize(MAX_NOTES_IN_FLIGHT);
	_notesInfos.resize(MAX_NOTES_IN_FLIGHT);

	_allMessages.reserve(MAX_NOTES_IN_FLIGHT);

	_secondsPerMeasure =
		computeMeasureDuration(
			_tempo,
			_signatureNum / _signatureDenom
		);

	_pedalInfos[-10000.0f] = Pedals();

	_dirtyNotes = true;
	_dirtyNotesRange = { 0, 0 };
}


void MIDISceneLive::updateSetsAndVisibleNotes(
	const SetOptions& options,
	const FilterOptions& filter)
{
	_currentSetOption = options;

	for (size_t nid = 0; nid < _notesCount; ++nid)
	{
		auto& note = _notes[nid];

		const int set =
			_currentSetOption.apply(
				int(note.note),
				_notesInfos[nid].channel,
				0,
				note.start
			);

		note.set = float(set);
	}

	updateVisibleNotes(filter);
}


void MIDISceneLive::updateVisibleNotes(
	const FilterOptions& filter)
{
	(void)filter;

	_dirtyNotes = true;
	_dirtyNotesRange = { 0, 0 };
}


void MIDISceneLive::updatesActiveNotes(
	double time,
	double speed,
	const FilterOptions& filter)
{
	(void)filter;


	/*
	 * IMPORTANT:
	 *
	 * Handle disconnect BEFORE calling get_message().
	 *
	 * get_message() may touch the stale WinMM connection after
	 * physical removal.
	 */
	if (_deviceDisconnected.exchange(false))
	{
		std::string removedDevice;

		{
			std::lock_guard<std::mutex> lock(_deviceMutex);
			removedDevice = _removedDeviceName;
		}

		handleMIDIDisconnect(removedDevice);

		/*
		 * The disconnect has now been consumed.
		 *
		 * _midiInputStale remains true, because the current midi_in
		 * is still unsafe and must be replaced when a new scene/device
		 * is created.
		 *
		 * DO NOT call get_message() after disconnect.
		 */
		return;
	}


	int minUpdated = MAX_NOTES_IN_FLIGHT;
	int maxUpdated = 0;


	/*
	 * If paused, empty the queue.
	 */
	if (_previousTime == time)
	{
		while (true)
		{
			auto message =
				shared().get_message();

			if (message.size() == 0)
			{
				break;
			}
		}

		return;
	}


	/*
	 * Update particle systems.
	 */
	for (auto& particle : _particles)
	{
		particle.elapsed =
			(float(time) - particle.start + 0.25f)
			/
			(float(speed) * particle.duration);

		if (
			float(time) >=
			particle.start + particle.duration ||
			float(time) < particle.start
			)
		{
			particle.note = -1;
			particle.set = -1;
			particle.duration = 0.0f;
			particle.start = 0.0f;
			particle.elapsed = 0.0f;
		}
	}


	/*
	 * Restore active flags.
	 */
	for (size_t nid = 0; nid < _actives.size(); ++nid)
	{
		_actives[nid] = -1;
	}


	/*
	 * Extend active notes.
	 */
	for (size_t nid = 0; nid < _actives.size(); ++nid)
	{
		if (!_activeRecording[nid])
		{
			continue;
		}

		const int noteId =
			_activeIds[nid];

		if (
			noteId < 0 ||
			noteId >= static_cast<int>(_notes.size())
			)
		{
			_activeRecording[nid] = false;
			_activeIds[nid] = -1;
			continue;
		}

		GPUNote& note =
			_notes[noteId];

		note.duration =
			(std::max)(
				float(time - double(note.start)),
				0.0f
				);

		_actives[nid] =
			int(note.set);

		minUpdated =
			(std::min)(
				minUpdated,
				noteId
				);

		maxUpdated =
			(std::max)(
				maxUpdated,
				noteId
				);
	}


	/*
	 * Restore pedals.
	 */
	_pedals = Pedals();

	auto nextBig =
		_pedalInfos.upper_bound(
			float(time)
		);

	if (nextBig != _pedalInfos.begin())
	{
		_pedals =
			std::prev(nextBig)->second;
	}


	/*
	 * Process MIDI events.
	 */
	MIDIFrame frame;

	frame.timestamp =
		_recording
		? time - _recordingStartTime
		: time;

	frame.messages.reserve(8);


	while (true)
	{
		auto message =
			shared().get_message();

		if (message.size() == 0)
		{
			break;
		}


		frame.messages.push_back(
			message
		);

		const auto type =
			message.get_message_type();


		/*
		 * Note events.
		 */
		if (message.is_note_on_or_off())
		{
			const short note =
				clamp<short>(
					short(message[1]),
					0,
					127
				);

			const short velocity =
				clamp<short>(
					short(message[2]),
					0,
					127
				);


			if (_verbose)
			{
				std::cout
					<< "Note: "
					<< int(note)
					<< " "
					<< int(velocity)
					<< " "
					<< (
						type ==
						libremidi::message_type::NOTE_ON
						? "on"
						: "off"
						)
					<< "("
					<< message.timestamp
					<< ")\n";
			}


			if (_activeRecording[note])
			{
				_activeRecording[note] = false;
				_actives[note] = -1;
			}


			/*
			 * Note ON.
			 */
			if (
				type ==
				libremidi::message_type::NOTE_ON &&
				velocity > 0
				)
			{
				if (_audioEngine)
				{
					_audioEngine->noteOn(
						message.get_channel(),
						note,
						float(velocity) / 127.0f
					);
				}


				const int index =
					_notesCount %
					MAX_NOTES_IN_FLIGHT;


				auto& newNote =
					_notes[index];

				newNote.start =
					float(time);

				newNote.duration =
					0.0f;

				newNote.note =
					note;


				_notesInfos[index].channel =
					message.get_channel();


				const int set =
					_currentSetOption.apply(
						int(newNote.note),
						_notesInfos[index].channel,
						0,
						newNote.start
					);

				newNote.set =
					float(set);

				_actives[note] =
					int(newNote.set);

				_activeRecording[note] =
					true;

				_activeIds[note] =
					index;


				const bool isMin =
					noteIsMinor[note % 12];

				const short shiftId =
					(note / 12) * 7 +
					noteShift[note % 12];

				newNote.isMinor =
					isMin ? 1.0f : 0.0f;

				newNote.note =
					float(shiftId);


				_notesInfos[index].note =
					note;


				minUpdated =
					(std::min)(
						minUpdated,
						index
						);

				maxUpdated =
					(std::max)(
						maxUpdated,
						index
						);


				for (auto& particle : _particles)
				{
					if (particle.note < 0)
					{
						particle.duration = 10.0f;
						particle.start =
							newNote.start;
						particle.note = note;
						particle.set =
							int(newNote.set);
						particle.elapsed = 0.0f;

						break;
					}
				}


				++_notesCount;
			}
			else
			{
				/*
				 * Note OFF.
				 */
				if (_audioEngine)
				{
					_audioEngine->noteOff(
						message.get_channel(),
						note,
						float(velocity) / 127.0f
					);
				}
			}
		}


		/*
		 * Meta events.
		 */
		else if (message.is_meta_event())
		{
			const auto metaType =
				message.get_meta_event_type();


			if (
				metaType ==
				libremidi::meta_event_type::TIME_SIGNATURE
				)
			{
				_signatureNum =
					double(message[3]);

				_signatureDenom =
					double(
						std::pow(
							2,
							short(message[4])
						)
						);

				_secondsPerMeasure =
					computeMeasureDuration(
						_tempo,
						_signatureNum /
						_signatureDenom
					);


				if (_verbose)
				{
					std::cout
						<< "Signature: "
						<< _signatureNum
						<< "/"
						<< _signatureDenom
						<< " "
						<< _secondsPerMeasure
						<< "("
						<< message.timestamp
						<< ")\n";
				}
			}
			else if (
				metaType ==
				libremidi::meta_event_type::TEMPO_CHANGE
				)
			{
				_tempo =
					int(
						(
							(message[3] & 0xFF)
							<< 16
							)
						|
						(
							(message[4] & 0xFF)
							<< 8
							)
						|
						(message[5] & 0xFF)
						);


				_secondsPerMeasure =
					computeMeasureDuration(
						_tempo,
						_signatureNum /
						_signatureDenom
					);

				std::cout
					<< "Tempo: "
					<< _tempo
					<< " "
					<< _secondsPerMeasure
					<< "("
					<< message.timestamp
					<< ")\n";
			}
			else if (_verbose)
			{
				std::cout
					<< "Meta: other ("
					<< message.timestamp
					<< ")\n";
			}
		}


		/*
		 * Control changes.
		 */
		else if (
			type ==
			libremidi::message_type::CONTROL_CHANGE
			)
		{
			const int rawType =
				clamp<int>(
					message[1],
					0,
					127
				);


			if (
				rawType != 64 &&
				rawType != 66 &&
				rawType != 67 &&
				rawType != 11
				)
			{
				continue;
			}


			const PedalType pedalType =
				PedalType(rawType);


			float& pedal =
				pedalType == DAMPER
				? _pedals.damper
				: (
					pedalType == SOSTENUTO
					? _pedals.sostenuto
					: (
						pedalType == SOFT
						? _pedals.soft
						: _pedals.expression
						)
					);


			const short val =
				clamp<short>(
					message[2],
					0,
					127
				);


			pedal = float(val) / 127.0f;

			_pedalInfos[float(time)] =
				Pedals(_pedals);


			if (_verbose)
			{
				std::cout
					<< "Control: "
					<< rawType
					<< "("
					<< message.timestamp
					<< "): "
					<< val
					<< "\n";
			}


			if (_audioEngine)
			{
				_audioEngine->controlChange(
					(int16_t)message.get_channel(),
					(int16_t)rawType,
					val
				);
			}
		}
		else if (_verbose)
		{
			std::cout
				<< "Other ("
				<< message.timestamp
				<< ")\n";
		}
	}


	/*
	 * Store messages.
	 */
	if (_recording && !frame.messages.empty())
	{
		_allMessages.push_back(
			std::move(frame)
		);
	}


	/*
	 * Update completed notes.
	 */
	for (size_t i = 0;
		i < _effectiveNotesCount;
		++i)
	{
		const auto& noteId =
			_notesInfos[i];


		if (_activeRecording[noteId.note])
		{
			continue;
		}


		auto& note =
			_notes[i];


		const float noteEnd =
			note.start +
			note.duration;


		if (
			noteEnd > _previousTime &&
			noteEnd <= time
			)
		{
			continue;
		}


		if (
			note.start <= time &&
			note.start +
			note.duration >= time
			)
		{
			_actives[noteId.note] =
				int(note.set);
		}


		if (
			note.start > _previousTime &&
			note.start <= time
			)
		{
			for (auto& particle : _particles)
			{
				if (particle.note < 0)
				{
					particle.duration =
						(std::max)(
							note.duration * 2.0f,
							note.duration + 1.2f
							);

					particle.start =
						note.start;

					particle.note =
						noteId.note;

					particle.set =
						int(note.set);

					particle.elapsed =
						0.0f;

					break;
				}
			}
		}
	}


	if (minUpdated <= maxUpdated)
	{
		_dirtyNotes = true;

		_dirtyNotesRange =
		{
			minUpdated,
			maxUpdated
		};
	}


	_effectiveNotesCount =
		std::min(
			MAX_NOTES_IN_FLIGHT,
			_notesCount
		);


	_previousTime = time;

	_maxTime =
	(std::max)(
		_recording ? time - _recordingStartTime : time,
		_maxTime
		);
}

void MIDISceneLive::startRecording(double time)
{
	if (_recording)
		return;

	// ------------------------------------------------------------
	// Completely clear previous recording
	// ------------------------------------------------------------

	_allMessages.clear();
	_notesInfos.clear();
	_pedalInfos.clear();

	_activeIds.fill(-1);
	_activeRecording.fill(false);

	_previousTime = time;
	_maxTime = 0.0;
	_recordingStartTime = time;

	_notesCount = 0;
	_effectiveNotesCount = 0;

	_tempo = 500000;

	_signatureNum = 4.0;
	_signatureDenom = 4.0;

	_secondsPerMeasure =
		computeMeasureDuration(
			_tempo,
			_signatureNum / _signatureDenom
		);

	_currentSetOption = SetOptions{};

	// Reset visible/active state.
	_actives.fill(-1);
	_pedals = Pedals();

	for (auto& particle : _particles)
	{
		particle.note = -1;
		particle.set = -1;
		particle.duration = 0.0f;
		particle.start = 0.0f;
		particle.elapsed = 0.0f;
	}

	// ------------------------------------------------------------
	// Start recording
	// ------------------------------------------------------------

	_recording = true;
}

void MIDISceneLive::stopRecording()
{
	if (!_recording)
		return;

	_recording = false;
}

double MIDISceneLive::duration() const
{
	return _maxTime;
}


double MIDISceneLive::secondsPerMeasure() const
{
	return _secondsPerMeasure;
}


int MIDISceneLive::notesCount() const
{
	return _notesCount;
}


int MIDISceneLive::tracksCount() const
{
	return 1;
}


void MIDISceneLive::print() const
{
	std::cout
		<< "[INFO]: Live scene with "
		<< notesCount()
		<< " notes, duration "
		<< duration()
		<< "s."
		<< std::endl;
}


void MIDISceneLive::save(std::ofstream& file) const
{
	const double quarterNotesPerSecond =
		1000000.0 /
		double(_tempo);

	const double unitsPerQuarterNote =
		960.0;

	const double unitsPerSecond =
		unitsPerQuarterNote *
		quarterNotesPerSecond;

	std::vector<MIDIFrame> allMessages(
		_allMessages
	);

	std::sort(
		allMessages.begin(),
		allMessages.end(),
		[](const MIDIFrame& a, const MIDIFrame& b)
		{
			return a.timestamp < b.timestamp;
		}
	);

	Logger::Log(
		"[MIDI] Recorded timestamp duration: %.6f s\n",
		allMessages.empty()
		? 0.0
		: allMessages.back().timestamp
	);

	Logger::Log(
		"[MIDI] Tempo: %d us/qn (%.2f BPM)\n",
		_tempo,
		60000000.0 /
		static_cast<double>(_tempo)
	);

	Logger::Log(
		"[MIDI] PPQ: %.0f\n",
		unitsPerQuarterNote
	);

	Logger::Log(
		"[MIDI] Units/sec: %.6f\n",
		unitsPerSecond
	);

	libremidi::writer writer;

	writer.ticksPerQuarterNote =
		int(unitsPerQuarterNote);

	writer.tracks.resize(1);

	writer.add_event(
		0,
		0,
		libremidi::meta_events::tempo(
			_tempo
		)
	);

	writer.add_event(
		0,
		0,
		libremidi::meta_events::time_signature(
			int(_signatureNum),
			int(_signatureDenom)
		)
	);

	writer.add_event(
		0,
		0,
		libremidi::meta_events::key_signature(
			1,
			false
		)
	);

	long long previousTicks = 0;

	for (const MIDIFrame& frame : allMessages)
	{
		if (frame.messages.empty())
			continue;

		/*
		 * Convert the absolute time to an absolute
		 * MIDI tick position first.
		 *
		 * Rounding the absolute position prevents
		 * fractional-tick errors from accumulating
		 * across thousands of MIDI events.
		 */
		const long long absoluteTicks =
			static_cast<long long>(
				std::llround(
					frame.timestamp *
					unitsPerSecond
				)
				);

		const long long deltaTicks =
			absoluteTicks -
			previousTicks;

		for (size_t mid = 0;
			mid < frame.messages.size();
			++mid)
		{
			writer.add_event(
				mid == 0
				? static_cast<int>(deltaTicks)
				: 0,
				0,
				frame.messages[mid]
			);
		}

		previousTicks =
			absoluteTicks;
	}

	writer.write(file);
}

void MIDISceneLive::setDeviceCallback(DeviceCallback callback)
{
	std::lock_guard<std::mutex> lock(_callbackMutex);

	_deviceCallback = std::move(callback);
}


void MIDISceneLive::handleMIDIDisconnect(
	const std::string& deviceName)
{
	Logger::Log(
		"[MIDI] Handling disconnect: %s\n",
		deviceName.c_str()
	);


	/*
	 * Release all active notes.
	 */
	for (int note = 0;
		note < 128;
		++note)
	{
		if (!_activeRecording[note])
		{
			continue;
		}


		const int noteId =
			_activeIds[note];


		if (
			noteId >= 0 &&
			noteId <
			static_cast<int>(
				_notes.size()
				)
			)
		{
			GPUNote& gpuNote =
				_notes[noteId];


			gpuNote.duration =
				static_cast<float>(
					_previousTime -
					gpuNote.start
					);


			if (gpuNote.duration < 0.0f)
			{
				gpuNote.duration = 0.0f;
			}


			if (_audioEngine)
			{
				_audioEngine->noteOff(
					_notesInfos[noteId].channel,
					note,
					0.0f
				);
			}
		}


		_activeRecording[note] = false;
		_activeIds[note] = -1;
		_actives[note] = -1;
	}


	/*
	 * Reset pedals.
	 */
	_pedals = Pedals();


	/*
	 * Mark full note buffer dirty.
	 */
	_dirtyNotes = true;
	_dirtyNotesRange = { 0, 0 };


	Logger::Log(
		"[MIDI] All active notes released after disconnect.\n"
	);
}


/*
 * Abandon the old midi_in object and create a new one.
 *
 * We intentionally do NOT delete the old object.
 *
 * The old libremidi WinMM destructor calls close_port(), and
 * close_port() is unsafe after the physical device has been
 * removed because connected_ can remain true.
 *
 * This is therefore a deliberate lifetime workaround for
 * libremidi 1.0.0.
 */
void MIDISceneLive::recreateMIDIInput()
{
	Logger::Log(
		"[MIDI] Creating new MIDI input after disconnect.\n"
	);

	/*
	 * DO NOT delete the old object.
	 *
	 * libremidi 1.0.0's WinMM destructor calls close_port(), which is
	 * unsafe after the physical device has been physically removed.
	 * The stale object is therefore intentionally abandoned.
	 */
	_sharedMIDIIn =
		new libremidi::midi_in(
			libremidi::API::UNSPECIFIED,
			"PianoVisualizer"
		);

	/*
	 * Do NOT reset/recreate the observer here.
	 * The observer watches the API's device list and is independent
	 * of this midi_in object's lifetime.
	 */
}


const std::string& MIDISceneLive::deviceName() const
{
	return _deviceName;
}


/*
 * Static state.
 */
libremidi::midi_in*
MIDISceneLive::_sharedMIDIIn = nullptr;

std::unique_ptr<libremidi::observer>
MIDISceneLive::_midiObserver = nullptr;

std::atomic<bool>
MIDISceneLive::_deviceDisconnected = false;

std::atomic<bool>
MIDISceneLive::_midiInputStale = false;

std::mutex
MIDISceneLive::_deviceMutex;

std::string
MIDISceneLive::_removedDeviceName;

std::vector<std::string>
MIDISceneLive::_availablePorts;

int
MIDISceneLive::_refreshIndex = 0;

MIDISceneLive::DeviceCallback
MIDISceneLive::_deviceCallback = nullptr;

std::mutex
MIDISceneLive::_callbackMutex;


libremidi::midi_in& MIDISceneLive::shared()
{
	/*
	 * Create MIDI input if necessary.
	 */
	if (_sharedMIDIIn == nullptr)
	{
		_sharedMIDIIn =
			new libremidi::midi_in(
				libremidi::API::UNSPECIFIED,
				"PianoVisualizer"
			);
	}


	/*
	 * Create observer if necessary.
	 */
	if (_midiObserver == nullptr)
	{
		_midiObserver =
			std::make_unique<libremidi::observer>(
				_sharedMIDIIn->get_current_api(),
				libremidi::observer::callbacks{
					.input_added =
						[](int port, std::string name)
						{
							(void)port;

							Logger::Log(
								"[MIDI] Input connected: %s\n",
								name.c_str()
							);

							std::lock_guard<std::mutex> lock(_callbackMutex);

							if (_deviceCallback)
							{
								_deviceCallback(
									true,
									name
								);
							}
						},

					.input_removed =
						[](int port, std::string name)
						{
							(void)port;

							Logger::Log(
								"[MIDI] Input disconnected: %s\n",
								name.c_str()
							);

							{
								std::lock_guard<std::mutex> lock(_deviceMutex);

								_removedDeviceName =
									name;
							}

							_deviceDisconnected.store(true);

							std::lock_guard<std::mutex> lock(_callbackMutex);

							if (_deviceCallback)
							{
								_deviceCallback(
									false,
									name
								);
							}
						},

					.output_added = nullptr,

					.output_removed = nullptr
				}
			);
	}


	return *_sharedMIDIIn;
}


const std::vector<std::string>& MIDISceneLive::availablePorts(
	bool force)
{
	if (
		(_refreshIndex == 0) ||
		force
		)
	{
		const int portCount =
			shared().get_port_count();


		_availablePorts.resize(
			portCount
		);


		for (int i = 0;
			i < portCount;
			++i)
		{
			_availablePorts[i] =
				shared().get_port_name(i);
		}
	}


	_refreshIndex =
		(_refreshIndex + 1) % 15;


	return _availablePorts;
}