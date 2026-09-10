struct VSInput {
    float3 v : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = float4(input.v,1.0);
    return output;
}
