#include "Viewer.h"

#include "../helpers/ProgramUtilities.h"
#include "../helpers/ResourcesManager.h"
#include "../helpers/ImGuiStyle.h"
#include "../resources/strings.h"

#include <cstring>
#include <iostream>
#include <utility>
#include <vector>
#include <algorithm>
#include <fstream>

#include <windows.h>
#include <shobjidl.h>
#include <stdio.h>

#include <imgui/imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include "scene/MIDIScene.h"
#include "scene/MIDISceneFile.h"
#include "scene/MIDISceneLive.h"

#include "../../../util/Logger.h"
#include "../../../util/config.h"

namespace fs = std::filesystem;

Viewer::Viewer(
	D3D11Interface d3dInterface,
	int width,
	int height
)
	: _device(d3dInterface.device),
	_context(d3dInterface.context)
{

	_showGUI = true;
	_showDebug = false;

	_windowSize = glm::ivec2(width, height);
	_camera.screen(_windowSize[0], _windowSize[1], 1.0f);
	_backbufferSize = glm::vec2(_windowSize);

	_tempDirectory =
		fs::temp_directory_path() /
		"PianoVisualizer";

	refreshRecordings();

	// Setup framebuffers, size does not really matter as we expect a resize event just after.
	const glm::ivec2 renderSize = _camera.renderSize();

	auto createFramebuffer = [this, &renderSize]() {
		return std::make_shared<Framebuffer>(
			_device,
			renderSize.x,
			renderSize.y
		);
	};

	_particlesFramebuffer = createFramebuffer();
	_blurFramebuffer0 = createFramebuffer();
	_blurFramebuffer1 = createFramebuffer();
	_renderFramebuffer = createFramebuffer();
	_finalFramebuffer = createFramebuffer();

	Logger::Log("Creating Blurring Screen Quad...\n");
	_blurringScreen.init(_device, _particlesFramebuffer->textureId(), "particlesblur_frag");
	
	Logger::Log("Creating fxaa Quad...\n");
	_fxaa.init(_device, "fxaa_frag");

	Logger::Log("Creating passthrough Quad...\n");
	_passthrough.init(_device, "screenquad_frag");

	// Create the layers.
	//_layers[Layer::BGCOLOR].type = Layer::BGCOLOR;
	//_layers[Layer::BGCOLOR].name = "Background color";
	//_layers[Layer::BGCOLOR].toggle = &_state.showBackground;

	_layers[Layer::BLUR].type = Layer::BLUR;
	_layers[Layer::BLUR].name = "Blur effect";
	_layers[Layer::BLUR].draw = &Viewer::drawBlur;

	//_layers[Layer::ANNOTATIONS].type = Layer::ANNOTATIONS;
	//_layers[Layer::ANNOTATIONS].name = "Score";
	//_layers[Layer::ANNOTATIONS].draw = &Viewer::drawScore;

	_layers[Layer::KEYBOARD].type = Layer::KEYBOARD;
	_layers[Layer::KEYBOARD].name = "Keyboard";
	_layers[Layer::KEYBOARD].draw = &Viewer::drawKeyboard;

	_layers[Layer::PARTICLES].type = Layer::PARTICLES;
	_layers[Layer::PARTICLES].name = "Particles";
	_layers[Layer::PARTICLES].draw = &Viewer::drawParticles;

	_layers[Layer::NOTES].type = Layer::NOTES;
	_layers[Layer::NOTES].name = "Notes";
	_layers[Layer::NOTES].draw = &Viewer::drawNotes;

	_layers[Layer::FLASHES].type = Layer::FLASHES;
	_layers[Layer::FLASHES].name = "Flashes";
	_layers[Layer::FLASHES].draw = &Viewer::drawFlashes;

	//_layers[Layer::PEDAL].type = Layer::PEDAL;
	//_layers[Layer::PEDAL].name = "Pedal";
	//_layers[Layer::PEDAL].draw = &Viewer::drawPedals;

	_layers[Layer::WAVE].type = Layer::WAVE;
	_layers[Layer::WAVE].name = "Waves";
	_layers[Layer::WAVE].draw = &Viewer::drawWaves;

	// Register state.
	//_layers[Layer::BGTEXTURE].toggle = &_state.background.image;
	_layers[Layer::BLUR].toggle = &_state.showBlur;
	//_layers[Layer::ANNOTATIONS].toggle = &_state.showScore;
	_layers[Layer::KEYBOARD].toggle = &_state.showKeyboard;
	_layers[Layer::PARTICLES].toggle = &_state.showParticles;
	_layers[Layer::NOTES].toggle = &_state.showNotes;
	_layers[Layer::FLASHES].toggle = &_state.showFlashes;
	//_layers[Layer::PEDAL].toggle = &_state.showPedal;
	_layers[Layer::WAVE].toggle = &_state.showWave;

	_renderer.renderSetup(_device, _context);

	MIDISceneLive::setDeviceCallback(
		[this](bool connected, const std::string& deviceName)
		{
			std::lock_guard<std::mutex> lock(
				_midiDeviceEventMutex
			);

			_pendingMIDIDeviceEvent = MIDIDeviceEvent{
				connected,
				deviceName
			};
		}
	);

	_scene.reset(new MIDIScene());

	if (MIDISceneLive::availablePortsCount() > 0)
	{
		_selectedPort = 0;
		connectDevice(_selectedPort);
	}

	// ------------------------------------------------------------
	// Blend states
	// ------------------------------------------------------------

	// Standard alpha blending:
	// src * alpha + dest * (1 - alpha)
	{
		D3D11_BLEND_DESC desc = {};

		desc.RenderTarget[0].BlendEnable = TRUE;

		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;

		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

		desc.RenderTarget[0].RenderTargetWriteMask =
			D3D11_COLOR_WRITE_ENABLE_ALL;

		HRESULT hr = _device->CreateBlendState(
			&desc,
			&_alphaBlendState
		);

		if (FAILED(hr))
		{
			// Handle error if desired.
		}
	}

	// Additive blending:
	// src + dest
	{
		D3D11_BLEND_DESC desc = {};

		desc.RenderTarget[0].BlendEnable = TRUE;

		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;

		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

		desc.RenderTarget[0].RenderTargetWriteMask =
			D3D11_COLOR_WRITE_ENABLE_ALL;

		HRESULT hr = _device->CreateBlendState(
			&desc,
			&_additiveBlendState
		);

		if (FAILED(hr))
		{
			// Handle error if desired.
		}
	}

	// Ensure we are using the C locale.
	System::forceLocale();

	Config::GetConfigDirectory();

	const std::vector<std::string> argv = {};

	_config = new Configuration(argv);

	if (_config->showHelp)
	{
		Configuration::printHelp();

		return;
	}

	if (_config->showVersion)
	{
		Configuration::printVersion();

		return;
	}

	// ---------------------------------------------------------
	// Load MIDI file if specified
	// ---------------------------------------------------------

	if (!_config->lastMidiPath.empty())
	{
		loadFile(
			_config->lastMidiPath
		);
	}

	// ---------------------------------------------------------
	// Apply custom state
	// ---------------------------------------------------------

	State state;

	if (!_config->lastConfigPath.empty())
	{
		Logger::Log("[Config] Loading config: %s\n", _config->lastConfigPath.c_str());
		state.load(_config->lastConfigPath);
	}

	state.load(
		_config->args()
	);

	setState(
		state
	);

	// ---------------------------------------------------------
	// Connect to MIDI device
	// ---------------------------------------------------------

	if (!_config->lastMidiDevice.empty())
	{
		connectDevice(
			_config->lastMidiDevice
		);
	}
}

Viewer::~Viewer() {
	Logger::Log("Viewer closed!\n");
}

bool Viewer::loadFile(const std::string& midiFilePath) {
	std::shared_ptr<MIDIScene> scene(nullptr);

	try {
		scene = std::make_shared<MIDISceneFile>(midiFilePath, _state.setOptions, _state.filter);
	} catch(...){
		// Failed to load.
		return false;
	}
	// Player.
	_timer = -_state.prerollTime;
	_shouldPlay = false;
	_liveplay = false;

	// Init objects.
	_scene = scene;
	applyAllSettings();
	return true;
}

bool Viewer::connectDevice(const std::string& deviceName) {
	_selectedPort = -1;
	
	const auto & devices = MIDISceneLive::availablePorts(true);
	for(int i = 0; i < devices.size(); ++i){
		if(devices[i] == deviceName){
			_selectedPort = i;
		}
	}

	if(_selectedPort == -1){
		if(deviceName != VIRTUAL_DEVICE_NAME){
			std::cerr << "[MIDI] Unable to connect to device named " << deviceName << "." << std::endl;
			return false;
		}
	}

	_scene = std::make_shared<MIDISceneLive>(_selectedPort, _verbose);
	_timer = 0.0f;
	// Don't start immediately
	// _shouldPlay = true;
	_state.reverseScroll = true;
	_state.scrollSpeed = 1.0f;
	_liveplay = true;
	applyAllSettings();

	return true;
}

bool Viewer::connectDevice(const int port) {

	MIDISceneLive::availablePorts();
	const int portCount = MIDISceneLive::availablePortsCount();

	if (port < 0 || port >= portCount)
	{
		Logger::Log("[MIDI] Unable to connect to device at port %d. Available ports: %d\n", port, portCount);
		return false;
	}

	_selectedPort = port;

	_scene = std::make_shared<MIDISceneLive>(_selectedPort, _verbose);
	_timer = 0.0f;
	// Don't start immediately
	_shouldPlay = true;
	_state.reverseScroll = true;
	_state.scrollSpeed = 1.0f;
	_liveplay = true;
	applyAllSettings();

	return true;
}

SystemAction Viewer::draw(
	float currentTime
)
{
	handleMIDIDeviceEvent();

	_timer =
		_shouldPlay
		? (currentTime - _timerStart)
		: _timer;

	// Render the complete scene, including the final
	// post-processing step, into _finalFramebuffer.
	drawScene(true);

	_texture = _finalFramebuffer->textureId();

	SystemAction action = SystemAction::NONE;

	if (_showGUI)
	{
		action = drawGUI(currentTime);
	}

	return action;
}

void Viewer::drawScene(bool transparentBG)
{
	// Update active notes listing.
	_scene->updatesActiveNotes(
		_state.scrollSpeed * _timer,
		_state.scrollSpeed,
		_state.filter
	);

	static float savedKeyboardSize = 0.f;

	if (!_state.showKeyboard)
	{
		if (_state.keyboard.size != 0.f)
		{
			savedKeyboardSize = _state.keyboard.size;
			_state.keyboard.size = 0.f;

			_renderer.setKeyboardSizeAndFadeout(_state.keyboard.size, _state.notes.fadeOut);
		}
	}
	else if (_state.keyboard.size == 0.f)
	{
		_state.keyboard.size = savedKeyboardSize;
		savedKeyboardSize = 0.f;

		_renderer.setKeyboardSizeAndFadeout(_state.keyboard.size, _state.notes.fadeOut);
	}

	// Upload any changed scene data to the GPU.
	_renderer.upload(_scene);

	if (!_renderFramebuffer || !_finalFramebuffer)
	{
		return;
	}

	const glm::vec2 invSizeFb =
		1.0f / glm::vec2(
			_renderFramebuffer->width(),
			_renderFramebuffer->height()
		);

	// ------------------------------------------------------------
	// Blur prepass
	// ------------------------------------------------------------

	if (_state.showBlur)
	{
		blurPrepass();
	}

	// ------------------------------------------------------------
	// Render scene into render framebuffer
	// ------------------------------------------------------------

	_renderFramebuffer->bind(_context);

	D3D11_VIEWPORT viewport{};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width =
		static_cast<float>(_renderFramebuffer->width());
	viewport.Height =
		static_cast<float>(_renderFramebuffer->height());
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	_context->RSSetViewports(1, &viewport);

	// Background color.
	const float clearColor[] =
	{
		0.0f,
		0.0f,
		0.0f,
		0.0f
	};

	_context->ClearRenderTargetView(
		_renderFramebuffer->renderTarget(),
		clearColor
	);

	// Draw all enabled layers in their configured order.
	for (int i = 0;
		i < static_cast<int>(_state.layersMap.size());
		++i)
	{
		const int layerId = _state.layersMap[i];

		if (layerId < 0 ||
			layerId >= static_cast<int>(_layers.size()))
		{
			continue;
		}

		if (_layers[layerId].draw &&
			*(_layers[layerId].toggle))
		{
			(this->*_layers[layerId].draw)(invSizeFb);
		}
	}

	_renderFramebuffer->unbind(_context);

	// ------------------------------------------------------------
	// Apply FXAA or copy render framebuffer to final framebuffer
	// ------------------------------------------------------------

	_finalFramebuffer->bind(_context);

	D3D11_VIEWPORT finalViewport{};
	finalViewport.TopLeftX = 0.0f;
	finalViewport.TopLeftY = 0.0f;
	finalViewport.Width =
		static_cast<float>(_finalFramebuffer->width());
	finalViewport.Height =
		static_cast<float>(_finalFramebuffer->height());
	finalViewport.MinDepth = 0.0f;
	finalViewport.MaxDepth = 1.0f;

	_context->RSSetViewports(1, &finalViewport);

	if (_state.applyAA)
	{
		_fxaa.draw(
			_context,
			_renderFramebuffer->textureId(),
			0.0f,
			invSizeFb
		);
	}
	else
	{
		_passthrough.draw(
			_context,
			_renderFramebuffer->textureId(),
			0.0f
		);
	}

	_finalFramebuffer->unbind(_context);
}

