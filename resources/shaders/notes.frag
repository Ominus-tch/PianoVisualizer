#define SETS_COUNT 12

struct PSInput {
 float4 position : SV_Position;
 float2 uv : TEXCOORD0;
 float2 noteSize : TEXCOORD1;
 nointerpolation float2 noteCorner : TEXCOORD2;
 float isMinor : TEXCOORD3;
 float channel : TEXCOORD4;
};

cbuffer NotesPixelConstants : register(b0) {
 float4 baseColor[SETS_COUNT];
 float4 minorColor[SETS_COUNT];
 float2 inverseScreenSize;
 float time;
 float mainSpeed;
 float colorScale;
 float keyboardHeight;
 float fadeOut;
 float edgeWidth;
 float edgeBrightness;
 float cornerRadius;
 int horizontalMode;
 int reverseMode;
 int useMajorTexture;
 int useMinorTexture;
 float2 texturesScale;
 float2 texturesStrength;
 float scrollMajorTexture;
 float scrollMinorTexture;
};

Texture2D majorTexture : register(t0);
Texture2D minorTexture : register(t1);
SamplerState majorSampler : register(s0);
SamplerState minorSampler : register(s1);

float2 reconstructScreenUV(float2 screenPosition)
{
 float2 normalizedCoord =
     screenPosition * inverseScreenSize;

 normalizedCoord.y = 1.0 - normalizedCoord.y;

 return normalizedCoord;
}

float2 reconstructNoteUV(float2 screenPosition)
{
 float screenRatio =
     inverseScreenSize.y /
     inverseScreenSize.x;

 float2 ndc;
 ndc.x =
     screenPosition.x *
     2.0 * inverseScreenSize.x -
     1.0;
 ndc.y =
     1.0 -
     screenPosition.y *
     2.0 * inverseScreenSize.y;

 float2 position =
     horizontalMode != 0
         ? float2(-ndc.y, ndc.x)
         : ndc;

 float keyboardPosition =
     2.0 * keyboardHeight - 1.0;

 float direction =
     reverseMode != 0
         ? -1.0
         : 1.0;

 float notePosition =
     time +
     direction *
     (position.y - keyboardPosition) /
     max(abs(mainSpeed), 1e-6);

 return float2(
     position.x * screenRatio,
     notePosition
 );
}

float4 main(PSInput input) : SV_Target
{
 float2 normalizedCoord =
     input.position.xy * inverseScreenSize;

 normalizedCoord.y = 1.0 - normalizedCoord.y;

 float3 tinting =
     float3(1.0, 1.0, 1.0);

 if (useMajorTexture != 0)
 {
     float2 texUV =
         reconstructScreenUV(
             input.position.xy
         );

     texUV.y -=
         scrollMajorTexture * time;

     texUV *= texturesScale.x;
     texUV = frac(texUV);

     float intensity =
         (1.0 - input.isMinor) *
         texturesStrength.x;

     float3 textureColor =
         majorTexture.Sample(
             majorSampler,
             texUV
         ).rgb;

     tinting = lerp(
         tinting,
         textureColor,
         intensity
     );
 }

 if (useMinorTexture != 0)
 {
     float2 texUV =
         reconstructScreenUV(
             input.position.xy
         );

     texUV.y -=
         scrollMinorTexture * time;

     texUV *= texturesScale.y;
     texUV = frac(texUV);

     float intensity =
         input.isMinor *
         texturesStrength.y;

     float3 textureColor =
         minorTexture.Sample(
             minorSampler,
             texUV
         ).rgb;

     tinting = lerp(
         tinting,
         textureColor,
         intensity
     );
 }

 float2 ellipseCoords =
     abs(input.uv / (0.5 * input.noteSize));

 float2 ellipseExps =
     input.noteSize /
     max(cornerRadius, 1e-3);

 float radiusPosition =
     pow(ellipseCoords.x, ellipseExps.x) +
     pow(ellipseCoords.y, ellipseExps.y);

 int cid = (int)input.channel;

 float4 fragColor;

 fragColor.rgb =
     tinting *
     colorScale *
     lerp(
         baseColor[cid].rgb,
         minorColor[cid].rgb,
         input.isMinor
     );

 float deltaPix =
     fwidth(input.uv.x) * 4.0;

 float edgeIntensity = smoothstep(
     1.0 - edgeWidth - deltaPix,
     1.0 - edgeWidth + deltaPix,
     radiusPosition
 );

 fragColor.rgb *=
     1.0 +
     (edgeBrightness - 1.0) *
     edgeIntensity;

 if (
     (horizontalMode != 0
         ? normalizedCoord.x
         : normalizedCoord.y)
     < keyboardHeight
 )
 {
     discard;
 }

 if (radiusPosition > 1.0)
     discard;

 float distFromBottom =
     horizontalMode != 0
         ? normalizedCoord.x
         : normalizedCoord.y;

 float fadeOutFinal =
     min(fadeOut, 0.9999);

 distFromBottom =
     max(
         distFromBottom - fadeOutFinal,
         0.0
     ) /
     (1.0 - fadeOutFinal);

 float alpha =
     1.0 - distFromBottom;

 fragColor.a = alpha;

 return fragColor;
}