#pragma once

#include <stdbool.h>

/*
 * Least-squares fit of an affine map from raw touch readings to screen pixels:
 *   x = m0*rx + m1*ry + m2,  y = m3*rx + m4*ry + m5
 * Needs at least three points that are not on one line.
 */
bool calibration_fit(const float raw_x[], const float raw_y[], const float scr_x[], const float scr_y[],
                     int count, float m[6]);