void Viewer::blurPrepass()
{
	// ------------------------------------------------------------
	// Particle prepass
	// ------------------------------------------------------------

	const glm::vec2 invSizeB =
		1.0f / glm::vec2(
			_particlesFramebuffer->width(),
			_particlesFramebuffer->height()
		);

	_particlesFramebuffer->bind(_context);

	D3D11_VIEWPORT particleViewport{};
	particleViewport.TopLeftX = 0.0f;
	particleViewport.TopLeftY = 0.0f;
	particleViewport.Width =
		static_cast<float>(_particlesFramebuffer->width());
	particleViewport.Height =
		static_cast<float>(_particlesFramebuffer->height());
	particleViewport.MinDepth = 0.0f;
	particleViewport.MaxDepth = 1.0f;

	_context->RSSetViewports(1, &particleViewport);

	// Draw blurred particles from previous frames.
	_passthrough.draw(
		_context,
		_blurFramebuffer1->textureId(),
		_timer
	);

	// Draw new particles.
	if (_state.showParticles)
	{
		_renderer.drawParticles(
			_scene,
			_timer,
			invSizeB,
			_state.particles,
			true
		);
	}

	// Draw notes into the blur buffer.
	if (_state.showBlurNotes)
	{
		_renderer.drawNotes(
			_scene,
			_timer * _state.scrollSpeed,
			invSizeB,
			_state.notes,
			_state.reverseScroll,
			true
		);
	}

	// ------------------------------------------------------------
	// Horizontal blur pass
	// ------------------------------------------------------------

	const glm::vec2 invBlurSize0 =
		1.0f / glm::vec2(
			_particlesFramebuffer->width(),
			_particlesFramebuffer->height()
		);

	_blurFramebuffer0->bind(_context);

	D3D11_VIEWPORT blur0Viewport{};
	blur0Viewport.TopLeftX = 0.0f;
	blur0Viewport.TopLeftY = 0.0f;
	blur0Viewport.Width =
		static_cast<float>(_blurFramebuffer0->width());
	blur0Viewport.Height =
		static_cast<float>(_blurFramebuffer0->height());
	blur0Viewport.MinDepth = 0.0f;
	blur0Viewport.MaxDepth = 1.0f;

	_context->RSSetViewports(1, &blur0Viewport);

	_blurringScreen.draw(
		_context,
		_particlesFramebuffer->textureId(),
		0.0f,
		invBlurSize0
	);

	// ------------------------------------------------------------
	// Vertical blur pass
	// ------------------------------------------------------------

	const glm::vec2 invBlurSize1 =
		1.0f / glm::vec2(
			_blurFramebuffer0->width(),
			_blurFramebuffer0->height()
		);

	_blurFramebuffer1->bind(_context);

	D3D11_VIEWPORT blur1Viewport{};
	blur1Viewport.TopLeftX = 0.0f;
	blur1Viewport.TopLeftY = 0.0f;
	blur1Viewport.Width =
		static_cast<float>(_blurFramebuffer1->width());
	blur1Viewport.Height =
		static_cast<float>(_blurFramebuffer1->height());
	blur1Viewport.MinDepth = 0.0f;
	blur1Viewport.MaxDepth = 1.0f;

	_context->RSSetViewports(1, &blur1Viewport);

	_blurringScreen.draw(
		_context,
		_blurFramebuffer0->textureId(),
		1.0f,
		invBlurSize1
	);

	// Leave render target unbound.
	_blurFramebuffer1->unbind(_context);
}

void Viewer::drawBlur(const glm::vec2&)
{
	setAlphaBlending(true);

	_passthrough.draw(
		_context,
		_blurFramebuffer1->textureId(),
		_timer
	);

	setAlphaBlending(false);
}

void Viewer::drawParticles(const glm::vec2 & invSize) {
	_renderer.drawParticles(_scene, _timer, invSize, _state.particles, false);
}

void Viewer::drawScore(const glm::vec2& invSize)
{
	setAlphaBlending(true);

	const auto& currentQuality =
		VisualizerQuality::availables.at(_state.quality);

	_renderer.drawScore(
		_scene,
		_timer * _state.scrollSpeed,
		invSize,
		_state.score,
		_state.scale,
		currentQuality.finalResolution,
		_state.keyboard.size,
		_state.horizontalScroll,
		_state.reverseScroll
	);

	setAlphaBlending(false);
}

void Viewer::drawKeyboard(const glm::vec2 & invSize) {
	const ColorArray & majColors = _state.keyboard.customKeyColors ? _state.keyboard.majorColor : _state.notes.majorColors;
	const ColorArray & minColors = _state.keyboard.customKeyColors ? _state.keyboard.minorColor : _state.notes.minorColors;
	_renderer.drawKeyboard(_scene, _timer, invSize, _state.keyboard.edgeColor, _state.keyboard.backColor, majColors, minColors, _state.keyboard.highlightKeys);
}

void Viewer::drawNotes(const glm::vec2& invSize)
{
	setAlphaBlending(true);

	_renderer.drawNotes(
		_scene,
		_timer * _state.scrollSpeed,
		invSize,
		_state.notes,
		_state.reverseScroll,
		false
	);

	setAlphaBlending(false);
}

void Viewer::drawFlashes(const glm::vec2 & invSize) {
	_renderer.drawFlashes(_scene, _timer, invSize, _state.flashes);
}

void Viewer::drawPedals(const glm::vec2 & invSize){
	// Extra shift above the waves.
	_renderer.drawPedals(_scene, _timer, invSize, _state.pedals, _state.keyboard.size + (_state.showWave ? 0.01f : 0.0f), _state.horizontalScroll);
}

void Viewer::drawWaves(const glm::vec2 & invSize){
	_renderer.drawWaves(_scene, _timer, invSize, _state.waves, _state.keyboard.size);
}

// Helper

static std::string wideToUtf8(const std::wstring& wide)
{
	if (wide.empty())
		return {};

	const int size = WideCharToMultiByte(
		CP_UTF8,
		0,
		wide.data(),
		static_cast<int>(wide.size()),
		nullptr,
		0,
		nullptr,
		nullptr
	);

	std::string result(size, '\0');

	WideCharToMultiByte(
		CP_UTF8,
		0,
		wide.data(),
		static_cast<int>(wide.size()),
		result.data(),
		size,
		nullptr,
		nullptr
	);

	return result;
}

SystemAction Viewer::drawGUI(const float currentTime) {

	SystemAction action = SystemAction::NONE;

	auto liveScene =
		std::dynamic_pointer_cast<MIDISceneLive>(
			_scene
		);

	if (_shouldOpenRecordingPopup)
	{
		ImGui::OpenPopup("Start Recording");
		_shouldOpenRecordingPopup = false;
	}

	if (ImGui::BeginPopupModal("Start Recording", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Recording Options");

		ImGui::Separator();

		ImGui::Checkbox(
			"Record camera",
			&_recordCamera
		);

		ImGui::Spacing();

		if (ImGui::Button(
			"Start Recording",
			ImVec2(140.0f, 0.0f)
		))
		{
			_recording = true;

			if (!createRecordingDirectory())
			{
				Logger::Log("Failed to create recording directory!\n");
				ImGui::CloseCurrentPopup();
			}
			else {
				if (_recordCamera && _onStartRecording)
				{
					_onStartRecording();
				}

				_recordingStartTime = _timer * _state.scrollSpeed;
				liveScene->startRecording(_recordingStartTime);
			}

			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button(
			"Cancel",
			ImVec2(100.0f, 0.0f)
		))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	if (ImGui::Begin("Visualizer Settings", &_showGUI, ImGuiWindowFlags_AlwaysAutoResize)) {

		action = showTopButtons(currentTime);
		ImGui::Separator();

		//// Detail text.
		//const int nCount = _scene->notesCount();
		//const double duration = _scene->duration();
		//const int speed = int(std::round(double(nCount)/(std::max)(0.001, duration)));
		//ImGui::Text("Time: %.2f, notes: %d, duration: %.1fs, speed: %d notes/s", _timer * _state.scrollSpeed, nCount, duration, speed);

		//ImGui::Separator();
		
		// Load button.
		//if (ImGui::Button("Load MIDI file..."))
		//{
		//	IFileOpenDialog* dialog = nullptr;

		//	HRESULT hr = CoCreateInstance(
		//		CLSID_FileOpenDialog,
		//		nullptr,
		//		CLSCTX_INPROC_SERVER,
		//		IID_PPV_ARGS(&dialog)
		//	);

		//	if (SUCCEEDED(hr))
		//	{
		//		// Only allow MIDI files.
		//		COMDLG_FILTERSPEC filters[] =
		//		{
		//			{ L"MIDI files", L"*.mid;*.midi" },
		//			{ L"All files", L"*.*" }
		//		};

		//		dialog->SetFileTypes(
		//			static_cast<UINT>(std::size(filters)),
		//			filters
		//		);

		//		dialog->SetTitle(L"Load MIDI file");

		//		hr = dialog->Show(nullptr);

		//		if (SUCCEEDED(hr))
		//		{
		//			IShellItem* item = nullptr;

		//			hr = dialog->GetResult(&item);

		//			if (SUCCEEDED(hr))
		//			{
		//				PWSTR path = nullptr;

		//				hr = item->GetDisplayName(
		//					SIGDN_FILESYSPATH,
		//					&path
		//				);

		//				if (SUCCEEDED(hr))
		//				{
		//					loadFile(wideToUtf8(std::wstring(path)));

		//					CoTaskMemFree(path);
		//				}

		//				item->Release();
		//			}
		//		}

		//		dialog->Release();
		//	}
		//}
		//ImGuiSameLine(COLUMN_SIZE);
		//if(_liveplay){
		//	if (ImGui::Button("Clear and stop session")) {
		//		_scene = std::make_shared<MIDIScene>();
		//		_liveplay = false;
		//		_shouldPlay = false;
		//		_timer = 0.0f;
		//		applyAllSettings();
		//	}
		//} else {
		//	if (ImGui::Button("Connect to device...")) {
		//		ImGui::OpenPopup("Devices");
		//	}
		//	showDevices();
		//}

		const char* deviceChangeLabel = _liveplay ? "Change device" : "Connect to device...";
		if (ImGui::Button(deviceChangeLabel)) {
			ImGui::OpenPopup("Devices");
		}
		showDevices();
		
		if (liveScene) {
			// Recording
			ImGui::SameLine();

			const char* recordLabel = _recording ? "Stop recording" : "Start recording";
			if (ImGui::Button(recordLabel)) {


				if (_recording) {
					stopRecording();
					liveScene->stopRecording();
				}
				else {
					startRecording();
				}
			}

			ImGui::SameLine();

			if (_recording) {
				double elapsedTime = getElapsedRecordingTime();
				ImGui::Text("Recording: %.2fs", elapsedTime);
			}
		}

		if (hasRecordings())
		{
			ImGui::SameLine();

			if (ImGui::Button("Playback Recording"))
			{
				refreshRecordings();

				_playbackWindowOpen = true;
			}
		}

		/*const bool existingScene =
			(std::dynamic_pointer_cast<MIDISceneFile>(_scene) != nullptr) ||
			(std::dynamic_pointer_cast<MIDISceneLive>(_scene) != nullptr);

		if (existingScene)
		{
			if (ImGui::Button("Export MIDI file..."))
			{
				IFileSaveDialog* dialog = nullptr;

				HRESULT hr = CoCreateInstance(
					CLSID_FileSaveDialog,
					nullptr,
					CLSCTX_INPROC_SERVER,
					IID_PPV_ARGS(&dialog)
				);

				if (SUCCEEDED(hr))
				{
					COMDLG_FILTERSPEC filters[] =
					{
						{ L"MIDI files", L"*.mid;*.midi" },
						{ L"All files", L"*.*" }
					};

					dialog->SetFileTypes(
						static_cast<UINT>(std::size(filters)),
						filters
					);

					dialog->SetTitle(L"Save MIDI file");
					dialog->SetDefaultExtension(L"mid");

					hr = dialog->Show(nullptr);

					if (SUCCEEDED(hr))
					{
						IShellItem* item = nullptr;

						hr = dialog->GetResult(&item);

						if (SUCCEEDED(hr))
						{
							PWSTR path = nullptr;

							hr = item->GetDisplayName(
								SIGDN_FILESYSPATH,
								&path
							);

							if (SUCCEEDED(hr))
							{
								std::ofstream outFile(
									wideToUtf8(path),
									std::ios::binary
								);

								if (outFile)
								{
									_scene->save(outFile);
									outFile.close();
								}

								CoTaskMemFree(path);
							}

							item->Release();
						}
					}

					dialog->Release();
				}
			}

			ImGui::helpTooltip(
				"Export a new MIDI file of the current session"
			);
		}*/

		ImGui::Separator();

		ImGuiPushItemWidth(100);
		if (ImGui::Combo("Quality", (int *)(&_state.quality), "Half\0Low\0Medium\0High\0Double\0\0")) {
			updateSizes();
		}
		ImGui::helpTooltip(s_quality_dsc);
		ImGui::PopItemWidth();

		// Add FXAA.
		ImGuiSameLine(COLUMN_SIZE);
		ImGui::Checkbox("Smoothing", &_state.applyAA);
		ImGui::helpTooltip(s_smooth_dsc);

		if (ImGui::Button("Show effect layers...")) {
			_showLayers = true;
		}
		ImGui::helpTooltip("Define which effects are visible and their ordering");

		if(_liveplay){
			ImGui::BeginDisabled();
		}

		ImGuiSameLine(COLUMN_SIZE);
		if (ImGui::Button("Tracks & channels visibility...")) {
			ImGui::OpenPopup("Visibility options");
		}
		ImGui::helpTooltip("Define which tracks and channels are visible");
		showVisibility();

		if(_liveplay){
			ImGui::EndDisabled();
			if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)){
				ImGui::SetTooltip("Not available in liveplay");
			}
		}

		if(ImGui::Checkbox("Per-set colors", &_state.perSetColors)){
			if(!_state.perSetColors){
				_state.synchronizeSets();
			}
		}
		ImGui::helpTooltip(s_colors_per_set_dsc);

		if(_state.perSetColors){
			ImGuiSameLine(COLUMN_SIZE);
			if(ImGui::Button("Define color sets...")){
				ImGui::OpenPopup("Note sets options");
			}
			ImGui::helpTooltip("Define how notes should be assigned to color sets");
			showSets();
		}

		if (ImGui::Checkbox("Same colors for all effects", &_state.lockParticleColor)) {
			// If we enable the lock, make sure the colors are synched.
			synchronizeColors(_state.notes.majorColors);
		}
		ImGui::helpTooltip(s_lock_colors_dsc);

		if(ImGui::CollapsingHeader("Playback##HEADER")){
			ImGuiPushItemWidth(100);
			if(ImGui::Combo("Min key", &_state.minKey, midiKeysStrings, 128)){
				updateMinMaxKeys();
			}
			ImGui::helpTooltip(s_min_key_dsc);

			ImGuiSameLine(COLUMN_SIZE);
			if(ImGui::Combo("Max key", &_state.maxKey, midiKeysStrings, 128)){
				updateMinMaxKeys();
			}
			ImGui::helpTooltip(s_max_key_dsc);

			//if (ImGui::InputFloat("Preroll", &_state.prerollTime, 0.1f, 1.0f, "%.1fs")) {
			//	reset();
			//}
			//ImGui::helpTooltip(s_preroll_dsc);

			//if(_liveplay){
			//	ImGui::BeginDisabled();
			//}
			//ImGuiSameLine(COLUMN_SIZE);
			//if(ImGui::SliderFloat("Speed", &_state.scrollSpeed, 0.1f, 5.0f, "%.1fx")){
			//	_state.scrollSpeed = (std::max)(0.01f, _state.scrollSpeed);
			//}
			//if(_liveplay){
			//	ImGui::EndDisabled();
			//	if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)){
			//		ImGui::SetTooltip("Not available in liveplay");
			//	}
			//} else {
			//	ImGui::helpTooltip(s_scroll_speed_dsc);
			//}
			//ImGui::PopItemWidth();

			//if(ImGui::Checkbox("Horizontal scroll", &_state.horizontalScroll)){
			//	_renderer.setOrientation(_state.horizontalScroll);
			//}
			//ImGui::helpTooltip(s_scroll_horizontal_dsc);

			/*if(_liveplay){
				ImGui::BeginDisabled();
			}
			ImGuiSameLine(COLUMN_SIZE);
			ImGui::Checkbox("Reverse scroll", &_state.reverseScroll);
			if(_liveplay){
				ImGui::EndDisabled();
				if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)){
					ImGui::SetTooltip("Not available in liveplay");
				}
			} else {
				ImGui::helpTooltip(s_scroll_reverse_dsc);
			}*/

			/*if(_liveplay){
				ImGui::BeginDisabled();
			}*/
			//ImGui::Checkbox("Loop", &_state.loop);
			//if(_liveplay){
			//	ImGui::EndDisabled();
			//	if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)){
			//		ImGui::SetTooltip("Not available in liveplay");
			//	}
			//} else {
			//	ImGui::helpTooltip(s_loop_dsc);
			//}
		}

		if(ImGui::CollapsingHeader("Notes##HEADER")){
			showNoteOptions();
		}

		if (_state.showFlashes && ImGui::CollapsingHeader("Flashes##HEADER")) {
			showFlashOptions();
		}

		if (_state.showParticles && ImGui::CollapsingHeader("Particles##HEADER")) {
			showParticleOptions();
		}

		if (_state.showKeyboard && ImGui::CollapsingHeader("Keyboard##HEADER")) {
			showKeyboardOptions();
		}

		//if(_state.showPedal && ImGui::CollapsingHeader("Pedal##HEADER")){
		//	showPedalOptions();
		//}

		if(_state.showWave && ImGui::CollapsingHeader("Wave##HEADER")){
			showWaveOptions();
		}

		//if (_state.showScore && ImGui::CollapsingHeader("Score##HEADER")) {
		//	showScoreOptions();
		//}

		if (_state.showBlur && ImGui::CollapsingHeader("Blur##HEADER")) {
			showBlurOptions();
		}

		//if (ImGui::CollapsingHeader("Background##HEADER")) {
		//	showBackgroundOptions();
		//}
		ImGui::Separator();

		showBottomButtons();

		if (_showDebug) {
			ImGui::Separator();
			ImGui::Text("Debug: ");
			ImGuiSameLine();
			ImGui::TextDisabled("(press D to hide)");
			ImGui::Text("%.1f FPS / %.1f ms", ImGui::GetIO().Framerate, ImGui::GetIO().DeltaTime * 1000.0f);
			ImGui::Text("Render size: %dx%d, screen size: %dx%d", _renderFramebuffer->width(), _renderFramebuffer->height(), _camera.screenSize()[0], _camera.screenSize()[1]);
			if (ImGui::Button("Print MIDI content to console")) {
				_scene->print();
			}
			ImGui::Checkbox("Verbose log", &_verbose);
			if(ImGui::Button("Assign random colors")){
				_state.lockParticleColor = true;

				const ColorArray debugColors = {
					glm::vec3{1.0f, 0.0f, 0.0f},
					glm::vec3{1.0f, 0.5f, 0.0f},
					glm::vec3{1.0f, 1.0f, 0.0f},
					glm::vec3{0.5f, 1.0f, 0.0f},
					glm::vec3{0.0f, 1.0f, 0.0f},
					glm::vec3{0.0f, 1.0f, 0.5f},
					glm::vec3{0.0f, 1.0f, 1.0f},
					glm::vec3{0.0f, 0.5f, 1.0f},
					glm::vec3{0.0f, 0.0f, 1.0f},
					glm::vec3{0.5f, 0.0f, 1.0f},
					glm::vec3{1.0f, 0.0f, 1.0f},
					glm::vec3{1.0f, 0.0f, 0.5f}
				};
				synchronizeColors(debugColors);
			}
		}
	}
	ImGui::End();

	

	if(_showLayers){
		showLayers();
	}

	if(_showSetListEditor){
		showSetEditor();
	}

	if(_showParticleEditor){
		showParticlesEditor();
	}

	if(_showPedalsEditor){
		showPedalsEditor();
	}

	if (_playbackWindowOpen){
		drawPlaybackSettings();
	}

	return action;
}

