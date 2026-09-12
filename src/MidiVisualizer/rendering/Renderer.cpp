#include <stdio.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

#include "../helpers/ResourcesManager.h"

#include "Renderer.h"
#include "scene/MIDIScene.h"

#include <imgui/imgui.h>

#include <stdexcept>

#include "../../../util/Logger.h"

#include <wrl/client.h>

#ifdef _WIN32
#undef MIN
#undef MAX
#endif

#ifndef MAX_NOTES_IN_FLIGHT
#define MAX_NOTES_IN_FLIGHT 8192
#endif

void Renderer::SetAlphaBlending()
{
	const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	_context->OMSetBlendState(
		_alphaBlendState.Get(),
		blendFactor,
		0xFFFFFFFF
	);
}

void Renderer::SetAdditiveBlending()
{
	const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	_context->OMSetBlendState(
		_additiveBlendState.Get(),
		blendFactor,
		0xFFFFFFFF
	);
}

void Renderer::SetDefaultBlending()
{
	_context->OMSetBlendState(
		nullptr,
		nullptr,
		0xFFFFFFFF
	);
}

void Renderer::renderSetup(
	ID3D11Device* device,
	ID3D11DeviceContext* context
)
{
	_device = device;
	_context = context;

	// --------------------------------------------------------
	// Basic quad
	// --------------------------------------------------------

	const std::vector<float> vertices =
	{
		-0.5f, -0.5f,
		 0.5f, -0.5f,
		 0.5f,  0.5f,
		-0.5f,  0.5f
	};

	const std::vector<unsigned int> indices =
	{
		1, 2, 0,
		2, 3, 0
	};

	_quadIndexCount = static_cast<UINT>(indices.size());

	{
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = static_cast<UINT>(
			vertices.size() * sizeof(float)
			);
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		D3D11_SUBRESOURCE_DATA data{};
		data.pSysMem = vertices.data();

		HRESULT hr = _device->CreateBuffer(
			&desc,
			&data,
			&_quadVertices
		);

		if (FAILED(hr))
			throw std::runtime_error("Failed to create quad vertex buffer.");

		//D3D11_BUFFER_DESC debugDesc{};
		//_quadVertices->GetDesc(&debugDesc);

		//Logger::Log(
		//	"[D3D11] quadVertices: ByteWidth=%u Usage=%u BindFlags=0x%X CPUAccessFlags=0x%X\n",
		//	debugDesc.ByteWidth,
		//	debugDesc.Usage,
		//	debugDesc.BindFlags,
		//	debugDesc.CPUAccessFlags
		//);
	}

	{
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = static_cast<UINT>(
			indices.size() * sizeof(unsigned int)
			);
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		D3D11_SUBRESOURCE_DATA data{};
		data.pSysMem = indices.data();

		HRESULT hr = _device->CreateBuffer(
			&desc,
			&data,
			&_quadIndices
		);

		if (FAILED(hr))
			throw std::runtime_error("Failed to create quad index buffer.");
	}


	// --------------------------------------------------------
	// Dynamic note data
	// --------------------------------------------------------

	{
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(MIDIScene::GPUNote) * MAX_NOTES_IN_FLIGHT;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = _device->CreateBuffer(
			&desc,
			nullptr,
			&_notesDataBuffer
		);

		if (FAILED(hr))
			throw std::runtime_error("Failed to create notes data buffer.");
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

	// --------------------------------------------------------
	// Active keys
	// --------------------------------------------------------

	/*{
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(int) * 128;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = _device->CreateBuffer(
			&desc,
			nullptr,
			&_keysDataBuffer
		);

		if (FAILED(hr))
			throw std::runtime_error("Failed to create keys data buffer.");
	}*/

	// --------------------------------------------------------
	// Wave geometry
	// --------------------------------------------------------

	const int numSegments = 512;

	std::vector<glm::vec2> waveVerts((numSegments + 1) * 2);

	for (int vid = 0; vid < numSegments + 1; ++vid)
	{
		const float x =
			2.0f * static_cast<float>(vid) /
			static_cast<float>(numSegments) - 1.0f;

		waveVerts[2 * vid] =
			glm::vec2(x, 1.0f);

		waveVerts[2 * vid + 1] =
			glm::vec2(x, -1.0f);
	}

	std::vector<unsigned int> waveInds(6 * numSegments);

	for (int sid = 0; sid < numSegments; ++sid)
	{
		const int bid = 6 * sid;
		const int vid = 2 * sid;

		waveInds[bid] = vid;
		waveInds[bid + 1] = vid + 1;
		waveInds[bid + 2] = vid + 2;

		waveInds[bid + 3] = vid + 2;
		waveInds[bid + 4] = vid + 1;
		waveInds[bid + 5] = vid + 3;
	}

	_waveIndexCount = static_cast<UINT>(waveInds.size());

	{
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = static_cast<UINT>(
			waveVerts.size() * sizeof(glm::vec2)
			);
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		D3D11_SUBRESOURCE_DATA data{};
		data.pSysMem = waveVerts.data();

		HRESULT hr = _device->CreateBuffer(
			&desc,
			&data,
			&_waveVertices
		);

		if (FAILED(hr))
			throw std::runtime_error("Failed to create wave vertex buffer.");
	}

	{
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = static_cast<UINT>(
			waveInds.size() * sizeof(unsigned int)
			);
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		D3D11_SUBRESOURCE_DATA data{};
		data.pSysMem = waveInds.data();

		HRESULT hr = _device->CreateBuffer(
			&desc,
			&data,
			&_waveIndices
		);

		if (FAILED(hr))
			throw std::runtime_error("Failed to create wave index buffer.");
	}


	// --------------------------------------------------------
	// Shader programs
	// --------------------------------------------------------

	_programNotes.init(
		_device,
		"notes_vert",
		"notes_frag",
		ShaderProgram::InputLayoutType::QuadWithNoteData
	);

	_programFlashes.init(
		_device,
		"flashes_vert",
		"flashes_frag",
		ShaderProgram::InputLayoutType::Position2D
	);

	_programParticules.init(
		_device,
		"particles_vert",
		"particles_frag",
		ShaderProgram::InputLayoutType::Position2D
	);

	_programKeyMajors.init(
		_device,
		"majorKeys_vert",
		"majorKeys_frag",
		ShaderProgram::InputLayoutType::Position2D
	);

	_programKeyMinors.init(
		_device,
		"minorKeys_vert",
		"minorKeys_frag",
		ShaderProgram::InputLayoutType::Position2D
	);

	//_programPedals.init(
	//	_device,
	//	"pedal_vert",
	//	"pedal_frag",
	//	ShaderProgram::InputLayoutType::QuadWithNoteData
	//);

	//_programScoreBars.init(
	//	_device,
	//	"score_bars_vert",
	//	"score_bars_frag",
	//	ShaderProgram::InputLayoutType::Position2D
	//);

	//_programScoreLabels.init(
	//	_device,
	//	"score_labels_vert",
	//	"score_labels_frag",
	//	ShaderProgram::InputLayoutType::Position2D
	//);

	_programWave.init(
		_device,
		"wave_vert",
		"wave_frag",
		ShaderProgram::InputLayoutType::Position2D
	);

	_programWaveNoise.init(
		_device,
		"wave_noise_vert",
		"wave_noise_frag",
		ShaderProgram::InputLayoutType::Position2D
	);


	// --------------------------------------------------------
	// Textures
	// --------------------------------------------------------

	_texFont =
		ResourcesManager::getTextureFor("font");

	_texNoise =
		ResourcesManager::getTextureFor("noise");

	_texParticles =
		ResourcesManager::getTextureFor("particles");


	// --------------------------------------------------------
	// Particle texture size
	// --------------------------------------------------------

	const glm::vec2 tsize =
		ResourcesManager::getTextureSizeFor("particles");

	_programParticules.use(_context);

	_programParticules.uniform(
		"inverseTextureSize",
		1.0f / tsize
	);
}

void Renderer::upload(const std::shared_ptr<MIDIScene>& scene)
{
	glm::ivec2 uploadRange;

	// --------------------------------------------------------
	// Notes
	// --------------------------------------------------------

	if (scene->dirtyNotes(uploadRange))
	{
		constexpr size_t noteSize = sizeof(MIDIScene::GPUNote);
		const auto& notes = scene->getNotes();

		if (!notes.empty())
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};

			const bool fullUpload =
				uploadRange.y == 0;

			const D3D11_MAP mapType =
				fullUpload
				? D3D11_MAP_WRITE_DISCARD
				: D3D11_MAP_WRITE_NO_OVERWRITE;

			const HRESULT hr =
				_context->Map(
					_notesDataBuffer.Get(),
					0,
					mapType,
					0,
					&mapped
				);

			if (SUCCEEDED(hr))
			{
				if (fullUpload)
				{
					memcpy(
						mapped.pData,
						notes.data(),
						noteSize * notes.size()
					);
				}
				else
				{
					const int first = uploadRange.x;
					const int size =
						uploadRange.y - uploadRange.x + 1;

					memcpy(
						static_cast<char*>(mapped.pData)
						+ first * noteSize,
						notes.data() + first,
						size * noteSize
					);
				}

				_context->Unmap(
					_notesDataBuffer.Get(),
					0
				);

				scene->setUpToDate();
			}
			else
			{
				Logger::Log(
					"[D3D11] upload: Failed to map notes buffer: 0x%08X\n",
					static_cast<unsigned int>(hr)
				);
			}
		}
	}

	// --------------------------------------------------------
	// Active keys
	// --------------------------------------------------------

	const auto& actives = scene->getActiveKeys();

	//if (!actives.empty())
	//{
	//	D3D11_MAPPED_SUBRESOURCE mapped{};

	//	if (SUCCEEDED(_context->Map(
	//		_keysDataBuffer.Get(),
	//		0,
	//		D3D11_MAP_WRITE_NO_OVERWRITE,
	//		0,
	//		&mapped)))
	//	{
	//		memcpy(
	//			mapped.pData,
	//			actives.data(),
	//			actives.size() * sizeof(int)
	//		);

	//		_context->Unmap(_keysDataBuffer.Get(), 0);
	//	}
	//}

	// --------------------------------------------------------
	// Active-key shader data
	// --------------------------------------------------------

	_programKeyMajors.uniforms(_context,
		"actives",
		actives.size(),
		actives.data()
	);

	_programKeyMinors.uniforms(_context,
		"actives",
		actives.size(),
		actives.data()
	);

	//_programFlashes.uniforms(_context,
	//	"actives",
	//	actives.size(),
	//	actives.data()
	//);
}

