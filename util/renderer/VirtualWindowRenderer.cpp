#include "VirtualWindowRenderer.h"

#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>

#include <d3dcompiler.h>

#include "../Logger.h"

bool VirtualWindowRenderer::InitializeD3D11Resources(
    ID3D11Device* device
)
{
    if (!device)
        return false;

    // ---------------------------------------------------------
    // Vertex shader
    // ---------------------------------------------------------

    static const char* vertexShaderSource = R"(
        struct VSInput
        {
            float2 position : POSITION;
            float2 uv       : TEXCOORD0;
        };

        struct VSOutput
        {
            float4 position : SV_POSITION;
            float2 uv       : TEXCOORD0;
        };

        VSOutput main(VSInput input)
        {
            VSOutput output;

            output.position =
                float4(
                    input.position,
                    0.0f,
                    1.0f
                );

            output.uv =
                input.uv;

            return output;
        }
    )";

    ComPtr<ID3DBlob> vertexBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr =
        D3DCompile(
            vertexShaderSource,
            strlen(vertexShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "vs_5_0",
            0,
            0,
            &vertexBlob,
            &errorBlob
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualWindowRenderer] Vertex shader compilation failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    hr =
        device->CreateVertexShader(
            vertexBlob->GetBufferPointer(),
            vertexBlob->GetBufferSize(),
            nullptr,
            &m_d3dVertexShader
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualWindowRenderer] CreateVertexShader failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    // ---------------------------------------------------------
    // Input layout
    // ---------------------------------------------------------

    const D3D11_INPUT_ELEMENT_DESC inputLayout[] =
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
            "TEXCOORD",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            sizeof(float) * 2,
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        }
    };

    hr =
        device->CreateInputLayout(
            inputLayout,
            ARRAYSIZE(inputLayout),
            vertexBlob->GetBufferPointer(),
            vertexBlob->GetBufferSize(),
            &m_d3dInputLayout
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualWindowRenderer] CreateInputLayout failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    // ---------------------------------------------------------
    // Pixel shader
    // ---------------------------------------------------------

    static const char* pixelShaderSource = R"(
        Texture2D screenTexture : register(t0);
        SamplerState screenSampler : register(s0);

        struct PSInput
        {
            float4 position : SV_POSITION;
            float2 uv       : TEXCOORD0;
        };

        float4 main(PSInput input) : SV_TARGET
        {
            return screenTexture.Sample(
                screenSampler,
                input.uv
            );
        }
    )";

    ComPtr<ID3DBlob> pixelBlob;

    hr =
        D3DCompile(
            pixelShaderSource,
            strlen(pixelShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "ps_5_0",
            0,
            0,
            &pixelBlob,
            &errorBlob
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualWindowRenderer] Pixel shader compilation failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    hr =
        device->CreatePixelShader(
            pixelBlob->GetBufferPointer(),
            pixelBlob->GetBufferSize(),
            nullptr,
            &m_d3dPixelShader
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualWindowRenderer] CreatePixelShader failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    // ---------------------------------------------------------
    // Sampler
    // ---------------------------------------------------------

    D3D11_SAMPLER_DESC samplerDesc{};

    samplerDesc.Filter =
        D3D11_FILTER_MIN_MAG_MIP_LINEAR;

    samplerDesc.AddressU =
        D3D11_TEXTURE_ADDRESS_CLAMP;

    samplerDesc.AddressV =
        D3D11_TEXTURE_ADDRESS_CLAMP;

    samplerDesc.AddressW =
        D3D11_TEXTURE_ADDRESS_CLAMP;

    samplerDesc.ComparisonFunc =
        D3D11_COMPARISON_ALWAYS;

    samplerDesc.MinLOD =
        0.0f;

    samplerDesc.MaxLOD =
        D3D11_FLOAT32_MAX;

    hr =
        device->CreateSamplerState(
            &samplerDesc,
            &m_d3dSamplerState
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualWindowRenderer] CreateSamplerState failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    // ---------------------------------------------------------
    // Alpha blend
    // ---------------------------------------------------------

    D3D11_BLEND_DESC blendDesc{};

    blendDesc.RenderTarget[0].BlendEnable =
        TRUE;

    blendDesc.RenderTarget[0].SrcBlend =
        D3D11_BLEND_SRC_ALPHA;

    blendDesc.RenderTarget[0].DestBlend =
        D3D11_BLEND_INV_SRC_ALPHA;

    blendDesc.RenderTarget[0].BlendOp =
        D3D11_BLEND_OP_ADD;

    blendDesc.RenderTarget[0].SrcBlendAlpha =
        D3D11_BLEND_ONE;

    blendDesc.RenderTarget[0].DestBlendAlpha =
        D3D11_BLEND_INV_SRC_ALPHA;

    blendDesc.RenderTarget[0].BlendOpAlpha =
        D3D11_BLEND_OP_ADD;

    blendDesc.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;

    hr =
        device->CreateBlendState(
            &blendDesc,
            &m_d3dBlendState
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualWindowRenderer] CreateBlendState failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    return true;
}

