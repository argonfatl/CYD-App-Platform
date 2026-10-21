#!/usr/bin/env python3
"""Compare sat_math_test output with Skyfield. Usage: compare_with_skyfield.py <path to sat_math_test>"""
import os, sys, subprocess
from skyfield.api import load, wgs84, EarthSatellite
from datetime import datetime, timezone
ts = load.timescale()
lines = open(os.path.join(os.path.dirname(__file__), 'tles.txt')).read().splitlines()
obs = wgs84.latlon(41.8781, -88.0067, elevation_m=220)
for i in range(0, len(lines), 3):
    name, l1, l2 = lines[i:i+3]
    sat = EarthSatellite(l1, l2, name, ts)
    base = int(sat.epoch.utc_datetime().timestamp()) + 3*3600
    out = subprocess.run([sys.argv[1], str(base), os.path.join(os.path.dirname(__file__), 'tles.txt')], capture_output=True, text=True).stdout.splitlines()
    looks = [o for o in out if o.startswith('LOOK') and name in o]
    passes = [o for o in out if o.startswith('PASS') and name in o]
    print('==', name, 'base', base)
    for o in looks:
        p = o.rsplit(' ', 5); t = int(p[1]); az, el, rng = map(float, p[2:5])
        tt = ts.from_datetime(datetime.fromtimestamp(t, timezone.utc))
        alt, azr, dist = (sat - obs).at(tt).altaz()
        print(f'  t+{t-base:5d}  az {az:8.3f} vs {azr.degrees:8.3f}  el {el:7.3f} vs {alt.degrees:7.3f}  range {rng:9.2f} vs {dist.km:9.2f}')
    t0 = ts.from_datetime(datetime.fromtimestamp(base, timezone.utc))
    t1 = ts.from_datetime(datetime.fromtimestamp(base+2*86400, timezone.utc))
    tt, ev = sat.find_events(obs, t0, t1, altitude_degrees=0.0)
    evs=[]
    for a,b in zip(tt,ev):
        evs.append((b, int(a.utc_datetime().timestamp()), (sat-obs).at(a).altaz()[0].degrees))
    # first pass with max el >= 10
    ref=None
    for k in range(len(evs)):
        if evs[k][0]==0:
            cul=[e for e in evs[k+1:] if e[0]==1]
            st=[e for e in evs[k+1:] if e[0]==2]
            if cul and st and cul[0][2]>=10: ref=(evs[k][1],cul[0][1],st[0][1],cul[0][2]); break
    print('  mine', passes[0] if passes else None)
    print('  ref  rise', ref)