void Renderer::setScaleAndMinorWidth(const float scale, const float minorWidth)
{
	_programNotes.uniform(_context, "mainSpeed", scale);
	_programNotes.uniform("minorsWidth", minorWidth);

	_programKeyMinors.uniform(_context, "minorsWidth", minorWidth);
}

void Renderer::setParticlesParameters(const float speed, const float expansion)
{
	_programParticules.uniform(_context, "speedScaling", speed);
	_programParticules.uniform("expansionFactor", expansion);
}

void Renderer::setKeyboardSizeAndFadeout(float keyboardHeight, float fadeOut)
{
	const float fadeOutFinal =
		keyboardHeight + (1.0f - keyboardHeight) * (1.0f - fadeOut);

	_programNotes.uniform(_context, "keyboardHeight", keyboardHeight);
	_programNotes.uniform("fadeOut", fadeOutFinal);

	_programParticules.uniform(_context, "keyboardHeight", keyboardHeight);

	_programKeyMinors.uniform(_context, "keyboardHeight", keyboardHeight);

	_programKeyMajors.uniform(_context, "keyboardHeight", keyboardHeight);

	_programFlashes.uniform(_context, "keyboardHeight", keyboardHeight);
}

void Renderer::setMinorEdgesAndHeight(bool minorEdges, float minorHeight)
{
	_programKeyMinors.uniform(_context, "edgeOnMinors", minorEdges);
	_programKeyMinors.uniform("minorsHeight", minorHeight);
}

void Renderer::setMinMaxKeys(int minKey, int minKeyMajor, int notesCount)
{
	_programNotes.uniform(_context, "minNoteMajor", minKeyMajor);
	_programNotes.uniform("notesCount", static_cast<float>(notesCount));

	_programFlashes.uniform(_context, "minNote", minKey);
	_programFlashes.uniform("notesCount", static_cast<float>(notesCount));

	_programKeyMajors.uniform(_context, "minNoteMajor", minKeyMajor);
	_programKeyMajors.uniform("notesCount", static_cast<float>(notesCount));

	_programKeyMinors.uniform(_context, "minNoteMajor", minKeyMajor);
	_programKeyMinors.uniform("notesCount", static_cast<float>(notesCount));

	_programParticules.uniform(_context, "minNote", minKey);
	_programParticules.uniform("notesCount", static_cast<float>(notesCount));

	// Refresh score settings (used on CPU)
	_minKeyMajor = minKeyMajor;
	_keyCount = notesCount;
}

