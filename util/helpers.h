#pragma once

#include <cmath>
#include <imgui/imgui.h>

#include "../util/camera/Homography.h"

// ============================================================
// Simple 3D vector
// ============================================================

struct Vec3
{
    float x;
    float y;
    float z;
};


// ============================================================
// Vector helpers
// ============================================================

static Vec3 Vec3Add(
    const Vec3& a,
    const Vec3& b
)
{
    return {
        a.x + b.x,
        a.y + b.y,
        a.z + b.z
    };
}


static Vec3 Vec3Subtract(
    const Vec3& a,
    const Vec3& b
)
{
    return {
        a.x - b.x,
        a.y - b.y,
        a.z - b.z
    };
}


static Vec3 Vec3Multiply(
    const Vec3& v,
    float s
)
{
    return {
        v.x * s,
        v.y * s,
        v.z * s
    };
}


static float Vec3Length(
    const Vec3& v
)
{
    return std::sqrt(
        v.x * v.x +
        v.y * v.y +
        v.z * v.z
    );
}


static Vec3 Vec3Normalize(
    const Vec3& v
)
{
    float length =
        Vec3Length(v);

    if (length < 0.000001f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    return {
        v.x / length,
        v.y / length,
        v.z / length
    };
}


static Vec3 Vec3Cross(
    const Vec3& a,
    const Vec3& b
)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}


static float Vec3Dot(
    const Vec3& a,
    const Vec3& b
)
{
    return
        a.x * b.x +
        a.y * b.y +
        a.z * b.z;
}


// ============================================================
// 3x3 matrix × vector
// ============================================================

static Vec3 Mat3Multiply(
    const float M[3][3],
    const Vec3& v
)
{
    return {
        M[0][0] * v.x +
        M[0][1] * v.y +
        M[0][2] * v.z,

        M[1][0] * v.x +
        M[1][1] * v.y +
        M[1][2] * v.z,

        M[2][0] * v.x +
        M[2][1] * v.y +
        M[2][2] * v.z
    };
}

// ============================================================
// Reconstruct camera pose from a planar homography
//
// The piano is treated as:
//
//          Y
//          ↑
//          |
// P1 ──────┼────── P4
//          |
//          |
//          P2/P3
//
// Z is world-up.
//
// The homography describes:
//
//     piano-plane coordinates -> camera image pixels
//
// We decompose:
//
//     H = K [ r1 r2 t ]
//
// and obtain r3 = r1 × r2.
//
// ============================================================

struct PianoCameraPose
{
    float K[3][3]{};

    Vec3 r1{};
    Vec3 r2{};
    Vec3 r3{};

    Vec3 translation{};

    // Automatically calculated camera parameters.
    float focalLength = 0.0f;
    float horizontalFovDegrees = 0.0f;

    bool valid = false;
};

static bool IntersectLines(
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    const ImVec2& p4,
    ImVec2& out
)
{
    const double A1 =
        static_cast<double>(p2.y) -
        static_cast<double>(p1.y);

    const double B1 =
        static_cast<double>(p1.x) -
        static_cast<double>(p2.x);

    const double C1 =
        static_cast<double>(p2.x) *
        static_cast<double>(p1.y) -
        static_cast<double>(p1.x) *
        static_cast<double>(p2.y);


    const double A2 =
        static_cast<double>(p4.y) -
        static_cast<double>(p3.y);

    const double B2 =
        static_cast<double>(p3.x) -
        static_cast<double>(p4.x);

    const double C2 =
        static_cast<double>(p4.x) *
        static_cast<double>(p3.y) -
        static_cast<double>(p3.x) *
        static_cast<double>(p4.y);


    const double denominator =
        A1 * B2 - A2 * B1;

    // Lines are parallel or almost parallel.
    if (std::abs(denominator) < 1e-10)
        return false;


    out.x =
        static_cast<float>(
            (B1 * C2 - B2 * C1) /
            denominator
            );

    out.y =
        static_cast<float>(
            (C1 * A2 - C2 * A1) /
            denominator
            );

    return
        std::isfinite(out.x) &&
        std::isfinite(out.y);
}

