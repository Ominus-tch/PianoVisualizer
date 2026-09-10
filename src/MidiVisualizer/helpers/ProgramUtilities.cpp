#include "ProgramUtilities.h"
#include "ResourcesManager.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "../../../util/Logger.h"

#include <wrl/client.h>

// ============================================================
// D3D11 error handling
// ============================================================

void _checkD3DError(HRESULT hr, const char* file, int line)
{
    if (FAILED(hr))
    {
        Logger::Log("[D3D11] Error in %s:%d HRESULT: %08X\n", file, line, hr);
    }
}


// ============================================================
// ShaderProgram
// ============================================================

ShaderProgram::ShaderProgram()
{
}


ShaderProgram::~ShaderProgram()
{
    clean();
}


void ShaderProgram::init(
    ID3D11Device* device,
    const std::string& vertexName,
    const std::string& fragmentName,
    InputLayoutType inputLayout,
    bool verbose)
{
    _verbose = verbose;

    if (!device)
	{
		Logger::Log("[D3D11] ShaderProgram::init: device is null\n");
		return;
	}

    if (_verbose)
        Logger::Log(
            "[D3D11] ShaderProgram::init: vertex='%s', pixel='%s', inputLayout=%d\n",
            vertexName.c_str(),
            fragmentName.c_str(),
            static_cast<int>(inputLayout)
        );

    clean();

    _device = device;
    _vertexName = vertexName;
    _fragmentName = fragmentName;

    {
        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;

        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

        samplerDesc.BorderColor[0] = 0.0f;
        samplerDesc.BorderColor[1] = 0.0f;
        samplerDesc.BorderColor[2] = 0.0f;
        samplerDesc.BorderColor[3] = 0.0f;

        samplerDesc.MinLOD = 0.0f;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        HRESULT hr = device->CreateSamplerState(
            &samplerDesc,
            &_sampler
        );

        if (FAILED(hr))
        {
            Logger::Log("[D3D11] Failed to create sampler state.\n");

            checkD3DError(hr);
        }
    }

    _inputLayoutType = inputLayout;

    const std::string vertexSource =
        ResourcesManager::getStringForShader(vertexName);

    const std::string fragmentSource =
        ResourcesManager::getStringForShader(fragmentName);

    if (vertexSource.empty())
    {
        Logger::Log("[D3D11] Vertex shader source is empty: %s\n", vertexName.c_str());
        return;
    }

    if (fragmentSource.empty())
    {
        Logger::Log("[D3D11] Pixel shader source is empty: %s\n", fragmentName.c_str());
        return;
    }

    loadVertexShader(
        device,
        vertexSource
    );

    loadPixelShader(
        device,
        fragmentSource
    );

    if (_vertexShaderBlob)
    {
        createInputLayout(
            device,
            _vertexShaderBlob.Get(),
            inputLayout
        );
    }
}


void ShaderProgram::use(ID3D11DeviceContext* context)
{
    if (!_device || !context)
        return;

    _context = context;

    context->VSSetShader(
        _vertexShader.Get(),
        nullptr,
        0
    );

    context->HSSetShader(
        nullptr,
        nullptr,
        0
    );

    context->DSSetShader(
        nullptr,
        nullptr,
        0
    );

    context->GSSetShader(
        nullptr,
        nullptr,
        0
    );

    context->PSSetShader(
        _pixelShader.Get(),
        nullptr,
        0
    );

    // --------------------------------------------------------
    // Sampler
    //
    // notes_frag uses:
    //
    //   majorSampler : register(s0)
    //   minorSampler : register(s1)
    //
    // The sampler state is identical for both, so bind the
    // same sampler to both slots.
    // --------------------------------------------------------

    if (_sampler)
    {
        ID3D11SamplerState* samplers[] =
        {
            _sampler.Get(),
            _sampler.Get()
        };

        context->PSSetSamplers(
            0,
            2,
            samplers
        );
    }

    // --------------------------------------------------------
    // Constant buffers
    //
    // Each ConstantBuffer belongs to exactly one shader stage.
    // Therefore VS b0 and PS b0 are independent buffers.
    // --------------------------------------------------------

    for (const auto& cb : _constantBuffers)
    {
        ID3D11Buffer* buffer =
            cb.buffer.Get();

        if (cb.stage == ShaderStage::Vertex)
        {
            context->VSSetConstantBuffers(
                cb.slot,
                1,
                &buffer
            );
        }
        else
        {
            context->PSSetConstantBuffers(
                cb.slot,
                1,
                &buffer
            );
        }
    }

    bindInputLayout(context);
}