void Renderer::setOrientation(bool horizontal)
{
	_programNotes.uniform(_context, "horizontalMode", horizontal);
	_programFlashes.uniform(_context, "horizontalMode", horizontal);
	_programKeyMajors.uniform(_context, "horizontalMode", horizontal);
	_programKeyMinors.uniform(_context, "horizontalMode", horizontal);
	_programParticules.uniform(_context, "horizontalMode", horizontal);
	_programWave.uniform(_context, "horizontalMode", horizontal);
	_programWaveNoise.uniform(_context, "horizontalMode", horizontal);
	_programScoreBars.uniform(_context, "horizontalMode", horizontal);
	_programScoreLabels.uniform(_context, "horizontalMode", horizontal);
}

void Renderer::drawParticles(
	const std::shared_ptr<MIDIScene>& scene,
	float time,
	const glm::vec2& invScreenSize,
	const State::ParticlesState& state,
	bool prepass
) {
	// --------------------------------------------------------
	// Shader
	// --------------------------------------------------------

	_programParticules.use(_context);

	// --------------------------------------------------------
	// Common uniforms
	// --------------------------------------------------------

	_programParticules.uniform("inverseScreenSize", invScreenSize);
	_programParticules.uniform("time", 0.0f);

	// Prepass: bigger, darker particles.
	_programParticules.uniform(
		"colorScale",
		prepass ? 0.6f : 1.6f
	);

	_programParticules.uniform(
		"scale",
		state.scale * (prepass ? 2.0f : 1.0f)
	);

	std::array<glm::vec4, SETS_COUNT> particleColors;

	for (size_t i = 0; i < SETS_COUNT; ++i)
	{
		particleColors[i] = glm::vec4(
			state.colors[i],
			1.0f
		);
	}

	_programParticules.uniforms(
		_context,
		"baseColor",
		particleColors.size(),
		particleColors.data()
	);

	// --------------------------------------------------------
	// Textures
	// --------------------------------------------------------

	_programParticules.texture(_context,
		"textureParticles",
		_texParticles.Get()
	);

	_programParticules.texture(_context,
		"textureNoise",
		_texNoise.Get()
	);

	_programParticules.texture(_context, 
		"lookParticles",
		state.tex
	);

	_programParticules.uniform(
		"turbulenceStrength",
		state.turbulenceStrength
	);

	_programParticules.uniform(
		"turbulenceScale",
		state.turbulenceScale
	);

	_programParticules.uniform(
		"texCount",
		state.texCount
	);

	// --------------------------------------------------------
	// Geometry
	// --------------------------------------------------------

	const UINT stride = sizeof(float) * 2;
	const UINT offset = 0;

	_context->IASetVertexBuffers(
		0,
		1,
		_quadVertices.GetAddressOf(),
		&stride,
		&offset
	);

	_context->IASetIndexBuffer(
		_quadIndices.Get(),
		DXGI_FORMAT_R32_UINT,
		0
	);

	_context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	// --------------------------------------------------------
	// Rasterizer state
	// --------------------------------------------------------

	D3D11_RASTERIZER_DESC rasterDesc{};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.FrontCounterClockwise = FALSE;
	rasterDesc.DepthClipEnable = TRUE;

	ComPtr<ID3D11RasterizerState> rasterState;

	HRESULT rasterHr =
		_device->CreateRasterizerState(
			&rasterDesc,
			&rasterState
		);

	if (FAILED(rasterHr))
	{
		Logger::Log(
			"[D3D11] drawNotes: Failed to create rasterizer state\n"
		);

		checkD3DError(rasterHr);
		return;
	}

	_context->RSSetState(rasterState.Get());

	SetDefaultBlending();

	// --------------------------------------------------------
	// Draw each active particle system
	// --------------------------------------------------------

	for (const auto& particle : scene->getParticles()) {
		if (particle.note >= 0) {
			_programParticules.uniform(
				"globalId",
				particle.note
			);

			_programParticules.uniform(
				"time",
				particle.elapsed
			);

			_programParticules.uniform(
				"duration",
				particle.duration
			);

			_programParticules.uniform(
				"channel",
				particle.set
			);

			_context->DrawIndexedInstanced(
				_quadIndexCount,
				state.count,
				0,
				0,
				0
			);
		}
	}

	// --------------------------------------------------------
	// Cleanup
	// --------------------------------------------------------

	ID3D11ShaderResourceView* nullSRV[3] = {
		nullptr,
		nullptr,
		nullptr
	};

	_context->RSSetState(nullptr);

	// If the particle shader uses three SRVs consecutively,
	// unbind them so they don't remain bound for later passes.
	_context->PSSetShaderResources(0, 3, nullSRV);
}

