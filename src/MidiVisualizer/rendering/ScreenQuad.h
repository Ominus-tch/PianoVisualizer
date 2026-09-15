#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <glm/glm.hpp>
#include <string>

#include "../helpers/ProgramUtilities.h"

using Microsoft::WRL::ComPtr;

class ScreenQuad
{
public:

    ScreenQuad();
    ~ScreenQuad();

    void init(
        ID3D11Device* device,
        ID3D11ShaderResourceView* texture,
        const std::string& fragName,
        const std::string& vertName = "screenquad_vert",
        bool verbose = false,
        ShaderProgram::InputLayoutType layoutType = ShaderProgram::InputLayoutType::Position3D
    );

    void init(
        ID3D11Device* device,
        const std::string& fragName,
        const std::string& vertName = "screenquad_vert",
        bool verbose = false,
        ShaderProgram::InputLayoutType layoutType = ShaderProgram::InputLayoutType::Position3D
    );

    void draw(
        ID3D11DeviceContext* context,
        float time,
        const glm::vec2& invScreenSize
    );

    void draw(
        ID3D11DeviceContext* context,
        float time
    );

    void draw(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* texture,
        float time,
        const glm::vec2& invScreenSize
    );

    void draw(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* texture,
        float time
    );

    void clean();

    ShaderProgram& program()
    {
        return _program;
    }

private:

    struct Vertex
    {
        glm::vec3 position;
    };

    ShaderProgram _program;

    ComPtr<ID3D11Buffer> _vertexBuffer;
    ComPtr<ID3D11Buffer> _indexBuffer;

    ID3D11ShaderResourceView* _texture = nullptr;
    ID3D11SamplerState* _screenSampler = nullptr;

    UINT _indexCount = 0;

    bool _verbose = false;
};