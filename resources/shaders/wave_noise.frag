struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float grad : TEXCOORD1;
};

cbuffer WaveNoisePixelConstants : register(b0) {
    float3 waveColor;
    float waveOpacity;
};

Texture2D textureNoise : register(t0);
SamplerState noiseSampler : register(s0);

float4 main(PSInput input) : SV_Target {
    float2 uv = input.uv;
    float noise = textureNoise.Sample(noiseSampler,uv).r;
    return noise * waveOpacity * float4(waveColor,1.0) * input.grad * 4.0;
}
