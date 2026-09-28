#include "holdover.h"
#include "../gnss/nmea_utils.h"

String makeHoldoverSentence(const DateTime& now, double lat, double lon, bool havePosition) {
    return formatRmc(now.unixtime(), havePosition ? lat : 0.0, havePosition ? lon : 0.0, false, 0.0);
}