void ShaderProgram::unuse()
{
    if (!_device || !_context)
        return;

    _context->VSSetShader(
        nullptr,
        nullptr,
        0
    );

    _context->PSSetShader(
        nullptr,
        nullptr,
        0
    );

    _context->HSSetShader(
        nullptr,
        nullptr,
        0
    );

    _context->DSSetShader(
        nullptr,
        nullptr,
        0
    );

    _context->GSSetShader(
        nullptr,
        nullptr,
        0
    );

    for (const auto& cb : _constantBuffers)
    {
        ID3D11Buffer* nullBuffer = nullptr;

        if (cb.stage == ShaderStage::Vertex)
        {
            _context->VSSetConstantBuffers(
                cb.slot,
                1,
                &nullBuffer
            );
        }
        else
        {
            _context->PSSetConstantBuffers(
                cb.slot,
                1,
                &nullBuffer
            );
        }
    }

    // Clear both sampler slots we use.
    ID3D11SamplerState* nullSamplers[2] =
    {
        nullptr,
        nullptr
    };

    _context->PSSetSamplers(
        0,
        2,
        nullSamplers
    );

    _context->IASetInputLayout(nullptr);

    _context = nullptr;
}

void ShaderProgram::bindInputLayout(ID3D11DeviceContext* context)
{
    if (!_device || !context)
        return;

    context->IASetInputLayout(_inputLayout.Get());
}


void ShaderProgram::texture(
    ID3D11DeviceContext* context,
    const std::string& name,
    ID3D11ShaderResourceView* texture)
{
    if (!context)
        return;

    auto it = _textures.find(name);

    if (it == _textures.end())
    {
        /*Logger::Log(
            "[D3D11] Texture not found in shader: %s\n",
            name.c_str()
        );*/
        return;
    }

    const TextureBinding& binding = it->second;

    if (binding.stage == ShaderStage::Vertex)
    {
        context->VSSetShaderResources(
            binding.slot,
            1,
            &texture
        );
    }
    else if (binding.stage == ShaderStage::Pixel)
    {
        context->PSSetShaderResources(
            binding.slot,
            1,
            &texture
        );
    }
}


void ShaderProgram::clean()
{
    _inputLayout.Reset();
    _sampler.Reset();

    _vertexShader.Reset();
    _pixelShader.Reset();

    _vertexShaderBlob.Reset();
    _vertexShaderBytecode.clear();

    _constantBuffers.clear();
    _uniforms.clear();
    _textures.clear();

    _device = nullptr;
    _context = nullptr;

    _inputLayoutType = InputLayoutType::None;
}


// ============================================================
// Uniforms
// ============================================================

void ShaderProgram::setUniform(
    const std::string& name,
    const unsigned char* data,
    size_t size)
{
    if (!_device || !_context)
    {
        //Logger::Log("[D3D11] Cannot set uniform without a device context: %s\n", name.c_str());

        return;
    }

    auto it = _uniforms.find(name);

    if (it == _uniforms.end())
    {
        //Logger::Log("[D3D11] Uniform not found in shader: %s\n", name.c_str());

        return;
    }

    bool updated = false;

    for (const UniformInfo& uniform : it->second)
    {
        if (size > uniform.size)
        {
            Logger::Log(
                "[D3D11] Uniform data is too large: %s (%d bytes, maximum %d)",
                name.c_str(),
                size,
                uniform.size
            );

            continue;
        }

        if (uniform.bufferIndex >=
            _constantBuffers.size())
        {
            Logger::Log(
                "[D3D11] Invalid constant buffer index for uniform: %s",
                name.c_str()
            );

            continue;
        }

        ConstantBuffer& cb =
            _constantBuffers[uniform.bufferIndex];

        if (uniform.offset + size > cb.data.size())
        {
            Logger::Log(
                "[D3D11] Uniform write exceeds constant buffer bounds: %s",
                name.c_str()
            );

            continue;
        }

        std::memcpy(
            cb.data.data() + uniform.offset,
            data,
            size
        );

        _context->UpdateSubresource(
            cb.buffer.Get(),
            0,
            nullptr,
            cb.data.data(),
            0,
            0
        );

        updated = true;
    }

    if (!updated)
    {
        Logger::Log("[D3D11] Failed to update uniform: %s\n", name.c_str());
    }
}


// ============================================================
// Shader reflection
// ============================================================

