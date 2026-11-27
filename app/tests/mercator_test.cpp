// Run on the Mac: clang++ -std=c++11 -I app/src app/tests/mercator_test.cpp -o /tmp/mercator_test && /tmp/mercator_test
#include "Mercator.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool near(double a, double b, double eps) { return fabs(a - b) < eps; }

int main()
{
    // East by a 360th of the world is one degree of longitude, at any latitude; the latitude stays.
    const double lats[] = { -80, -45.5, 0, 12.34, 45.4642, 84 };
    for (int zoom = 13; zoom <= 18; zoom++) {
        const double world = 256.0 * pow(2.0, zoom);
        for (int i = 0; i < 6; i++) {
            double lat = lats[i], lon = 9.19;
            mercatorPan(lat, lon, zoom, world / 360, 0);
            assert(near(lon, 10.19, 1e-9));
            assert(near(lat, lats[i], 1e-9));
        }
    }
    // A pan and its opposite come back to the start.
    double lat = 45.4642, lon = 9.19;
    mercatorPan(lat, lon, 16, 123.4, -56.7);
    assert(!near(lat, 45.4642, 1e-6) && !near(lon, 9.19, 1e-6));
    mercatorPan(lat, lon, 16, -123.4, 56.7);
    assert(near(lat, 45.4642, 1e-9) && near(lon, 9.19, 1e-9));
    // North (y up the screen) raises the latitude, south lowers it.
    lat = 45.4642, lon = 9.19;
    mercatorPan(lat, lon, 15, 0, -100);
    assert(lat > 45.4642 && near(lon, 9.19, 1e-9));
    lat = 45.4642;
    mercatorPan(lat, lon, 15, 0, 100);
    assert(lat < 45.4642);
    // The longitude wraps at ±180, into [-180, 180).
    const double degree = 256.0 * pow(2.0, 14) / 360;
    lat = 0, lon = 179.5;
    mercatorPan(lat, lon, 14, degree, 0);
    assert(near(lon, -179.5, 1e-9));
    mercatorPan(lat, lon, 14, -degree, 0);
    assert(near(lon, 179.5, 1e-9));
    lat = 0, lon = -179.5;
    mercatorPan(lat, lon, 14, -degree, 0);
    assert(near(lon, 179.5, 1e-9));
    lat = 0, lon = 179.5;
    mercatorPan(lat, lon, 14, degree / 2, 0);
    assert(near(lon, -180, 1e-9));
    // The latitude stops at ±85, however far the drag.
    lat = 84.9, lon = 0;
    mercatorPan(lat, lon, 13, 0, -1e7);
    assert(lat == 85);
    lat = -84.9;
    mercatorPan(lat, lon, 13, 0, 1e7);
    assert(lat == -85);
    printf("mercator_test: ok\n");
    return 0;
}