// =============================================================
// RENDER
// =============================================================

void VirtualWindowRenderer::Render(
    ImDrawList* drawList,
    ImTextureID texture,

    const ImVec2& topLeft,
    const ImVec2& topRight,
    const ImVec2& bottomRight,
    const ImVec2& bottomLeft,

    const Settings& settings
)
{
    Render(
        drawList,
        texture,

        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImVec2(0.0f, 1.0f),

        topLeft,
        topRight,
        bottomRight,
        bottomLeft,

        settings
    );
}

void VirtualWindowRenderer::Render(
    ImDrawList* drawList,
    ImTextureID texture,

    const ImVec2& sourceTopLeft,
    const ImVec2& sourceTopRight,
    const ImVec2& sourceBottomRight,
    const ImVec2& sourceBottomLeft,

    const ImVec2& destinationTopLeft,
    const ImVec2& destinationTopRight,
    const ImVec2& destinationBottomRight,
    const ImVec2& destinationBottomLeft,

    const Settings& settings
)
{
    if (!drawList)
        return;

    if (!texture)
        return;

    const int gridX =
        settings.gridX;

    const int gridY =
        settings.gridY;

    if (
        gridX < 1 ||
        gridY < 1
        )
    {
        return;
    }

    // =========================================================
    // CALCULATE PROJECTIVE TRANSFORMATION
    // =========================================================

    const ImVec2 source[4] =
    {
        sourceTopLeft,
        sourceTopRight,
        sourceBottomRight,
        sourceBottomLeft
    };

    const ImVec2 destination[4] =
    {
        destinationTopLeft,
        destinationTopRight,
        destinationBottomRight,
        destinationBottomLeft
    };

    // =========================================================
    // HOMOGRAPHY CACHE
    // =========================================================

    bool homographyChanged =
        !m_hasCachedHomography;

    if (!homographyChanged)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (
                m_cachedSource[i].x != source[i].x ||
                m_cachedSource[i].y != source[i].y ||
                m_cachedDestination[i].x != destination[i].x ||
                m_cachedDestination[i].y != destination[i].y
                )
            {
                homographyChanged = true;
                break;
            }
        }
    }

    if (homographyChanged)
    {
        m_cachedHomography =
            CalculateHomography(
                source,
                destination
            );

        if (!m_cachedHomography.valid)
        {
            m_hasCachedHomography = false;
            return;
        }

        for (int i = 0; i < 4; ++i)
        {
            m_cachedSource[i] =
                source[i];

            m_cachedDestination[i] =
                destination[i];
        }

        m_hasCachedHomography = true;
    }

    const Homography& H =
        m_cachedHomography;

    // =========================================================
    // SOURCE BOTTOM SCALING
    // =========================================================

    const float scale =
        settings.sourceBottomScale;

    const ImVec2 scaledBottomLeft(
        sourceTopLeft.x +
        (
            sourceBottomLeft.x -
            sourceTopLeft.x
            ) * scale,

        sourceTopLeft.y +
        (
            sourceBottomLeft.y -
            sourceTopLeft.y
            ) * scale
    );

    const ImVec2 scaledBottomRight(
        sourceTopRight.x +
        (
            sourceBottomRight.x -
            sourceTopRight.x
            ) * scale,

        sourceTopRight.y +
        (
            sourceBottomRight.y -
            sourceTopRight.y
            ) * scale
    );

    // =========================================================
    // PRECOMPUTE GRID VALUES
    // =========================================================

    const float invGridX =
        1.0f / static_cast<float>(gridX);

    const float invGridY =
        1.0f / static_cast<float>(gridY);

    for (int y = 0; y < gridY; ++y)
    {
        const float v0 =
            static_cast<float>(y) * invGridY;

        const float v1 =
            static_cast<float>(y + 1) * invGridY;

        for (int x = 0; x < gridX; ++x)
        {
            const float u0 =
                static_cast<float>(x) * invGridX;

            const float u1 =
                static_cast<float>(x + 1) * invGridX;

            // -------------------------------------------------
            // SOURCE TOP EDGE
            // -------------------------------------------------

            const ImVec2 sourceTop0(
                sourceTopLeft.x +
                (
                    sourceTopRight.x -
                    sourceTopLeft.x
                    ) * u0,

                sourceTopLeft.y +
                (
                    sourceTopRight.y -
                    sourceTopLeft.y
                    ) * u0
            );

            const ImVec2 sourceTop1(
                sourceTopLeft.x +
                (
                    sourceTopRight.x -
                    sourceTopLeft.x
                    ) * u1,

                sourceTopLeft.y +
                (
                    sourceTopRight.y -
                    sourceTopLeft.y
                    ) * u1
            );

            // -------------------------------------------------
            // SOURCE BOTTOM EDGE
            // -------------------------------------------------

            const ImVec2 sourceBottom0(
                scaledBottomLeft.x +
                (
                    scaledBottomRight.x -
                    scaledBottomLeft.x
                    ) * u0,

                scaledBottomLeft.y +
                (
                    scaledBottomRight.y -
                    scaledBottomLeft.y
                    ) * u0
            );

            const ImVec2 sourceBottom1(
                scaledBottomLeft.x +
                (
                    scaledBottomRight.x -
                    scaledBottomLeft.x
                    ) * u1,

                scaledBottomLeft.y +
                (
                    scaledBottomRight.y -
                    scaledBottomLeft.y
                    ) * u1
            );

            // -------------------------------------------------
            // SOURCE QUAD
            // -------------------------------------------------

            const ImVec2 source00(
                sourceTop0.x +
                (
                    sourceBottom0.x -
                    sourceTop0.x
                    ) * v0,

                sourceTop0.y +
                (
                    sourceBottom0.y -
                    sourceTop0.y
                    ) * v0
            );

            const ImVec2 source10(
                sourceTop1.x +
                (
                    sourceBottom1.x -
                    sourceTop1.x
                    ) * v0,

                sourceTop1.y +
                (
                    sourceBottom1.y -
                    sourceTop1.y
                    ) * v0
            );

            const ImVec2 source11(
                sourceTop1.x +
                (
                    sourceBottom1.x -
                    sourceTop1.x
                    ) * v1,

                sourceTop1.y +
                (
                    sourceBottom1.y -
                    sourceTop1.y
                    ) * v1
            );

            const ImVec2 source01(
                sourceTop0.x +
                (
                    sourceBottom0.x -
                    sourceTop0.x
                    ) * v1,

                sourceTop0.y +
                (
                    sourceBottom0.y -
                    sourceTop0.y
                    ) * v1
            );

            // -------------------------------------------------
            // PROJECT SOURCE POSITIONS
            // -------------------------------------------------

            const ImVec2 p00 =
                H.Project(
                    source00.x,
                    source00.y
                );

            const ImVec2 p10 =
                H.Project(
                    source10.x,
                    source10.y
                );

            const ImVec2 p11 =
                H.Project(
                    source11.x,
                    source11.y
                );

            const ImVec2 p01 =
                H.Project(
                    source01.x,
                    source01.y
                );

            // -------------------------------------------------
            // DRAW
            // -------------------------------------------------

            drawList->AddImageQuad(
                texture,

                p00,
                p10,
                p11,
                p01,

                source00,
                source10,
                source11,
                source01,

                settings.tint
            );
        }
    }

    // =========================================================
    // DEBUG OUTLINE
    // =========================================================

    if (settings.drawDebugLines)
    {
        const ImVec2 extendedDestinationBottomLeft =
            H.Project(
                scaledBottomLeft.x,
                scaledBottomLeft.y
            );

        const ImVec2 extendedDestinationBottomRight =
            H.Project(
                scaledBottomRight.x,
                scaledBottomRight.y
            );

        drawList->AddLine(
            destinationTopLeft,
            destinationTopRight,
            IM_COL32(
                0,
                150,
                255,
                230
            ),
            2.0f
        );

        drawList->AddLine(
            destinationTopRight,
            extendedDestinationBottomRight,
            IM_COL32(
                0,
                150,
                255,
                230
            ),
            2.0f
        );

        drawList->AddLine(
            extendedDestinationBottomRight,
            extendedDestinationBottomLeft,
            IM_COL32(
                0,
                150,
                255,
                230
            ),
            2.0f
        );

        drawList->AddLine(
            extendedDestinationBottomLeft,
            destinationTopLeft,
            IM_COL32(
                0,
                150,
                255,
                230
            ),
            2.0f
        );
    }
}