void ShaderProgram::reflectShader(
    ID3DBlob* shaderBlob,
    ShaderStage stage)
{
    if (!shaderBlob)
        return;

    ComPtr<ID3D11ShaderReflection> reflection;

    HRESULT hr = D3DReflect(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        IID_PPV_ARGS(&reflection)
    );

    if (FAILED(hr))
    {
        checkD3DError(hr);
        return;
    }

    D3D11_SHADER_DESC shaderDesc{};

    hr = reflection->GetDesc(&shaderDesc);

    if (FAILED(hr))
        return;

    if (_verbose)
        Logger::Log(
            "[D3D11] stage=%s: InputParameters=%u OutputParameters=%u\n",
            stage == ShaderStage::Vertex ? "VS" : "PS",
            shaderDesc.InputParameters,
            shaderDesc.OutputParameters
        );

    // --------------------------------------------------------
    // Constant buffers
    // --------------------------------------------------------

    for (UINT i = 0;
        i < shaderDesc.ConstantBuffers;
        ++i)
    {
        ID3D11ShaderReflectionConstantBuffer*
            reflectedBuffer =
            reflection->GetConstantBufferByIndex(i);

        if (!reflectedBuffer)
            continue;

        D3D11_SHADER_BUFFER_DESC bufferDesc{};

        if (FAILED(
            reflectedBuffer->GetDesc(&bufferDesc)))
        {
            continue;
        }

        if (bufferDesc.Size == 0)
            continue;

        // ----------------------------------------------------
        // Find the register slot.
        // ----------------------------------------------------

        UINT slot = 0;

        for (UINT r = 0;
            r < shaderDesc.BoundResources;
            ++r)
        {
            D3D11_SHADER_INPUT_BIND_DESC bindDesc{};

            if (FAILED(
                reflection->GetResourceBindingDesc(
                    r,
                    &bindDesc)))
            {
                continue;
            }

            if (bindDesc.Type ==
                D3D_SIT_CBUFFER &&
                std::string(bindDesc.Name) ==
                bufferDesc.Name)
            {
                slot = bindDesc.BindPoint;
                break;
            }
        }

        // ----------------------------------------------------
        // Find an existing buffer with the SAME stage and
        // SAME slot.
        //
        // VS b0 and PS b0 are therefore different buffers.
        // ----------------------------------------------------

        size_t bufferIndex =
            std::numeric_limits<size_t>::max();

        for (size_t j = 0;
            j < _constantBuffers.size();
            ++j)
        {
            const ConstantBuffer& existing =
                _constantBuffers[j];

            if (existing.stage == stage &&
                existing.slot == slot)
            {
                bufferIndex = j;
                break;
            }
        }

        // ----------------------------------------------------
        // Create constant buffer.
        // ----------------------------------------------------

        if (bufferIndex ==
            std::numeric_limits<size_t>::max())
        {
            ConstantBuffer cb;

            cb.size =
                (bufferDesc.Size + 15u) & ~15u;

            cb.slot = slot;
            cb.stage = stage;

            cb.data.resize(
                cb.size,
                0
            );

            D3D11_BUFFER_DESC desc{};

            desc.ByteWidth = cb.size;

            desc.Usage =
                D3D11_USAGE_DEFAULT;

            desc.BindFlags =
                D3D11_BIND_CONSTANT_BUFFER;

            hr = _device->CreateBuffer(
                &desc,
                nullptr,
                &cb.buffer
            );

            if (FAILED(hr))
            {
                checkD3DError(hr);
                continue;
            }

            _constantBuffers.push_back(
                std::move(cb)
            );

            bufferIndex =
                _constantBuffers.size() - 1;
        }

        ConstantBuffer& cb =
            _constantBuffers[bufferIndex];

        // ----------------------------------------------------
        // Variables
        // ----------------------------------------------------

        for (UINT variableIndex = 0;
            variableIndex < bufferDesc.Variables;
            ++variableIndex)
        {
            ID3D11ShaderReflectionVariable*
                variable =
                reflectedBuffer->GetVariableByIndex(
                    variableIndex
                );

            if (!variable)
                continue;

            D3D11_SHADER_VARIABLE_DESC variableDesc{};

            if (FAILED(
                variable->GetDesc(&variableDesc)))
            {
                continue;
            }

            ID3D11ShaderReflectionType* type =
                variable->GetType();

            D3D11_SHADER_TYPE_DESC typeDesc{};

            if (type)
            {
                type->GetDesc(&typeDesc);
            }

            UniformInfo info;

            info.bufferIndex = bufferIndex;
            info.offset = variableDesc.StartOffset;
            info.size = variableDesc.Size;
            info.arrayElements = typeDesc.Elements;

            if (typeDesc.Elements > 1)
            {
                info.arrayStride = 16;
            }

            info.stage = stage;

            _uniforms[variableDesc.Name].push_back(
                info
            );
        }
    }

    // --------------------------------------------------------
// Textures
// --------------------------------------------------------

    for (UINT i = 0;
        i < shaderDesc.BoundResources;
        ++i)
    {
        D3D11_SHADER_INPUT_BIND_DESC bindDesc{};

        if (FAILED(
            reflection->GetResourceBindingDesc(
                i,
                &bindDesc)))
        {
            continue;
        }

        if (bindDesc.Type != D3D_SIT_TEXTURE)
            continue;

        TextureBinding binding;

        binding.slot = bindDesc.BindPoint;
        binding.stage = stage;

        _textures[bindDesc.Name] = binding;

        if (_verbose)
            Logger::Log(
                "   texture=%s slot=%u stage=%s\n",
                bindDesc.Name,
                bindDesc.BindPoint,
                stage == ShaderStage::Vertex ? "VS" : "PS"
            );
    }

    // --------------------------------------------------------
    // Debug output
    // --------------------------------------------------------
    if (_verbose) {
        Logger::Log(
            "Shader reflection for %s shader:\n",
            stage == ShaderStage::Vertex ? "vertex" : "pixel"
        );

        for (const auto& [name, infos] : _uniforms)
        {
            for (const UniformInfo& uniform : infos)
            {
                if (uniform.stage != stage)
                    continue;

                Logger::Log(
                    "   %s offset=%d size=%d buffer=%d\n",
                    name.c_str(),
                    uniform.offset,
                    uniform.size,
                    uniform.bufferIndex
                );
            }
        }
    }
}

