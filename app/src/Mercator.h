#ifndef MERCATOR_H
#define MERCATOR_H

#include <math.h>

// The send-location page's drag (MapPicker): Telegram's map pictures are Web Mercator tiles' projection, a world 256 * 2^zoom
// pixels wide at zoom `zoom`, so a drag of the picture is a move of its centre by as many of those pixels. Plain C++, so
// app/tests/mercator_test.cpp checks it on the Mac.

// Moves the centre (lat, lon, in degrees) by (dx, dy) logical pixels of the map of that zoom: x to the east, y to the south.
// The latitude stays within ±85 (Web Mercator ends at about 85.05), the longitude is wrapped into [-180, 180).
inline void mercatorPan(double &lat, double &lon, int zoom, double dx, double dy)
{
    const double pi = 3.14159265358979323846;
    const double world = 256.0 * pow(2.0, zoom);
    const double phi = lat * pi / 180;
    double x = (lon + 180) / 360 * world + dx;
    double y = (1 - log(tan(phi) + 1 / cos(phi)) / pi) / 2 * world + dy;
    lon = fmod(x / world * 360, 360.0);
    if (lon < 0)
        lon += 360;
    lon -= 180;
    lat = atan(sinh(pi * (1 - 2 * y / world))) * 180 / pi;
    if (lat > 85)
        lat = 85;
    if (lat < -85)
        lat = -85;
}

#endif