static bool EstimateFocalLength(
    const ImVec2& P1,
    const ImVec2& P2,
    const ImVec2& P3,
    const ImVec2& P4,
    float imageWidth,
    float imageHeight,
    float& outFocalLength
)
{
    const double cx =
        static_cast<double>(imageWidth) * 0.5;

    const double cy =
        static_cast<double>(imageHeight) * 0.5;


    // ---------------------------------------------------------
    // Find the two vanishing points.
    //
    // P1 -> P2 and P4 -> P3 are parallel in 3D.
    //
    // P1 -> P4 and P2 -> P3 are the other parallel pair.
    // ---------------------------------------------------------

    ImVec2 vanishingPoint1;

    if (!IntersectLines(
        P1,
        P2,
        P4,
        P3,
        vanishingPoint1
    ))
    {
        return false;
    }


    ImVec2 vanishingPoint2;

    if (!IntersectLines(
        P1,
        P4,
        P2,
        P3,
        vanishingPoint2
    ))
    {
        return false;
    }


    // ---------------------------------------------------------
    // Shift vanishing points so the principal point becomes
    // the origin.
    // ---------------------------------------------------------

    const double vx =
        static_cast<double>(vanishingPoint1.x) -
        cx;

    const double vy =
        static_cast<double>(vanishingPoint1.y) -
        cy;


    const double wx =
        static_cast<double>(vanishingPoint2.x) -
        cx;

    const double wy =
        static_cast<double>(vanishingPoint2.y) -
        cy;


    // ---------------------------------------------------------
    // For two perpendicular world directions:
    //
    //   (v - c)^T (w - c) = -f^2
    //
    // Therefore:
    //
    //   f^2 = -(vx * wx + vy * wy)
    // ---------------------------------------------------------

    const double focalSquared =
        -(vx * wx + vy * wy);


    if (!std::isfinite(focalSquared) ||
        focalSquared <= 0.0)
    {
        return false;
    }


    const double focalLength =
        std::sqrt(focalSquared);


    if (!std::isfinite(focalLength) ||
        focalLength <= 1.0)
    {
        return false;
    }


    outFocalLength =
        static_cast<float>(focalLength);

    return true;
}

// ============================================================
// Calculate camera pose
// ============================================================