void Renderer::drawNotes(
	const std::shared_ptr<MIDIScene>& scene,
	float time,
	const glm::vec2& invScreenSize,
	const State::NotesState& state,
	bool reverseScroll,
	bool prepass)
{
	_programNotes.use(_context);

	// --------------------------------------------------------
	// Uniforms that are specific to drawing notes
	// --------------------------------------------------------

	const float maxCornerRadius = 0.12f;

	_programNotes.uniform(_context, "inverseScreenSize", invScreenSize);
	_programNotes.uniform(_context, "time", time);
	_programNotes.uniform(_context, "colorScale", prepass ? 0.6f : 1.0f);
	_programNotes.uniform(_context, "reverseMode", reverseScroll);

	_programNotes.uniform(_context, "edgeWidth", state.edgeWidth);
	_programNotes.uniform(_context, "edgeBrightness", state.edgeBrightness);

	_programNotes.uniform(
		_context,
		"texturesScale",
		glm::vec2{
			state.majorTexScale,
			state.minorTexScale
		}
	);

	_programNotes.uniform(
		_context,
		"texturesStrength",
		glm::vec2{
			state.majorTexAlpha,
			state.minorTexAlpha
		}
	);

	_programNotes.uniform(
		_context,
		"scrollMajorTexture",
		reverseScroll ? state.majorTexScroll : -state.majorTexScroll
	);

	_programNotes.uniform(
		_context,
		"scrollMinorTexture",
		reverseScroll ? state.minorTexScroll : -state.minorTexScroll
	);

	_programNotes.uniform(
		_context,
		"cornerRadius",
		state.cornerRadius * maxCornerRadius
	);

	_programNotes.uniform(
		_context,
		"fadeOut",
		state.fadeOut
	);

	// --------------------------------------------------------
	// Note colors
	// --------------------------------------------------------

	std::array<glm::vec4, SETS_COUNT> baseColors;
	std::array<glm::vec4, SETS_COUNT> minorColors;

	for (size_t i = 0; i < SETS_COUNT; ++i)
	{
		baseColors[i] = glm::vec4(
			state.majorColors[i],
			0.0f
		);

		minorColors[i] = glm::vec4(
			state.minorColors[i],
			0.0f
		);
	}

	_programNotes.uniforms(
		_context,
		"baseColor",
		baseColors.size(),
		baseColors.data()
	);

	_programNotes.uniforms(
		_context,
		"minorColor",
		minorColors.size(),
		minorColors.data()
	);

	// --------------------------------------------------------
	// Note textures
	// --------------------------------------------------------

	const bool useMajorTexture =
		state.majorTex != nullptr;

	const bool useMinorTexture =
		state.minorTex != nullptr;

	_programNotes.uniform(
		_context,
		"useMajorTexture",
		useMajorTexture
	);

	_programNotes.uniform(
		_context,
		"useMinorTexture",
		useMinorTexture
	);

	if (useMajorTexture)
	{
		_programNotes.texture(
			_context,
			"majorTexture",
			state.majorTex.Get()
		);
	}

	if (useMinorTexture)
	{
		_programNotes.texture(
			_context,
			"minorTexture",
			state.minorTex.Get()
		);
	}

	// --------------------------------------------------------
	// Input layout
	// --------------------------------------------------------

	ID3D11InputLayout* inputLayout =
		_programNotes.getInputLayout();

	if (!inputLayout)
	{
		Logger::Log(
			"[D3D11] drawNotes: NO INPUT LAYOUT\n"
		);

		return;
	}

	_context->IASetInputLayout(inputLayout);

	// --------------------------------------------------------
	// Vertex buffers
	//
	// Slot 0:
	//   float2 position
	//
	// Slot 1:
	//   GPUNote
	//
	// GPUNote:
	//   note     -> NOTE_DATA.x
	//   start    -> NOTE_DATA.y
	//   duration -> NOTE_DATA.z
	//   isMinor  -> NOTE_DATA.w
	//   set      -> NOTE_DATA1.x
	// --------------------------------------------------------

	ID3D11Buffer* vertexBuffers[] =
	{
		_quadVertices.Get(),
		_notesDataBuffer.Get()
	};

	UINT strides[] =
	{
		sizeof(float) * 2,
		sizeof(MIDIScene::GPUNote)
	};

	UINT offsets[] =
	{
		0,
		0
	};

	_context->IASetVertexBuffers(
		0,
		2,
		vertexBuffers,
		strides,
		offsets
	);

	// --------------------------------------------------------
	// Index buffer
	// --------------------------------------------------------

	_context->IASetIndexBuffer(
		_quadIndices.Get(),
		DXGI_FORMAT_R32_UINT,
		0
	);

	// --------------------------------------------------------
	// Primitive topology
	// --------------------------------------------------------

	_context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	// --------------------------------------------------------
	// Rasterizer state
	// --------------------------------------------------------

	D3D11_RASTERIZER_DESC rasterDesc{};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.FrontCounterClockwise = FALSE;
	rasterDesc.DepthClipEnable = TRUE;

	ComPtr<ID3D11RasterizerState> rasterState;

	HRESULT rasterHr =
		_device->CreateRasterizerState(
			&rasterDesc,
			&rasterState
		);

	if (FAILED(rasterHr))
	{
		Logger::Log(
			"[D3D11] drawNotes: Failed to create rasterizer state\n"
		);

		checkD3DError(rasterHr);
		return;
	}

	_context->RSSetState(rasterState.Get());

	// --------------------------------------------------------
	// Draw all notes
	// --------------------------------------------------------

	const UINT noteCount =
		static_cast<UINT>(
			scene->getEffectiveNotesCount()
			);

	_context->DrawIndexedInstanced(
		_quadIndexCount,
		noteCount,
		0,
		0,
		0
	);

	// --------------------------------------------------------
	// Restore rasterizer state
	// --------------------------------------------------------

	_context->RSSetState(nullptr);
}

void Renderer::drawFlashes(
	const std::shared_ptr<MIDIScene>& scene,
	float time,
	const glm::vec2& invScreenSize,
	const State::FlashesState& state)
{
	// Handle flash alpha blending
	const auto& actives = scene->getActiveKeys();

	for (int i = 0; i < 128; ++i)
	{
		FlashState& flash = _flashes[i];

		const bool isActive = actives[i] >= 0;

		// Key was just pressed.
		if (isActive && !flash.active)
		{
			flash.active = true;
			flash.fadeStart = time;
			flash.fadeFrom = 0.0f;
			flash.colorId = actives[i];
		}

		// Key was just released.
		else if (!isActive && flash.active)
		{
			// Calculate the alpha at the exact moment of release.
			const float elapsed = time - flash.fadeStart;

			const float currentAlpha =
				min(elapsed / state.fadeTime, 1.0f);

			flash.active = false;
			flash.fadeStart = time;
			flash.fadeFrom = currentAlpha;
		}
	}

	SetAdditiveBlending();

	_programFlashes.use(_context);

	// Uniforms
	_programFlashes.uniform(
		"inverseScreenSize",
		invScreenSize
	);

	_programFlashes.uniform(
		"time",
		time
	);

	std::array<glm::vec4, SETS_COUNT> flashColors;

	for (size_t i = 0; i < SETS_COUNT; ++i)
	{
		flashColors[i] = glm::vec4(
			state.colors[i],
			1.0f
		);
	}

	_programFlashes.uniforms(
		_context,
		"baseColor",
		flashColors.size(),
		flashColors.data()
	);

	_programFlashes.uniform(
		"userScale",
		state.size
	);

	_programFlashes.uniform(
		"haloInnerRadius",
		state.haloInnerRadius
	);

	_programFlashes.uniform(
		"haloOuterRadius",
		state.haloOuterRadius
	);

	_programFlashes.uniform(
		"haloIntensity",
		state.haloIntensity
	);

	_programFlashes.uniform(
		"texRowCount",
		state.texRowCount
	);

	_programFlashes.uniform(
		"texColCount",
		state.texColCount
	);

	// Flash texture
	_programFlashes.texture(
		_context,
		"textureFlash",
		state.tex
	);


	ID3D11Buffer* vertexBuffers[] = { _quadVertices.Get() };
	UINT strides[] = { sizeof(float) * 2 };
	UINT offsets[] = { 0 };

	_context->IASetVertexBuffers(
		0,
		1,
		vertexBuffers,
		strides,
		offsets
	);

	_context->IASetIndexBuffer(
		_quadIndices.Get(),
		DXGI_FORMAT_R32_UINT,
		0
	);

	_context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	// --------------------------------------------------------
	// Rasterizer state
	// --------------------------------------------------------

	D3D11_RASTERIZER_DESC rasterDesc{};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.FrontCounterClockwise = FALSE;
	rasterDesc.DepthClipEnable = TRUE;

	ComPtr<ID3D11RasterizerState> rasterState;

	HRESULT rasterHr =
		_device->CreateRasterizerState(
			&rasterDesc,
			&rasterState
		);

	if (FAILED(rasterHr))
	{
		Logger::Log(
			"[D3D11] drawNotes: Failed to create rasterizer state\n"
		);

		checkD3DError(rasterHr);
		return;
	}

	_context->RSSetState(rasterState.Get());

	for (int i = 0; i < 128; ++i)
	{
		FlashState& flash = _flashes[i];

		float flashAlpha = 0.0f;

		if (flash.active)
		{
			// Fade in: 0 -> 1.
			flashAlpha =
				min(
					(time - flash.fadeStart) / state.fadeTime,
					1.0f
				);
		}
		else if (flash.fadeFrom > 0.0f)
		{
			// Fade out: fadeFrom -> 0.
			flashAlpha =
				max(
					flash.fadeFrom -
					(time - flash.fadeStart) / state.fadeTime,
					0.0f
				);
		}

		// Nothing to render.
		if (flashAlpha <= 0.0f)
			continue;

		_programFlashes.uniform(
			"globalId",
			i
		);

		_programFlashes.uniform(
			"colorId",
			flash.colorId
		);

		_programFlashes.uniform(
			"flashAlpha",
			flashAlpha
		);

		_context->DrawIndexed(
			_quadIndexCount,
			0,
			0
		);
	}

	SetDefaultBlending();

	// Prevent the render state from leaking into subsequent draws.
	ID3D11ShaderResourceView* nullSRV = nullptr;

	_context->PSSetShaderResources(
		0,
		1,
		&nullSRV
	);

	// --------------------------------------------------------
	// Restore rasterizer state
	// --------------------------------------------------------

	_context->RSSetState(nullptr);
}

