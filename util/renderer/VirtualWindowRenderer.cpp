#include "VirtualWindowRenderer.h"

#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>

#include <d3dcompiler.h>

#include "../Logger.h"

// =============================================================
// Small 3x3 matrix solver
//
// Solves:
//
//     A * x = b
//
// using Gaussian elimination.
// =============================================================

static bool Solve8x8(
    double A[8][8],
    double b[8],
    double x[8]
)
{
    for (int i = 0; i < 8; ++i)
    {
        // -----------------------------------------------------
        // Find pivot
        // -----------------------------------------------------

        int pivot = i;

        double maxValue =
            std::abs(A[i][i]);

        for (int r = i + 1; r < 8; ++r)
        {
            double value =
                std::abs(A[r][i]);

            if (value > maxValue)
            {
                maxValue = value;
                pivot = r;
            }
        }

        if (maxValue < 1e-12)
            return false;


        // -----------------------------------------------------
        // Swap rows
        // -----------------------------------------------------

        if (pivot != i)
        {
            for (int c = i; c < 8; ++c)
            {
                std::swap(
                    A[i][c],
                    A[pivot][c]
                );
            }

            std::swap(
                b[i],
                b[pivot]
            );
        }


        // -----------------------------------------------------
        // Normalize pivot row
        // -----------------------------------------------------

        const double divisor =
            A[i][i];

        for (int c = i; c < 8; ++c)
        {
            A[i][c] /= divisor;
        }

        b[i] /= divisor;


        // -----------------------------------------------------
        // Eliminate column
        // -----------------------------------------------------

        for (int r = 0; r < 8; ++r)
        {
            if (r == i)
                continue;

            const double factor =
                A[r][i];

            if (std::abs(factor) < 1e-12)
                continue;

            for (int c = i; c < 8; ++c)
            {
                A[r][c] -=
                    factor * A[i][c];
            }

            b[r] -=
                factor * b[i];
        }
    }


    for (int i = 0; i < 8; ++i)
    {
        x[i] = b[i];
    }

    return true;
}


// =============================================================
// HOMOGRAPHY
//
// Maps:
//
//     (u,v)
//
// to:
//
//     (screenX, screenY)
//
//
//
// Source:
//
//     (0,0) ---------------- (1,0)
//       |                       |
//       |                       |
//       |                       |
//     (0,1) ---------------- (1,1)
//
// Destination:
//
//     topLeft ---------------- topRight
//       |                         |
//       |                         |
//       |                         |
//     P1/bottomLeft -------- P4/bottomRight
//
// =============================================================

struct Homography
{
    double h11;
    double h12;
    double h13;

    double h21;
    double h22;
    double h23;

    double h31;
    double h32;

    bool valid = false;


    ImVec2 Project(
        float u,
        float v
    ) const
    {
        const double denominator =
            h31 * u +
            h32 * v +
            1.0;

        if (std::abs(denominator) < 1e-12)
        {
            return ImVec2(
                0.0f,
                0.0f
            );
        }


        const double x =
            (
                h11 * u +
                h12 * v +
                h13
                ) / denominator;


        const double y =
            (
                h21 * u +
                h22 * v +
                h23
                ) / denominator;


        return ImVec2(
            static_cast<float>(x),
            static_cast<float>(y)
        );
    }
};


// =============================================================
// CALCULATE HOMOGRAPHY
// =============================================================

