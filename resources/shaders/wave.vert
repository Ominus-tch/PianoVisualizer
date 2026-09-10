struct VSInput {
    float2 v : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
    float grad : TEXCOORD0;
};

cbuffer WaveVertexConstants : register(b0) {
    float amplitude;
    float keyboardSize;
    float freq;
    float phase;
    float spread;
    int horizontalMode;
};

float2 flipIfNeeded(float2 inPos) {
    return horizontalMode != 0 ? float2(inPos.y,-inPos.x) : inPos;
}

VSOutput main(VSInput input) {
    VSOutput output;

    float2 pos = float2(1.0,spread * 0.02) * input.v.xy;
    float waveShift = amplitude * sin(freq * input.v.x + phase);
    pos += float2(0.0,waveShift + (-1.0 + 2.0 * keyboardSize));

    output.position = float4(flipIfNeeded(pos),0.5,1.0);
    output.grad = input.v.y;

    return output;
}
