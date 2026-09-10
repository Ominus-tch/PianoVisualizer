struct VSInput {
    float2 v : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float grad : TEXCOORD1;
};

cbuffer WaveNoiseVertexConstants : register(b0) {
    float keyboardSize;
    float scale;
    int horizontalMode;
    float noiseScale;
    float2 inverseScreenSize;
    float offset;
};

float2 flipIfNeeded(float2 inPos) {
    return horizontalMode != 0 ? float2(inPos.y,-inPos.x) : inPos;
}

VSOutput main(VSInput input) {
    VSOutput output;

    float2 pos = 2.0 * input.v.xy * float2(1.0,2.0 * scale);
    pos.y += -1.0 + 2.0 * keyboardSize + 2.0 * scale;

    output.position = float4(flipIfNeeded(pos),0.8,1.0);

    float ratio = horizontalMode != 0
        ? (inverseScreenSize.y / inverseScreenSize.x)
        : (inverseScreenSize.x / inverseScreenSize.y);

    output.uv = (input.v.xy + 0.5) * float2(1.0,scale) * noiseScale * float2(1.0,ratio);
    output.uv.y -= offset;
    output.grad = 0.5 - input.v.y;

    return output;
}