void Renderer::drawKeyboard(
	const std::shared_ptr<MIDIScene>& scene,
	float,
	const glm::vec2& invScreenSize,
	const glm::vec3& edgeColor,
	const glm::vec3& keyColor,
	const ColorArray& majorColors,
	const ColorArray& minorColors,
	bool highlightKeys)
{
	// --------------------------------------------------------
	// Major keys
	// --------------------------------------------------------

	_programKeyMajors.use(_context);

	_programKeyMajors.uniform("inverseScreenSize", invScreenSize);
	_programKeyMajors.uniform("edgeColor", edgeColor);
	_programKeyMajors.uniform("keyColor", keyColor);

	std::array<glm::vec4, SETS_COUNT> majorKeyColors;

	for (size_t i = 0; i < SETS_COUNT; ++i)
	{
		majorKeyColors[i] = glm::vec4(
			majorColors[i],
			1.0f
		);
	}

	_programKeyMajors.uniforms(
		"majorColor",
		majorKeyColors.size(),
		majorKeyColors.data()
	);

	_programKeyMajors.uniform("highlightKeys", highlightKeys);

	// --------------------------------------------------------
	// Rasterizer state
	// --------------------------------------------------------

	D3D11_RASTERIZER_DESC rasterDesc{};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.FrontCounterClockwise = FALSE;
	rasterDesc.DepthClipEnable = TRUE;

	ComPtr<ID3D11RasterizerState> rasterState;

	HRESULT rasterHr =
		_device->CreateRasterizerState(
			&rasterDesc,
			&rasterState
		);

	if (FAILED(rasterHr))
	{
		Logger::Log(
			"[D3D11] drawKeyboard: Failed to create rasterizer state\n"
		);

		checkD3DError(rasterHr);
		return;
	}

	_context->RSSetState(rasterState.Get());

	{
		ID3D11Buffer* vertexBuffers[] = { _quadVertices.Get() };
		UINT strides[] = { sizeof(float) * 2 };
		UINT offsets[] = { 0 };

		_context->IASetVertexBuffers(
			0,
			1,
			vertexBuffers,
			strides,
			offsets
		);

		_context->IASetIndexBuffer(
			_quadIndices.Get(),
			DXGI_FORMAT_R32_UINT,
			0
		);

		_context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
		);

		_context->DrawIndexed(
			_quadIndexCount,
			0,
			0
		);
	}


	// --------------------------------------------------------
	// Minor keys
	// --------------------------------------------------------

	_programKeyMinors.use(_context);

	_programKeyMinors.uniform("inverseScreenSize", invScreenSize);
	_programKeyMinors.uniform("edgeColor", edgeColor);

	std::array<glm::vec4, SETS_COUNT> minorKeyColors;

	for (size_t i = 0; i < SETS_COUNT; ++i)
	{
		minorKeyColors[i] = glm::vec4(
			minorColors[i],
			1.0f
		);
	}

	_programKeyMinors.uniforms("minorColor",
		minorKeyColors.size(),
		minorKeyColors.data());

	_programKeyMinors.uniform("highlightKeys", highlightKeys);

	_programKeyMinors.use(_context);

	ID3D11Buffer* vertexBuffer = _quadVertices.Get();
	UINT stride = sizeof(float) * 2;
	UINT offset = 0;

	_context->IASetVertexBuffers(
		0, 1,
		&vertexBuffer,
		&stride,
		&offset
	);

	_context->IASetIndexBuffer(
		_quadIndices.Get(),
		DXGI_FORMAT_R32_UINT,
		0
	);

	_context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	_context->DrawIndexedInstanced(
		_quadIndexCount,
		53,
		0, 0, 0
	);


	// --------------------------------------------------------
	// Cleanup IA state
	// --------------------------------------------------------

	ID3D11Buffer* nullBuffers[] = { nullptr, nullptr };
	UINT nullStrides[] = { 0, 0 };
	UINT nullOffsets[] = { 0, 0 };

	_context->IASetVertexBuffers(
		0,
		2,
		nullBuffers,
		nullStrides,
		nullOffsets
	);

	_context->IASetIndexBuffer(
		nullptr,
		DXGI_FORMAT_UNKNOWN,
		0
	);

	_context->IASetInputLayout(nullptr);

	_programKeyMinors.unuse();

	_context->RSSetState(nullptr);
}

