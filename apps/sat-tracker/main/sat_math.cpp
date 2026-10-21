#include "sat_math.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

using namespace SGP4Funcs;

static const double DEG = M_PI / 180.0;

/* ---------- TLE parsing (fixed columns, as in python-sgp4's io.py) ---------- */

static double field(const char *line, int start, int end)
{
    char buf[24];
    int n = end - start;
    if (n <= 0 || n >= (int)sizeof(buf)) return 0;
    memcpy(buf, line + start, n);
    buf[n] = '\0';
    return atof(buf);
}

/* "-12345-6" style: implied decimal point, sign, and exponent (columns start..start+8). */
static double implied_exp(const char *line, int start)
{
    char mant[16];
    char sign = line[start];
    int i = 0;
    mant[i++] = (sign == '-') ? '-' : '+';
    mant[i++] = '0';
    mant[i++] = '.';
    for (int k = start + 1; k < start + 6; ++k) mant[i++] = (line[k] == ' ') ? '0' : line[k];
    mant[i] = '\0';
    char exp[4] = {line[start + 6], line[start + 7], '\0', '\0'};
    return atof(mant) * pow(10.0, atoi(exp));
}

static bool checksum_ok(const char *line)
{
    int sum = 0;
    for (int i = 0; i < 68; ++i) {
        char c = line[i];
        if (c >= '0' && c <= '9') sum += c - '0';
        else if (c == '-') sum += 1;
    }
    return (sum % 10) == (line[68] - '0');
}

bool sat_parse_tle(const char *name, const char *line1, const char *line2, satellite_t *sat)
{
    if (strlen(line1) < 69 || strlen(line2) < 69 || line1[0] != '1' || line2[0] != '2') return false;
    if (!checksum_ok(line1) || !checksum_ok(line2)) return false;

    memset(sat, 0, sizeof(*sat));
    if (name) {
        while (*name == ' ') name++;
        if (name[0] == '0' && name[1] == ' ') name += 2; /* 3LE format "0 NAME" */
        snprintf(sat->name, sizeof(sat->name), "%s", name);
        for (int i = (int)strlen(sat->name) - 1; i >= 0 && isspace((unsigned char)sat->name[i]); --i) sat->name[i] = '\0';
    }

    char satnum[6];
    memcpy(satnum, line1 + 2, 5);
    satnum[5] = '\0';
    sat->catnum = atoi(satnum);
    if (!name || !sat->name[0]) snprintf(sat->name, sizeof(sat->name), "%s", satnum);

    elsetrec &r = sat->rec;
    const double xpdotp = 1440.0 / (2.0 * M_PI);

    int epochyr = (int)field(line1, 18, 20);
    double epochdays = field(line1, 20, 32);
    r.ndot = field(line1, 33, 43);
    r.nddot = implied_exp(line1, 44);
    r.bstar = implied_exp(line1, 53);
    r.inclo = field(line2, 8, 16);
    r.nodeo = field(line2, 17, 25);
    char ecc[16] = "0.";
    memcpy(ecc + 2, line2 + 26, 7);
    ecc[9] = '\0';
    r.ecco = atof(ecc);
    r.argpo = field(line2, 34, 42);
    r.mo = field(line2, 43, 51);
    r.no_kozai = field(line2, 52, 63);

    r.no_kozai /= xpdotp;
    r.ndot /= (xpdotp * 1440.0);
    r.nddot /= (xpdotp * 1440.0 * 1440.0);
    r.inclo *= DEG;
    r.nodeo *= DEG;
    r.argpo *= DEG;
    r.mo *= DEG;

    int year = epochyr < 57 ? epochyr + 2000 : epochyr + 1900;
    double jd0, jd0f;
    jday_SGP4(year, 1, 1, 0, 0, 0.0, jd0, jd0f);
    double whole = floor(epochdays);
    r.jdsatepoch = jd0 + whole - 1.0;
    r.jdsatepochF = epochdays - whole;
    r.epochyr = epochyr;
    r.epochdays = epochdays;

    double epoch = r.jdsatepoch + r.jdsatepochF - 2433281.5;
    if (!sgp4init(wgs72, 'i', satnum, epoch, r.bstar, r.ndot, r.nddot, r.ecco, r.argpo,
                  r.inclo, r.mo, r.no_kozai, r.nodeo, r)) {
        return false;
    }
    return r.error == 0;
}

/* ---------- Coordinates ---------- */

double sat_jd(time_t unix_time, double extra_seconds)
{
    return ((double)unix_time + extra_seconds) / 86400.0 + 2440587.5;
}

static void observer_ecef(const observer_t *obs, double out[3])
{
    const double a = 6378.137, f = 1.0 / 298.257223563, e2 = f * (2.0 - f);
    double lat = obs->lat_deg * DEG, lon = obs->lon_deg * DEG, h = obs->alt_m / 1000.0;
    double n = a / sqrt(1.0 - e2 * sin(lat) * sin(lat));
    out[0] = (n + h) * cos(lat) * cos(lon);
    out[1] = (n + h) * cos(lat) * sin(lon);
    out[2] = (n * (1.0 - e2) + h) * sin(lat);
}

