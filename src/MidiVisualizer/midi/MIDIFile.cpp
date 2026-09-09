#include <algorithm>
#include <iterator>
#include <fstream>

#include "MIDIFile.h"
#include "../helpers/System.h"
#include "../rendering/State.h"

#include "../../../util/Logger.h"

MIDIFile::MIDIFile(){};

MIDIFile::MIDIFile(const std::string & filePath){
	std::ifstream input = System::openInputFile(filePath, true);

	if(!input.is_open()) {
		Logger::Log("[Error]: Couldn't find file at path %s\n", filePath.c_str());
		throw "BadInput";
	}
	
	std::vector<char> buffer;
	std::copy(std::istreambuf_iterator<char>(input),
			  std::istreambuf_iterator<char>(),
			  std::back_inserter(buffer));
	input.close();

	// Check midi header
	if(buffer.size() < 5 || !(buffer[0] == 'M' && buffer[1] == 'T' && buffer[2] == 'h' && buffer[3] == 'd') || read32(buffer, 4) != 6){
		Logger::Log("[Error]: %s is not a midi file.\n", filePath.c_str());
		throw "BadInput";
	}
	
	_format = static_cast<MIDIType>(read16(buffer, 8));
	const uint16_t tracksCount = read16(buffer, 10);

	const std::vector<std::string> formatNames = { "Single track (0)", "Tempo track (1)", "Multiple songs (2)"};

	if(_format == multipleSongs){
		Logger::Log("[Error]: %s is not a supported MIDI file (type 2).\n", filePath.c_str());
		throw "Unsupported MIDI type (2)";
	}

	if(tracksCount == 0){
		Logger::Log("[Error]: %s has no tracks.\n", filePath.c_str());
		throw "BadInput";
	}

	bool shouldMerge = false;
	if(_format == singleTrack && tracksCount > 1){
		Logger::Log("[Warning]: %s has too many tracks, will merge all tracks.\n", filePath.c_str());
		shouldMerge = true;
	}

	// Division mode.
	uint16_t division = read16(buffer, 12);
	bool divisionMode = getBit(division, 15);

	if(divisionMode){
		_unitsPerFrame = division & 0xFF;
		const std::vector<float> fpsValues = {24.0f, 25.0f, 29.97f, 30.0f};

		uint16_t fpsIndicator = ((division >> 8) & 0b1100000) >> 5;
		fpsIndicator = (std::min)(fpsIndicator, uint16_t(int(fpsValues.size()) - 1));
		_framesPerSeconds = fpsValues[fpsIndicator];
		Logger::Log("[MIDI File]: %d units per frame, %f frames per second.\n", _unitsPerFrame, _framesPerSeconds);
		Logger::Log("[MIDI File]: Division mode is not well supported.\n");
		_unitsPerQuarterNote = 1;
		
	} else {
		// In that case the 15th bit is 0, nothing to do.
		_unitsPerQuarterNote = division;
		_unitsPerFrame = 0;
		_framesPerSeconds = 0.0f;
	}

	// Parse tracks.
	size_t pos = 14;
	for(size_t trackId = 0; trackId < tracksCount; ++trackId){
		_tracks.emplace_back();
		pos = _tracks.back().readTrack(buffer, pos);
	}

	// Extract tempos and the signature.
	populateTemposAndSignature();
	populatePlaybackEvents();

	// Update seconds per measure.
	_secondsPerMeasure = computeMeasureDuration(_tempos[0].tempo, _signature);

	// Convert each track to real notes.
	for(size_t tid = 0; tid < _tracks.size(); ++tid){
		auto & track = _tracks[tid];
		track.extractNotes(_tempos, _unitsPerQuarterNote, (unsigned int)tid);
	}
	// Save count before merging.
	_trackCount = _tracks.size();

	// For now, still merge.
	shouldMerge = true;
	if(shouldMerge){
		mergeTracks();
	}

	// Normalize pedal values.
	for(auto & track : _tracks){
		track.normalizePedalVelocity();
	}

	// Compute duration.
	FilterOptions noFilter;
	for(const auto & track : _tracks){
		std::vector<MIDINote> notes;
		track.getNotes(notes, NoteType::ALL, noFilter);
		for(const auto & note : notes){
			_duration = (std::max)(_duration, note.start + note.duration);
		}
		_notesCount += int(notes.size());
	}
}