void ShaderProgram::createInputLayout(
    ID3D11Device* device,
    ID3DBlob* shaderBlob,
    InputLayoutType type)
{
    if (!device || !shaderBlob || type == InputLayoutType::None)
        return;

    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;

    if (_verbose)
        Logger::Log(
            "[D3D11] createInputLayout: type=%d\n",
            static_cast<int>(type)
        );

    // --------------------------------------------------------
    // All quad layouts use:
    //
    //   Slot 0 = per-vertex quad data
    //   Slot 1 = per-instance data
    //
    // Instance data advances once for every instance.
    // --------------------------------------------------------

    switch (type)
    {
        // --------------------------------------------------------
        // Position2D
        //
        // HLSL:
        //   float2 v : POSITION;
        //
        // Slot 0:
        //   float2 position
        // --------------------------------------------------------
    case InputLayoutType::Position2D:
    {
        elements =
        {
            {
                "POSITION",
                0,
                DXGI_FORMAT_R32G32_FLOAT,
                0,
                0,
                D3D11_INPUT_PER_VERTEX_DATA,
                0
            }
        };

        break;
    }

    // --------------------------------------------------------
    // Position3D
    //
    // HLSL:
    //   float3 v : POSITION;
    //
    // Slot 0:
    //   float3 position
    // --------------------------------------------------------
    case InputLayoutType::Position3D:
    {
        elements =
        {
            {
                "POSITION",
                0,
                DXGI_FORMAT_R32G32B32_FLOAT,
                0,
                0,
                D3D11_INPUT_PER_VERTEX_DATA,
                0
            }
        };

        break;
    }

    // --------------------------------------------------------
    // QuadWithFlashData
    //
    // HLSL:
    //   float2 v       : POSITION;
    //   float  channel : CHANNEL_DATA0;
    //
    // Slot 0:
    //   float2 position
    //
    // Slot 1:
    //   float channel
    // --------------------------------------------------------
    case InputLayoutType::QuadWithFlashData:
    {
        elements =
        {
            {
                "POSITION",
                0,
                DXGI_FORMAT_R32G32_FLOAT,
                0,
                0,
                D3D11_INPUT_PER_VERTEX_DATA,
                0
            }
        };

        break;
    }

    // --------------------------------------------------------
    // QuadWithNoteData
    //
    // GPUNote:
    //
    //   struct GPUNote
    //   {
    //       float note;
    //       float start;
    //       float duration;
    //       float isMinor;
    //       float set;
    //   };
    //
    // HLSL:
    //   float2 v        : POSITION;
    //   float  note     : NOTE_DATA0;
    //   float  start    : NOTE_DATA1;
    //   float  duration : NOTE_DATA2;
    //   float  isMinor  : NOTE_DATA3;
    //   float  set      : NOTE_DATA4;
    //
    // Slot 0:
    //   float2 position
    //
    // Slot 1:
    //   GPUNote instance data
    //
    // The five note values are stored consecutively in the
    // 20-byte GPUNote structure.
    // --------------------------------------------------------
    case InputLayoutType::QuadWithNoteData:
    {
        elements =
        {
            {
                "POSITION",
                0,
                DXGI_FORMAT_R32G32_FLOAT,
                0,
                0,
                D3D11_INPUT_PER_VERTEX_DATA,
                0
            },
            {
                "NOTE_DATA",
                0,
                DXGI_FORMAT_R32_FLOAT,
                1,
                0,
                D3D11_INPUT_PER_INSTANCE_DATA,
                1
            },
            {
                "NOTE_DATA",
                1,
                DXGI_FORMAT_R32_FLOAT,
                1,
                4,
                D3D11_INPUT_PER_INSTANCE_DATA,
                1
            },
            {
                "NOTE_DATA",
                2,
                DXGI_FORMAT_R32_FLOAT,
                1,
                8,
                D3D11_INPUT_PER_INSTANCE_DATA,
                1
            },
            {
                "NOTE_DATA",
                3,
                DXGI_FORMAT_R32_FLOAT,
                1,
                12,
                D3D11_INPUT_PER_INSTANCE_DATA,
                1
            },
            {
                "NOTE_DATA",
                4,
                DXGI_FORMAT_R32_FLOAT,
                1,
                16,
                D3D11_INPUT_PER_INSTANCE_DATA,
                1
            }
        };

        break;
    }

    // --------------------------------------------------------
    // QuadWithKeyData
    //
    // HLSL:
    //   float2 v : POSITION;
    //   int   key : KEY_DATA0;
    //
    // Slot 0:
    //   float2 position
    //
    // Slot 1:
    //   int key
    // --------------------------------------------------------
    case InputLayoutType::QuadWithKeyData:
    {
        elements =
        {
            {
                "POSITION",
                0,
                DXGI_FORMAT_R32G32_FLOAT,
                0,
                0,
                D3D11_INPUT_PER_VERTEX_DATA,
                0
            },
            {
                "KEY_DATA",
                0,
                DXGI_FORMAT_R32_SINT,
                1,
                0,
                D3D11_INPUT_PER_INSTANCE_DATA,
                1
            }
        };

        break;
    }

    default:
        Logger::Log(
            "[D3D11] Unknown input layout type: %d\n",
            static_cast<int>(type)
        );
        return;
    }

    // --------------------------------------------------------
    // Create the D3D11 input layout.
    // --------------------------------------------------------

    HRESULT hr = device->CreateInputLayout(
        elements.data(),
        static_cast<UINT>(elements.size()),
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        &_inputLayout
    );

    if (FAILED(hr))
    {
        Logger::Log(
            "[D3D11] Failed to create input layout for %s: HRESULT=0x%08X\n",
            _vertexName.c_str(),
            hr
        );

        checkD3DError(hr);
        return;
    }

    if (_verbose)
        Logger::Log(
            "[D3D11] Input layout for %s created successfully.\n",
            _vertexName.c_str()
        );
}

