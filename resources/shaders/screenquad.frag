Texture2D screenTexture : register(t0);
SamplerState screenSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return screenTexture.Sample(screenSampler, input.uv);
}
