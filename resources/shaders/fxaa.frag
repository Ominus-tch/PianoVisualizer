struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer FXAAConstants : register(b0) {
    float2 inverseScreenSize;
};

Texture2D screenTexture : register(t0);
SamplerState screenSampler : register(s0);

#define EDGE_THRESHOLD_MIN 0.0312f
#define EDGE_THRESHOLD_MAX 0.125f
#define ITERATIONS 12
#define SUBPIXEL_QUALITY 0.75f

#define QUALITY(q) ((q) < 5 ? 1.0f : ((q) > 5 ? ((q) < 10 ? 2.0f : ((q) < 11 ? 4.0f : 8.0f)) : 1.5f))

float rgb2luma(float3 rgb) {
    return sqrt(dot(rgb, float3(0.299f, 0.587f, 0.114f)));
}

float4 main(PSInput input) : SV_Target {
    float2 uv = input.uv;

    float4 colorCenter = screenTexture.SampleLevel(screenSampler, uv, 0.0f);

    float lumaCenter = rgb2luma(colorCenter.rgb);

    float lumaDown = rgb2luma(
        screenTexture.SampleLevel(
            screenSampler,
            uv + float2(0.0f, -inverseScreenSize.y),
            0.0f
        ).rgb
    );

    float lumaUp = rgb2luma(
        screenTexture.SampleLevel(
            screenSampler,
            uv + float2(0.0f, inverseScreenSize.y),
            0.0f
        ).rgb
    );

    float lumaLeft = rgb2luma(
        screenTexture.SampleLevel(
            screenSampler,
            uv + float2(-inverseScreenSize.x, 0.0f),
            0.0f
        ).rgb
    );

    float lumaRight = rgb2luma(
        screenTexture.SampleLevel(
            screenSampler,
            uv + float2(inverseScreenSize.x, 0.0f),
            0.0f
        ).rgb
    );

    float lumaMin = min(
        lumaCenter,
        min(
            min(lumaDown, lumaUp),
            min(lumaLeft, lumaRight)
        )
    );

    float lumaMax = max(
        lumaCenter,
        max(
            max(lumaDown, lumaUp),
            max(lumaLeft, lumaRight)
        )
    );

    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX)) {
        return colorCenter;
    }

    float lumaDownLeft = rgb2luma(
        screenTexture.SampleLevel(
            screenSampler,
            uv + float2(-inverseScreenSize.x, -inverseScreenSize.y),
            0.0f
        ).rgb
    );

    float lumaUpRight = rgb2luma(
        screenTexture.SampleLevel(
            screenSampler,
            uv + float2(inverseScreenSize.x, inverseScreenSize.y),
            0.0f
        ).rgb
    );

    float lumaUpLeft = rgb2luma(
        screenTexture.SampleLevel(
            screenSampler,
            uv + float2(-inverseScreenSize.x, inverseScreenSize.y),
            0.0f
        ).rgb
    );

    float lumaDownRight = rgb2luma(
        screenTexture.SampleLevel(
            screenSampler,
            uv + float2(inverseScreenSize.x, -inverseScreenSize.y),
            0.0f
        ).rgb
    );

    float lumaDownUp = lumaDown + lumaUp;
    float lumaLeftRight = lumaLeft + lumaRight;

    float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
    float lumaDownCorners = lumaDownLeft + lumaDownRight;
    float lumaRightCorners = lumaDownRight + lumaUpRight;
    float lumaUpCorners = lumaUpRight + lumaUpLeft;

    float edgeHorizontal =
        abs(-2.0f * lumaLeft + lumaLeftCorners) +
        abs(-2.0f * lumaCenter + lumaDownUp) * 2.0f +
        abs(-2.0f * lumaRight + lumaRightCorners);

    float edgeVertical =
        abs(-2.0f * lumaUp + lumaUpCorners) +
        abs(-2.0f * lumaCenter + lumaLeftRight) * 2.0f +
        abs(-2.0f * lumaDown + lumaDownCorners);

    bool isHorizontal = edgeHorizontal >= edgeVertical;

    float stepLength = isHorizontal
        ? inverseScreenSize.y
        : inverseScreenSize.x;

    float luma1 = isHorizontal ? lumaDown : lumaLeft;
    float luma2 = isHorizontal ? lumaUp : lumaRight;

    float gradient1 = luma1 - lumaCenter;
    float gradient2 = luma2 - lumaCenter;

    bool is1Steepest = abs(gradient1) >= abs(gradient2);

    float gradientScaled =
        0.25f * max(abs(gradient1), abs(gradient2));

    float lumaLocalAverage;

    if (is1Steepest) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5f * (luma1 + lumaCenter);
    }
    else {
        lumaLocalAverage = 0.5f * (luma2 + lumaCenter);
    }

    float2 currentUv = uv;

    if (isHorizontal) {
        currentUv.y += stepLength * 0.5f;
    }
    else {
        currentUv.x += stepLength * 0.5f;
    }

    float2 offset = isHorizontal
        ? float2(inverseScreenSize.x, 0.0f)
        : float2(0.0f, inverseScreenSize.y);

    float2 uv1 = currentUv - offset * QUALITY(0);
    float2 uv2 = currentUv + offset * QUALITY(0);

    float lumaEnd1 = rgb2luma(
        screenTexture.SampleLevel(screenSampler, uv1, 0.0f).rgb
    );

    float lumaEnd2 = rgb2luma(
        screenTexture.SampleLevel(screenSampler, uv2, 0.0f).rgb
    );

    lumaEnd1 -= lumaLocalAverage;
    lumaEnd2 -= lumaLocalAverage;

    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    bool reachedBoth = reached1 && reached2;

    if (!reached1) {
        uv1 -= offset * QUALITY(1);
    }

    if (!reached2) {
        uv2 += offset * QUALITY(1);
    }

    if (!reachedBoth) {
        for (int i = 2; i < ITERATIONS; ++i) {
            if (!reached1) {
                lumaEnd1 = rgb2luma(
                    screenTexture.SampleLevel(screenSampler, uv1, 0.0f).rgb
                );
                lumaEnd1 -= lumaLocalAverage;
            }

            if (!reached2) {
                lumaEnd2 = rgb2luma(
                    screenTexture.SampleLevel(screenSampler, uv2, 0.0f).rgb
                );
                lumaEnd2 -= lumaLocalAverage;
            }

            reached1 = abs(lumaEnd1) >= gradientScaled;
            reached2 = abs(lumaEnd2) >= gradientScaled;
            reachedBoth = reached1 && reached2;

            if (!reached1) {
                uv1 -= offset * QUALITY(i);
            }

            if (!reached2) {
                uv2 += offset * QUALITY(i);
            }

            if (reachedBoth) {
                break;
            }
        }
    }

    float distance1 = isHorizontal
        ? (uv.x - uv1.x)
        : (uv.y - uv1.y);

    float distance2 = isHorizontal
        ? (uv2.x - uv.x)
        : (uv2.y - uv.y);

    bool isDirection1 = distance1 < distance2;

    float distanceFinal = min(distance1, distance2);
    float edgeThickness = distance1 + distance2;

    bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;

    bool correctVariation1 =
        (lumaEnd1 < 0.0f) != isLumaCenterSmaller;

    bool correctVariation2 =
        (lumaEnd2 < 0.0f) != isLumaCenterSmaller;

    bool correctVariation =
        isDirection1 ? correctVariation1 : correctVariation2;

    float pixelOffset =
        -distanceFinal / edgeThickness + 0.5f;

    float finalOffset =
        correctVariation ? pixelOffset : 0.0f;

    float lumaAverage =
        (1.0f / 12.0f) *
        (
            2.0f * (lumaDownUp + lumaLeftRight) +
            lumaLeftCorners +
            lumaRightCorners
        );

    float subPixelOffset1 = clamp(
        abs(lumaAverage - lumaCenter) / lumaRange,
        0.0f,
        1.0f
    );

    float subPixelOffset2 =
        (-2.0f * subPixelOffset1 + 3.0f) *
        subPixelOffset1 *
        subPixelOffset1;

    float subPixelOffsetFinal =
        subPixelOffset2 *
        subPixelOffset2 *
        SUBPIXEL_QUALITY;

    finalOffset = max(
        finalOffset,
        subPixelOffsetFinal
    );

    float2 finalUv = uv;

    if (isHorizontal) {
        finalUv.y += finalOffset * stepLength;
    }
    else {
        finalUv.x += finalOffset * stepLength;
    }

    return screenTexture.SampleLevel(
        screenSampler,
        finalUv,
        0.0f
    );
}
