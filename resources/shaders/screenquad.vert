struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput output;

    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };

    float2 uvs[3] = {
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f),
        float2(2.0f, 1.0f)
    };

    output.position = float4(positions[vertexID], 0.0f, 1.0f);
    output.uv = uvs[vertexID];

    return output;
}
