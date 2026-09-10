struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Texture2D screenTexture : register(t0);
SamplerState screenTextureSampler : register(s0);

cbuffer BackgroundTexturePSConstants : register(b0)
{
    float textureAlpha;
    float2 scroll;
    int behindKeyboard;
};

float4 main(PSInput In) : SV_Target
{
    float4 fragColor =
        screenTexture.Sample(
            screenTextureSampler,
            In.uv + scroll
        );

    fragColor.a *= textureAlpha;

    return fragColor;
}
