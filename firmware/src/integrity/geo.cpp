#include "geo.h"
#include <math.h>

static constexpr double EARTH_RADIUS_M = 6371000.0;
static constexpr double DEG_TO_RAD = 0.017453292519943295;

double haversineMeters(double lat1, double lon1, double lat2, double lon2) {
    double p1 = lat1 * DEG_TO_RAD;
    double p2 = lat2 * DEG_TO_RAD;
    double dp = (lat2 - lat1) * DEG_TO_RAD;
    double dl = (lon2 - lon1) * DEG_TO_RAD;
    double a = sin(dp/2)*sin(dp/2) + cos(p1)*cos(p2)*sin(dl/2)*sin(dl/2);
    return EARTH_RADIUS_M * 2.0 * atan2(sqrt(a), sqrt(1.0-a));
}