void MIDIFile::print() const {
	for(size_t tid = 0; tid < _tracks.size(); ++tid){
		Logger::Log("[MIDI File]: ---- Track %d\n", tid);
		_tracks[tid].print();
	}
}

void MIDIFile::populateTemposAndSignature(){
	std::vector<MIDITempo> mixedTempos;
	const double defaultSign = 4.0/4.0;
	_signature = defaultSign;

	for (auto& track : _tracks) {
		const double trackSign = track.extractTempos(mixedTempos);
		if(trackSign != defaultSign){
			_signature = trackSign;
		}
	}

	// Merge all tempos.
	std::unordered_map<size_t, MIDITempo> tempoChanges;
	// Emplace default tempo, will be overwritten as soon as there is an initial tempo event.
	tempoChanges[0] = MIDITempo(0, 500000);
	for(const auto & tempo : mixedTempos){
		tempoChanges[tempo.start] = tempo;
	}
	for(const auto & tempo : tempoChanges){
		_tempos.push_back(tempo.second);
	}
	std::sort(_tempos.begin(), _tempos.end(), [](const MIDITempo& a, const MIDITempo& b){
		return a.start < b.start;
	});

	// Compute the real time stamp of each tempo.
	// We are guaranteed that there is an event at t = 0.
	double currentTime = 0.0;
	_tempos[0].timestamp = 0.0;
	for(size_t tid = 1; tid < _tempos.size(); ++tid){
		const int delta = int(_tempos[tid].start) - int(_tempos[tid-1].start);
		currentTime += computeUnitsDuration(_tempos[tid-1].tempo, delta, _unitsPerQuarterNote);
		_tempos[tid].timestamp = currentTime;
	}
}

void MIDIFile::populatePlaybackEvents()
{
	_playbackEvents.clear();

	for (const auto& track : _tracks)
	{
		size_t timeInUnits = 0;

		for (const auto& event : track.events())
		{
			timeInUnits += event.delta;

			if (event.category != EventCategory::MIDI)
			{
				continue;
			}

			if (
				event.type != noteOn &&
				event.type != noteOff &&
				event.type != controllerChange
				)
			{
				continue;
			}

			double time = 0.0;

			for (size_t tid = 0; tid < _tempos.size(); ++tid)
			{
				if (
					tid == _tempos.size() - 1 ||
					_tempos[tid + 1].start > timeInUnits
					)
				{
					time =
						_tempos[tid].timestamp +
						computeUnitsDuration(
							_tempos[tid].tempo,
							timeInUnits - _tempos[tid].start,
							_unitsPerQuarterNote
						);

					break;
				}
			}

			time /= 1000000.0;

			MIDIPlaybackEvent playbackEvent;

			playbackEvent.time = time;
			playbackEvent.type =
				static_cast<MIDIEventType>(event.type);

			playbackEvent.channel =
				event.data.size() > 0
				? event.data[0]
				: 0;

			playbackEvent.data1 =
				event.data.size() > 1
				? event.data[1]
				: 0;

			playbackEvent.data2 =
				event.data.size() > 2
				? event.data[2]
				: 0;

			_playbackEvents.push_back(
				playbackEvent
			);
		}
	}

	std::sort(
		_playbackEvents.begin(),
		_playbackEvents.end(),
		[](const MIDIPlaybackEvent& a,
			const MIDIPlaybackEvent& b)
		{
			return a.time < b.time;
		}
	);
}

void MIDIFile::mergeTracks(){
	
	for(size_t i = 1; i < _tracks.size(); ++i){
		_tracks[0].merge(_tracks[i]);
	}
	_tracks.resize(1);
	
}

void MIDIFile::getNotes(std::vector<MIDINote> & notes, NoteType type, const FilterOptions& filter, size_t track) const {
	if(track >= _tracks.size()){
		return;
	}
	_tracks[track].getNotes(notes, type, filter );
}

void MIDIFile::getNotesActive(ActiveNotesArray & actives, double time, const FilterOptions& filter, size_t track) const {
	if(track >= _tracks.size()){
		return;
	}
	_tracks[track].getNotesActive(actives, time, filter);
}

void MIDIFile::getPedalsActive(float & damper, float &sostenuto, float &soft, float &expression, double time, size_t track) const {
	if(track >= _tracks.size()){
		return;
	}
	_tracks[track].getPedalsActive(damper, sostenuto, soft, expression, time);
}

void MIDIFile::updateSets(const SetOptions & options){
	for(auto & track : _tracks){
		track.updateSets(options);
	}
}