void Viewer::synchronizeColors(const ColorArray & colors){
	// Keep the colors in sync if needed.
	if (!_state.lockParticleColor) {
		return;
	}

	for(size_t cid = 0; cid < SETS_COUNT; ++cid){
		_state.notes.majorColors[cid] = _state.particles.colors[cid] = _state.notes.minorColors[cid] = _state.flashes.colors[cid] = colors[cid];
	}

	// If we have only one channel, synchronize one-shot effects.
	// Disable this because it's not symmetric.
	//if(!_state.perChannelColors){
	//	_state.pedals.color = _state.waves.color = _state.notes.majorColors[0];
	//}
}

SystemAction Viewer::showTopButtons(double currentTime){
	//if (ImGui::Button(_shouldPlay ? "Pause (p)" : "Play (p)")) {
	//	_shouldPlay = !_shouldPlay;
	//	_timerStart = float(currentTime) - _timer;
	//}
	//ImGuiSameLine();
	//if (ImGui::Button("Restart (r)")) {
	//	reset();
	//}
	//ImGuiSameLine();
	if (ImGui::Button("Hide (i)")) {
		_showGUI = false;
	}
	ImGuiSameLine();
	if(ImGui::Button("Display")){
		ImGui::OpenPopup("Display options");
	}
	ImGui::helpTooltip("Configure display settings");

	SystemAction action = SystemAction::NONE;
	if(ImGui::BeginPopup("Display options")){
		
		ImGuiPushItemWidth(100);
		if(ImGui::InputFloat("GUI size", &_guiScale, 0.25f, 1.0f, "%.2fx")){
			_guiScale = glm::clamp(_guiScale, 0.25f, 4.0f);
			setGUIScale(_guiScale);
		}
		ImGui::helpTooltip("Scale of the interface texts and buttons on screen");
		ImGui::PopItemWidth();
		ImGuiSameLine(EXPORT_COLUMN_SIZE);
		if(ImGui::Button("Reset##GUI")){
			setGUIScale(1.0f);
		}
		ImGui::helpTooltip("Reset the scale of the interface to 1x");

		ImGui::EndPopup();
	}

	return action;
}

void Viewer::showNoteOptions() {

	if(channelColorEdit("Notes", "Notes", _state.notes.majorColors)){
		synchronizeColors(_state.notes.majorColors);
	}
	ImGui::helpTooltip(s_color_major_dsc);
	ImGuiSameLine();

	if(channelColorEdit("Minors", "Minors", _state.notes.minorColors)){
		synchronizeColors(_state.notes.minorColors);
	}
	ImGui::helpTooltip(s_color_minor_dsc);
	ImGuiSameLine(COLUMN_SIZE);

	ImGuiPushItemWidth(100);
	bool smw0 = ImGui::InputFloat("Scale", &_state.scale, 0.01f, 0.1f, "%.2fx");
	ImGui::helpTooltip(s_time_scale_dsc);;

	smw0 = ImGui::SliderPercent("Minor width", &_state.background.minorsWidth, 0.1f, 1.0f) || smw0;
	ImGui::helpTooltip(s_minor_size_dsc);
	ImGui::PopItemWidth();

	if (smw0) {
		_state.scale = (std::max)(_state.scale, 0.01f);
		_state.background.minorsWidth = glm::clamp(_state.background.minorsWidth, 0.1f, 1.0f);
		// TODO: (MV) just apply when needed?
		_renderer.setScaleAndMinorWidth(_state.scale, _state.background.minorsWidth);
	}

	ImGuiPushItemWidth(100);
	if(ImGui::SliderFloat("Radius", &_state.notes.cornerRadius, 0.0f, 1.0f)){
		_state.notes.cornerRadius = glm::clamp(_state.notes.cornerRadius, 0.0f, 1.0f);
	}
	ImGui::helpTooltip(s_notes_corner_radius_dsc);

	ImGuiSameLine(COLUMN_SIZE);
	if(ImGui::SliderFloat("Fadeout", &_state.notes.fadeOut, 0.0f, 1.0f)){
		_state.notes.fadeOut = glm::clamp(_state.notes.fadeOut, 0.0f, 1.0f);
		_renderer.setKeyboardSizeAndFadeout(_state.keyboard.size, _state.notes.fadeOut);
	}
	ImGui::helpTooltip(s_fadeout_notes_dsc);

	if(ImGui::SliderPercent("Edge", &_state.notes.edgeWidth, 0.0f, 1.0f)){
		_state.notes.edgeWidth = glm::clamp(_state.notes.edgeWidth, 0.0f, 1.0f);
	}
	ImGui::helpTooltip(s_notes_edge_width_dsc);
	ImGuiSameLine(COLUMN_SIZE);

	if(ImGui::SliderPercent("Intensity", &_state.notes.edgeBrightness, 0.0f, 4.0f)){
		_state.notes.edgeBrightness = glm::max(_state.notes.edgeBrightness, 0.0f);
	}
	ImGui::helpTooltip(s_notes_edge_intensity_dsc);

	ImGui::PopItemWidth();
}