void Renderer::drawPedals(
	const std::shared_ptr<MIDIScene>& scene,
	float time,
	const glm::vec2& invScreenSize,
	const State::PedalsState& state,
	float keyboardHeight,
	bool horizontalMode)
{
	// --------------------------------------------------------
	// Blending
	// --------------------------------------------------------

	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	// --------------------------------------------------------
	// Geometry calculations
	// --------------------------------------------------------

	_programPedals.use(_context);

	// Adjust for aspect ratio.
	const float rat = invScreenSize.y / invScreenSize.x;

	const glm::vec2 ratioScale =
		(rat < 1.0f)
		? glm::vec2(1.0f, rat)
		: glm::vec2(1.0f / rat, 1.0f);

	const glm::vec2 globalScale =
		2.0f *
		state.size *
		ratioScale *
		glm::vec2(1.2f, 1.0f);

	const State::PedalsState::Location mode = state.location;

	const float vertSign =
		(mode == State::PedalsState::TOPLEFT ||
			mode == State::PedalsState::TOPRIGHT)
		? 1.0f
		: -1.0f;

	const float horizSign =
		(mode == State::PedalsState::TOPLEFT ||
			mode == State::PedalsState::BOTTOMLEFT)
		? -1.0f
		: 1.0f;

	const float safetyMargin = 0.02f;

	glm::vec2 globalShift =
		glm::vec2(horizSign, vertSign) *
		(1.0f - 0.5f * globalScale - safetyMargin * ratioScale);

	// If at the bottom, shift above the keyboard.
	if (horizontalMode)
	{
		if (mode == State::PedalsState::TOPLEFT ||
			mode == State::PedalsState::BOTTOMLEFT)
		{
			globalShift[0] += 2.0f * keyboardHeight;
		}
	}
	else
	{
		if (mode == State::PedalsState::BOTTOMLEFT ||
			mode == State::PedalsState::BOTTOMRIGHT)
		{
			globalShift[1] += 2.0f * keyboardHeight;
		}
	}

	const float tightenShiftX = state.margin.x;
	const float tightenShiftY = state.margin.y;

	// --------------------------------------------------------
	// Pedal layout
	// --------------------------------------------------------

	const float expressionHeight = 0.20f;
	const float expressionWidth = 1.0f;

	const float sidesWidth = 0.35f;
	const float sidesHeight = 1.0f - expressionHeight;

	const float centralWidth = 1.0f - 2.0f * sidesWidth;
	const float centralHeight = sidesHeight;

	const float expressionShiftX = 0.0f;
	const float expressionShiftY =
		0.5f * (1.0f - expressionHeight) - tightenShiftY;

	const float sidesShiftX =
		0.5f * (sidesWidth + centralWidth) - tightenShiftX;

	const float sidesShiftY =
		0.5f * (sidesHeight - 1.0f);

	const float centralShiftX = 0.0f;
	const float centralShiftY =
		0.5f * (sidesHeight - 1.0f);

	const glm::vec2 localScales[4] =
	{
		glm::vec2(sidesWidth, sidesHeight),
		glm::vec2(centralWidth, centralHeight),
		glm::vec2(sidesWidth, sidesHeight),
		glm::vec2(expressionWidth, expressionHeight)
	};

	const glm::vec2 localShifts[4] =
	{
		glm::vec2(-sidesShiftX, sidesShiftY),
		glm::vec2(centralShiftX, centralShiftY),
		glm::vec2(sidesShiftX, sidesShiftY),
		glm::vec2(expressionShiftX, expressionShiftY)
	};

	// --------------------------------------------------------
	// Pedal state
	// --------------------------------------------------------

	const MIDIScene::Pedals& pedals = scene->getPedals();

	const float actives[4] =
	{
		pedals.soft,
		pedals.sostenuto,
		pedals.damper,
		pedals.expression
	};

	const glm::vec3* colors[4] =
	{
		&state.leftColor,
		&state.centerColor,
		&state.rightColor,
		&state.topColor
	};

	ID3D11ShaderResourceView* textures[4] =
	{
		state.texSides[0],
		state.texCenter,
		state.texSides[1],
		state.texTop
	};

	_programPedals.uniform("pedalOpacity", state.opacity);

	// --------------------------------------------------------
	// Input assembler setup
	// --------------------------------------------------------

	_programPedals.use(_context);

	ID3D11Buffer* vertexBuffer = _quadVertices.Get();

	const UINT stride = sizeof(float) * 2;
	const UINT offset = 0;

	_context->IASetVertexBuffers(
		0,
		1,
		&vertexBuffer,
		&stride,
		&offset
	);

	_context->IASetIndexBuffer(
		_quadIndices.Get(),
		DXGI_FORMAT_R32_UINT,
		0
	);

	_context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	// --------------------------------------------------------
	// Draw
	// --------------------------------------------------------

	if (state.merge)
	{
		const float active =
			(glm::max)(
				(glm::max)(actives[0], actives[1]),
				(glm::max)(actives[2], actives[3])
				);

		// We want to move the central pedal to the side.
		const glm::vec2 localShift =
			glm::vec2(horizSign, vertSign) *
			glm::vec2(sidesShiftX, 0.5f * expressionHeight);

		_programPedals.uniform(
			"scale",
			globalScale * localScales[1]
		);

		_programPedals.uniform(
			"shift",
			globalShift + globalScale * localShift
		);

		_programPedals.uniform(
			"pedalFlag",
			active
		);

		_programPedals.uniform(
			"mirror",
			false
		);

		_programPedals.texture(
			_context,
			"pedalTexture",
			textures[1]
		);

		_programPedals.uniform(
			"pedalColor",
			*colors[1]
		);

		_context->DrawIndexed(
			_quadIndexCount,
			0,
			0
		);
	}
	else
	{
		for (unsigned int i = 0; i < 4; ++i)
		{
			_programPedals.uniform(
				"scale",
				globalScale * localScales[i]
			);

			_programPedals.uniform(
				"shift",
				globalShift + globalScale * localShifts[i]
			);

			_programPedals.uniform(
				"pedalFlag",
				actives[i]
			);

			_programPedals.texture(
				_context,
				"pedalTexture",
				textures[i]
			);

			_programPedals.uniform(
				"mirror",
				state.mirror && (i == 2)
			);

			_programPedals.uniform(
				"pedalColor",
				*colors[i]
			);

			_context->DrawIndexed(
				_quadIndexCount,
				0,
				0
			);
		}
	}

	// --------------------------------------------------------
	// Cleanup
	// --------------------------------------------------------

	ID3D11ShaderResourceView* nullSRV = nullptr;

	_context->PSSetShaderResources(
		0,
		1,
		&nullSRV
	);

	ID3D11Buffer* nullBuffer = nullptr;

	_context->IASetVertexBuffers(
		0,
		1,
		&nullBuffer,
		&stride,
		&offset
	);

	_context->IASetIndexBuffer(
		nullptr,
		DXGI_FORMAT_UNKNOWN,
		0
	);

	_context->IASetInputLayout(nullptr);

	_programPedals.unuse();
}

