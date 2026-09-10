#define SETS_COUNT 12
#define MAJOR_COUNT 75

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer MajorKeysPixelConstants : register(b0) {
    float notesCount;
    int minNoteMajor;
    int highlightKeys;
    int horizontalMode;
    float2 inverseScreenSize;
    float3 edgeColor;
    float3 keyColor;
    float4 majorColor[SETS_COUNT];
    int4 actives[32];
};

static const int majorIds[MAJOR_COUNT] = { 0,2,4,5,7,9,11,12,14,16,17,19,21,23,24,26,28,29,31,33,35,36,38,40,41,43,45,47,48,50,52,53,55,57,59,60,62,64,65,67,69,71,72,74,76,77,79,81,83,84,86,88,89,91,93,95,96,98,100,101,103,105,107,108,110,112,113,115,117,119,120,122,124,125,127 };

float4 main(PSInput input) : SV_Target {
    float widthScaling = horizontalMode != 0 ? inverseScreenSize.y : inverseScreenSize.x;
    float majorUvX = frac(input.uv.x * notesCount);
    float centerIntensity = 1.0 - step(1.0 - 2.0 * notesCount * widthScaling,abs(majorUvX * 2.0 - 1.0));
    int index = clamp((int)(input.uv.x * notesCount) + minNoteMajor,0,MAJOR_COUNT - 1);
    int majorId = majorIds[index];
    int cidMajor = actives[majorId / 4][majorId % 4];
    float3 frontColor = (highlightKeys != 0 && cidMajor >= 0 && cidMajor < SETS_COUNT) ? majorColor[cidMajor].rgb : keyColor;
    float4 fragColor;
    fragColor.rgb = lerp(edgeColor,frontColor,centerIntensity);
    fragColor.a = 1.0;
    return fragColor;
}
