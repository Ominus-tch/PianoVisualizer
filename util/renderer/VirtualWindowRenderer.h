#pragma once

#include <imgui/imgui.h>
#include <d3d11.h>

#include <wrl/client.h>

#include "../camera/Homography.h"

using Microsoft::WRL::ComPtr;

class VirtualWindowRenderer
{
public:

    struct Settings
    {
        int gridX = 64;
        int gridY = 36;

        bool drawDebugLines = false;

        float sourceBottomScale = 1.0f;

        ImU32 tint =
            IM_COL32(
                255,
                255,
                255,
                255
            );
    };

    bool InitializeD3D11Resources(
        ID3D11Device* device
    );

    void InvalidateHomographyCache() {
        m_hasCachedHomography = false;
    }

    void Render(
        ImDrawList* drawList,
        ImTextureID texture,

        const ImVec2& topLeft,
        const ImVec2& topRight,
        const ImVec2& bottomRight,
        const ImVec2& bottomLeft,

        const Settings& settings
    );

    void Render(
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
    );

    void RenderD3D11(
        ID3D11DeviceContext* context,
        ID3D11RenderTargetView* renderTarget,
        ID3D11ShaderResourceView* texture,

        const ImVec2& topLeft,
        const ImVec2& topRight,
        const ImVec2& bottomRight,
        const ImVec2& bottomLeft
    );


private:

    ImVec2 Bilinear(
        const ImVec2& topLeft,
        const ImVec2& topRight,
        const ImVec2& bottomRight,
        const ImVec2& bottomLeft,

        float u,
        float v
    );

    struct D3D11Vertex
    {
        float x;
        float y;
        float u;
        float v;
    };

    ComPtr<ID3D11VertexShader> m_d3dVertexShader;
    ComPtr<ID3D11PixelShader> m_d3dPixelShader;
    ComPtr<ID3D11InputLayout> m_d3dInputLayout;
    ComPtr<ID3D11Buffer> m_d3dVertexBuffer;
    ComPtr<ID3D11Buffer> m_d3dIndexBuffer;
    ComPtr<ID3D11SamplerState> m_d3dSamplerState;
    ComPtr<ID3D11BlendState> m_d3dBlendState;

    Homography m_cachedHomography;
    ImVec2 m_cachedSource[4]{};
    ImVec2 m_cachedDestination[4]{};
    bool m_hasCachedHomography = false;
};