// ============================================================
// Shader compilation
// ============================================================

void ShaderProgram::loadVertexShader(
    ID3D11Device* device,
    const std::string& source)
{
    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;


    if (_verbose) {
        Logger::Log("[D3D11] Loading vertex shader...\n");
        Logger::Log(
            "[D3D11] Vertex shader source:\n%s\n",
            source.c_str()
        );
    }

    HRESULT hr = D3DCompile(
        source.data(),
        source.size(),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            Logger::Log(
                "[D3D11] Vertex shader compilation failed:\n%s",
                static_cast<const char*>(errorBlob->GetBufferPointer())
            );
        }

        checkD3DError(hr);
        return;
    }

    // Keep the compiled shader blob.
    _vertexShaderBlob = shaderBlob;

    // Keep a copy of the bytecode.
    _vertexShaderBytecode.resize(
        shaderBlob->GetBufferSize()
    );

    std::memcpy(
        _vertexShaderBytecode.data(),
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize()
    );

    // --------------------------------------------------------
    // Create vertex shader
    // --------------------------------------------------------

    hr = device->CreateVertexShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &_vertexShader
    );

    if (FAILED(hr))
    {
        checkD3DError(hr);
        return;
    }

    // --------------------------------------------------------
    // Reflect uniforms / textures.
    // --------------------------------------------------------

    reflectShader(
        shaderBlob.Get(),
        ShaderStage::Vertex
    );
}


void ShaderProgram::loadPixelShader(
    ID3D11Device* device,
    const std::string& source)
{
    if (_verbose) {
        Logger::Log("[D3D11] Loading pixel shader...\n");


        Logger::Log(
            "[D3D11] Fragment shader source:\n%s\n",
            source.c_str()
        );
    }

    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(
        source.data(),
        source.size(),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            Logger::Log(
                "[D3D11] Pixel shader compilation failed:\n%s",
                static_cast<const char*>(errorBlob->GetBufferPointer())
            );
        }

        checkD3DError(hr);

        Logger::Log("[D3D11] CreatePixelShader failed. 1\n");

        return;
    }

    hr = device->CreatePixelShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &_pixelShader
    );

    if (FAILED(hr))
    {
        Logger::Log("[D3D11] CreatePixelShader failed. 2\n");

        checkD3DError(hr);

        return;
    }
    
    if (_verbose)
        Logger::Log("[D3D11] Pixel shader compiled and created successfully.\n");

    if (SUCCEEDED(hr))
    {
        reflectShader(
            shaderBlob.Get(),
            ShaderStage::Pixel
        );
    }
}