void Viewer::showFlashOptions() {
	if(channelColorEdit("Color##Flashes", "Color", _state.flashes.colors)){
		synchronizeColors(_state.flashes.colors);
	}
	ImGui::helpTooltip(s_color_flashes_dsc);

	ImGuiPushItemWidth(100);
	ImGui::SliderFloat("Scale##flash", &_state.flashes.size, 0.1f, 3.0f, "%.2fx");
	ImGui::helpTooltip(s_flashes_size_dsc);

	ImGuiSameLine(COLUMN_SIZE);
	ImGui::SliderFloat("Fade Time##flash", &_state.flashes.fadeTime, 0.f, 3.f, "%.3fs");
	ImGui::helpTooltip(s_flashes_fade_dsc);
	ImGui::PopItemWidth();

	// Additional halo control
	ImGuiPushItemWidth(100);
	if(ImGui::SliderFloat("Halo min", &_state.flashes.haloInnerRadius, 0.0f, 1.0f)){
		_state.flashes.haloInnerRadius = glm::clamp(_state.flashes.haloInnerRadius, 0.0f, _state.flashes.haloOuterRadius);
	}
	ImGui::helpTooltip(s_flashes_halo_inner_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	if(ImGui::SliderFloat("Halo max", &_state.flashes.haloOuterRadius, 0.0f, 1.0f)){
		_state.flashes.haloOuterRadius = glm::clamp(_state.flashes.haloOuterRadius, _state.flashes.haloInnerRadius, 1.0f);
	}
	ImGui::helpTooltip(s_flashes_halo_outer_dsc);

	if(ImGui::SliderPercent("Intensity##Halo", &_state.flashes.haloIntensity, 0.0f, 2.0f)){
		_state.flashes.haloIntensity = glm::max(0.f, _state.flashes.haloIntensity);
	}
	ImGui::helpTooltip(s_flashes_halo_intensity_dsc);

	ImGuiSameLine(COLUMN_SIZE);
	if (ImGui::Button("Clear image##Flashes")) {
		_state.flashes.imagePath.clear();
		if(_state.flashes.tex != ResourcesManager::getTextureFor("flash")){
			if (_state.flashes.tex)
				_state.flashes.tex->Release();
		}
		_state.flashes.tex = ResourcesManager::getTextureFor("flash");
		_state.flashes.texColCount = 2;
		_state.flashes.texRowCount = 4;
	}
	ImGui::helpTooltip("Restore the default flash image atlas");

	// Don't expose tiling on default image.
	if(!_state.flashes.imagePath.empty()){
		if(ImGui::InputInt("Columns", &_state.flashes.texColCount)){
			_state.flashes.texColCount = glm::max(1, _state.flashes.texColCount);
		}
		ImGui::helpTooltip(s_flashes_img_columns_dsc);
		ImGuiSameLine(COLUMN_SIZE);
		if(ImGui::InputInt("Rows", &_state.flashes.texRowCount)){
			_state.flashes.texRowCount = glm::max(1, _state.flashes.texRowCount);
		}
		ImGui::helpTooltip(s_flashes_img_rows_dsc);
	}

	ImGui::PopItemWidth();
}

void Viewer::showParticleOptions(){
	ImGui::PushID("ParticlesSettings");

	if(channelColorEdit("Color##Particles", "Color", _state.particles.colors)){
		synchronizeColors(_state.particles.colors);
	}
	ImGui::helpTooltip(s_color_particles_dsc);

	ImGuiSameLine(COLUMN_SIZE);

	ImGuiPushItemWidth(100);
	if (ImGui::InputFloat("Size##particles", &_state.particles.scale, 1.0f, 10.0f, "%.0fpx")) {
		_state.particles.scale = (std::max)(1.0f, _state.particles.scale);
	}
	ImGui::helpTooltip(s_particles_size_dsc);

	const bool mp0 = ImGui::InputFloat("Speed", &_state.particles.speed, 0.01f, 1.0f, "%.2fx");
	ImGui::helpTooltip(s_particles_speed_dsc);
	ImGuiSameLine(COLUMN_SIZE);

	const bool mp1 = ImGui::InputFloat("Spread", &_state.particles.expansion, 0.1f, 5.0f, "%.1fx");
	ImGui::helpTooltip(s_particles_expansion_dsc);

	if (ImGui::SliderInt("Count", &_state.particles.count, 1, 512)) {
		_state.particles.count = glm::clamp(_state.particles.count, 1, 512);
	}
	ImGui::helpTooltip(s_particles_count_dsc);
	ImGui::PopItemWidth();

	if (mp1 || mp0) {
		// TODO: (MV) just apply when needed?
		_renderer.setParticlesParameters(_state.particles.speed, _state.particles.expansion);
	}

	ImGuiSameLine(COLUMN_SIZE);

	if(ImGui::Button("Configure images...##Particles")) {
		_showParticleEditor = true;
		_backupState = _state;
	}
	ImGui::helpTooltip("Define images to assign randomly to each particle");

	ImGuiPushItemWidth(100);
	ImGui::SliderPercent("Turbulences", &_state.particles.turbulenceStrength, 0.01f, 8.0f);
	ImGui::helpTooltip(s_particles_turbulences_dsc);
	ImGui::PopItemWidth();

	ImGui::PopID();
}

void Viewer::showKeyboardOptions(){
	ImGuiPushItemWidth(25);
	ImGui::ColorEdit3("Edge Color##Keys", &_state.keyboard.edgeColor[0], ImGuiColorEditFlags_NoInputs);
	ImGui::helpTooltip(s_color_keyboard_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	ImGui::ColorEdit3("Fill Color##Keys", &_state.keyboard.backColor[0], ImGuiColorEditFlags_NoInputs);
	ImGui::helpTooltip(s_color_keyboard_bg_dsc);
	ImGui::PopItemWidth();

	ImGuiPushItemWidth(100);
	if(ImGui::SliderPercent("Height##Keys", &_state.keyboard.size, 0.0f, 1.0f)){
		_state.keyboard.size = glm::clamp(_state.keyboard.size, 0.0f, 1.0f);
		_renderer.setKeyboardSizeAndFadeout(_state.keyboard.size, _state.notes.fadeOut);
	}
	ImGui::helpTooltip(s_keyboard_size_dsc);
	ImGuiSameLine(COLUMN_SIZE);

	if(ImGui::SliderPercent("Minor height##Keys", &_state.keyboard.minorHeight, 0.0f, 1.0f)){
		_state.keyboard.minorHeight = glm::clamp(_state.keyboard.minorHeight, 0.0f, 1.0f);
		// TODO: (MV) just apply when needed?
		_renderer.setMinorEdgesAndHeight(_state.keyboard.minorEdges, _state.keyboard.minorHeight);
	}
	ImGui::helpTooltip(s_keyboard_minor_height_dsc);
	ImGui::PopItemWidth();

	ImGuiPushItemWidth(25);
	if (ImGui::Checkbox("Minor edges##Keys", &_state.keyboard.minorEdges)){
		// TODO: (MV) just apply when needed?
		_renderer.setMinorEdgesAndHeight(_state.keyboard.minorEdges, _state.keyboard.minorHeight);
	}
	ImGui::helpTooltip(s_keyboard_minor_edges_dsc);
	ImGui::PopItemWidth();
	ImGuiSameLine(COLUMN_SIZE);
	ImGui::Checkbox("Highlight keys", &_state.keyboard.highlightKeys);
	ImGui::helpTooltip(s_keyboard_highlight_dsc);

	if (_state.keyboard.highlightKeys) {
		ImGui::Checkbox("Custom colors", &_state.keyboard.customKeyColors);
		ImGui::helpTooltip(s_keyboard_custom_colors_dsc);

		if (_state.keyboard.customKeyColors) {
			ImGuiSameLine(COLUMN_SIZE);
			ImGuiPushItemWidth(25);
			if(ImGui::ColorEdit3("Major##KeysHighlight", &_state.keyboard.majorColor[0][0], ImGuiColorEditFlags_NoInputs)){
				// Ensure synchronization of the override array.
				for(size_t cid = 1; cid < _state.keyboard.majorColor.size(); ++cid){
					_state.keyboard.majorColor[cid] = _state.keyboard.majorColor[0];
				}
			}
			ImGui::helpTooltip(s_color_keyboard_major_dsc);

			ImGuiSameLine(COLUMN_SIZE+80);
			if(ImGui::ColorEdit3("Minor##KeysHighlight", &_state.keyboard.minorColor[0][0], ImGuiColorEditFlags_NoInputs)){
				// Ensure synchronization of the override array.
				for(size_t cid = 1; cid < _state.keyboard.minorColor.size(); ++cid){
					_state.keyboard.minorColor[cid] = _state.keyboard.minorColor[0];
				}
			}
			ImGui::helpTooltip(s_color_keyboard_minor_dsc);
			ImGui::PopItemWidth();
		}
	}
}

void Viewer::showPedalOptions(){
	ImGuiPushItemWidth(25);
	if(ImGui::ColorEdit3("Colors##Pedals", &_state.pedals.centerColor[0], ImGuiColorEditFlags_NoInputs)){
		// Synchronize other colors
		_state.pedals.topColor = _state.pedals.centerColor;
		_state.pedals.leftColor = _state.pedals.centerColor;
		_state.pedals.rightColor = _state.pedals.centerColor;
	}
	ImGui::helpTooltip(s_color_pedal_dsc);
	ImGui::PopItemWidth();

	ImGuiPushItemWidth(100);
	ImGuiSameLine(COLUMN_SIZE);
	int locationValue = int(_state.pedals.location);
	if(ImGui::Combo("Location", &locationValue, "Top left\0Bottom left\0Top right\0Bottom right\0")){
		_state.pedals.location = State::PedalsState::Location(locationValue);
	}
	ImGui::helpTooltip(s_pedal_location_dsc);

	if(ImGui::SliderPercent("Opacity##Pedals", &_state.pedals.opacity, 0.0f, 1.0f)){
		_state.pedals.opacity = glm::clamp(_state.pedals.opacity, 0.0f, 1.0f);
	}
	ImGui::helpTooltip(s_pedal_opacity_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	if(ImGui::SliderPercent("Size##Pedals", &_state.pedals.size, 0.05f, 0.5f)){
		_state.pedals.size = glm::clamp(_state.pedals.size, 0.05f, 0.5f);
	}
	ImGui::helpTooltip(s_pedal_size_dsc);
	ImGui::PopItemWidth();

	ImGui::Checkbox("Merge pedals", &_state.pedals.merge);
	ImGui::helpTooltip(s_pedal_merge_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	if(ImGui::Button("Configure images...##Pedals")) {
		_showPedalsEditor = true;
		_backupState = _state;
	}
	ImGui::helpTooltip("Define images to use for each pedal and their layout");

}

void Viewer::showWaveOptions(){
	ImGuiPushItemWidth(25);
	ImGui::ColorEdit3("Color##Waves", &_state.waves.color[0], ImGuiColorEditFlags_NoInputs);
	ImGui::helpTooltip(s_color_wave_dsc);
	ImGui::PopItemWidth();

	ImGuiPushItemWidth(100);
	ImGuiSameLine(COLUMN_SIZE);
	ImGui::SliderFloat("Amplitude##Waves", &_state.waves.amplitude, 0.0f, 5.0f, "%.2fx");
	ImGui::helpTooltip(s_wave_amplitude_dsc);

	ImGui::SliderFloat("Spread##Waves", &_state.waves.spread, 0.0f, 5.0f, "%.2fx");
	ImGui::helpTooltip(s_wave_size_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	ImGui::SliderFloat("Frequency##Waves", &_state.waves.frequency, 0.0f, 5.0f, "%.2fx");
	ImGui::helpTooltip(s_wave_frequency_dsc);

	if(ImGui::SliderPercent("Opacity##Waves", &_state.waves.opacity, 0.0f, 1.0f)){
		_state.waves.opacity = glm::clamp(_state.waves.opacity, 0.0f, 1.0f);
	}
	ImGui::helpTooltip(s_wave_opacity_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	ImGui::SliderFloat("Speed##Waves", &_state.waves.speed, 0.0f, 5.0f, "%.2fx");
	ImGui::helpTooltip(s_wave_speed_dsc);
	
	ImGui::SliderPercent("Noise##Waves", &_state.waves.noiseIntensity, 0.0f, 2.0f);
	ImGui::helpTooltip(s_wave_noise_intensity_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	ImGui::SliderPercent("Extent##Waves", &_state.waves.noiseSize, 0.0f, 1.0f);
	ImGui::helpTooltip(s_wave_noise_extent_dsc);
	ImGui::PopItemWidth();

}

void Viewer::showBlurOptions()
{
	ImGui::Checkbox("Blur the notes", &_state.showBlurNotes);
	ImGui::helpTooltip(s_show_blur_notes_dsc);
	ImGuiSameLine(COLUMN_SIZE);

	ImGuiPushItemWidth(100);

	if (ImGui::SliderFloat(
		"Fading",
		&_state.attenuation,
		0.0f,
		1.0f))
	{
		_state.attenuation =
			glm::clamp(_state.attenuation, 0.0f, 1.0f);

		_blurringScreen.program().use(_context);
		_blurringScreen.program().uniform(
			"attenuationFactor",
			_state.attenuation
		);
	}

	ImGui::helpTooltip(s_blur_attenuation_dsc);
	ImGui::PopItemWidth();
}

void Viewer::showScoreOptions(){
	ImGuiPushItemWidth(25);
	ImGui::ColorEdit3("Vertical lines##Background", &_state.score.vLinesColor[0], ImGuiColorEditFlags_NoInputs);
	ImGui::helpTooltip(s_color_lines_vertical_dsc);
	ImGuiSameLine();

	ImGui::ColorEdit3("Horizontal lines##Background", &_state.score.hLinesColor[0], ImGuiColorEditFlags_NoInputs);
	ImGui::helpTooltip(s_color_lines_horizontal_dsc);
	ImGuiSameLine();

	ImGui::ColorEdit3("Labels##Background", &_state.score.digitsColor[0], ImGuiColorEditFlags_NoInputs);
	ImGui::helpTooltip(s_color_numbers_dsc);
	ImGui::PopItemWidth();

	ImGui::Checkbox("Horizontal lines", &_state.score.hLines);
	ImGui::helpTooltip(s_show_horiz_lines_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	ImGuiPushItemWidth(100);
	ImGui::SliderFloat("Thickness##Horizontal", &_state.score.hLinesWidth, 0.0f, 15.0f, "%.0fpx");
	ImGui::helpTooltip(s_score_lines_horizontal_width_dsc);
	ImGui::PopItemWidth();

	ImGui::Checkbox("Vertical lines", &_state.score.vLines);
	ImGui::helpTooltip(s_show_vert_lines_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	ImGuiPushItemWidth(100);
	ImGui::SliderFloat("Thickness##Vertical", &_state.score.vLinesWidth, 0.0f, 15.0f, "%.0fpx");
	ImGui::helpTooltip(s_score_lines_vertical_width_dsc);
	ImGui::PopItemWidth();

	ImGui::Checkbox("Digits", &_state.score.digits);
	ImGui::helpTooltip(s_show_numbers_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	ImGuiPushItemWidth(100);
	ImGui::SliderPercent("Scale##Digits", &_state.score.digitsScale, 0.0f, 0.5f);
	ImGui::helpTooltip(s_score_digits_size_dsc);
	ImGui::PopItemWidth();

	ImGuiPushItemWidth(100);
	ImGui::SliderPercent("Offset X##Digits", &_state.score.digitsOffset[0], -1.f, 1.0f);
	ImGui::helpTooltip(s_score_digits_offset_x_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	ImGui::SliderPercent("Offset Y##Digits", &_state.score.digitsOffset[1], -1.f, 1.0f);
	ImGui::helpTooltip(s_score_digits_offset_y_dsc);
	ImGui::PopItemWidth();

}

void Viewer::showBackgroundOptions(){
	ImGuiPushItemWidth(25);
	const glm::vec3 oldColor(_state.background.color);
	ImGui::ColorEdit3("Color##Background", &_state.background.color[0],
		ImGuiColorEditFlags_NoInputs);
	ImGui::helpTooltip(s_color_bg_dsc);
	if(oldColor != _state.background.color){
		applyBackgroundColor();
	}
	ImGui::PopItemWidth();
	ImGuiSameLine(COLUMN_SIZE);

	ImGuiPushItemWidth(100);
	if (ImGui::SliderPercent("Opacity##Background", &_state.background.imageAlpha, 0.0f, 1.0f)) {
		_state.background.imageAlpha = glm::clamp(_state.background.imageAlpha, 0.0f, 1.0f);
	}
	ImGui::helpTooltip(s_bg_img_opacity_dsc);

	ImGuiPushItemWidth(100);
	ImGui::SliderFloat("Scroll X##Background", &_state.background.scrollSpeed[0], -0.25f, 0.25f);
	ImGui::helpTooltip(s_bg_img_scroll_x_dsc);
	ImGuiSameLine(COLUMN_SIZE);
	ImGui::SliderFloat("Scroll Y##Background", &_state.background.scrollSpeed[1], -0.25f, 0.25f);
	ImGui::helpTooltip(s_bg_img_scroll_y_dsc);
	ImGui::PopItemWidth();

	ImGui::Checkbox("Image extends under keyboard", &_state.background.imageBehindKeyboard);
	ImGui::helpTooltip(s_bg_img_behind_keyboard_dsc);

}

void Viewer::showBottomButtons()
{
	const fs::path presetDirectory = Config::GetVisualizerConfigPath();

	std::error_code error;
	fs::create_directories(presetDirectory, error);

	// Build list of available presets.
	std::vector<std::string> presets;

	if (!error)
	{
		for (const auto& entry : fs::directory_iterator(presetDirectory, error))
		{
			if (error)
				break;

			if (!entry.is_regular_file())
				continue;

			if (entry.path().extension() != ".ini")
				continue;

			presets.push_back(entry.path().stem().string());
		}
	}

	std::sort(presets.begin(), presets.end());

	// Current preset.
	std::string currentPreset = _config->lastConfigPath;

	// lastConfigPath may contain the full path, so only display the preset name.
	if (!currentPreset.empty())
	{
		currentPreset = fs::path(currentPreset).stem().string();
	}

	// Persistent input buffers.
	static char presetName[256] = {};
	static char renamePresetName[256] = {};

	// Preset dropdown.
	ImGui::SetNextItemWidth(200.0f);

	const char* preview = currentPreset.empty()
		? "No preset loaded"
		: currentPreset.c_str();

	if (ImGui::BeginCombo("Preset", preview))
	{
		for (const std::string& preset : presets)
		{
			const bool selected = preset == currentPreset;

			if (ImGui::Selectable(preset.c_str(), selected))
			{
				const fs::path presetPath =
					presetDirectory / preset;

				if (_state.load(presetPath.string()))
				{
					setState(_state);

					_config->lastConfigPath = preset;
					_config->save();

					currentPreset = preset;
				}
			}

			if (selected)
				ImGui::SetItemDefaultFocus();
		}

		ImGui::EndCombo();
	}

	ImGuiSameLine();

	// Save preset.
	if (ImGui::Button("Save Preset..."))
	{
		std::strncpy(
			presetName,
			currentPreset.c_str(),
			sizeof(presetName) - 1
		);
		presetName[sizeof(presetName) - 1] = '\0';

		_inputting = true;
		ImGui::OpenPopup("Save Preset");
	}

	ImGui::helpTooltip("Save the current visualizer settings as a preset");

	if (ImGui::BeginPopupModal(
		"Save Preset",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Preset name:");
		ImGui::InputText(
			"##preset_name",
			presetName,
			sizeof(presetName)
		);

		const std::string name = presetName;
		const bool emptyName = name.empty();

		const fs::path presetPath =
			presetDirectory / name;

		const bool alreadyExists =
			!emptyName &&
			fs::exists(presetPath.string() + ".ini");

		if (alreadyExists)
		{
			ImGui::TextWrapped(
				"A preset with this name already exists. "
				"Saving will overwrite it."
			);
		}

		ImGui::Spacing();

		if (ImGui::Button("Cancel"))
		{
			presetName[0] = '\0';
			_inputting = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		ImGui::BeginDisabled(emptyName);

		if (ImGui::Button(alreadyExists ? "Overwrite" : "Save"))
		{
			Logger::Log("Saving %s\n", name.c_str());

			if (_state.save(name))
			{
				_config->lastConfigPath = name;
				_config->save();
			}

			presetName[0] = '\0';
			_inputting = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndDisabled();

		ImGui::EndPopup();
	}

	ImGuiSameLine();

	// Rename preset.
	ImGui::BeginDisabled(currentPreset.empty());

	if (ImGui::Button("Rename Preset..."))
	{
		std::strncpy(
			renamePresetName,
			currentPreset.c_str(),
			sizeof(renamePresetName) - 1
		);
		renamePresetName[sizeof(renamePresetName) - 1] = '\0';

		_inputting = true;
		ImGui::OpenPopup("Rename Preset");
	}

	ImGui::EndDisabled();

	ImGui::helpTooltip("Rename the currently loaded preset");

	if (ImGui::BeginPopupModal(
		"Rename Preset",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("New preset name:");

		ImGui::InputText(
			"##rename_preset_name",
			renamePresetName,
			sizeof(renamePresetName)
		);

		const std::string newName = renamePresetName;
		const bool emptyName = newName.empty();
		const bool sameName = newName == currentPreset;

		const fs::path oldPath =
			presetDirectory / currentPreset;

		const fs::path newPath =
			presetDirectory / newName;

		const bool alreadyExists =
			!emptyName &&
			!sameName &&
			fs::exists(newPath.string() + ".ini");

		if (alreadyExists)
		{
			ImGui::TextWrapped(
				"A preset with this name already exists."
			);
		}

		ImGui::Spacing();

		if (ImGui::Button("Cancel"))
		{
			renamePresetName[0] = '\0';
			_inputting = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		ImGui::BeginDisabled(emptyName || sameName || alreadyExists);

		if (ImGui::Button("Rename"))
		{
			const fs::path oldFile =
				oldPath.string() + ".ini";

			const fs::path newFile =
				newPath.string() + ".ini";

			std::error_code renameError;
			fs::rename(
				oldFile,
				newFile,
				renameError
			);

			if (renameError)
			{
				Logger::Log(
					"Failed to rename preset '%s' to '%s': %s\n",
					currentPreset.c_str(),
					newName.c_str(),
					renameError.message().c_str()
				);
			}
			else
			{
				Logger::Log(
					"Renamed preset '%s' to '%s'\n",
					currentPreset.c_str(),
					newName.c_str()
				);

				_config->lastConfigPath = newName;
				_config->save();
			}

			renamePresetName[0] = '\0';
			_inputting = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndDisabled();

		ImGui::EndPopup();
	}

	ImGuiSameLine();

	if (ImGui::Button("Reset##config"))
	{
		_state.reset();
		setState(_state);

		_config->lastConfigPath.clear();
		_config->save();
	}

	ImGui::helpTooltip("Restore the default effects settings");
}

void Viewer::showLayers() {
	const ImVec2 & screenSize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(screenSize.x * 0.5f, screenSize.y * 0.5f), ImGuiCond_Once, ImVec2(0.5f, 0.5f));
	
	if (ImGui::Begin("Layers", &_showLayers)) {
		ImGui::TextDisabled("You can drag and drop layers to reorder them.");
		for (int i = int(_state.layersMap.size()) - 1; i >= 0; --i) {
			const int layerId = _state.layersMap[i];
			if (layerId >= _layers.size()) {
				continue;
			}
			auto & layer = _layers[layerId];
			if (layer.type == Layer::BGCOLOR) {
				continue;
			}
			ImGui::Separator();
			ImGui::PushID(layerId);

			ImGui::Checkbox("##LayerCheckbox", layer.toggle);
			ImGuiSameLine();
			ImGui::Selectable(layer.name.c_str());

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				ImGui::Text("%s", layer.name.c_str());
				ImGui::SetDragDropPayload("REORDER_LAYER", &i, sizeof(int));
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REORDER_LAYER"))
				{
					int iPayload = *(const int*)payload->Data;
					int newId = _state.layersMap[iPayload];
					// Instead of just swapping, we shift all intermediate indices.
					const int ddlt = (iPayload <= i ? 1 : -1);
					for (int lid = iPayload; lid != i; lid += ddlt) {
						_state.layersMap[lid] = _state.layersMap[lid + ddlt];
					}
					_state.layersMap[i] = newId;
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::PopID();
		}
		ImGui::Separator();
	}
	ImGui::End();
}

void Viewer::showDevices(){
	if(ImGui::BeginPopupModal("Devices", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){

		bool starting = false;
		const ImVec2 buttonSize(_guiScale * (EXPORT_COLUMN_SIZE-20.0f), 0.0f);

		ImGui::Text("Select a device to listen to or");

		/*ImGuiSameLine();

		if(ImGui::SmallButton("start virtual device")){
			_scene = std::make_shared<MIDISceneLive>(-1, _verbose);
			starting = true;
		}
		ImGui::helpTooltip("Act as a virtual device (via JACK)\nother MIDI elements can connect to");*/
		ImGui::Separator();

		const auto & devices = MIDISceneLive::availablePorts();
		for(int i = 0; i < devices.size(); ++i){
			ImGui::RadioButton(devices[i].c_str(), &_selectedPort, i);
		}

		if(devices.empty()){
			ImGui::TextDisabled("No device found.");
		}

		ImGui::Separator();

		if(ImGui::Button("Cancel", buttonSize)){
			ImGui::CloseCurrentPopup();
		}

		if(!devices.empty()){
			ImGuiSameLine(EXPORT_COLUMN_SIZE);
			if(ImGui::Button("Start", buttonSize)){
				_scene = std::make_shared<MIDISceneLive>(_selectedPort, _verbose);
				starting = true;
			}
		}

		if(starting){
			_timer = 0.0f;
			_shouldPlay = true;
			_state.reverseScroll = true;
			_state.scrollSpeed = 1.0f;
			_liveplay = true;
			applyAllSettings();

			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void Viewer::showVisibility(){

	if(ImGui::BeginPopup("Visibility options")){
		bool shouldUpdate = false;

		ImGui::Text("Tracks");
		ImGui::Separator();
		ImGuiPushItemWidth(35);
		const size_t trackCount = _state.filter.tracks.size();
		const std::string tenPrefix = trackCount < 10 ? "" : "0";
		for(size_t cid = 0; cid < trackCount; ++cid){
			const std::string nameC = (cid < 10 ? tenPrefix : "") + std::to_string(cid);
			bool val = _state.filter.tracks[cid];
			shouldUpdate = ImGui::Checkbox(nameC.c_str(), &val) || shouldUpdate;
			if(shouldUpdate){
				_state.filter.tracks[cid] = val;
			}
			if((cid % 4 != 3) && (cid != (trackCount-1))){
				ImGuiSameLine();
			}
		}
		ImGui::PopItemWidth();

		// Do 4x4 columns of checkboxes
		ImGui::Text("Channels");
		ImGui::Separator();
		ImGuiPushItemWidth(35);
		for(size_t cid = 0; cid < _state.filter.channels.size(); ++cid){
			const std::string nameC = std::string(cid < 10 ? "0" : "") + std::to_string(cid);
			shouldUpdate = ImGui::Checkbox(nameC.c_str(), &_state.filter.channels[cid]) || shouldUpdate;
			if(cid % 4 != 3){
				ImGuiSameLine();
			}
		}
		ImGui::PopItemWidth();

		if(shouldUpdate){
			_scene->updateVisibleNotes(_state.filter);
		}
		ImGui::EndPopup();
	}

}

void Viewer::showSets(){
	if(ImGui::BeginPopup("Note sets options")){
		ImGui::Text("Decide how notes should be grouped in multiple sets");
		ImGui::Text("(to which you can assign different key/effects colors).");
		ImGui::Text("This can be based on the MIDI channel, the track, the key,");
		ImGui::Text("using the chromatic sequence, separating notes that are");
		ImGui::Text("lower or higher than a given key, or defining custom lists.");

		bool shouldUpdate = false;
		shouldUpdate = radioButtonSetMode("Channel", _state.setOptions.mode, SetMode::CHANNEL) || shouldUpdate;
		ImGui::helpTooltip("Assign each channel to a color set");
		ImGuiSameLine(90);
		shouldUpdate = radioButtonSetMode("Track", _state.setOptions.mode, SetMode::TRACK) || shouldUpdate;
		ImGui::helpTooltip("Assign each track to a color set");
		ImGuiSameLine(2*90);
		shouldUpdate = radioButtonSetMode("Key", _state.setOptions.mode, SetMode::KEY) || shouldUpdate;
		ImGui::helpTooltip("Assign each of the eight keys to a color set");
		ImGuiSameLine(3*90);
		shouldUpdate = radioButtonSetMode("Chromatic", _state.setOptions.mode, SetMode::CHROMATIC) || shouldUpdate;
		ImGui::helpTooltip("Assign each of the twelve keys to a color set");

		shouldUpdate = radioButtonSetMode("Split", _state.setOptions.mode, SetMode::SPLIT) || shouldUpdate;
		ImGui::helpTooltip("Assign keys to a color set based on a separating key");
		ImGuiSameLine();
		ImGuiPushItemWidth(100);
		shouldUpdate = ImGui::Combo("##key", &_state.setOptions.key, midiKeysStrings, 128) || shouldUpdate;
		ImGui::PopItemWidth();

		ImGuiSameLine(2*90);
		shouldUpdate = radioButtonSetMode("List", _state.setOptions.mode, SetMode::LIST) || shouldUpdate;
		ImGui::helpTooltip("Assign keys to sets based on a list of keys, sets and timings");
		ImGuiSameLine();
		if(ImGui::Button("Configure...")){
			_showSetListEditor = true;
			_backupState = _state;
			ImGui::CloseCurrentPopup();
		}

		if(shouldUpdate){
			_state.setOptions.rebuild();
			_scene->updateSetsAndVisibleNotes(_state.setOptions, _state.filter);
		}
		ImGui::EndPopup();
	}

}

static constexpr char const* kSetsComboString = " 0\0 1\0 2\0 3\0 4\0 5\0 6\0 7\0 8\0 9\0 10\0 11\0\0";

void Viewer::showSetEditor(){

	const unsigned int colWidth = 80;
	const unsigned int colButtonWidth = 50;
	const float offset = 8;

	// For editing.
	static SetOptions::KeyFrame newKey;

	// Initial window position.
	const ImVec2 & screenSize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(screenSize.x * 0.5f, screenSize.y * 0.1f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
	ImGui::SetNextWindowSize({360, 360}, ImGuiCond_FirstUseEver);

	if(ImGui::Begin("Set List Editor", &_showSetListEditor)){

		bool refreshSetOptions = false;

		// Header
		ImGui::TextWrapped("Control points will determine which range of notes belong to each set at a given time. All notes below the given key will be assigned to the specified set (except if a set with a lower number is defined). Control points will kept being applied until a new one is encountered for the set.");

		// Load/save as CSV.
		if (ImGui::Button("Save control points..."))
		{
			IFileSaveDialog* dialog = nullptr;

			HRESULT hr = CoCreateInstance(
				CLSID_FileSaveDialog,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&dialog)
			);

			if (SUCCEEDED(hr))
			{
				COMDLG_FILTERSPEC filters[] =
				{
					{ L"CSV files", L"*.csv" },
					{ L"All files", L"*.*" }
				};

				dialog->SetFileTypes(
					static_cast<UINT>(std::size(filters)),
					filters
				);

				dialog->SetTitle(L"Create CSV file");
				dialog->SetDefaultExtension(L"csv");

				hr = dialog->Show(nullptr);

				if (SUCCEEDED(hr))
				{
					IShellItem* item = nullptr;

					hr = dialog->GetResult(&item);

					if (SUCCEEDED(hr))
					{
						PWSTR path = nullptr;

						hr = item->GetDisplayName(
							SIGDN_FILESYSPATH,
							&path
						);

						if (SUCCEEDED(hr))
						{
							const std::string content =
								_state.setOptions.toKeysString("\n");

							std::ofstream file(wideToUtf8(path));

							if (file)
							{
								file << content;
							}

							CoTaskMemFree(path);
						}

						item->Release();
					}
				}

				dialog->Release();
			}
		}

		ImGuiSameLine();

		if (ImGui::Button("Load control points..."))
		{
			IFileOpenDialog* dialog = nullptr;

			HRESULT hr = CoCreateInstance(
				CLSID_FileOpenDialog,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&dialog)
			);

			if (SUCCEEDED(hr))
			{
				COMDLG_FILTERSPEC filters[] =
				{
					{ L"CSV files", L"*.csv" },
					{ L"All files", L"*.*" }
				};

				dialog->SetFileTypes(
					static_cast<UINT>(std::size(filters)),
					filters
				);

				dialog->SetTitle(L"Select CSV file");

				hr = dialog->Show(nullptr);

				if (SUCCEEDED(hr))
				{
					IShellItem* item = nullptr;

					hr = dialog->GetResult(&item);

					if (SUCCEEDED(hr))
					{
						PWSTR path = nullptr;

						hr = item->GetDisplayName(
							SIGDN_FILESYSPATH,
							&path
						);

						if (SUCCEEDED(hr))
						{
							std::ifstream file(wideToUtf8(path));

							if (file)
							{
								std::string str(
									(std::istreambuf_iterator<char>(file)),
									std::istreambuf_iterator<char>()
								);

								_state.setOptions.fromKeysString(str);
							}

							CoTaskMemFree(path);
						}

						item->Release();
					}
				}

				dialog->Release();
			}

			refreshSetOptions = true;
		}
		ImGuiSameLine();
		// Just restore the last backup.
		if(ImGui::Button("Reset")){
			_state = _backupState;
			refreshSetOptions = true;
		}
		ImGui::Separator();

		// List of existing keys.
		// Keep some room at the bottom for the "new key" section.
		ImVec2 listSize = ImGui::GetContentRegionAvail();
		listSize.y -= 1.5f * ImGui::GetTextLineHeightWithSpacing();

		if(ImGui::BeginTable("#List", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |  ImGuiTableFlags_BordersH, listSize)){
			const size_t rowCount = _state.setOptions.keys.size();

			// Header
			ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
			ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, _guiScale * colWidth);
			ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, _guiScale * colWidth);
			ImGui::TableSetupColumn("Set", ImGuiTableColumnFlags_WidthFixed, _guiScale * colWidth);
			ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_NoHeaderLabel | ImGuiTableColumnFlags_WidthFixed, colButtonWidth);
			ImGui::TableHeadersRow();

			int removeIndex = -1;

			for(size_t row = 0u; row < rowCount; ++row){

				SetOptions::KeyFrame& key = _state.setOptions.keys[row];

				ImGui::TableNextColumn();
				ImGui::PushID((unsigned int)row);

				ImGuiPushItemWidth(colWidth);
				if(ImGui::InputDouble("##Time", &key.time, 0, 0, "%.3fs")){
					key.time = (std::max)(key.time, 0.0);
				}
				// Postpone update until we are not focused anymore (else rows will jump around).
				if(ImGui::IsItemDeactivatedAfterEdit()){
					refreshSetOptions = true;
				}
				ImGui::PopItemWidth();

				ImGui::TableNextColumn();
				ImGuiPushItemWidth(colWidth);
				if(ImGui::Combo("##Key", &key.key, midiKeysStrings, 128)){
					refreshSetOptions = true;
				}
				ImGui::PopItemWidth();

				ImGui::TableNextColumn();
				ImGuiPushItemWidth(colWidth);
				// It is simpler to use a combo here (no weird focus issues when sorting rows).
				if(ImGui::Combo("##Set", &key.set, kSetsComboString)){
					refreshSetOptions = true;
				}
				ImGui::PopItemWidth();

				ImGui::TableNextColumn();
				if(ImGui::Button("x")){
					removeIndex = int(row);
				}

				ImGui::PopID();
			}

			ImGui::EndTable();

			// Remove after displaying the table.
			if(removeIndex >= 0){
				_state.setOptions.keys.erase(_state.setOptions.keys.begin() + removeIndex);
				refreshSetOptions = true;
			}
		}

		// Section to add a new key.
		// Mimic the inputs and size/alignment of the table items.
		ImGuiPushItemWidth(colWidth);
		if(ImGui::InputDouble("##Time", &newKey.time, 0, 0, "%.3fs")){
			newKey.time = (std::max)(0.0, newKey.time);
		}
		ImGui::PopItemWidth();
		ImGuiSameLine(int(colWidth + 2 * offset));
		ImGuiPushItemWidth(colWidth);
		ImGui::Combo("##Key", &newKey.key, midiKeysStrings, 128);
		ImGui::PopItemWidth();

		ImGuiSameLine(int(2 * colWidth + 3 * offset));
		ImGuiPushItemWidth(colWidth);
		ImGui::Combo("##Set", &newKey.set, kSetsComboString);
		ImGui::PopItemWidth();

		ImGuiSameLine(int(3 * colWidth + 4 * offset));
		if(ImGui::Button("Add")){
			auto insert = std::upper_bound(_state.setOptions.keys.begin(), _state.setOptions.keys.end(), newKey);
			_state.setOptions.keys.insert(insert, newKey);
			refreshSetOptions = true;
		}

		// Actions
		if(!_showSetListEditor){
			// If we are exiting, refresh the existing set.
			refreshSetOptions = true;
		}

		// If refresh is needed, ensure the set options are rebuilt and the scene udpated for live preview.
		if(refreshSetOptions){
			_state.setOptions.rebuild();
			if(_scene){
				_scene->updateSetsAndVisibleNotes(_state.setOptions, _state.filter);
			}
		}

		if(_showDebug){
			const double time = _timer * _state.scrollSpeed;

			std::array<int, SETS_COUNT> firstNoteInSet;
			std::array<int, SETS_COUNT> lastNoteInSet;
			firstNoteInSet.fill(-1);
			lastNoteInSet.fill(128);
			int lastSet = -1;
			for(int nid = 0; nid < 128; ++nid){
				int newSet = _state.setOptions.apply(nid, 0, 0, time);
				if(newSet != lastSet){
					if(lastSet >= 0){
						lastNoteInSet[lastSet] = nid - 1;
					}
					firstNoteInSet[newSet] = nid;
					lastSet = newSet;
				}
			}
			ImGui::Separator();
			ImGui::Text("Debug: ");
			ImGuiSameLine();
			ImGui::TextDisabled("(press D to hide)");
			ImGui::Text("At time %.3fs: ", time);

			for(unsigned int sid = 0; sid < SETS_COUNT; ++sid){
				int firstNote = firstNoteInSet[sid];
				int lastNote = lastNoteInSet[sid];

				if((firstNote != -1) || (lastNote != 128)){
					firstNote = glm::clamp(firstNote, 0, 127);
					lastNote  = glm::clamp(lastNote, 0, 127);

					ImGui::Text("* Set %u contains notes from %s to %s", sid, midiKeysStrings[firstNote], midiKeysStrings[lastNote]);
				}
			}

		}
	}
	ImGui::End();
}

void Viewer::showParticlesEditor(){

	const unsigned int colWidth = 270;
	const unsigned int colButtonWidth = 20;
	const float offset = 8;
	const unsigned int thumbSize = 24;
	const float thumbDisplaySize = _guiScale * thumbSize;

	// For previewing.
	static std::vector<ComPtr<ID3D11ShaderResourceView>> previewTextures;

	if (previewTextures.empty() && !_state.particles.imagePaths.empty())
	{
		previewTextures =
			generate2DViewsOfArray(_device, _context, _state.particles.tex, thumbSize);
	}

	// Initial window position.
	const ImVec2 & screenSize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(screenSize.x * 0.5f, screenSize.y * 0.1f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
	ImGui::SetNextWindowSize({360, 360}, ImGuiCond_FirstUseEver);

	if(ImGui::Begin("Particle Images Editor", &_showParticleEditor)){

		bool refreshTextures = false;

		// Header
		ImGui::TextWrapped("You can select multiple images (PNG or JPEG). They should be square and in grey levels, where black indicates transparent regions, and white regions are fully opaque.");

		if(ImGui::Button("Clear all")){
			_state.particles.imagePaths.clear();
			refreshTextures = true;
		}
		ImGui::helpTooltip("Remove all particle images");
		ImGuiSameLine();
		// Just restore the last backup.
		if(ImGui::Button("Reset")){
			_state = _backupState;
			refreshTextures = true;
		}
		ImGui::helpTooltip("Restore the previous particle images set");
		ImGui::Separator();

		// List of existing keys.
		// Keep some room at the bottom for the "new key" section.
		ImVec2 listSize = ImGui::GetContentRegionAvail();
		listSize.y -= 1.5f * ImGui::GetTextLineHeightWithSpacing();

		if(ImGui::BeginTable("#List", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |  ImGuiTableFlags_BordersH, listSize)){
			const size_t rowCount = _state.particles.imagePaths.size();

			// Header
			ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
			ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderLabel, 1.5f * thumbDisplaySize);
			ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_NoHeaderLabel | ImGuiTableColumnFlags_WidthFixed, _guiScale * colButtonWidth);
			ImGui::TableHeadersRow();

			int removeIndex = -1;
			for(size_t row = 0u; row < rowCount; ++row){

				const std::string& path = _state.particles.imagePaths[row];

				ImGui::TableNextColumn();
				ImGui::PushID((unsigned int)row);
				if (ImGui::Selectable(
					"##rowSelector",
					false,
					ImGuiSelectableFlags_SpanAllColumns,
					ImVec2(0.f, thumbDisplaySize)))
				{
					// Open directory in Windows Explorer.
					ShellExecuteA(
						nullptr,
						"open",
						path.c_str(),
						nullptr,
						nullptr,
						SW_SHOWNORMAL
					);
				}
				if(ImGui::IsItemHovered()){
					ImGui::SetTooltip("%s",path.c_str());
				}
				if(row < previewTextures.size()){
					ImGui::SameLine();
					ImGui::Image((ImTextureID)(uint64_t)previewTextures[row].Get(), ImVec2(thumbDisplaySize, thumbDisplaySize), ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));
				}
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGuiPushItemWidth(colWidth);
				// Display the filename.
				std::string::size_type pos = path.find_last_of("/\\");
				pos = (pos == std::string::npos) ? 0 : (pos + 1);
				ImGui::Text("%s", path.c_str() + pos);

				ImGui::PopItemWidth();

				ImGui::TableNextColumn();
				if(ImGui::Button("x")){
					removeIndex = int(row);
				}
				ImGui::helpTooltip("Remove from the set");
				ImGui::PopID();
			}
			ImGui::EndTable();

			// Remove after displaying the table.
			if(removeIndex >= 0){
				_state.particles.imagePaths.erase(_state.particles.imagePaths.begin() + removeIndex);
				refreshTextures = true;
			}
		}

		// Section to add a new image.
		if (ImGui::Button("Add"))
		{
			IFileOpenDialog* dialog = nullptr;

			HRESULT hr = CoCreateInstance(
				CLSID_FileOpenDialog,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&dialog)
			);

			if (SUCCEEDED(hr))
			{
				// Allow selecting multiple files.
				DWORD options = 0;
				dialog->GetOptions(&options);

				dialog->SetOptions(
					options | FOS_ALLOWMULTISELECT
				);

				COMDLG_FILTERSPEC filters[] =
				{
					{
						L"Image files",
						L"*.png;*.jpg;*.jpeg"
					},
					{
						L"PNG files",
						L"*.png"
					},
					{
						L"JPEG files",
						L"*.jpg;*.jpeg"
					},
					{
						L"All files",
						L"*.*"
					}
				};

				dialog->SetFileTypes(
					static_cast<UINT>(std::size(filters)),
					filters
				);

				dialog->SetTitle(L"Select images");

				hr = dialog->Show(nullptr);

				if (SUCCEEDED(hr))
				{
					IShellItemArray* items = nullptr;

					hr = dialog->GetResults(&items);

					if (SUCCEEDED(hr))
					{
						DWORD count = 0;
						items->GetCount(&count);

						const bool wasEmpty =
							_state.particles.imagePaths.empty();

						for (DWORD i = 0; i < count; ++i)
						{
							IShellItem* item = nullptr;

							if (FAILED(items->GetItemAt(i, &item)))
								continue;

							PWSTR widePath = nullptr;

							if (SUCCEEDED(item->GetDisplayName(
								SIGDN_FILESYSPATH,
								&widePath)))
							{
								_state.particles.imagePaths.push_back(
									wideToUtf8(widePath)
								);

								CoTaskMemFree(widePath);
							}

							item->Release();
						}

						// Ensure particles are zoomed in enough.
						if (wasEmpty &&
							_state.particles.scale <= 9.0f)
						{
							_state.particles.scale = 10.0f;
						}

						if (count > 0)
							refreshTextures = true;

						items->Release();
					}
				}

				dialog->Release();
			}
		}
		ImGui::helpTooltip("Add a new particle image to the set");

		// Actions
		if(!_showParticleEditor){
			// If we are exiting, refresh the existing set.
			refreshTextures = true;
		}

		// If refresh is needed, ensure that the texture array is up to date.
		if (refreshTextures)
		{
			// Release the previous particle texture unless it is the
			// built-in blank array texture.
			if (_state.particles.tex &&
				_state.particles.tex != ResourcesManager::getTextureFor("blankarray"))
			{
				_state.particles.tex = nullptr;
			}

			// Release preview textures.
			previewTextures.clear();

			if (_state.particles.imagePaths.empty())
			{
				// Use a white square particle appearance by default.
				_state.particles.tex =
					ResourcesManager::getTextureFor("blankarray");

				_state.particles.texCount = 1;
				_state.particles.scale = 1.0f;
			}
			else
			{
				// Load new particles.
				_state.particles.tex =
					loadTextureArray(
						_device,
						_state.particles.imagePaths,
						false,
						_state.particles.texCount
					).Get();

				previewTextures =
					generate2DViewsOfArray(
						_device,
						_context,
						_state.particles.tex,
						thumbSize
					);
			}

			refreshTextures = false;
		}
	}
	ImGui::End();
}

bool Viewer::drawPedalImageSettings(
	ID3D11ShaderResourceView* tex,
	const glm::vec2& size,
	bool labelsAfter,
	bool flipUV,
	PathCollection& path,
	unsigned int index,
	glm::vec3& color)
{
	bool refresh = false;

	const ImVec2 startUV(
		flipUV ? 1.0f : 0.0f,
		1.0f
	);

	const ImVec2 endUV(
		flipUV ? 0.0f : 1.0f,
		0.0f
	);

	const ImVec2 imageSize(
		size.x,
		size.y
	);

	const ImVec4 tint(
		color.r,
		color.g,
		color.b,
		1.0f
	);

	ImGui::BeginGroup();

	if (labelsAfter)
	{
		ImGui::ImageWithBg(
			(ImTextureID)tex,
			imageSize,
			startUV,
			endUV,
			tint
		);
	}
	else
	{
		// Pad top buttons
		ImGui::Dummy(
			ImVec2(0.3f * size.x, 5.0f)
		);

		ImGuiSameLine(0);
	}

	ImGui::PushStyleVar(
		ImGuiStyleVar_FramePadding,
		ImVec2(0.0f, 0.0f)
	);

	ImGui::ColorEdit3(
		"Picker",
		&color[0],
		ImGuiColorEditFlags_NoInputs |
		ImGuiColorEditFlags_NoLabel
	);

	ImGui::PopStyleVar();

	ImGui::helpTooltip("Define the pedal color");

	ImGui::SameLine(0);

	if (ImGui::SmallButton("Load"))
	{
		IFileOpenDialog* dialog = nullptr;

		HRESULT hr = CoCreateInstance(
			CLSID_FileOpenDialog,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&dialog)
		);

		if (SUCCEEDED(hr))
		{
			COMDLG_FILTERSPEC filters[] =
			{
				{
					L"Image files",
					L"*.png;*.jpg;*.jpeg"
				},
				{
					L"PNG files",
					L"*.png"
				},
				{
					L"JPEG files",
					L"*.jpg;*.jpeg"
				},
				{
					L"All files",
					L"*.*"
				}
			};

			dialog->SetFileTypes(
				static_cast<UINT>(std::size(filters)),
				filters
			);

			dialog->SetTitle(L"Select image");

			hr = dialog->Show(nullptr);

			if (SUCCEEDED(hr))
			{
				IShellItem* item = nullptr;

				hr = dialog->GetResult(&item);

				if (SUCCEEDED(hr))
				{
					PWSTR widePath = nullptr;

					hr = item->GetDisplayName(
						SIGDN_FILESYSPATH,
						&widePath
					);

					if (SUCCEEDED(hr))
					{
						if (index >= path.size())
						{
							path.resize(
								index + 1,
								wideToUtf8(widePath)
							);
						}
						else
						{
							path[index] =
								wideToUtf8(widePath);
						}

						refresh = true;

						CoTaskMemFree(widePath);
					}

					item->Release();
				}
			}

			dialog->Release();
		}
	}

	ImGui::helpTooltip(
		"Load an image for the pedal"
	);

	ImGui::SameLine(0);

	if (ImGui::SmallButton("x"))
	{
		path.clear();
		refresh = true;
	}

	ImGui::helpTooltip(
		"Restore the default image"
	);

	if (!labelsAfter)
	{
		ImGui::ImageWithBg(
			(ImTextureID)tex,
			imageSize,
			startUV,
			endUV,
			tint
		);
	}

	ImGui::EndGroup();

	return refresh;
}

void Viewer::refreshPedalTextures(State::PedalsState& pedals)
{
	const auto defaultCenter =
		ResourcesManager::getTextureFor("pedal_center");

	const auto defaultTop =
		ResourcesManager::getTextureFor("pedal_top");

	const auto defaultSide =
		ResourcesManager::getTextureFor("pedal_side");


	// ------------------------------------------------------------
	// Load center texture
	// ------------------------------------------------------------

	if (pedals.centerImagePath.empty())
	{
		pedals.texCenter = defaultCenter;
	}
	else
	{
		pedals.texCenter =
			loadTexture(
				_device,
				pedals.centerImagePath[0],
				1,
				false
			).Get();
	}


	// ------------------------------------------------------------
	// Load top texture
	// ------------------------------------------------------------

	if (pedals.topImagePath.empty())
	{
		pedals.texTop = defaultTop;
	}
	else
	{
		pedals.texTop =
			loadTexture(
				_device,
				pedals.topImagePath[0],
				1,
				false
			).Get();
	}


	// ------------------------------------------------------------
	// Load side textures
	// ------------------------------------------------------------

	const unsigned int newCount =
		static_cast<unsigned int>(
			pedals.sideImagePaths.size()
			);

	for (unsigned int i = 0; i < 2; ++i)
	{
		if (i < newCount)
		{
			pedals.texSides[i] =
				loadTexture(
					_device,
					pedals.sideImagePaths[i],
					1,
					false
				).Get();
		}
		else if (newCount == 0)
		{
			pedals.texSides[i] = defaultSide;
		}
		else
		{
			// Use the first side texture for the missing side.
			pedals.texSides[i] = pedals.texSides[0];
		}
	}
}

void Viewer::showPedalsEditor(){

	// Initial window position.
	const ImVec2 & screenSize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(screenSize.x * 0.5f, screenSize.y * 0.1f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
	const float fixedWidth = _guiScale * 300.0f;
	const float fixedHeight = _guiScale * 440.0f;
	ImGui::SetNextWindowSize({fixedWidth, fixedHeight}, ImGuiCond_Always);

	if(ImGui::Begin("Pedal Images Editor", &_showPedalsEditor, ImGuiWindowFlags_NoResize)){

		bool refreshTextures = false;

		// Header
		ImGui::TextWrapped("Load images to use as masks for each pedal. Overlap between pedals can be tweaked using offsets.");
		{
			if(ImGui::Button("Clear all")){
				_state.pedals.centerImagePath.clear();
				_state.pedals.topImagePath.clear();
				_state.pedals.sideImagePaths.clear();
				refreshTextures = true;
			}
			ImGui::helpTooltip("Remove all images");
			ImGuiSameLine();
			// Just restore the last backup.
			if(ImGui::Button("Reset")){
				_state = _backupState;
				refreshTextures = true;
			}
			ImGui::helpTooltip("Restore the previous images for all pedals");
		}
		ImGui::Separator();

		const float scale = 0.95f;
		const ImVec2 diagSize(scale * fixedWidth, scale * fixedWidth / 1.2f /* existing pedal ratio correction */); 
		const float topHeight = 0.20f;
		const float bottomHeight = 0.72f;
		const float sideWidth = 0.32f;
		const float centerWidth = 0.29f;
		const float topWidth = 0.99f;
		const glm::vec2 sideSize = glm::vec2(sideWidth * diagSize.x, bottomHeight * diagSize.y);
		const glm::vec2 topSize = glm::vec2(topWidth * diagSize.x, topHeight * diagSize.y);
		const glm::vec2 centerSize = glm::vec2(centerWidth * diagSize.x, bottomHeight * diagSize.y);

		// Approximately center buttons
		ImGui::PushID("Top");
		refreshTextures |= drawPedalImageSettings(_state.pedals.texTop, topSize, false, false, _state.pedals.topImagePath, 0, _state.pedals.topColor);
		ImGui::PopID();

		ImGui::PushID("Left");
		refreshTextures |= drawPedalImageSettings(_state.pedals.texSides[0], sideSize, true, false, _state.pedals.sideImagePaths, 0, _state.pedals.leftColor);
		ImGui::PopID();
		ImGuiSameLine(0);

		ImGui::PushID("Center");
		refreshTextures |= drawPedalImageSettings(_state.pedals.texCenter, centerSize, true, false, _state.pedals.centerImagePath, 0, _state.pedals.centerColor);
		ImGui::PopID();
		ImGuiSameLine(0);

		ImGui::PushID("Right");
		refreshTextures |= drawPedalImageSettings(_state.pedals.texSides[1], sideSize, true, _state.pedals.mirror, _state.pedals.sideImagePaths, 1, _state.pedals.rightColor);
		ImGui::PopID();

		ImGui::Separator();

		{
			ImGuiPushItemWidth(100);
			if(ImGui::SliderFloat("##OffsetX", &_state.pedals.margin[0], -0.5f, 0.5f, "Horiz.: %.2f")){
				_state.pedals.margin[0] = glm::clamp(_state.pedals.margin[0], -0.5f, 0.5f);
			}
			ImGui::helpTooltip(s_pedal_img_offset_x_dsc);
			ImGuiSameLine();
			if(ImGui::SliderFloat("##OffsetY", &_state.pedals.margin[1], -0.5f, 0.5f, "Vert.: %.2f")){
				_state.pedals.margin[1] = glm::clamp(_state.pedals.margin[1], -0.5f, 0.5f);
			}
			ImGui::helpTooltip(s_pedal_img_offset_y_dsc);
			ImGui::PopItemWidth();
			ImGuiSameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Offsets");

			ImGui::Checkbox("Mirror right pedal", &_state.pedals.mirror);
			ImGui::helpTooltip(s_pedal_img_mirrored_dsc);
		}
		
		// Actions
		if(!_showPedalsEditor){
			// If we are exiting, refresh the existing set.
			refreshTextures = true;
		}

		// If refresh is needed, ensure that the texture array is up to date.
		if(refreshTextures){
			refreshPedalTextures(_state.pedals);
			// Auto-mirror if only one side pedal image.
			_state.pedals.mirror = _state.pedals.sideImagePaths.size() < 2;
		}
	}
	ImGui::End();
}

void Viewer::applyBackgroundColor()
{
	//const float clearColor[4] =
	//{
	//	_state.background.color.r,
	//	_state.background.color.g,
	//	_state.background.color.b,
	//	0.0f
	//};

	const float clearColor[4] =
	{
		0.1f,
		0.1f,
		0.1f,
		0.0f
	};

	// Clear particle framebuffer.
	_particlesFramebuffer->bind(_context);
	_context->ClearRenderTargetView(
		_particlesFramebuffer->renderTarget(),
		clearColor
	);
	_particlesFramebuffer->unbind(_context);

	// Clear blur framebuffer 0.
	_blurFramebuffer0->bind(_context);
	_context->ClearRenderTargetView(
		_blurFramebuffer0->renderTarget(),
		clearColor
	);
	_blurFramebuffer0->unbind(_context);

	// Clear blur framebuffer 1.
	_blurFramebuffer1->bind(_context);
	_context->ClearRenderTargetView(
		_blurFramebuffer1->renderTarget(),
		clearColor
	);
	_blurFramebuffer1->unbind(_context);

	// Update blur shader background color.
	_blurringScreen.program().use(_context);

	_blurringScreen.program().uniform(
		"backgroundColor",
		_state.background.color
	);
}

void Viewer::applyAllSettings()
{
	// Adjust tracks count.
	int trackCount =
		(std::max)(
			_scene->tracksCount(),
			static_cast<int>(_state.filter.tracks.size())
			);

	trackCount = (std::max)(trackCount, 1);

	_state.filter.tracks.resize(
		trackCount,
		true
	);


	// ------------------------------------------------------------
	// One-shot renderer parameters.
	// ------------------------------------------------------------

	_renderer.setScaleAndMinorWidth(
		_state.scale,
		_state.background.minorsWidth
	);

	_renderer.setParticlesParameters(
		_state.particles.speed,
		_state.particles.expansion
	);

	_renderer.setKeyboardSizeAndFadeout(
		_state.keyboard.size,
		_state.notes.fadeOut
	);

	_renderer.setMinorEdgesAndHeight(
		_state.keyboard.minorEdges,
		_state.keyboard.minorHeight
	);

	_renderer.setOrientation(
		_state.horizontalScroll
	);


	updateMinMaxKeys();


	// ------------------------------------------------------------
	// Reset framebuffer contents and blur parameters.
	// ------------------------------------------------------------

	applyBackgroundColor();

	_blurringScreen.program().use(_context);

	_blurringScreen.program().uniform(
		"attenuationFactor",
		_state.attenuation
	);


	// ------------------------------------------------------------
	// Resize framebuffers.
	// ------------------------------------------------------------

	updateSizes();


	// Finally, restore the track at the beginning.
	//reset();

	// All other parameters are directly used at render time.
}

void Viewer::clean() {

	// Clean objects.
	_renderer.clean();
	_blurringScreen.clean();
	_passthrough.clean();
	_fxaa.clean();
	_particlesFramebuffer->clean();
	_blurFramebuffer0->clean();
	_blurFramebuffer1->clean();
	_finalFramebuffer->clean();
	_renderFramebuffer->clean();
}

void Viewer::rescale(float scale){
	resizeAndRescale(_camera.screenSize()[0], _camera.screenSize()[1], scale);
}

void Viewer::resize(int width, int height) {
	resizeAndRescale(width, height, _camera.scale());
}

void Viewer::resizeAndRescale(int width, int height, float scale) {
	_backbufferSize[0] = width;
	_backbufferSize[1] = height;

	if (_verbose) {
		std::cout << "[INFO]: Resizing to " << width << " x " << height << std::endl;
	}
	// Update the projection matrix.
	_camera.screen(width, height, scale);
	updateSizes();
}



void Viewer::updateSizes(){
	// Resize the framebuffers.
	const auto &currentQuality = VisualizerQuality::availables.at(_state.quality);
	const glm::vec2 baseRes(_camera.renderSize());
	_particlesFramebuffer->resize(_device, currentQuality.particlesResolution * baseRes);
	_blurFramebuffer0->resize(_device, currentQuality.blurResolution * baseRes);
	_blurFramebuffer1->resize(_device, currentQuality.blurResolution * baseRes);
	_renderFramebuffer->resize(_device, currentQuality.finalResolution * baseRes);
	_finalFramebuffer->resize(_device, currentQuality.finalResolution * baseRes);
}

void Viewer::keyPressed(int key, int action)
{
	if (action != 1) // 1 = key pressed
		return;

	if (_inputting)
		return;

	switch (key)
	{
	case 'P':
		_shouldPlay = !_shouldPlay;

		_timerStart =
			DEBUG_SPEED * static_cast<float>(
				std::chrono::duration<float>(
					std::chrono::steady_clock::now().time_since_epoch()
				).count()
				) - _timer;

		break;

	case 'R':
		reset();
		break;

	case 'I':
		_showGUI = !_showGUI;
		break;

	case 'D':
		_showDebug = !_showDebug;
		break;

	case VK_ESCAPE:
		_shouldQuit = 1;
		break;
	}
}

void Viewer::reset() {
	_timer = -_state.prerollTime;
	_timerStart = getCurrentTime() + (_shouldPlay ? _state.prerollTime : 0.0f);
	_scene->resetParticles();
}

void Viewer::setState(const State & state){
	_state = state;
	_state.setOptions.rebuild();
	_backupState = _state;
	
	// Update toggles.
	_layers[Layer::BGTEXTURE].toggle = &_state.background.image;
	_layers[Layer::BLUR].toggle = &_state.showBlur;
	_layers[Layer::ANNOTATIONS].toggle = &_state.showScore;
	_layers[Layer::KEYBOARD].toggle = &_state.showKeyboard;
	_layers[Layer::PARTICLES].toggle = &_state.showParticles;
	_layers[Layer::NOTES].toggle = &_state.showNotes;
	_layers[Layer::FLASHES].toggle = &_state.showFlashes;
	_layers[Layer::PEDAL].toggle = &_state.showPedal;
	_layers[Layer::WAVE].toggle = &_state.showWave;

	// Update split notes.
	if(_scene){
		_scene->updateSetsAndVisibleNotes(_state.setOptions, _state.filter);
	}
	applyAllSettings();

	// Textures.
	// Load notes images
	if (!_state.notes.majorImagePath.empty())
	{
		_state.notes.majorTex =
			loadTexture(
				_device,
				_state.notes.majorImagePath[0],
				4,
				false
			).Get();
	}

	if (!_state.notes.minorImagePath.empty())
	{
		_state.notes.minorTex =
			loadTexture(
				_device,
				_state.notes.minorImagePath[0],
				4,
				false
			).Get();
	}

	if (!_state.flashes.imagePath.empty())
	{
		_state.flashes.tex =
			loadTexture(
				_device,
				_state.flashes.imagePath[0],
				1,
				false
			).Get();
	}

	if (!_state.particles.imagePaths.empty())
	{
		// Load new particles.
		_state.particles.tex =
			loadTextureArray(
				_device,
				_state.particles.imagePaths,
				false,
				_state.particles.texCount
			).Get();
	}

	refreshPedalTextures(_state.pedals);
}

void Viewer::setOnStartRecording(
	RecordingCallback callback
)
{
	_onStartRecording =
		std::move(callback);
}


void Viewer::setOnStopRecording(
	RecordingCallback callback
)
{
	_onStopRecording =
		std::move(callback);
}

void Viewer::setOnStartPlayback(
	PlaybackStartCallback callback
)
{
	_onStartPlayback =
		std::move(callback);
}

void Viewer::setOnStopPlayback(
	PlaybackStopCallback callback
)
{
	_onStopPlayback =
		std::move(callback);
}

bool Viewer::startRecording()
{
	if (_recording)
		return false;

	_recordCamera = true;
	_shouldOpenRecordingPopup = true;

	return true;
}

bool Viewer::stopRecording()
{
	if (!_recording)
		return false;

	auto liveScene =
		std::dynamic_pointer_cast<MIDISceneLive>(
			_scene
		);

	if (!liveScene)
	{
		Logger::Log(
			"[Recording] Cannot save: current scene is not live.\n"
		);

		_recording = false;

		if (_onStopRecording)
			_onStopRecording();

		return false;
	}

	std::string midiFilePath = (_recordingDirectory / "midiRecording.mid").string();

	std::ofstream file(
		midiFilePath,
		std::ios::binary
	);

	if (!file.is_open())
	{
		Logger::Log(
			"[Recording] Failed to open MIDI file: %s\n",
			midiFilePath.c_str()
		);

		return false;
	}

	liveScene->save(file);

	file.close();

	if (!file)
	{
		Logger::Log(
			"[Recording] Failed while writing MIDI file: %s\n",
			midiFilePath.c_str()
		);

		return false;
	}

	_recording = false;

	if (_onStopRecording)
		_onStopRecording();

	Logger::Log(
		"[Recording] Saved MIDI recording: %s\n",
		midiFilePath.c_str()
	);

	refreshRecordings();

	return true;
}

void  Viewer::setGUIScale(float scale){
	_guiScale = (std::max)(0.25f, scale);
	ImGui::GetStyle() = ImGuiStyle();
	ImGui::configureStyle();
	ImGui::GetIO().FontGlobalScale = _guiScale;
	ImGui::GetStyle().ScaleAllSizes(_guiScale);
	ImGui::GetStyle().FrameRounding = 3 * _guiScale;
}


void Viewer::updateConfiguration(Configuration& config){
	// Reset
	config.lastMidiPath = "";
	config.lastMidiDevice = "";
	config.guiScale = _guiScale;
	// Settings file.
	config.lastConfigPath = _state.filePath();
	// MIDI File.
	std::shared_ptr<MIDISceneFile> fileScene = std::dynamic_pointer_cast<MIDISceneFile>(_scene);
	if(fileScene){
		config.lastMidiPath = fileScene->filePath();
	}
	// MIDI device.
	std::shared_ptr<MIDISceneLive> liveScene = std::dynamic_pointer_cast<MIDISceneLive>(_scene);
	if(liveScene){
		config.lastMidiDevice = liveScene->deviceName();
	}
}

void Viewer::setAlphaBlending(bool enabled)
{
	if (enabled)
	{
		const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		_context->OMSetBlendState(
			_alphaBlendState.Get(),
			blendFactor,
			0xFFFFFFFF
		);
	}
	else
	{
		_context->OMSetBlendState(
			nullptr,
			nullptr,
			0xFFFFFFFF
		);
	}
}

void Viewer::setAdditiveBlending(bool enabled)
{
	if (enabled)
	{
		const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		_context->OMSetBlendState(
			_additiveBlendState.Get(),
			blendFactor,
			0xFFFFFFFF
		);
	}
	else
	{
		_context->OMSetBlendState(
			nullptr,
			nullptr,
			0xFFFFFFFF
		);
	}
}

bool Viewer::radioButtonSetMode(const char* name, SetMode& mode, SetMode value){
	int rawMode = int(mode);
	int rawValue = int(value);
	bool active = ImGui::RadioButton(name, &rawMode, rawValue);
	if(active){
		mode = SetMode(rawMode);
	}
	return active;
}

bool Viewer::channelColorEdit(const char * name, const char * displayName, ColorArray & colors){
	if(!_state.perSetColors){
		// If locked, display color sink.
		ImGuiPushItemWidth(25);
		const bool inter = ImGui::ColorEdit3(name, &colors[0][0], ImGuiColorEditFlags_NoInputs);
		ImGui::PopItemWidth();

		if(_state.lockParticleColor && ImGui::IsItemHovered()){
			ImGui::SetTooltip("(!) Effect colors\n    are synced");
		}

		if(inter){
			// Ensure synchronization.
			for(size_t cid = 1; cid < colors.size(); ++cid){
				colors[cid] = colors[0];
			}
		}
		return inter;
	}

	// Else, we display a drop down and a popup.
	if(ImGui::ArrowButton(name, ImGuiDir_Down)){
		ImGui::OpenPopup(name);
	}
	ImGuiSameLine(); ImGui::Text("%s", displayName);

	if(ImGui::BeginPopup(name)){
		// Do 3 columns of color sinks.
		bool edit = false;
		ImGuiPushItemWidth(35);
		for(size_t cid = 0; cid < colors.size(); ++cid){
			const std::string nameC = "Set " + std::to_string(cid);
			edit = ImGui::ColorEdit3(nameC.c_str(), &colors[cid][0], ImGuiColorEditFlags_NoInputs) || edit;
			if(cid % 3 != 2 && cid != colors.size()-1){
				ImGuiSameLine(75 * (cid%3+1));
			}
		}
		ImGui::PopItemWidth();
		ImGui::EndPopup();
		return edit;
	}
	return false;
}


void Viewer::updateMinMaxKeys(){
	// Make sure keys are properly ordered.
	if(_state.minKey > _state.maxKey){
		std::swap(_state.minKey, _state.maxKey);
	}

	// Force edges to align with a major key.
	int realMinKey = _state.minKey;
	if(noteIsMinor[_state.minKey % 12]){
		realMinKey -= 1;
	}

	int realMaxKey = _state.maxKey;
	if(noteIsMinor[_state.maxKey % 12]){
		realMaxKey += 1;
	}

	// Convert to "major" only indices.
	const int minKeyMaj = (_state.minKey/12) * 7 + noteShift[realMinKey % 12];
	const int maxKeyMaj = (_state.maxKey/12) * 7 + noteShift[realMaxKey % 12];
	const int noteCount = (maxKeyMaj - minKeyMaj + 1);

	_renderer.setMinMaxKeys(realMinKey, minKeyMaj, noteCount);
}


void Viewer::ImGuiPushItemWidth(int w){
	ImGui::PushItemWidth(_guiScale * w);
}

void Viewer::ImGuiSameLine(int w){
	ImGui::SameLine(_guiScale * w);
}

bool Viewer::createRecordingDirectory()
{
	try
	{
		const fs::path baseDirectory =
			fs::temp_directory_path() /
			"PianoVisualizer";

		fs::create_directories(
			baseDirectory
		);

		const auto now =
			std::chrono::system_clock::now();

		const auto time =
			std::chrono::system_clock::to_time_t(now);

		std::tm localTime{};

		localtime_s(
			&localTime,
			&time
		);

		char timestamp[64];

		std::strftime(
			timestamp,
			sizeof(timestamp),
			"%Y%m%d_%H%M%S",
			&localTime
		);

		_recordingDirectory =
			baseDirectory /
			("Recording_" + std::string(timestamp));

		int suffix = 1;

		while (fs::exists(_recordingDirectory))
		{
			_recordingDirectory =
				baseDirectory /
				(
					"Recording_" +
					std::string(timestamp) +
					"_" +
					std::to_string(suffix++)
					);
		}

		fs::create_directories(
			_recordingDirectory
		);

		Logger::Log(
			"[Recording] Created temporary directory: %s\n",
			_recordingDirectory.string().c_str()
		);

		return true;
	}
	catch (const std::exception& e)
	{
		Logger::Log(
			"[Recording] Failed to create temporary directory: %s\n",
			e.what()
		);

		_recordingDirectory.clear();

		return false;
	}
}

void Viewer::refreshRecordings()
{
	_availableRecordings.clear();

	if (
		_tempDirectory.empty() ||
		!std::filesystem::exists(_tempDirectory)
		)
	{
		return;
	}


	for (const auto& entry :
		std::filesystem::directory_iterator(_tempDirectory))
	{
		if (!entry.is_directory())
		{
			continue;
		}

		_availableRecordings.push_back(
			entry.path()
		);
	}

	/*
	 * Newest recording first.
	 */
	std::sort(
		_availableRecordings.begin(),
		_availableRecordings.end(),
		[](const auto& a, const auto& b)
		{
			return std::filesystem::last_write_time(a) >
				std::filesystem::last_write_time(b);
		}
	);
}

bool Viewer::hasRecordings() const
{
	return !_availableRecordings.empty();
}

void Viewer::startPlayback()
{
	if (!_playbackLoaded)
	{
		return;
	}

	
	std::shared_ptr<MIDIScene> scene(nullptr);

	try {
		scene = std::make_shared<MIDISceneFile>(_playbackMidiPath, _state.setOptions, _state.filter);
	}
	catch (...) {
		Logger::Log("[Error] Failed to create recording scene!\n");
		return;
	}

	// Player.


	_timerStart = getCurrentTime();

	if (!_state.reverseScroll)
		_timerStart += _state.prerollTime;

	_shouldPlay = true;
	_liveplay = false;


	// Init objects.
	_scene = scene;
	applyAllSettings();

	Logger::Log(
		"[Playback] Started.\n"
	);

	_playbackPlaying = true;
	_playbackPaused = false;

	return;
}

void Viewer::pausePlayback()
{
	if (!_playbackLoaded)
	{
		return;
	}

	_playbackPaused = !_playbackPaused;
	_shouldPlay = !_playbackPaused;

	double currentTime = getCurrentTime();


	if (_shouldPlay)
	{
		_timerStart += (
			currentTime -
			_playbackPauseStart
			);
	}
	else
	{
		_playbackPauseStart = currentTime;
	}
	
	Logger::Log(
		"[Playback] Paused.\n"
	);
}

void Viewer::stopPlayback()
{
	_playbackLoaded = false;
	_playbackPlaying = false;
	_playbackPaused = false;
	_playbackWindowOpen = false;

	_playbackRecordingDirectory.clear();
	_playbackVideoPath.clear();
	_playbackMidiPath.clear();
	_playbackPauseStart = 0;

	_shouldPlay = true;

	_scene.reset(new MIDIScene());

	if (MIDISceneLive::availablePortsCount()-1 >= _selectedPort)
	{
		connectDevice(_selectedPort);
	}
	else if (MIDISceneLive::availablePortsCount() > 0)
	{
		_selectedPort = 0;
		connectDevice(_selectedPort);
	}

	if (_onStopPlayback)
		_onStopPlayback();

	Logger::Log(
		"[Playback] Stopped.\n"
	);
}

void Viewer::drawPlaybackSettings()
{
	if (!ImGui::Begin(
		"Playback Settings",
		&_playbackWindowOpen,
		ImGuiWindowFlags_AlwaysAutoResize
	))
	{
		ImGui::End();
		return;
	}

	// ---------------------------------------------------------
	// Recording selection
	// ---------------------------------------------------------

	bool openDeletePopup = false;

	ImGui::Text("Recording");

	if (_availableRecordings.empty())
	{
		ImGui::Text("No recordings available.");
	}
	else
	{
		for (const auto& recording : _availableRecordings)
		{
			const std::string name =
				recording.filename().string();

			const bool selected =
				recording == _playbackRecordingDirectory;

			ImGui::PushID(
				recording.string().c_str()
			);

			if (ImGui::RadioButton(
				name.c_str(),
				selected
			))
			{
				const std::string videoPath =
					(
						recording /
						"cameraRecording.mp4"
						).string();

				if (
					_onStartPlayback &&
					_onStartPlayback(videoPath)
					)
				{
					_playbackRecordingDirectory =
						recording;

					_playbackVideoPath =
						videoPath;

					_playbackMidiPath =
						(
							recording /
							"midiRecording.mid"
							).string();

					_playbackLoaded = true;
					_playbackPlaying = false;
					_playbackPaused = false;

					_shouldPlay = false;

					Logger::Log(
						"[Playback] Selected recording: %s\n",
						name.c_str()
					);
				}
				else
				{
					Logger::Log(
						"[Playback] Failed to open video: %s\n",
						videoPath.c_str()
					);
				}
			}

			ImGui::SameLine();

			if (ImGui::SmallButton("X"))
			{
				_recordingToDelete =
					recording;

				_deleteRecordingPopupOpen =
					true;

				openDeletePopup =
					true;
			}

			ImGui::PopID();
		}
	}

	// ---------------------------------------------------------
	// Delete confirmation
	// ---------------------------------------------------------

	if (openDeletePopup)
	{
		ImGui::OpenPopup(
			"Delete Recording"
		);
	}

	if (ImGui::BeginPopupModal(
		"Delete Recording",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	))
	{
		const std::string name =
			_recordingToDelete.filename().string();

		ImGui::Text(
			"Are you sure you want to delete:"
		);

		ImGui::Spacing();

		ImGui::TextWrapped(
			"\"%s\""
			,
			name.c_str()
		);

		ImGui::Spacing();

		ImGui::Text(
			"This will permanently delete the recording"
		);

		ImGui::Text(
			"folder and everything inside it."
		);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button(
			"Delete",
			ImVec2(120.0f, 0.0f)
		))
		{
			const std::filesystem::path recording =
				_recordingToDelete;

			std::error_code error;

			const uintmax_t removed =
				std::filesystem::remove_all(
					recording,
					error
				);

			if (error)
			{
				Logger::Log(
					"[Playback] Failed to delete recording: %s (%s)\n",
					recording.string().c_str(),
					error.message().c_str()
				);
			}
			else
			{
				Logger::Log(
					"[Playback] Deleted recording: %s (%llu entries)\n",
					recording.string().c_str(),
					static_cast<unsigned long long>(
						removed
						)
				);

				/*
				 * The currently selected recording was deleted.
				 */
				if (
					recording ==
					_playbackRecordingDirectory
					)
				{
					_playbackLoaded = false;
					_playbackPlaying = false;
					_playbackPaused = false;

					_playbackRecordingDirectory.clear();
					_playbackVideoPath.clear();
					_playbackMidiPath.clear();

					_shouldPlay = true;
				}

				/*
				 * Remove it from the available recording list.
				 */
				_availableRecordings.erase(
					std::remove(
						_availableRecordings.begin(),
						_availableRecordings.end(),
						recording
					),
					_availableRecordings.end()
				);
			}

			_recordingToDelete.clear();
			_deleteRecordingPopupOpen = false;

			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button(
			"Cancel",
			ImVec2(120.0f, 0.0f)
		))
		{
			_recordingToDelete.clear();
			_deleteRecordingPopupOpen = false;

			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}


	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();


	// ---------------------------------------------------------
	// Reverse scroll
	// ---------------------------------------------------------

	bool reverseScroll =
		!_state.reverseScroll;

	if (ImGui::Checkbox(
		"Reverse Scroll",
		&reverseScroll
	))
	{
		_state.reverseScroll =
			!reverseScroll;
	}

	if (!_state.reverseScroll)
	{
		ImGui::SameLine();

		ImGui::SliderFloat(
			"Preroll Time",
			&_state.prerollTime,
			0.0f,
			10.0f,
			"%.1f s"
		);
	}


	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();


	// ---------------------------------------------------------
	// Playback controls
	// ---------------------------------------------------------

	if (ImGui::Button("Play"))
	{
		startPlayback();
	}

	ImGui::SameLine();

	if (ImGui::Button("Pause"))
	{
		pausePlayback();
	}

	ImGui::SameLine();

	if (ImGui::Button("Stop"))
	{
		stopPlayback();
	}


	ImGui::Separator();


	if (_playbackLoaded)
	{
		ImGui::Text(
			"Selected: %s",
			_playbackRecordingDirectory
			.filename()
			.string()
			.c_str()
		);

		const char* label =
			_playbackPlaying
			? _playbackPaused
			? "Paused"
			: "Playing"
			: "Stopped";

		ImGui::Text(
			"Status: %s",
			label
		);
	}
	else
	{
		ImGui::Text(
			"No recording selected."
		);
	}


	ImGui::End();
}

SystemAction::SystemAction(SystemAction::Type act) {
	type = act;
	data = glm::ivec4(0);
}

void Viewer::handleMIDIDeviceEvent()
{
	std::optional<MIDIDeviceEvent> event;

	{
		std::lock_guard<std::mutex> lock(
			_midiDeviceEventMutex
		);

		if (!_pendingMIDIDeviceEvent)
			return;

		event =
			std::move(_pendingMIDIDeviceEvent);

		_pendingMIDIDeviceEvent.reset();
	}

	if (event->connected)
	{
		const auto& devices = MIDISceneLive::availablePorts();

		if (devices.size() == 1)
		{
			connectDevice(1);
		}
	}
	else
	{
		_liveplay = false;
		_timer = 0.0f;
	}
}