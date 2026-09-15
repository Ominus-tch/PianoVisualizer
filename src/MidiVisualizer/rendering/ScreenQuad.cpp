#include "ScreenQuad.h"

#include <iostream>

ScreenQuad::ScreenQuad()
{
}

ScreenQuad::~ScreenQuad()
{
    clean();
}

void ScreenQuad::init(
    ID3D11Device* device,
    ID3D11ShaderResourceView* texture,
    const std::string& fragName,
    const std::string& vertName,
    bool verbose,
    ShaderProgram::InputLayoutType layoutType
)
{
    init(device, fragName, vertName, verbose, layoutType);

    _texture = texture;
}

void ScreenQuad::init(
    ID3D11Device* device,
    const std::string& fragName,
    const std::string& vertName,
    bool verbose,
    ShaderProgram::InputLayoutType layoutType
)
{
    _verbose = verbose;
    _program.init(
        device,
        vertName,
        fragName,
        layoutType,
        verbose
    );

    // ------------------------------------------------------------
    // Full-screen quad
    // ------------------------------------------------------------

    const Vertex vertices[] =
    {
        { {-1.0f, -1.0f, 0.0f} },
        { { 1.0f, -1.0f, 0.0f} },
        { {-1.0f,  1.0f, 0.0f} },
        { { 1.0f,  1.0f, 0.0f} }
    };

    const UINT indices[] =
    {
        0, 1, 2,
        2, 1, 3
    };

    _indexCount = 6;

    // ------------------------------------------------------------
    // Vertex buffer
    // ------------------------------------------------------------

    D3D11_BUFFER_DESC vertexDesc{};
    vertexDesc.ByteWidth = sizeof(vertices);
    vertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vertexData{};
    vertexData.pSysMem = vertices;

    HRESULT hr = device->CreateBuffer(
        &vertexDesc,
        &vertexData,
        &_vertexBuffer
    );

    if (FAILED(hr))
    {
        std::cout
            << "[D3D11]: Failed to create ScreenQuad vertex buffer."
            << std::endl;

        return;
    }

    // ------------------------------------------------------------
    // Index buffer
    // ------------------------------------------------------------

    D3D11_BUFFER_DESC indexDesc{};
    indexDesc.ByteWidth = sizeof(indices);
    indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA indexData{};
    indexData.pSysMem = indices;

    hr = device->CreateBuffer(
        &indexDesc,
        &indexData,
        &_indexBuffer
    );

    if (FAILED(hr))
    {
        std::cout
            << "[D3D11]: Failed to create ScreenQuad index buffer."
            << std::endl;

        return;
    }
}

// ------------------------------------------------------------
// Draw
// ------------------------------------------------------------

void ScreenQuad::draw(
    ID3D11DeviceContext* context,
    float time
)
{
    draw(context, _texture, time);
}

void ScreenQuad::draw(
    ID3D11DeviceContext* context,
    float time,
    const glm::vec2& invScreenSize
)
{
    draw(context, _texture, time, invScreenSize);
}

//void ScreenQuad::draw(
//    ID3D11DeviceContext* context,
//    ID3D11ShaderResourceView* texture,
//    float time
//)
//{
//    _program.use(context);
//
//    // Uniforms.
//    _program.uniform("time", time);
//
//    // Texture.
//    if (texture)
//    {
//        _program.texture(
//            context,
//            "screenTexture",
//            texture
//        );
//    }
//
//    // Input layout.
//    _program.bindInputLayout(context);
//
//    // Vertex buffer.
//    const UINT stride = sizeof(Vertex);
//    const UINT offset = 0;
//
//    ID3D11Buffer* vertexBuffer = _vertexBuffer.Get();
//
//    context->IASetVertexBuffers(
//        0,
//        1,
//        &vertexBuffer,
//        &stride,
//        &offset
//    );
//
//    // Index buffer.
//    context->IASetIndexBuffer(
//        _indexBuffer.Get(),
//        DXGI_FORMAT_R32_UINT,
//        0
//    );
//
//    // Primitive type.
//    context->IASetPrimitiveTopology(
//        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
//    );
//
//    // Draw.
//    context->DrawIndexed(
//        _indexCount,
//        0,
//        0
//    );
//}

void ScreenQuad::draw(
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* texture,
    float time
)
{
    if (!context)
        return;

    _program.use(context);

    if (texture)
    {
        _program.texture(
            context,
            "screenTexture",
            texture
        );
    }

    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;

    context->IASetVertexBuffers(
        0,
        1,
        _vertexBuffer.GetAddressOf(),
        &stride,
        &offset
    );

    context->IASetIndexBuffer(
        _indexBuffer.Get(),
        DXGI_FORMAT_R32_UINT,
        0
    );

    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
    );

    context->DrawIndexed(
        _indexCount,
        0,
        0
    );
}

void ScreenQuad::draw(
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* texture,
    float time,
    const glm::vec2& invScreenSize
)
{
    if (!context)
        return;

    _program.use(context);

    _program.uniform(
        "inverseScreenSize",
        invScreenSize
    );

    _program.uniform(
        "time",
        time
    );

    if (texture)
    {
        _program.texture(
            context,
            "screenTexture",
            texture
        );
    }

    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;

    context->IASetVertexBuffers(
        0,
        1,
        _vertexBuffer.GetAddressOf(),
        &stride,
        &offset
    );

    context->IASetIndexBuffer(
        _indexBuffer.Get(),
        DXGI_FORMAT_R32_UINT,
        0
    );

    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
    );

    context->DrawIndexed(
        _indexCount,
        0,
        0
    );
}

// ------------------------------------------------------------
// Cleanup
// ------------------------------------------------------------

void ScreenQuad::clean()
{
    _vertexBuffer.Reset();
    _indexBuffer.Reset();

    _texture = nullptr;
}