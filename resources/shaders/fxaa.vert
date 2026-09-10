struct VSInput {
    float3 v : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;

    output.position = float4(input.v, 1.0f);
    output.uv = input.v.xy * 0.5f + 0.5f;

    return output;
}
