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
    bool valid = false;

    float K[3][3]{};

    Vec3 r1{};
    Vec3 r2{};
    Vec3 r3{};

    Vec3 translation{};
};


// ============================================================
// Calculate camera pose
// ============================================================

static PianoCameraPose CalculatePianoCameraPose(
    const ImVec2& P1,
    const ImVec2& P2,
    const ImVec2& P3,
    const ImVec2& P4,
    float imageWidth,
    float imageHeight,
    float horizontalFovDegrees
)
{
    PianoCameraPose pose{};

    // --------------------------------------------------------
    // Camera intrinsics
    //
    // We assume the optical center is the center of the image.
    //
    // horizontal FOV is the only camera parameter we need
    // to provide manually.
    // --------------------------------------------------------

    const float pi =
        3.14159265358979323846f;

    const float fovRadians =
        horizontalFovDegrees *
        pi /
        180.0f;

    const float fx =
        (imageWidth * 0.5f) /
        std::tan(fovRadians * 0.5f);

    const float fy =
        fx;

    const float cx =
        imageWidth * 0.5f;

    const float cy =
        imageHeight * 0.5f;


    pose.K[0][0] = fx;
    pose.K[0][1] = 0.0f;
    pose.K[0][2] = cx;

    pose.K[1][0] = 0.0f;
    pose.K[1][1] = fy;
    pose.K[1][2] = cy;

    pose.K[2][0] = 0.0f;
    pose.K[2][1] = 0.0f;
    pose.K[2][2] = 1.0f;


    // --------------------------------------------------------
    // Plane coordinates
    //
    // P1 = (0,0)
    // P2 = (0,1)
    // P3 = (1,1)
    // P4 = (1,0)
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // K^-1 H
    //
    // Since K is:
    //
    // [ fx  0 cx ]
    // [  0 fy cy ]
    // [  0  0  1 ]
    //
    // K^-1 can be applied directly.
    // --------------------------------------------------------

    double B[3][3]{};

    // H =
    //
    // [ h11 h12 h13 ]
    // [ h21 h22 h23 ]
    // [ h31 h32  1  ]

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


    // --------------------------------------------------------
    // Extract homography columns
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // Both r1 and r2 should have the same scale.
    // --------------------------------------------------------

    const float norm1 =
        Vec3Length(b1);

    const float norm2 =
        Vec3Length(b2);

    if (
        norm1 < 0.000001f ||
        norm2 < 0.000001f
        )
    {
        return pose;
    }


    const float lambda =
        2.0f /
        (norm1 + norm2);


    // --------------------------------------------------------
    // Recover rotation axes
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // Orthonormalize r2 against r1.
    //
    // This removes small numerical errors.
    // --------------------------------------------------------

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

    r2 =
        Vec3Normalize(r2);

    r1 =
        Vec3Normalize(r1);


    // --------------------------------------------------------
    // Third rotation axis
    //
    // This is the normal of the piano plane.
    // --------------------------------------------------------

    Vec3 r3 =
        Vec3Cross(
            r1,
            r2
        );

    r3 =
        Vec3Normalize(r3);


    // --------------------------------------------------------
    // Translation
    // --------------------------------------------------------

    Vec3 translation =
        Vec3Multiply(
            b3,
            lambda
        );


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


    // --------------------------------------------------------
    // Camera-space point
    //
    //     camera = R * world + t
    //
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // Behind camera
    // --------------------------------------------------------

    //if (cameraPoint.z <= 0.000001f)
    //{
    //    Logger::Log(
    //        "Point behind camera: %.3f %.3f %.3f\n",
    //        cameraPoint.x,
    //        cameraPoint.y,
    //        cameraPoint.z
    //    );

    //    return false;
    //}

    if (cameraPoint.z == 0.0f)
        cameraPoint.z = 0.000001f;

    cameraPoint.z = std::abs(cameraPoint.z);

    // --------------------------------------------------------
    // Perspective projection
    // --------------------------------------------------------

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


    return true;
}