void Renderer::drawWaves(
	const std::shared_ptr<MIDIScene>& scene,
	float time,
	const glm::vec2& invScreenSize,
	const State::WaveState& state,
	float keyboardHeight)
{
	// --------------------------------------------------------
	// Rasterizer state
	// --------------------------------------------------------

	D3D11_RASTERIZER_DESC rasterDesc{};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_BACK;
	rasterDesc.FrontCounterClockwise = TRUE;
	rasterDesc.DepthClipEnable = TRUE;

	ComPtr<ID3D11RasterizerState> rasterState;

	HRESULT rasterHr =
		_device->CreateRasterizerState(
			&rasterDesc,
			&rasterState
		);

	if (FAILED(rasterHr))
	{
		Logger::Log(
			"[D3D11] drawNotes: Failed to create rasterizer state\n"
		);

		checkD3DError(rasterHr);
		return;
	}

	_context->RSSetState(rasterState.Get());

	// --------------------------------------------------------
	// Noise layer
	// --------------------------------------------------------

	_programWaveNoise.use(_context);

	_programWaveNoise.uniform(
		"keyboardSize",
		keyboardHeight
	);

	_programWaveNoise.uniform(
		"scale",
		state.noiseSize * 0.5f
	);

	_programWaveNoise.texture(
		_context,
		"textureNoise",
		_texNoise.Get()
	);

	_programWaveNoise.uniform(
		"offset",
		time * state.speed * 0.05f
	);

	_programWaveNoise.uniform(
		"waveColor",
		state.color
	);

	_programWaveNoise.uniform(
		"noiseScale",
		state.frequency
	);

	_programWaveNoise.uniform(
		"waveOpacity",
		state.noiseIntensity
	);

	_programWaveNoise.uniform(
		"inverseScreenSize",
		invScreenSize
	);

	ID3D11Buffer* quadVertexBuffer = _quadVertices.Get();

	const UINT quadStride = sizeof(float) * 2;
	const UINT quadOffset = 0;

	_context->IASetVertexBuffers(
		0,
		1,
		&quadVertexBuffer,
		&quadStride,
		&quadOffset
	);

	_context->IASetIndexBuffer(
		_quadIndices.Get(),
		DXGI_FORMAT_R32_UINT,
		0
	);

	_context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	SetAlphaBlending();

	_context->DrawIndexed(
		_quadIndexCount,
		0,
		0
	);

	_programWaveNoise.unuse();

	// --------------------------------------------------------
	// Wave geometry
	// --------------------------------------------------------

	_programWave.use(_context);

	_programWave.uniform("waveColor", state.color);
	_programWave.uniform("keyboardSize", keyboardHeight);
	_programWave.uniform("waveOpacity", state.opacity);
	_programWave.uniform("spread", state.spread);

	ID3D11Buffer* waveVertexBuffer = _waveVertices.Get();

	const UINT waveStride = sizeof(float) * 2;
	const UINT waveOffset = 0;

	_context->IASetVertexBuffers(
		0,
		1,
		&waveVertexBuffer,
		&waveStride,
		&waveOffset
	);

	_context->IASetIndexBuffer(
		_waveIndices.Get(),
		DXGI_FORMAT_R32_UINT,
		0
	);

	_context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	// Fixed initial parameters.
	const float ampls[4] =
	{
		-0.023f,
		-0.011f,
		 0.017f,
		 0.009f
	};

	const float freqs[4] =
	{
		 10.3f,
		 -8.27f,
		 -4.4f,
		 13.7f
	};

	const float phases[4] =
	{
		 5.2f,
		 4.7f,
		 9.3f,
		-7.1f
	};

	SetAdditiveBlending();

	// Render multiple waves with additive blending.
	for (int i = 0; i < 4; ++i)
	{
		const float ampl =
			state.amplitude * ampls[i];

		const float freq =
			state.frequency * freqs[i];

		const float phase =
			phases[i] * time * state.speed
			+ float(i + 1) * 7.39f;

		_programWave.uniform("amplitude", ampl);
		_programWave.uniform("freq", freq);
		_programWave.uniform("phase", phase);

		_context->DrawIndexed(
			_waveIndexCount,
			0,
			0
		);
	}

	_programWave.unuse();

	// --------------------------------------------------------
	// Cleanup
	// --------------------------------------------------------

	_context->IASetInputLayout(nullptr);

	ID3D11Buffer* nullBuffer = nullptr;

	_context->IASetVertexBuffers(
		0,
		1,
		&nullBuffer,
		&waveStride,
		&waveOffset
	);

	_context->IASetIndexBuffer(
		nullptr,
		DXGI_FORMAT_UNKNOWN,
		0
	);

	ID3D11ShaderResourceView* nullSRV = nullptr;

	_context->PSSetShaderResources(
		0,
		1,
		&nullSRV
	);

	SetDefaultBlending();

	_context->RSSetState(nullptr);
}

