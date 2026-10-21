/*
 * Satellite positions for an observer on the ground: TLE parsing, SGP4
 * propagation, look angles (azimuth/elevation/range), and pass search.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "sgp4/SGP4.h"

typedef struct {
    double lat_deg;
    double lon_deg;
    double alt_m;
} observer_t;

typedef struct {
    char name[25];
    int catnum;        /* NORAD catalog number */
    elsetrec rec;      /* SGP4 state; sgp4() modifies it, so do not share between tasks */
} satellite_t;

typedef struct {
    double az_deg;     /* 0 = north, 90 = east */
    double el_deg;     /* above the horizon when > 0 */
    double range_km;
    double alt_km;     /* height above the Earth's surface */
} look_t;

typedef struct {
    time_t rise;       /* UTC */
    time_t culminate;
    time_t set;
    double max_el_deg;
    double rise_az_deg;
    double set_az_deg;
} pass_t;

/* Parses one TLE (name line optional, may be NULL) into sat. Returns false on bad input. */
bool sat_parse_tle(const char *name, const char *line1, const char *line2, satellite_t *sat);

/* Julian date (UT) for a Unix time. */
double sat_jd(time_t unix_time, double extra_seconds);

/* Position of sat seen from obs at the given Julian date. Returns false if SGP4 fails. */
bool sat_look(satellite_t *sat, const observer_t *obs, double jd, look_t *out);

/*
 * Finds the first pass that starts after `from` and before `until`, rising above
 * 0 degrees, with peak elevation >= min_el_deg. step_s is the coarse search step.
 * Returns false if none was found.
 */
bool sat_next_pass(satellite_t *sat, const observer_t *obs, time_t from, time_t until,
                   double min_el_deg, int step_s, pass_t *out);
