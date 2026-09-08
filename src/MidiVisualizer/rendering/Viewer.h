#pragma once
#include <d3d11.h>
#include <glm/glm.hpp>
#include <memory>
#include <array>
#include <mutex>
#include <string>
#include <filesystem>

#include <functional>
#include <optional>

#include "../helpers/System.h"

#include "Framebuffer.h"
#include "camera/MidiCamera.h"
#include "scene/MIDIScene.h"
#include "ScreenQuad.h"

#include "State.h"
#include "Renderer.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

#define DEBUG_SPEED (1.0f)

struct SystemAction {
	enum Type {
		NONE, FIX_SIZE, FREE_SIZE, FULLSCREEN, QUIT, RESIZE
	};

	Type type;
	glm::ivec4 data;

	SystemAction(Type action);
};

struct D3D11Interface {
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
};

class Viewer {

public:
	Viewer(
		D3D11Interface d3dInterface,
		int width,
		int height
	);

	~Viewer();
	
	bool loadFile(const std::string & midiFilePath);

	bool connectDevice(const std::string & deviceName);
	bool connectDevice(const int port);

	void setState(const State & state);
	bool isInputting() const { return _inputting; }

	using RecordingCallback = std::function<void()>;

	using PlaybackStartCallback = std::function<bool(const std::string&)>;
	using PlaybackStopCallback = std::function<void()>;

	void setOnStartRecording(
		RecordingCallback callback
	);

	void setOnStopRecording(
		RecordingCallback callback
	);

	void setOnStartPlayback(
		PlaybackStartCallback callback
	);

	void setOnStopPlayback(
		PlaybackStopCallback callback
	);

	bool startRecording();

	bool stopRecording();

	bool isRecording() const
	{
		return _recording;
	}

	bool isRecordingCamera() const
	{
		return _recording && _recordCamera;
	}

	const std::filesystem::path& recordingDirectory() const
	{
		return _recordingDirectory;
	}
	
	/// Draw function
	SystemAction draw(const float currentTime);
	ID3D11ShaderResourceView* getTexture() { return _texture.Get(); }

	std::shared_ptr<MIDIScene> scene() const
	{
		return _scene;
	}
	
	/// Clean function
	void clean();

	/// Handle screen resizing
	void resize(int width, int height);

	/// Handle window content density.
	void rescale(float scale);

	void resizeAndRescale(int width, int height, float scale);

	/// Handle keyboard inputs
	void keyPressed(int key, int action);

	void setGUIScale(float scale);
	void setShowGUI(bool state) { _showGUI = state; }

	void updateConfiguration(Configuration& config);

	void setAlphaBlending(bool enabled);
	void setAdditiveBlending(bool enabled);

	glm::ivec2 getSize() {
		return _backbufferSize;
	}

	ScreenQuad& getFullscreenQuad() {
		return _passthrough;
	}

	double getElapsedRecordingTime() {
		return (_timer * _state.scrollSpeed) - _recordingStartTime;
	}

	double getCurrentTime() {
		return DEBUG_SPEED * float(System::time());
	}

	bool isPlaybackLoaded() const
	{
		return _playbackLoaded;
	}

	bool isPlaybackPlaying() const
	{
		return _playbackPlaying;
	}

	bool isPlaybackPaused() const
	{
		return _playbackPaused;
	}

	double getTime() const
	{
		return _timer;
	}

	double getTimeAdjusted() const
	{
		return std::max(0.0f, _timer);
	}

private:

	struct Layer {
		
		enum Type : unsigned int {
			BGCOLOR = 0, BGTEXTURE, BLUR, ANNOTATIONS, KEYBOARD, PARTICLES, NOTES, FLASHES, PEDAL, WAVE, COUNT
		};

		Type type = BGCOLOR;
		std::string name = "None";
		void (Viewer::*draw)(const glm::vec2 &) = nullptr;
		bool * toggle = nullptr;

	};

	void blurPrepass();

	void drawBlur(const glm::vec2 & invSize);

	void drawParticles(const glm::vec2 & invSize);

	void drawScore(const glm::vec2 & invSize);

	void drawKeyboard(const glm::vec2 & invSize);

	void drawNotes(const glm::vec2 & invSize);

	void drawFlashes(const glm::vec2 & invSize);

	void drawPedals(const glm::vec2 & invSize);
	