void VirtualWindowRenderer::RenderD3D11(
    ID3D11DeviceContext* context,
    ID3D11RenderTargetView* renderTarget,
    ID3D11ShaderResourceView* texture,

    const ImVec2& topLeft,
    const ImVec2& topRight,
    const ImVec2& bottomRight,
    const ImVec2& bottomLeft
)
{
    if (!context)
        return;

    if (!renderTarget)
        return;

    if (!texture)
        return;

    // ---------------------------------------------------------
    // Device
    // ---------------------------------------------------------

    ID3D11Device* device = nullptr;

    context->GetDevice(
        &device
    );

    if (!device)
        return;

    // ---------------------------------------------------------
    // Initialize D3D11 resources
    // ---------------------------------------------------------

    if (!m_d3dVertexShader ||
        !m_d3dPixelShader ||
        !m_d3dInputLayout ||
        !m_d3dSamplerState ||
        !m_d3dBlendState)
    {
        if (!InitializeD3D11Resources(device))
        {
            device->Release();
            return;
        }
    }

    // ---------------------------------------------------------
    // Grid
    // ---------------------------------------------------------

    constexpr int gridX = 64;
    constexpr int gridY = 36;

    constexpr int vertexCount =
        (gridX + 1) *
        (gridY + 1);

    constexpr int indexCount =
        gridX *
        gridY *
        6;

    std::vector<D3D11Vertex> vertices(
        vertexCount
    );

    std::vector<uint32_t> indices(
        indexCount
    );

    // ---------------------------------------------------------
    // Calculate homography
    //
    // Source:
    //
    //   (0,0) -------- (1,0)
    //      |              |
    //      |              |
    //   (0,1) -------- (1,1)
    //
    // Destination:
    //
    //   topLeft -------- topRight
    //      |                 |
    //      |                 |
    //   bottomLeft ---- bottomRight
    // ---------------------------------------------------------

    const ImVec2 source[4] =
    {
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImVec2(0.0f, 1.0f)
    };

    const ImVec2 destination[4] =
    {
        topLeft,
        topRight,
        bottomRight,
        bottomLeft
    };

    const Homography H =
        CalculateHomography(
            source,
            destination
        );

    if (!H.valid)
    {
        device->Release();
        return;
    }

    // ---------------------------------------------------------
    // Pixel -> NDC
    //
    // Virtual camera:
    //
    //     1920 x 1080
    //
    // D3D11 NDC:
    //
    //     X: -1 ... +1
    //     Y: +1 ... -1
    //
    // ImGui coordinates:
    //
    //     X: 0 ... 1920
    //     Y: 0 ... 1080
    // ---------------------------------------------------------

    constexpr float frameWidth =
        1920.0f;

    constexpr float frameHeight =
        1080.0f;

    auto PixelToNDC =
        [](const ImVec2& point)
        {
            return ImVec2(
                (point.x / frameWidth) * 2.0f - 1.0f,
                1.0f - (point.y / frameHeight) * 2.0f
            );
        };

    // ---------------------------------------------------------
    // Build vertices
    // ---------------------------------------------------------

    int vertexIndex = 0;

    for (int y = 0; y <= gridY; ++y)
    {
        const float v =
            static_cast<float>(y) /
            static_cast<float>(gridY);

        for (int x = 0; x <= gridX; ++x)
        {
            const float u =
                static_cast<float>(x) /
                static_cast<float>(gridX);

            // -------------------------------------------------
            // Project through the same homography used by
            // VirtualWindowRenderer::Render()
            // -------------------------------------------------

            const ImVec2 position =
                H.Project(
                    u,
                    v
                );

            const ImVec2 ndc =
                PixelToNDC(
                    position
                );

            D3D11Vertex& vertex =
                vertices[vertexIndex++];

            vertex.x =
                ndc.x;

            vertex.y =
                ndc.y;

            vertex.u =
                u;

            vertex.v =
                v;
        }
    }

    // ---------------------------------------------------------
    // Build indices
    // ---------------------------------------------------------

    int index = 0;

    for (int y = 0; y < gridY; ++y)
    {
        for (int x = 0; x < gridX; ++x)
        {
            const uint32_t i00 =
                static_cast<uint32_t>(
                    y * (gridX + 1) + x
                    );

            const uint32_t i10 =
                i00 + 1;

            const uint32_t i01 =
                static_cast<uint32_t>(
                    (y + 1) * (gridX + 1) + x
                    );

            const uint32_t i11 =
                i01 + 1;

            // First triangle
            indices[index++] = i00;
            indices[index++] = i10;
            indices[index++] = i11;

            // Second triangle
            indices[index++] = i00;
            indices[index++] = i11;
            indices[index++] = i01;
        }
    }

    // ---------------------------------------------------------
    // Vertex buffer
    // ---------------------------------------------------------

    const UINT vertexBufferSize =
        static_cast<UINT>(
            sizeof(D3D11Vertex) *
            vertexCount
            );

    if (!m_d3dVertexBuffer)
    {
        D3D11_BUFFER_DESC description{};

        description.Usage =
            D3D11_USAGE_DYNAMIC;

        description.ByteWidth =
            vertexBufferSize;

        description.BindFlags =
            D3D11_BIND_VERTEX_BUFFER;

        description.CPUAccessFlags =
            D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA data{};

        data.pSysMem =
            vertices.data();

        HRESULT hr =
            device->CreateBuffer(
                &description,
                &data,
                &m_d3dVertexBuffer
            );

        if (FAILED(hr))
        {
            Logger::Log(
                "[VirtualWindowRenderer] Failed to create D3D11 vertex buffer. HRESULT: 0x%08X\n",
                static_cast<unsigned int>(hr)
            );

            device->Release();
            return;
        }
    }
    else
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        HRESULT hr =
            context->Map(
                m_d3dVertexBuffer.Get(),
                0,
                D3D11_MAP_WRITE_DISCARD,
                0,
                &mapped
            );

        if (FAILED(hr))
        {
            Logger::Log(
                "[VirtualWindowRenderer] Failed to map D3D11 vertex buffer. HRESULT: 0x%08X\n",
                static_cast<unsigned int>(hr)
            );

            device->Release();
            return;
        }

        std::memcpy(
            mapped.pData,
            vertices.data(),
            vertexBufferSize
        );

        context->Unmap(
            m_d3dVertexBuffer.Get(),
            0
        );
    }

    // ---------------------------------------------------------
    // Index buffer
    // ---------------------------------------------------------

    if (!m_d3dIndexBuffer)
    {
        const UINT indexBufferSize =
            static_cast<UINT>(
                sizeof(uint32_t) *
                indexCount
                );

        D3D11_BUFFER_DESC description{};

        description.Usage =
            D3D11_USAGE_DEFAULT;

        description.ByteWidth =
            indexBufferSize;

        description.BindFlags =
            D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA data{};

        data.pSysMem =
            indices.data();

        HRESULT hr =
            device->CreateBuffer(
                &description,
                &data,
                &m_d3dIndexBuffer
            );

        if (FAILED(hr))
        {
            Logger::Log(
                "[VirtualWindowRenderer] Failed to create D3D11 index buffer. HRESULT: 0x%08X\n",
                static_cast<unsigned int>(hr)
            );

            device->Release();
            return;
        }
    }

    // ---------------------------------------------------------
    // Viewport
    // ---------------------------------------------------------

    D3D11_VIEWPORT viewport{};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    viewport.Width =
        frameWidth;

    viewport.Height =
        frameHeight;

    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    context->RSSetViewports(
        1,
        &viewport
    );

    // ---------------------------------------------------------
    // Render target
    // ---------------------------------------------------------

    context->OMSetRenderTargets(
        1,
        &renderTarget,
        nullptr
    );

    // ---------------------------------------------------------
    // Alpha blending
    //
    // Visualizer texture:
    //
    //     RGB * alpha
    //     + camera * (1 - alpha)
    //
    // This allows the transparent parts of the visualizer
    // texture to leave the camera visible underneath.
    // ---------------------------------------------------------

    const float blendFactor[4] =
    {
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };

    context->OMSetBlendState(
        m_d3dBlendState.Get(),
        blendFactor,
        0xFFFFFFFF
    );

    // ---------------------------------------------------------
    // Vertex buffer
    // ---------------------------------------------------------

    UINT stride =
        sizeof(D3D11Vertex);

    UINT offset = 0;

    ID3D11Buffer* vertexBuffer =
        m_d3dVertexBuffer.Get();

    context->IASetVertexBuffers(
        0,
        1,
        &vertexBuffer,
        &stride,
        &offset
    );

    // ---------------------------------------------------------
    // Index buffer
    // ---------------------------------------------------------

    context->IASetIndexBuffer(
        m_d3dIndexBuffer.Get(),
        DXGI_FORMAT_R32_UINT,
        0
    );

    // ---------------------------------------------------------
    // Input layout
    // ---------------------------------------------------------

    context->IASetInputLayout(
        m_d3dInputLayout.Get()
    );

    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
    );

    // ---------------------------------------------------------
    // Shaders
    // ---------------------------------------------------------

    context->VSSetShader(
        m_d3dVertexShader.Get(),
        nullptr,
        0
    );

    context->PSSetShader(
        m_d3dPixelShader.Get(),
        nullptr,
        0
    );

    // ---------------------------------------------------------
    // Texture
    // ---------------------------------------------------------

    context->PSSetShaderResources(
        0,
        1,
        &texture
    );

    ID3D11SamplerState* sampler =
        m_d3dSamplerState.Get();

    context->PSSetSamplers(
        0,
        1,
        &sampler
    );

    // ---------------------------------------------------------
    // Draw
    // ---------------------------------------------------------

    context->DrawIndexed(
        indexCount,
        0,
        0
    );

    // ---------------------------------------------------------
    // Unbind texture
    // ---------------------------------------------------------

    ID3D11ShaderResourceView* nullSRV =
        nullptr;

    context->PSSetShaderResources(
        0,
        1,
        &nullSRV
    );

    // ---------------------------------------------------------
    // Restore blend state
    // ---------------------------------------------------------

    context->OMSetBlendState(
        nullptr,
        nullptr,
        0xFFFFFFFF
    );

    // ---------------------------------------------------------
    // Release device
    // ---------------------------------------------------------

    device->Release();
}