void Renderer::drawScore(
	const std::shared_ptr<MIDIScene>& scene,
	float time,
	const glm::vec2& invScreenSize,
	const State::ScoreState& state,
	float measureScale,
	float qualityScale,
	float keyboardHeight,
	bool horizontalMode,
	bool reverseScroll)
{
	const glm::vec2 pixelSize =
		qualityScale *
		(horizontalMode
			? glm::vec2(invScreenSize.y, invScreenSize.x)
			: invScreenSize);

	// --------------------------------------------------------
	// Common quad input-assembler state
	// --------------------------------------------------------


	ID3D11Buffer* vertexBuffer = _quadVertices.Get();

	const UINT stride = sizeof(float) * 2;
	const UINT offset = 0;

	_context->IASetVertexBuffers(
		0,
		1,
		&vertexBuffer,
		&stride,
		&offset
	);

	_context->IASetIndexBuffer(
		_quadIndices.Get(),
		DXGI_FORMAT_R32_UINT,
		0
	);

	_context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	// --------------------------------------------------------
	// Draw vertical lines
	// --------------------------------------------------------

	if (state.vLines)
	{
		// 7 major keys.
		const int firstBar =
			(_minKeyMajor + 6) / 7;

		const int barCount =
			_keyCount / 7 + 1;

		// minKeyMajor maps to -1
		// minKeyMajor + keyCount maps to 1
		float firstBarCoord =
			(firstBar * 7 - _minKeyMajor) /
			static_cast<float>(_keyCount);

		firstBarCoord =
			2.0f * firstBarCoord - 1.0f;

		float nextBarDeltaCoord =
			7.0f / static_cast<float>(_keyCount);

		nextBarDeltaCoord *= 2.0f;

		_programScoreBars.use(_context);

		_programScoreBars.uniform(
			"baseOffset",
			glm::vec2(firstBarCoord, 0.0f)
		);

		_programScoreBars.uniform(
			"nextOffset",
			glm::vec2(nextBarDeltaCoord, 0.0f)
		);

		_programScoreBars.uniform(
			"scale",
			glm::vec2(
				state.vLinesWidth * pixelSize.x,
				1.0f
			)
		);

		_programScoreBars.uniform(
			"color",
			state.vLinesColor
		);

		_context->DrawIndexedInstanced(
			_quadIndexCount,
			barCount,
			0,
			0,
			0
		);

		_programScoreBars.unuse();
	}

	// --------------------------------------------------------
	// Horizontal lines / measure numbers
	// --------------------------------------------------------

	if (state.hLines || state.digits)
	{
		const float currentAbscisse =
			time / float(scene->secondsPerMeasure());

		// Find the measure at the bottom of the screen if forward,
		// top of the screen if reverse.
		const float magicValue0 = 2.0f;
		const float magicValue1 =
			measureScale * float(scene->secondsPerMeasure());

		const float distanceToScreenEdge =
			magicValue0 *
			(reverseScroll
				? (1.0f - keyboardHeight)
				: keyboardHeight);

		float firstMesureAbscisse =
			currentAbscisse -
			distanceToScreenEdge / measureScale;

		int firstMeasure =
			static_cast<int>(
				std::floor(firstMesureAbscisse)
				);

		const float firstMeasureTime =
			float(scene->secondsPerMeasure()) *
			float(firstMeasure);

		const float direction =
			reverseScroll ? -1.0f : 1.0f;

		const float keyboardPos =
			2.0f * keyboardHeight - 1.0f;

		const float nextBarDeltaCoord =
			direction * magicValue1;

		float firstBarCoord =
			direction *
			(firstMeasureTime - time) *
			measureScale +
			keyboardPos;

		// Corrective step.
		// While the next bar is not on screen,
		// keep stepping forwards.
		while (
			direction *
			(firstBarCoord + nextBarDeltaCoord) < -1.0f
			)
		{
			firstBarCoord += nextBarDeltaCoord;
			++firstMeasure;
		}

		// While the first bar is on screen,
		// keep stepping backwards.
		while (
			direction * firstBarCoord > -1.0f
			)
		{
			firstBarCoord -= nextBarDeltaCoord;
			--firstMeasure;
		}

		const int barCount =
			static_cast<int>(
				std::ceil(
					std::abs(2.0f / nextBarDeltaCoord)
				)
				) + 2;

		// ----------------------------------------------------
		// Horizontal lines
		// ----------------------------------------------------

		if (state.hLines)
		{
			_programScoreBars.use(_context);

			_programScoreBars.uniform(
				"baseOffset",
				glm::vec2(0.0f, firstBarCoord)
			);

			_programScoreBars.uniform(
				"nextOffset",
				glm::vec2(0.0f, nextBarDeltaCoord)
			);

			_programScoreBars.uniform(
				"scale",
				glm::vec2(
					1.0f,
					state.hLinesWidth * pixelSize.y
				)
			);

			_programScoreBars.uniform(
				"color",
				state.hLinesColor
			);

			_context->DrawIndexedInstanced(
				_quadIndexCount,
				barCount,
				0,
				0,
				0
			);

			_programScoreBars.unuse();
		}

		// ----------------------------------------------------
		// Measure digits
		// ----------------------------------------------------

		if (state.digits)
		{
			const float textScale =
				state.digitsScale;

			// Based on texture size.
			const glm::vec2 digitResolution =
				glm::vec2(200.0f, 256.0f);

			const glm::vec2 digitSize =
				textScale *
				qualityScale *
				invScreenSize *
				digitResolution;

			// Ensure at least one measure.
			const float maxMeasureCount =
				float(
					scene->duration() /
					scene->secondsPerMeasure()
					) + 1.0f;

			// Add one extra digit so that postroll measures
			// are displayed entirely in all cases.
			const float digitCount =
				(
					std::floor(
						std::log10(
							(std::max)(
								1.0f,
								maxMeasureCount
								)
						)
					) + 1.0f
					) + 1.0f;

			const glm::vec2 offset =
				2.0f *
				digitSize *
				state.digitsOffset;

			const glm::vec2 margin =
				horizontalMode
				? glm::vec2(offset.y, offset.x)
				: offset;

			_programScoreLabels.use(_context);

			_programScoreLabels.uniform(
				"baseOffset",
				glm::vec2(-1.0f, firstBarCoord) +
				margin
			);

			_programScoreLabels.uniform(
				"nextOffset",
				glm::vec2(0.0f, nextBarDeltaCoord)
			);

			_programScoreLabels.uniform(
				"scale",
				digitSize
			);

			_programScoreLabels.uniform(
				"color",
				state.digitsColor
			);

			_programScoreLabels.uniform(
				"maxDigitCount",
				static_cast<int>(digitCount)
			);

			_programScoreLabels.uniform(
				"firstMeasure",
				firstMeasure
			);

			_programScoreLabels.texture(
				_context,
				"font",
				_texFont.Get()
			);

			_context->DrawIndexedInstanced(
				_quadIndexCount,
				barCount,
				0,
				0,
				0
			);

			_programScoreLabels.unuse();
		}
	}

	// --------------------------------------------------------
	// Cleanup
	// --------------------------------------------------------

	_context->IASetInputLayout(nullptr);

	ID3D11Buffer* nullBuffer = nullptr;

	_context->IASetVertexBuffers(
		0,
		1,
		&nullBuffer,
		&stride,
		&offset
	);

	_context->IASetIndexBuffer(
		nullptr,
		DXGI_FORMAT_UNKNOWN,
		0
	);

	ID3D11ShaderResourceView* nullSRV = nullptr;

	_context->PSSetShaderResources(
		0,
		1,
		&nullSRV
	);
}

void Renderer::clean()
{
	// Vertex buffers / index buffers
	_quadVertices.Reset();
	_quadIndices.Reset();

	_waveVertices.Reset();
	_waveIndices.Reset();

	_notesDataBuffer.Reset();
	//_keysDataBuffer.Reset();

	// Shader programs
	_programNotes.clean();
	_programFlashes.clean();
	_programParticules.clean();
	_programKeyMinors.clean();
	_programKeyMajors.clean();
	_programPedals.clean();
	_programWave.clean();
	_programWaveNoise.clean();
	_programScoreBars.clean();
	_programScoreLabels.clean();

	// Renderer does not own these.
	_device = nullptr;
	_context = nullptr;
}