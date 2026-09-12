struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer BackgroundTextureVSConstants : register(b0)
{
    int behindKeyboard;
    float keyboardHeight;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput output;

    float2 positions[6] =
    {
        // Triangle 1
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),

        // Triangle 2
        float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f,  1.0f)
    };

    float2 uvs[6] =
    {
        // Triangle 1
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),

        // Triangle 2
        float2(1.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f)
    };

    float2 pos = positions[vertexID];
    if (behindKeyboard != 0) {
        pos.y = (1.0 - keyboardHeight) * pos.y + keyboardHeight;
    }

    output.position = float4(
        positions[vertexID],
        0.0f,
        1.0f
    );

    output.uv = uvs[vertexID];

    return output;
}