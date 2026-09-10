struct PSInput {
    float4 position : SV_Position;
    float grad : TEXCOORD0;
};

cbuffer WavePixelConstants : register(b0) {
    float3 waveColor;
    float waveOpacity;
};

float4 main(PSInput input) : SV_Target {
    float intensity = 1.0 - abs(0.0);
    return waveOpacity * intensity * float4(waveColor,1.0);
}