// ============================================================
// Texture loading
// ============================================================

ComPtr<ID3D11ShaderResourceView> loadTexture(
    ID3D11Device* device,
    const std::string& path,
    unsigned int channels,
    bool sRGB)
{
    int width = 0;
    int height = 0;
    int sourceChannels = 0;

    stbi_set_flip_vertically_on_load(true);

    unsigned char* image = stbi_load(
        path.c_str(),
        &width,
        &height,
        &sourceChannels,
        channels
    );

    stbi_set_flip_vertically_on_load(false);

    if (!image)
    {
        Logger::Log("[D3D11] Unable to load texture: %s\n", path.c_str());
        return nullptr;
    }

    auto texture = loadTexture(
        device,
        image,
        width,
        height,
        channels,
        sRGB
    );

    stbi_image_free(image);

    return texture;
}


ComPtr<ID3D11ShaderResourceView> loadTexture(
    ID3D11Device* device,
    unsigned char* image,
    unsigned int width,
    unsigned int height,
    unsigned int channels,
    bool sRGB)
{
    if (!device)
    {
        Logger::Log("[D3D11] loadTexture: device is null\n");
        return nullptr;
    }

    DXGI_FORMAT format;

    switch (channels)
    {
    case 1:
        format = DXGI_FORMAT_R8_UNORM;
        break;

    case 3:
        format = sRGB
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM;
        break;

    case 4:
        format = sRGB
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM;
        break;

    default:
        Logger::Log("[D3D11] Unsupported texture channel count: %u\n", channels);
        return nullptr;
    }




    // Convert RGB -> RGBA.
    std::vector<unsigned char> rgba;

    const unsigned char* source = image;

    if (channels == 3)
    {
        rgba.resize(
            static_cast<size_t>(width) *
            static_cast<size_t>(height) *
            4
        );

        for (size_t i = 0;
            i < static_cast<size_t>(width) *
            static_cast<size_t>(height);
            ++i)
        {
            rgba[i * 4 + 0] = image[i * 3 + 0];
            rgba[i * 4 + 1] = image[i * 3 + 1];
            rgba[i * 4 + 2] = image[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }

        source = rgba.data();
    }


    D3D11_TEXTURE2D_DESC desc{};

    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;


    D3D11_SUBRESOURCE_DATA data{};

    data.pSysMem = source;

    data.SysMemPitch =
        width * (channels == 1 ? 1 : 4);


    ComPtr<ID3D11Texture2D> texture;

    HRESULT hr = device->CreateTexture2D(
        &desc,
        &data,
        &texture
    );

    if (FAILED(hr))
    {
        checkD3DError(hr);
        return nullptr;
    }


    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};

    srvDesc.Format = format;
    srvDesc.ViewDimension =
        D3D11_SRV_DIMENSION_TEXTURE2D;

    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;


    ComPtr<ID3D11ShaderResourceView> srv;

    hr = device->CreateShaderResourceView(
        texture.Get(),
        &srvDesc,
        &srv
    );

    if (FAILED(hr))
    {
        checkD3DError(hr);
        return nullptr;
    }

    return srv;
}


// ============================================================
// Simple nearest-neighbor resize
// ============================================================

static std::vector<unsigned char> resizeImage(
    const unsigned char* source,
    int sourceWidth,
    int sourceHeight,
    int targetWidth,
    int targetHeight,
    unsigned int channels)
{
    std::vector<unsigned char> result(
        static_cast<size_t>(targetWidth) *
        static_cast<size_t>(targetHeight) *
        channels
    );

    for (int y = 0; y < targetHeight; ++y)
    {
        const int sourceY =
            (y * sourceHeight) / targetHeight;

        for (int x = 0; x < targetWidth; ++x)
        {
            const int sourceX =
                (x * sourceWidth) / targetWidth;

            const size_t sourceIndex =
                (static_cast<size_t>(sourceY) *
                    sourceWidth +
                    sourceX) *
                channels;

            const size_t targetIndex =
                (static_cast<size_t>(y) *
                    targetWidth +
                    x) *
                channels;

            for (unsigned int c = 0;
                c < channels;
                ++c)
            {
                result[targetIndex + c] =
                    source[sourceIndex + c];
            }
        }
    }

    return result;
}


// ============================================================
// Texture arrays
// ============================================================

