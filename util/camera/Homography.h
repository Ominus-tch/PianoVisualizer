#pragma once

#include <imgui/imgui.h>
#include <cmath>

// =============================================================
// HOMOGRAPHY
//
// Maps:
//
//     (u,v)
//
// to:
//
//     (screenX, screenY)
//
//
//
// Source:
//
//     (0,0) ---------------- (1,0)
//       |                       |
//       |                       |
//       |                       |
//     (0,1) ---------------- (1,1)
//
// Destination:
//
//     topLeft ---------------- topRight
//       |                         |
//       |                         |
//       |                         |
//     P1/bottomLeft -------- P4/bottomRight
//
// =============================================================

struct Homography
{
    double h11;
    double h12;
    double h13;

    double h21;
    double h22;
    double h23;

    double h31;
    double h32;

    bool valid = false;


    ImVec2 Project(
        float u,
        float v
    ) const
    {
        const double denominator =
            h31 * u +
            h32 * v +
            1.0;

        if (std::abs(denominator) < 1e-12)
        {
            return ImVec2(
                0.0f,
                0.0f
            );
        }


        const double x =
            (
                h11 * u +
                h12 * v +
                h13
                ) / denominator;


        const double y =
            (
                h21 * u +
                h22 * v +
                h23
                ) / denominator;


        return ImVec2(
            static_cast<float>(x),
            static_cast<float>(y)
        );
    }
};


// =============================================================
// CALCULATE HOMOGRAPHY
// =============================================================

Homography CalculateHomography(
    const ImVec2 src[4],
    const ImVec2 dst[4]
);