static PianoCameraPose CalculatePianoCameraPose(
    const ImVec2& P1,
    const ImVec2& P2,
    const ImVec2& P3,
    const ImVec2& P4,
    float imageWidth,
    float imageHeight
)
{
    PianoCameraPose pose{};


    // ---------------------------------------------------------
    // Camera principal point.
    //
    // We assume the optical center is at the center of the
    // image.
    // ---------------------------------------------------------

    const float cx =
        imageWidth * 0.5f;

    const float cy =
        imageHeight * 0.5f;


    // ---------------------------------------------------------
    // Automatically estimate focal length from the four
    // keyboard corners.
    // ---------------------------------------------------------

    float focalLength = 0.0f;

    if (!EstimateFocalLength(
        P1,
        P2,
        P3,
        P4,
        imageWidth,
        imageHeight,
        focalLength
    ))
    {
        return pose;
    }


    const float fx =
        focalLength;

    const float fy =
        focalLength;


    // ---------------------------------------------------------
    // Camera intrinsic matrix:
    //
    // [ fx  0  cx ]
    // [  0 fy  cy ]
    // [  0  0   1 ]
    // ---------------------------------------------------------

    pose.K[0][0] = fx;
    pose.K[0][1] = 0.0f;
    pose.K[0][2] = cx;

    pose.K[1][0] = 0.0f;
    pose.K[1][1] = fy;
    pose.K[1][2] = cy;

    pose.K[2][0] = 0.0f;
    pose.K[2][1] = 0.0f;
    pose.K[2][2] = 1.0f;


    pose.focalLength =
        focalLength;


    // ---------------------------------------------------------
    // Calculate horizontal FOV from the estimated focal length.
    //
    // This is now derived information, not a user input.
    // ---------------------------------------------------------

    const float fovRadians =
        2.0f *
        std::atan(
            imageWidth /
            (2.0f * focalLength)
        );

    pose.horizontalFovDegrees =
        fovRadians *
        180.0f /
        3.14159265358979323846f;


    // ---------------------------------------------------------
    // Plane coordinates.
    //
    // P1 = (0,0)
    // P2 = (0,1)
    // P3 = (1,1)
    // P4 = (1,0)
    // ---------------------------------------------------------

    const ImVec2 src[4] =
    {
        ImVec2(0.0f, 0.0f), // P1
        ImVec2(0.0f, 1.0f), // P2
        ImVec2(1.0f, 1.0f), // P3
        ImVec2(1.0f, 0.0f)  // P4
    };


    const ImVec2 dst[4] =
    {
        P1,
        P2,
        P3,
        P4
    };


    Homography H =
        CalculateHomography(
            src,
            dst
        );


    if (!H.valid)
    {
        return pose;
    }


    // ---------------------------------------------------------
    // K^-1 * H
    // ---------------------------------------------------------

    double B[3][3]{};


    B[0][0] =
        (H.h11 - cx * H.h31) /
        fx;

    B[0][1] =
        (H.h12 - cx * H.h32) /
        fx;

    B[0][2] =
        (H.h13 - cx) /
        fx;


    B[1][0] =
        (H.h21 - cy * H.h31) /
        fy;

    B[1][1] =
        (H.h22 - cy * H.h32) /
        fy;

    B[1][2] =
        (H.h23 - cy) /
        fy;


    B[2][0] =
        H.h31;

    B[2][1] =
        H.h32;

    B[2][2] =
        1.0;


    // ---------------------------------------------------------
    // Extract homography columns.
    // ---------------------------------------------------------

    Vec3 b1 =
    {
        static_cast<float>(B[0][0]),
        static_cast<float>(B[1][0]),
        static_cast<float>(B[2][0])
    };


    Vec3 b2 =
    {
        static_cast<float>(B[0][1]),
        static_cast<float>(B[1][1]),
        static_cast<float>(B[2][1])
    };


    Vec3 b3 =
    {
        static_cast<float>(B[0][2]),
        static_cast<float>(B[1][2]),
        static_cast<float>(B[2][2])
    };


    // ---------------------------------------------------------
    // Recover the common scale.
    // ---------------------------------------------------------

    const float norm1 =
        Vec3Length(b1);

    const float norm2 =
        Vec3Length(b2);


    if (norm1 < 0.000001f ||
        norm2 < 0.000001f)
    {
        return pose;
    }


    const float lambda =
        2.0f /
        (norm1 + norm2);


    // ---------------------------------------------------------
    // Recover rotation axes.
    // ---------------------------------------------------------

    Vec3 r1 =
        Vec3Multiply(
            b1,
            lambda
        );


    Vec3 r2 =
        Vec3Multiply(
            b2,
            lambda
        );


    // ---------------------------------------------------------
    // Orthonormalize r2 against r1.
    // ---------------------------------------------------------

    const float projection =
        Vec3Dot(
            r1,
            r2
        );


    r2 =
        Vec3Subtract(
            r2,
            Vec3Multiply(
                r1,
                projection
            )
        );


    r1 =
        Vec3Normalize(r1);

    r2 =
        Vec3Normalize(r2);


    // ---------------------------------------------------------
    // Third rotation axis = plane normal.
    // ---------------------------------------------------------

    Vec3 r3 =
        Vec3Cross(
            r1,
            r2
        );

    r3 =
        Vec3Normalize(r3);


    // ---------------------------------------------------------
    // Translation.
    // ---------------------------------------------------------

    Vec3 translation =
        Vec3Multiply(
            b3,
            lambda
        );


    // ---------------------------------------------------------
    // Store result.
    // ---------------------------------------------------------

    pose.r1 =
        r1;

    pose.r2 =
        r2;

    pose.r3 =
        r3;

    pose.translation =
        translation;

    pose.valid =
        true;


    return pose;
}

// ============================================================
// Project 3D piano/world point into camera image
//
// World coordinates:
//
// X = piano width
// Y = piano depth
// Z = world UP
//
// Z is therefore exactly what we use for the virtual
// piano's height.
// ============================================================

