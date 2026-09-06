#ifndef MIDISceneLive_h
#define MIDISceneLive_h

#include <glm/glm.hpp>

#include "../../midi/MIDIBase.h"
#include "../State.h"
#include "MIDIScene.h"

#include <libremidi/libremidi.hpp>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <functional>

#include "../../../Audio/AudioEngine.h"

#define VIRTUAL_DEVICE_NAME "VIRTUAL"

class MIDISceneLive : public MIDIScene {

public:

	MIDISceneLive(int port, bool verbose);

	~MIDISceneLive();

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

	void startRecording(double time);
	void stopRecording();

	double duration() const override;

	double secondsPerMeasure() const override;

	int notesCount() const override;

	int tracksCount() const override;

	void print() const override;

	void save(std::ofstream& file) const override;

	const std::string& deviceName() const;

	static const std::vector<std::string>& availablePorts(
		bool force = false
	);

	static const int availablePortsCount()
	{
		return shared().get_port_count();
	}

	void setAudioEngine(audio::AudioEngine* engine)
	{
		_audioEngine = engine;
	}

	using DeviceCallback =
		std::function<void(bool connected, const std::string& deviceName)>;

	static void setDeviceCallback(DeviceCallback callback);

private:

	void handleMIDIDisconnect(
		const std::string& deviceName
	);

	/*
	 * The old WinMM backend can leave its midi_in object in a
	 * state where close_port() is unsafe after a physical
	 * disconnect.
	 *
	 * We therefore abandon that object and create a fresh one
	 * instead of attempting to close it.
	 */
	static void recreateMIDIInput();

	struct NoteInfos {
		short note;
		short channel;
	};

	struct MIDIFrame {
		std::vector<libremidi::message> messages;
		double timestamp;
	};

	std::vector<NoteInfos> _notesInfos;

	std::array<int, 128> _activeIds;
	std::array<bool, 128> _activeRecording;

	std::map<float, Pedals> _pedalInfos;
	std::vector<MIDIFrame> _allMessages;

	double _previousTime = 0.0;
	double _maxTime = 0.0;

	double _recordingStartTime = 0.0;

	double _signatureNum = 4.0;
	double _signatureDenom = 4.0;
	double _secondsPerMeasure = 1.0;

	bool _recording = false;

	int _notesCount = 0;
	int _tempo = 500000;

	SetOptions _currentSetOption;

	std::string _deviceName;
	bool _verbose = false;

	static libremidi::midi_in& shared();

	static libremidi::midi_in* _sharedMIDIIn;
	static std::atomic<bool> _midiInputStale;
	libremidi::midi_in* _midiInputInstance = nullptr;

	static std::unique_ptr<libremidi::observer> _midiObserver;

	static std::atomic<bool> _deviceDisconnected;

	static std::mutex _deviceMutex;
	static std::string _removedDeviceName;

	static std::vector<std::string> _availablePorts;
	static int _refreshIndex;

	audio::AudioEngine* _audioEngine = nullptr;

	static DeviceCallback _deviceCallback;
	static std::mutex _callbackMutex;
};

#endif