bool sat_look(satellite_t *sat, const observer_t *obs, double jd, look_t *out)
{
    elsetrec &r = sat->rec;
    double tsince = (jd - r.jdsatepoch - r.jdsatepochF) * 1440.0;
    double pos[3], vel[3];
    if (!sgp4(r, tsince, pos, vel) || r.error != 0) return false;

    /* TEME to Earth-fixed: rotate by Greenwich sidereal time (polar motion ignored). */
    double g = gstime_SGP4(jd);
    double x = cos(g) * pos[0] + sin(g) * pos[1];
    double y = -sin(g) * pos[0] + cos(g) * pos[1];
    double z = pos[2];

    double o[3];
    observer_ecef(obs, o);
    double rx = x - o[0], ry = y - o[1], rz = z - o[2];

    double lat = obs->lat_deg * DEG, lon = obs->lon_deg * DEG;
    double south = sin(lat) * cos(lon) * rx + sin(lat) * sin(lon) * ry - cos(lat) * rz;
    double east = -sin(lon) * rx + cos(lon) * ry;
    double up = cos(lat) * cos(lon) * rx + cos(lat) * sin(lon) * ry + sin(lat) * rz;
    double range = sqrt(rx * rx + ry * ry + rz * rz);

    out->range_km = range;
    out->el_deg = asin(up / range) / DEG;
    double az = atan2(east, -south) / DEG;
    out->az_deg = az < 0 ? az + 360.0 : az;
    out->alt_km = sqrt(pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2]) - 6378.135;
    return true;
}

/* ---------- Passes ---------- */

static double elevation_at(satellite_t *sat, const observer_t *obs, time_t t, double frac, look_t *look)
{
    look_t l;
    if (!sat_look(sat, obs, sat_jd(t, frac), &l)) return -90.0;
    if (look) *look = l;
    return l.el_deg;
}

/* Bisects between a (below) and b (above) or vice versa to about one second. */
static double find_crossing(satellite_t *sat, const observer_t *obs, time_t base, double a, double b)
{
    double ea = elevation_at(sat, obs, base, a, NULL);
    for (int i = 0; i < 12 && b - a > 1.0; ++i) {
        double m = (a + b) / 2.0;
        double em = elevation_at(sat, obs, base, m, NULL);
        if ((em > 0) == (ea > 0)) {
            a = m;
            ea = em;
        } else {
            b = m;
        }
    }
    return (a + b) / 2.0;
}

bool sat_next_pass(satellite_t *sat, const observer_t *obs, time_t from, time_t until,
                   double min_el_deg, int step_s, pass_t *out)
{
    double prev = elevation_at(sat, obs, from, 0, NULL);
    double t = 0;
    const double end = difftime(until, from);

    /* If it is already up, step back to find this pass's rise. */
    if (prev > 0) {
        double back = 0;
        while (back > -3600 && elevation_at(sat, obs, from, back - step_s, NULL) > 0) back -= step_s;
        t = back - step_s;
        prev = elevation_at(sat, obs, from, t, NULL);
    }

    while (t < end) {
        double next_t = t + step_s;
        double e = elevation_at(sat, obs, from, next_t, NULL);
        if (prev <= 0 && e > 0) {
            double rise = find_crossing(sat, obs, from, t, next_t);
            /* Follow the pass to its set, tracking the peak. */
            double best_t = next_t, best_e = e, pt = next_t;
            while (pt < end + 3600) {
                double nt = pt + step_s / 2.0;
                double ne = elevation_at(sat, obs, from, nt, NULL);
                if (ne > best_e) {
                    best_e = ne;
                    best_t = nt;
                }
                if (ne <= 0) {
                    double set = find_crossing(sat, obs, from, pt, nt);
                    /* Refine the peak with a few golden-section steps. */
                    double lo = best_t - step_s / 2.0, hi = best_t + step_s / 2.0;
                    for (int i = 0; i < 10; ++i) {
                        double m1 = hi - (hi - lo) * 0.618, m2 = lo + (hi - lo) * 0.618;
                        if (elevation_at(sat, obs, from, m1, NULL) > elevation_at(sat, obs, from, m2, NULL)) hi = m2;
                        else lo = m1;
                    }
                    best_t = (lo + hi) / 2.0;
                    look_t peak, r_look, s_look;
                    best_e = elevation_at(sat, obs, from, best_t, &peak);
                    if (best_e >= min_el_deg) {
                        elevation_at(sat, obs, from, rise, &r_look);
                        elevation_at(sat, obs, from, set, &s_look);
                        out->rise = from + (time_t)llround(rise);
                        out->culminate = from + (time_t)llround(best_t);
                        out->set = from + (time_t)llround(set);
                        out->max_el_deg = best_e;
                        out->rise_az_deg = r_look.az_deg;
                        out->set_az_deg = s_look.az_deg;
                        return true;
                    }
                    t = nt;
                    prev = ne;
                    break;
                }
                pt = nt;
            }
            if (pt >= end + 3600) return false;
            continue;
        }
        prev = e;
        t = next_t;
    }
    return false;
}