static bool ProjectPianoPoint(
    const PianoCameraPose& pose,
    float x,
    float y,
    float z,
    float& outX,
    float& outY
)
{
    if (!pose.valid)
        return false;


    // ---------------------------------------------------------
    // Camera-space point
    //
    // camera = R * world + t
    // ---------------------------------------------------------

    Vec3 cameraPoint =
    {
        pose.r1.x * x +
        pose.r2.x * y +
        pose.r3.x * z +
        pose.translation.x,

        pose.r1.y * x +
        pose.r2.y * y +
        pose.r3.y * z +
        pose.translation.y,

        pose.r1.z * x +
        pose.r2.z * y +
        pose.r3.z * z +
        pose.translation.z
    };


    // ---------------------------------------------------------
    // Point must be in front of the camera.
    // ---------------------------------------------------------

    if (!std::isfinite(cameraPoint.x) ||
        !std::isfinite(cameraPoint.y) ||
        !std::isfinite(cameraPoint.z))
    {
        return false;
    }


    if (cameraPoint.z <= 0.000001f)
    {
        return false;
    }


    // ---------------------------------------------------------
    // Perspective projection.
    // ---------------------------------------------------------

    outX =
        pose.K[0][0] *
        (
            cameraPoint.x /
            cameraPoint.z
            ) +
        pose.K[0][2];


    outY =
        pose.K[1][1] *
        (
            cameraPoint.y /
            cameraPoint.z
            ) +
        pose.K[1][2];


    return
        std::isfinite(outX) &&
        std::isfinite(outY);
}

static bool CalculateSurfaceHeightToTop(
    const PianoCameraPose& pose,
    float planePivot,
    float& outSurfaceHeight
)
{
    if (!pose.valid)
        return false;

    const float pi =
        3.14159265358979323846f;

    const float angle =
        planePivot * pi / 180.0f;

    const float c =
        std::cos(angle);

    const float s =
        std::sin(angle);

    const float q =
        pose.K[1][2] /
        pose.K[1][1];

    float planeWidth = 16.0f / 9.0f;

    // ---------------------------------------------------------
    // Solve the surface height for one top corner.
    //
    // Camera-space:
    //
    // Y = A + hB
    // Z = C + hD
    //
    // We want projected Y = 0:
    //
    //     fy * Y / Z + cy = 0
    //
    // ---------------------------------------------------------

    auto SolveCorner =
        [&](float x, float& height) -> bool
        {
            const float A =
                pose.r1.y * x +
                pose.translation.y;

            const float C =
                pose.r1.z * x +
                pose.translation.z;

            const float B =
                pose.r2.y * c -
                pose.r3.y * s;

            const float D =
                pose.r2.z * c -
                pose.r3.z * s;

            const float denominator =
                B + q * D;

            if (std::abs(denominator) < 0.000001f)
                return false;

            height =
                (-q * C - A) /
                denominator;

            return std::isfinite(height) &&
                height > 0.0f;
        };


    float leftHeight = 0.0f;
    float rightHeight = 0.0f;

    const bool leftValid =
        SolveCorner(
            0.0f,
            leftHeight
        );

    const bool rightValid =
        SolveCorner(
            planeWidth,
            rightHeight
        );


    if (!leftValid && !rightValid)
        return false;


    // ---------------------------------------------------------
    // Determine which candidate actually makes the LOWER
    // top corner touch the top of the image.
    // ---------------------------------------------------------

    auto ProjectTopY =
        [&](float x, float height) -> float
        {
            const float worldY =
                height * c;

            const float worldZ =
                -height * s;


            const float cameraY =
                pose.r1.y * x +
                pose.r2.y * worldY +
                pose.r3.y * worldZ +
                pose.translation.y;

            const float cameraZ =
                pose.r1.z * x +
                pose.r2.z * worldY +
                pose.r3.z * worldZ +
                pose.translation.z;


            if (cameraZ <= 0.000001f)
            {
                return std::numeric_limits<float>::infinity();
            }


            return
                pose.K[1][1] *
                (cameraY / cameraZ) +
                pose.K[1][2];
        };


    float bestHeight = 0.0f;
    float bestError = 1e30f;


    auto TestCandidate =
        [&](float height)
        {
            const float leftY =
                ProjectTopY(
                    0.0f,
                    height
                );

            const float rightY =
                ProjectTopY(
                    planeWidth,
                    height
                );

            if (!std::isfinite(leftY) ||
                !std::isfinite(rightY))
            {
                return;
            }

            // The lower of the two top corners is the one with
            // the larger screen-space Y.
            const float lowerY =
                max(
                    leftY,
                    rightY
                );

            const float error =
                std::abs(lowerY);

            if (error < bestError)
            {
                bestError = error;
                bestHeight = height;
            }
        };


    if (leftValid)
        TestCandidate(leftHeight);

    if (rightValid)
        TestCandidate(rightHeight);


    if (!std::isfinite(bestHeight) ||
        bestHeight <= 0.0f)
    {
        return false;
    }


    outSurfaceHeight =
        bestHeight;

    return true;
}