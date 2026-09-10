#define SETS_COUNT 12

struct PSInput {
    float4 position : SV_Position;
};

cbuffer FlashesPixelConstants : register(b0) {
    float time;
    float2 inverseScreenSize;
    float userScale;
    float keyboardHeight;
    int minNote;
    float notesCount;
    int globalId;
    int horizontalMode;
    float4 baseColor[SETS_COUNT];
    float haloIntensity;
    float haloInnerRadius;
    float haloOuterRadius;
    int texRowCount;
    int texColCount;
    float flashAlpha;
    int colorId;
};

static const float shifts[128] = { 0,0.5,1,1.5,2,3,3.5,4,4.5,5,5.5,6,7,7.5,8,8.5,9,10,10.5,11,11.5,12,12.5,13,14,14.5,15,15.5,16,17,17.5,18,18.5,19,19.5,20,21,21.5,22,22.5,23,24,24.5,25,25.5,26,26.5,27,28,28.5,29,29.5,30,31,31.5,32,32.5,33,33.5,34,35,35.5,36,36.5,37,38,38.5,39,39.5,40,40.5,41,42,42.5,43,43.5,44,45,45.5,46,46.5,47,47.5,48,49,49.5,50,50.5,51,52,52.5,53,53.5,54,54.5,55,56,56.5,57,57.5,58,59,59.5,60,60.5,61,61.5,62,63,63.5,64,64.5,65,66,66.5,67,67.5,68,68.5,69,70,70.5,71,71.5,72,73,73.5,74};

static const float2 flashScale = 0.9 * float2(3.5,3.0);

Texture2D textureFlash : register(t0);
SamplerState flashSampler : register(s0);

float rand(float2 co) {
    return frac(sin(dot(co,float2(12.9898,78.233))) * 43758.5453);
}

float2 reconstructV(float2 screenPosition) {
    float screenRatio = inverseScreenSize.y / inverseScreenSize.x;
    float2 scalingFactor = float2(1.0,horizontalMode != 0 ? 1.0 / screenRatio : screenRatio);

    float2 ndc;
    ndc.x = screenPosition.x * 2.0 * inverseScreenSize.x - 1.0;
    ndc.y = 1.0 - screenPosition.y * 2.0 * inverseScreenSize.y;

    float2 position = horizontalMode != 0 ? float2(-ndc.y,ndc.x) : ndc;

    float2 scaledFactor = 2.0 * flashScale * userScale / notesCount * scalingFactor;

    float2 globalShift = float2(
        -1.0 + ((shifts[globalId] - shifts[minNote]) * 2.0 + 1.0) / notesCount,
        2.0 * keyboardHeight - 1.0
    );

    return (position - globalShift) / scaledFactor;
}

float4 main(PSInput input) : SV_Target {
    int cid = colorId;
    float mask = 0.0;
    const float atlasSpeed = 15.0;
    const float safetyMargin = 0.05;

    float2 uv = reconstructV(input.position.xy);

    if (uv.y > 0.0) {
        int atlasShift = (int)(floor(fmod(atlasSpeed * time,(float)(texRowCount * texColCount))) + floor(rand(globalId * float2(time,1.0))));
        int2 atlasIndex = int2(atlasShift % texColCount,atlasShift / texColCount);
        float2 localUV = clamp(uv * float2(1.0,2.0) + float2(0.5,0.0),safetyMargin,1.0 - safetyMargin);
        float2 finalUV = (float2(atlasIndex) + localUV) / float2((float)texColCount,(float)texRowCount);
        mask = textureFlash.Sample(flashSampler,finalUV).r;
    }

    if (cid < 0 || cid >= SETS_COUNT)
        discard;

    float4 spriteColor = float4(baseColor[cid].rgb,mask * flashAlpha);
    float haloAlpha = 1.0 - smoothstep(haloInnerRadius,haloOuterRadius,length(uv) / 0.5);
    float4 haloColor;
    haloColor.rgb = baseColor[cid].rgb + haloIntensity * float3(1.0,1.0,1.0);
    haloColor.a = haloAlpha * 0.92 * flashAlpha;

    float4 fragColor = lerp(spriteColor,haloColor,haloColor.a);
    fragColor *= 1.1;
    fragColor.rgb *= fragColor.a;

    return fragColor;
}