	void drawWaves(const glm::vec2 & invSize);

	SystemAction drawGUI(const float currentTime);

	void drawScene(bool transparentBG);

	SystemAction showTopButtons(double currentTime);

	void showNoteOptions();

	void showParticleOptions();

	void showKeyboardOptions();

	void showFlashOptions();

	void showPedalOptions();
	
	void showWaveOptions();

	void showBlurOptions();

	void showScoreOptions();

	void showBackgroundOptions();

	void showBottomButtons();

	void showLayers();

	void showDevices();

	void showVisibility();

	void showSets();

	void showSetEditor();

	void showParticlesEditor();

	bool drawPedalImageSettings(ID3D11ShaderResourceView* tex, const glm::vec2& size, bool labelsAfter, bool flipUV, PathCollection& path, unsigned int index, glm::vec3& color);

	void refreshPedalTextures(State::PedalsState& pedals);

	void showPedalsEditor();

	void applyBackgroundColor();

	void applyAllSettings();
	
	void reset();

	void updateSizes();

	bool radioButtonSetMode(const char* name, SetMode& mode, SetMode value);

	bool channelColorEdit(const char * name, const char * displayName, ColorArray & colors);
	
	void updateMinMaxKeys();

	void synchronizeColors(const ColorArray & colors);

	void ImGuiPushItemWidth(int w);

	void ImGuiSameLine(int w = 0);

	bool createRecordingDirectory();

	void refreshRecordings();

	void drawPlaybackSettings();

	void startPlayback();

	void pausePlayback();

	void stopPlayback();

	bool hasRecordings() const;

	State _state;
	Configuration* _config = nullptr;
	std::array<Layer, Layer::COUNT> _layers;
	State _backupState;

	float _timer = 0.0f;
	float _timerStart = 0.0f;
	bool _shouldPlay = false;
	bool _showGUI = true;
	bool _showDebug = false;
	bool _verbose = false;

	ID3D11Device* _device = nullptr;
	ID3D11DeviceContext* _context = nullptr;

	ComPtr<ID3D11BlendState> _alphaBlendState;
	ComPtr<ID3D11BlendState> _additiveBlendState;

	Renderer _renderer;
	MidiCamera _camera;
	ComPtr<ID3D11ShaderResourceView> _texture;
	
	std::shared_ptr<Framebuffer> _particlesFramebuffer;
	std::shared_ptr<Framebuffer> _blurFramebuffer0;
	std::shared_ptr<Framebuffer> _blurFramebuffer1;
	std::shared_ptr<Framebuffer> _renderFramebuffer;
	std::shared_ptr<Framebuffer> _finalFramebuffer;

	std::shared_ptr<MIDIScene> _scene;
	ScreenQuad _blurringScreen;
	ScreenQuad _passthrough;
	ScreenQuad _fxaa;

	glm::ivec2 _windowSize;
	glm::ivec2 _backbufferSize;
	float _guiScale = 1.0f;
	unsigned int _shouldQuit = 0;
	int _selectedPort = 0;
	bool _showLayers = false;
	bool _showSetListEditor = false;
	bool _showParticleEditor = false;
	bool _showPedalsEditor = false;
	bool _exitAfterRecording = false;
	bool _liveplay = false;

	RecordingCallback _onStartRecording;
	RecordingCallback _onStopRecording;

	std::filesystem::path _tempDirectory;
	std::filesystem::path _recordingDirectory;
	double _recordingStartTime = 0.0;

	bool _recording = false;
	bool _recordCamera = true;
	bool _shouldOpenRecordingPopup = false;

	PlaybackStartCallback _onStartPlayback;
	PlaybackStopCallback _onStopPlayback;

	bool _playbackWindowOpen = false;
	bool _playbackLoaded = false;
	bool _playbackPlaying = false;
	bool _playbackPaused = false;

	double _playbackPauseStart = 0.0;

	std::filesystem::path _playbackRecordingDirectory;
	std::string _playbackVideoPath;
	std::string _playbackMidiPath;

	std::vector<std::filesystem::path> _availableRecordings;

	bool _inputting = false;

	struct MIDIDeviceEvent {
		bool connected;
		std::string deviceName;
	};

	void handleMIDIDeviceEvent();

	std::mutex _midiDeviceEventMutex;
	std::optional<MIDIDeviceEvent> _pendingMIDIDeviceEvent;
};
