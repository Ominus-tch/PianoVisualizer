#define SETS_COUNT 12

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float id : TEXCOORD1;
};

cbuffer MinorKeysPixelConstants : register(b0) {
    int highlightKeys;
    int horizontalMode;
    float keyboardHeight;
    float notesCount;
    int minNoteMajor;
    float2 inverseScreenSize;
    float3 edgeColor;
    float4 minorColor[SETS_COUNT];
    int edgeOnMinors;
    float minorsHeight;
    float minorsWidth;
};

float4 main(PSInput input) : SV_Target {
    float2 ndc = 2.0 * input.uv - 1.0;
    float widthScaling = horizontalMode != 0 ? inverseScreenSize.y : inverseScreenSize.x;
    float heightScaling = horizontalMode != 0 ? inverseScreenSize.x : inverseScreenSize.y;
    float uvWidth = minorsWidth / notesCount * 0.5;
    float uvHeight = minorsHeight * keyboardHeight * 0.5;
    float xStep = step(1.0 - 2.0 * widthScaling / uvWidth,abs(ndc.x));
    float yStep = step(1.0 - 2.0 * heightScaling / uvHeight,-ndc.y);
    float centerIntensity = edgeOnMinors != 0 ? (1.0 - xStep) * (1.0 - yStep) : 1.0;
    int cidMinor = (int)input.id;
    float3 frontColor = (highlightKeys != 0 && cidMinor >= 0 && cidMinor < SETS_COUNT) ? minorColor[cidMinor].rgb : edgeColor;
    float4 fragColor;
    fragColor.rgb = lerp(edgeColor,frontColor,centerIntensity);
    fragColor.a = 1.0;
    return fragColor;
}
