struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer ParticlesBlurPixelConstants : register(b0) {
    float2 inverseScreenSize;
    float3 backgroundColor;
    float attenuationFactor;
    float time;
};

Texture2D screenTexture : register(t0);
SamplerState screenSampler : register(s0);

//float4 blur(float2 uv,float vert) {
//    float4 color = 0.2270270270 * screenTexture.Sample(screenSampler,uv);
//    float2 pixelOffset = vert > 0.5 ? float2(0.0,inverseScreenSize.y) : float2(inverseScreenSize.x,0.0);
//    float2 texCoordOffset0 = 1.3846153846 * pixelOffset;
//    float4 col0 = screenTexture.Sample(screenSampler,uv + texCoordOffset0) + screenTexture.Sample(screenSampler,uv - texCoordOffset0);
//    color += 0.3162162162 * col0;
//    float2 texCoordOffset1 = 3.2307692308 * pixelOffset;
//    float4 col1 = screenTexture.Sample(screenSampler,uv + texCoordOffset1) + screenTexture.Sample(screenSampler,uv - texCoordOffset1);
//    color += 0.0702702703 * col1;
//    return color;
//}

float4 blur(float2 uv, float vert)
{
    float2 pixelOffset =
        vert > 0.5
        ? float2(0.0, inverseScreenSize.y)
        : float2(inverseScreenSize.x, 0.0);

    float4 color = 0.0;

    color += screenTexture.Sample(screenSampler, uv) * 0.25;
    color += screenTexture.Sample(screenSampler, uv + pixelOffset * 5.0) * 0.20;
    color += screenTexture.Sample(screenSampler, uv - pixelOffset * 5.0) * 0.20;
    color += screenTexture.Sample(screenSampler, uv + pixelOffset * 10.0) * 0.175;
    color += screenTexture.Sample(screenSampler, uv - pixelOffset * 10.0) * 0.175;

    return color;
}

float4 main(PSInput input) : SV_Target {
    float4 color = blur(input.uv,time > 0.5 ? 1.0 : 0.0);
    return lerp(float4(backgroundColor,0.0),color,attenuationFactor);
}
