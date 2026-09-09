#include "Homography.h"

#include <algorithm>


// ============================================================
// Solve an 8x8 linear system using Gaussian elimination
// ============================================================

static bool Solve8x8(
    double A[8][8],
    double b[8],
    double x[8]
)
{
    for (int i = 0; i < 8; ++i)
    {
        // -----------------------------------------------------
        // Find pivot
        // -----------------------------------------------------

        int pivot = i;

        double maxValue =
            std::abs(A[i][i]);

        for (int r = i + 1; r < 8; ++r)
        {
            double value =
                std::abs(A[r][i]);

            if (value > maxValue)
            {
                maxValue = value;
                pivot = r;
            }
        }

        if (maxValue < 1e-12)
            return false;


        // -----------------------------------------------------
        // Swap rows
        // -----------------------------------------------------

        if (pivot != i)
        {
            for (int c = i; c < 8; ++c)
            {
                std::swap(
                    A[i][c],
                    A[pivot][c]
                );
            }

            std::swap(
                b[i],
                b[pivot]
            );
        }


        // -----------------------------------------------------
        // Normalize pivot row
        // -----------------------------------------------------

        const double divisor =
            A[i][i];

        for (int c = i; c < 8; ++c)
        {
            A[i][c] /= divisor;
        }

        b[i] /= divisor;


        // -----------------------------------------------------
        // Eliminate column
        // -----------------------------------------------------

        for (int r = 0; r < 8; ++r)
        {
            if (r == i)
                continue;

            const double factor =
                A[r][i];

            if (std::abs(factor) < 1e-12)
                continue;

            for (int c = i; c < 8; ++c)
            {
                A[r][c] -=
                    factor * A[i][c];
            }

            b[r] -=
                factor * b[i];
        }
    }


    for (int i = 0; i < 8; ++i)
    {
        x[i] = b[i];
    }

    return true;
}


// ============================================================
// Calculate Homography
// ============================================================

Homography CalculateHomography(
    const ImVec2 src[4],
    const ImVec2 dst[4]
)
{
    Homography result{};


    double A[8][8] = {};
    double b[8] = {};


    for (int i = 0; i < 4; ++i)
    {
        const double x =
            src[i].x;

        const double y =
            src[i].y;

        const double X =
            dst[i].x;

        const double Y =
            dst[i].y;


        const int row =
            i * 2;


        // -----------------------------------------------------
        // X equation
        // -----------------------------------------------------

        A[row][0] = x;
        A[row][1] = y;
        A[row][2] = 1.0;

        A[row][3] = 0.0;
        A[row][4] = 0.0;
        A[row][5] = 0.0;

        A[row][6] =
            -X * x;

        A[row][7] =
            -X * y;

        b[row] = X;


        // -----------------------------------------------------
        // Y equation
        // -----------------------------------------------------

        A[row + 1][0] = 0.0;
        A[row + 1][1] = 0.0;
        A[row + 1][2] = 0.0;

        A[row + 1][3] = x;
        A[row + 1][4] = y;
        A[row + 1][5] = 1.0;

        A[row + 1][6] =
            -Y * x;

        A[row + 1][7] =
            -Y * y;

        b[row + 1] = Y;
    }


    double x[8] = {};


    if (!Solve8x8(
        A,
        b,
        x
    ))
    {
        return result;
    }


    result.h11 = x[0];
    result.h12 = x[1];
    result.h13 = x[2];

    result.h21 = x[3];
    result.h22 = x[4];
    result.h23 = x[5];

    result.h31 = x[6];
    result.h32 = x[7];

    result.valid = true;


    return result;
}