ComPtr<ID3D11ShaderResourceView> loadTextureArray(
    ID3D11Device* device,
    const std::vector<std::string>& paths,
    bool sRGB,
    int& layers)
{
    std::vector<unsigned char*> images;
    std::vector<glm::ivec2> sizes;

    stbi_set_flip_vertically_on_load(true);

    for (const auto& path : paths)
    {
        int width = 0;
        int height = 0;
        int channels = 0;

        unsigned char* image = stbi_load(
            path.c_str(),
            &width,
            &height,
            &channels,
            1
        );

        if (!image)
        {
            Logger::Log("[D3D11] Unable to load texture: %s\n", path.c_str());
            continue;
        }

        images.push_back(image);
        sizes.emplace_back(width, height);
    }

    stbi_set_flip_vertically_on_load(false);

    layers =
        static_cast<int>(images.size());

    auto result = loadTextureArray(
        device,
        images,
        sizes,
        1,
        sRGB
    );

    for (auto* image : images)
        stbi_image_free(image);

    return result;
}


ComPtr<ID3D11ShaderResourceView> loadTextureArray(
    ID3D11Device* device,
    const std::vector<unsigned char*>& images,
    const std::vector<glm::ivec2>& sizes,
    unsigned int channels,
    bool sRGB)
{
    if (images.empty())
        return nullptr;

    if (images.size() != sizes.size())
        return nullptr;


    // --------------------------------------------------------
    // Find largest layer.
    // --------------------------------------------------------

    glm::ivec2 maxSize(0);

    for (const auto& size : sizes)
    {
        maxSize.x =
            std::max(maxSize.x, size.x);

        maxSize.y =
            std::max(maxSize.y, size.y);
    }


    DXGI_FORMAT format;

    switch (channels)
    {
    case 1:
        format = DXGI_FORMAT_R8_UNORM;
        break;

    case 4:
        format = sRGB
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM;
        break;

    default:
        Logger::Log("[D3D11] Unsupported texture array channel count: %u\n", channels);

        return nullptr;
    }


    const UINT width =
        static_cast<UINT>(maxSize.x);

    const UINT height =
        static_cast<UINT>(maxSize.y);

    const UINT layerCount =
        static_cast<UINT>(images.size());

    const UINT bytesPerPixel =
        channels == 1 ? 1 : 4;


    // --------------------------------------------------------
    // Create texture array.
    // --------------------------------------------------------

    D3D11_TEXTURE2D_DESC desc{};

    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = layerCount;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;


    ComPtr<ID3D11Texture2D> texture;

    HRESULT hr = device->CreateTexture2D(
        &desc,
        nullptr,
        &texture
    );

    if (FAILED(hr))
    {
        checkD3DError(hr);
        return nullptr;
    }


    // --------------------------------------------------------
    // Get immediate context.
    // --------------------------------------------------------

    ComPtr<ID3D11DeviceContext> context;

    device->GetImmediateContext(
        &context
    );


    // --------------------------------------------------------
    // Upload every layer.
    // --------------------------------------------------------

    for (UINT layer = 0;
        layer < layerCount;
        ++layer)
    {
        const int sourceWidth =
            sizes[layer].x;

        const int sourceHeight =
            sizes[layer].y;


        std::vector<unsigned char> resized;

        const unsigned char* source =
            images[layer];


        if (sourceWidth != static_cast<int>(width) ||
            sourceHeight != static_cast<int>(height))
        {
            resized = resizeImage(
                source,
                sourceWidth,
                sourceHeight,
                width,
                height,
                channels
            );

            source = resized.data();
        }


        const UINT subresource =
            D3D11CalcSubresource(
                0,
                layer,
                1
            );


        context->UpdateSubresource(
            texture.Get(),
            subresource,
            nullptr,
            source,
            width * bytesPerPixel,
            0
        );
    }


    // --------------------------------------------------------
    // Shader resource view.
    // --------------------------------------------------------

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};

    srvDesc.Format = format;
    srvDesc.ViewDimension =
        D3D11_SRV_DIMENSION_TEXTURE2DARRAY;

    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = layerCount;


    ComPtr<ID3D11ShaderResourceView> srv;

    hr = device->CreateShaderResourceView(
        texture.Get(),
        &srvDesc,
        &srv
    );

    if (FAILED(hr))
    {
        checkD3DError(hr);
        return nullptr;
    }

    return srv;
}

