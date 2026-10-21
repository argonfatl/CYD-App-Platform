#include <cstdio>
#include <cstring>
#include "sat_math.h"
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <unix time> <tles.txt>\n", argv[0]); return 2; }
    FILE *f = fopen(argv[2], "r");
    char n[80], l1[130], l2[130];
    observer_t obs = {41.8781, -88.0067, 220};
    while (fgets(n, sizeof n, f) && fgets(l1, sizeof l1, f) && fgets(l2, sizeof l2, f)) {
        n[strcspn(n, "\r\n")] = 0; l1[strcspn(l1, "\r\n")] = 0; l2[strcspn(l2, "\r\n")] = 0;
        satellite_t s;
        if (!sat_parse_tle(n, l1, l2, &s)) { printf("PARSE FAIL %s\n", n); continue; }
        time_t base = (time_t)atol(argv[1]);
        for (int k = 0; k < 3; ++k) {
            look_t l; time_t t = base + k * 600;
            sat_look(&s, &obs, sat_jd(t, 0), &l);
            printf("LOOK %s %ld %.4f %.4f %.3f %.3f\n", s.name, (long)t, l.az_deg, l.el_deg, l.range_km, l.alt_km);
        }
        pass_t p;
        if (sat_next_pass(&s, &obs, base, base + 2 * 86400, 10, 60, &p))
            printf("PASS %s %ld %ld %ld %.3f %.2f %.2f\n", s.name, (long)p.rise, (long)p.culminate, (long)p.set, p.max_el_deg, p.rise_az_deg, p.set_az_deg);
        else printf("PASS %s none\n", s.name);
    }
}