static Homography CalculateHomography(
    const ImVec2 src[4],
    const ImVec2 dst[4]
)
{
    Homography result{};


    double A[8][8] = {};
    double b[8] = {};


    for (int i = 0; i < 4; ++i)
    {
        const double x =
            src[i].x;

        const double y =
            src[i].y;

        const double X =
            dst[i].x;

        const double Y =
            dst[i].y;


        const int row =
            i * 2;


        // -----------------------------------------------------
        // X equation
        // -----------------------------------------------------

        A[row][0] = x;
        A[row][1] = y;
        A[row][2] = 1.0;

        A[row][3] = 0.0;
        A[row][4] = 0.0;
        A[row][5] = 0.0;

        A[row][6] =
            -X * x;

        A[row][7] =
            -X * y;

        b[row] = X;


        // -----------------------------------------------------
        // Y equation
        // -----------------------------------------------------

        A[row + 1][0] = 0.0;
        A[row + 1][1] = 0.0;
        A[row + 1][2] = 0.0;

        A[row + 1][3] = x;
        A[row + 1][4] = y;
        A[row + 1][5] = 1.0;

        A[row + 1][6] =
            -Y * x;

        A[row + 1][7] =
            -Y * y;

        b[row + 1] = Y;
    }


    double x[8] = {};


    if (!Solve8x8(
        A,
        b,
        x
    ))
    {
        return result;
    }


    result.h11 = x[0];
    result.h12 = x[1];
    result.h13 = x[2];

    result.h21 = x[3];
    result.h22 = x[4];
    result.h23 = x[5];

    result.h31 = x[6];
    result.h32 = x[7];

    result.valid = true;


    return result;
}

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
    if (!drawList)
        return;

    if (!texture)
        return;


    const int gridX =
        settings.gridX;

    const int gridY =
        settings.gridY;


    if (gridX < 1 ||
        gridY < 1)
    {
        return;
    }


    // =========================================================
    // SOURCE RECTANGLE
    //
    // This is the captured window.
    //
    // topLeft     = (0,0)
    // topRight    = (1,0)
    // bottomRight = (1,1)
    // bottomLeft  = (0,1)
    // =========================================================

    const ImVec2 source[4] =
    {
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImVec2(0.0f, 1.0f)
    };


    // =========================================================
    // DESTINATION
    //
    // IMPORTANT:
    //
    // Your physical keyboard is:
    //
    //     P1 ---------------- P4
    //      |                    |
    //      |                    |
    //     P2 ---------------- P3
    //
    //
    // But the virtual window has:
    //
    //     topLeft -------- topRight
    //        |                 |
    //        |                 |
    //     P1 ---------------- P4
    //
    // Therefore:
    //
    // bottomLeft  = P1
    // bottomRight = P4
    //
    // exactly as you described.
    // =========================================================

    const ImVec2 destination[4] =
    {
        topLeft,
        topRight,
        bottomRight,
        bottomLeft
    };


    // =========================================================
    // CALCULATE PROJECTIVE TRANSFORMATION
    // =========================================================

    const Homography H =
        CalculateHomography(
            source,
            destination
        );


    if (!H.valid)
        return;


    // =========================================================
    // DRAW PERSPECTIVE-WARPED TEXTURE
    // =========================================================

    for (int y = 0; y < gridY; ++y)
    {
        for (int x = 0; x < gridX; ++x)
        {
            const float u0 =
                static_cast<float>(x) /
                static_cast<float>(gridX);

            const float u1 =
                static_cast<float>(x + 1) /
                static_cast<float>(gridX);


            const float v0 =
                static_cast<float>(y) /
                static_cast<float>(gridY);

            const float v1 =
                static_cast<float>(y + 1) /
                static_cast<float>(gridY);


            // -------------------------------------------------
            // PROJECT EACH CORNER
            // -------------------------------------------------

            const ImVec2 p00 =
                H.Project(
                    u0,
                    v0
                );


            const ImVec2 p10 =
                H.Project(
                    u1,
                    v0
                );


            const ImVec2 p11 =
                H.Project(
                    u1,
                    v1
                );


            const ImVec2 p01 =
                H.Project(
                    u0,
                    v1
                );


            // -------------------------------------------------
            // UVs
            //
            // No flipping.
            //
            // Source and destination have the same orientation.
            // -------------------------------------------------

            const ImVec2 uv00(
                u0,
                v0
            );

            const ImVec2 uv10(
                u1,
                v0
            );

            const ImVec2 uv11(
                u1,
                v1
            );

            const ImVec2 uv01(
                u0,
                v1
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

                uv00,
                uv10,
                uv11,
                uv01,

                settings.tint
            );
        }
    }

    if (settings.drawDebugLines) {
        // =========================================================
        // DEBUG OUTLINE
        // =========================================================

        drawList->AddLine(
            topLeft,
            topRight,
            IM_COL32(
                0,
                150,
                255,
                230
            ),
            2.0f
        );


        drawList->AddLine(
            topRight,
            bottomRight,
            IM_COL32(
                0,
                150,
                255,
                230
            ),
            2.0f
        );


        drawList->AddLine(
            bottomRight,
            bottomLeft,
            IM_COL32(
                0,
                150,
                255,
                230
            ),
            2.0f
        );


        drawList->AddLine(
            bottomLeft,
            topLeft,
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