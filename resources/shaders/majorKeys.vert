struct VSInput {
    float2 v : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer MajorKeysVertexConstants : register(b0) {
    float keyboardHeight;
    int horizontalMode;
};

float2 flipIfNeeded(float2 inPos) {
    return horizontalMode != 0 ? float2(inPos.y,-inPos.x) : inPos;
}

VSOutput main(VSInput input) {
    VSOutput output;
    float yShift = keyboardHeight * (2.0 * input.v.y + 1.0) - 1.0;
    float2 pos2D = float2(input.v.x * 2.0,yShift);
    output.position = float4(flipIfNeeded(pos2D),0.0,1.0);
    output.uv = input.v + 0.5;
    return output;
}
