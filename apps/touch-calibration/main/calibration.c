#include "calibration.h"

#include <math.h>

/* Solve the 3x3 system a * x = b with Cramer's rule. */
static bool solve3(const double a[3][3], const double b[3], double x[3])
{
    double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
                 a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                 a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    if (fabs(det) < 1e-9) {
        return false;
    }
    for (int col = 0; col < 3; ++col) {
        double t[3][3];
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                t[r][c] = (c == col) ? b[r] : a[r][c];
            }
        }
        double d = t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1]) -
                   t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0]) +
                   t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0]);
        x[col] = d / det;
    }
    return true;
}

bool calibration_fit(const float raw_x[], const float raw_y[], const float scr_x[], const float scr_y[],
                     int count, float m[6])
{
    if (count < 3) {
        return false;
    }
    /* Normal equations: (A^T A) p = A^T s, with rows A = [rx ry 1]. */
    double ata[3][3] = {{0}};
    double atx[3] = {0}, aty[3] = {0};
    for (int i = 0; i < count; ++i) {
        double row[3] = {raw_x[i], raw_y[i], 1.0};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                ata[r][c] += row[r] * row[c];
            }
            atx[r] += row[r] * scr_x[i];
            aty[r] += row[r] * scr_y[i];
        }
    }
    double px[3], py[3];
    if (!solve3(ata, atx, px) || !solve3(ata, aty, py)) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        m[i] = (float)px[i];
        m[3 + i] = (float)py[i];
    }
    return true;
}
