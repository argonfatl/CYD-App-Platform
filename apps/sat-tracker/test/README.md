# Host test for the satellite math

Builds `sat_math.cpp` and SGP4 for the computer and compares look angles and passes
with [Skyfield](https://rhodesmill.org/skyfield/):

```sh
cd apps/sat-tracker/test
g++ -O2 -I../main sat_math_test.cpp ../main/sat_math.cpp ../main/sgp4/SGP4.cpp -o sat_math_test
pip install skyfield
python3 compare_with_skyfield.py ./sat_math_test
```

`tles.txt` holds test elements (ISS, HST, a Molniya orbit) with valid checksums; they
are not current, so the output is only for comparing the two implementations.