std::vector<ComPtr<ID3D11ShaderResourceView>>
generate2DViewsOfArray(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* textureArray,
    unsigned int maxSize)
{
    std::vector<ComPtr<ID3D11ShaderResourceView>> result;

    if (!device || !context || !textureArray || maxSize == 0)
        return result;

    // --------------------------------------------------------
    // Get the underlying texture.
    // --------------------------------------------------------

    ComPtr<ID3D11Resource> resource;

    textureArray->GetResource(&resource);

    ComPtr<ID3D11Texture2D> arrayTexture;

    if (FAILED(resource.As(&arrayTexture)))
        return result;

    D3D11_TEXTURE2D_DESC arrayDesc{};
    arrayTexture->GetDesc(&arrayDesc);

    if (arrayDesc.ArraySize == 0)
        return result;

    // --------------------------------------------------------
    // Determine the number of channels.
    // --------------------------------------------------------

    unsigned int channels = 4;

    switch (arrayDesc.Format)
    {
    case DXGI_FORMAT_R8_UNORM:
        channels = 1;
        break;

    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        channels = 4;
        break;

    default:
        Logger::Log("[D3D11] Unsupported texture array format "
            "for preview generation.\n");

        return result;
    }

    // --------------------------------------------------------
    // Create a staging texture containing the whole array.
    //
    // This allows us to read every layer from the GPU.
    // --------------------------------------------------------

    D3D11_TEXTURE2D_DESC stagingDesc = arrayDesc;

    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTexture;

    HRESULT hr = device->CreateTexture2D(
        &stagingDesc,
        nullptr,
        &stagingTexture
    );

    if (FAILED(hr))
        return result;

    context->CopyResource(
        stagingTexture.Get(),
        arrayTexture.Get()
    );

    // --------------------------------------------------------
    // Determine the output format.
    // --------------------------------------------------------

    DXGI_FORMAT outputFormat;

    if (channels == 1)
    {
        outputFormat = DXGI_FORMAT_R8_UNORM;
    }
    else
    {
        outputFormat =
            arrayDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    // --------------------------------------------------------
    // Process every layer.
    // --------------------------------------------------------

    result.reserve(arrayDesc.ArraySize);

    for (UINT layer = 0;
        layer < arrayDesc.ArraySize;
        ++layer)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        const UINT subresource =
            D3D11CalcSubresource(
                0,
                layer,
                arrayDesc.MipLevels
            );

        hr = context->Map(
            stagingTexture.Get(),
            subresource,
            D3D11_MAP_READ,
            0,
            &mapped
        );

        if (FAILED(hr))
            continue;

        const size_t sourceRowSize =
            static_cast<size_t>(arrayDesc.Width) *
            channels;

        std::vector<unsigned char> source(
            static_cast<size_t>(arrayDesc.Width) *
            static_cast<size_t>(arrayDesc.Height) *
            channels
        );

        for (UINT y = 0; y < arrayDesc.Height; ++y)
        {
            memcpy(
                source.data() +
                static_cast<size_t>(y) *
                sourceRowSize,

                static_cast<const unsigned char*>(
                    mapped.pData
                    ) +
                static_cast<size_t>(y) *
                mapped.RowPitch,

                sourceRowSize
            );
        }

        context->Unmap(
            stagingTexture.Get(),
            subresource
        );

        // ----------------------------------------------------
        // Resize to thumbnail.
        // ----------------------------------------------------

        std::vector<unsigned char> resized;

        const unsigned char* image = source.data();

        if (arrayDesc.Width != maxSize ||
            arrayDesc.Height != maxSize)
        {
            resized = resizeImage(
                source.data(),
                static_cast<int>(arrayDesc.Width),
                static_cast<int>(arrayDesc.Height),
                maxSize,
                maxSize,
                channels
            );

            image = resized.data();
        }

        // ----------------------------------------------------
        // Create normal 2D texture.
        // ----------------------------------------------------

        D3D11_TEXTURE2D_DESC previewDesc{};

        previewDesc.Width = maxSize;
        previewDesc.Height = maxSize;
        previewDesc.MipLevels = 1;
        previewDesc.ArraySize = 1;
        previewDesc.Format = outputFormat;
        previewDesc.SampleDesc.Count = 1;
        previewDesc.Usage = D3D11_USAGE_DEFAULT;
        previewDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initialData{};

        initialData.pSysMem = image;
        initialData.SysMemPitch =
            maxSize * channels;

        ComPtr<ID3D11Texture2D> previewTexture;

        hr = device->CreateTexture2D(
            &previewDesc,
            &initialData,
            &previewTexture
        );

        if (FAILED(hr))
            continue;

        // ----------------------------------------------------
        // Create SRV.
        // ----------------------------------------------------

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};

        srvDesc.Format = outputFormat;
        srvDesc.ViewDimension =
            D3D11_SRV_DIMENSION_TEXTURE2D;

        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        ComPtr<ID3D11ShaderResourceView> previewSRV;

        hr = device->CreateShaderResourceView(
            previewTexture.Get(),
            &srvDesc,
            &previewSRV
        );

        if (SUCCEEDED(hr))
        {
            result.push_back(
                std::move(previewSRV)
            );
        }
    }

    return result;
}