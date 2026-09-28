#include "nmea_utils.h"
#include <time.h>
#include <math.h>

bool validNmeaChecksum(const String& sentence) {
    if (!sentence.startsWith("$") || sentence.indexOf('*') < 0) return false;
    int star = sentence.lastIndexOf('*');
    if (star < 2 || star + 3 > (int)sentence.length()) return false;
    uint8_t sum = 0;
    for (int i = 1; i < star; ++i) sum ^= static_cast<uint8_t>(sentence[i]);
    String supplied = sentence.substring(star + 1);
    supplied.toUpperCase();
    char expected[3];
    snprintf(expected, sizeof(expected), "%02X", sum);
    return supplied == expected;
}

String sentenceType(const String& sentence) {
    if (!sentence.startsWith("$")) return "";
    int comma = sentence.indexOf(',');
    int star = sentence.indexOf('*');
    int end = comma > 0 ? comma : star;
    if (end < 0) return "";
    return sentence.substring(1, end);
}

String nmeaWithChecksum(const String& payload) {
    uint8_t sum = 0;
    for (size_t i = 0; i < payload.length(); ++i) sum ^= static_cast<uint8_t>(payload[i]);
    char buf[4];
    snprintf(buf, sizeof(buf), "%02X", sum);
    return "$" + payload + "*" + String(buf);
}

static void latLonFields(double value, bool lat, String& field, char& hemi) {
    double a = fabs(value);
    int degrees = static_cast<int>(a);
    double minutes = (a - degrees) * 60.0;
    hemi = value >= 0 ? (lat ? 'N' : 'E') : (lat ? 'S' : 'W');
    field = String(degrees < 100 && lat ? (degrees < 10 ? "0" : "" ) : "");
    if (lat) {
        char b[16]; snprintf(b, sizeof(b), "%02d%07.4f", degrees, minutes); field = b;
    } else {
        char b[16]; snprintf(b, sizeof(b), "%03d%07.4f", degrees, minutes); field = b;
    }
}

String formatRmc(uint32_t epoch, double lat, double lon, bool valid, double speedKmh) {
    time_t t = static_cast<time_t>(epoch);
    struct tm gm{};
    gmtime_r(&t, &gm);
    char ts[16], ds[12];
    snprintf(ts, sizeof(ts), "%02d%02d%02d.00", gm.tm_hour, gm.tm_min, gm.tm_sec);
    snprintf(ds, sizeof(ds), "%02d%02d%02d", gm.tm_mday, gm.tm_mon + 1, (gm.tm_year + 1900) % 100);

    String latf, lonf; char lath='N', lonh='E';
    latLonFields(lat, true, latf, lath);
    latLonFields(lon, false, lonf, lonh);
    double knots = speedKmh / 1.852;

    String payload = "GPRMC," + String(ts) + "," + (valid ? "A" : "V") + "," +
                     latf + "," + String(lath) + "," + lonf + "," + String(lonh) + "," +
                     String(knots, 1) + ",000.0," + String(ds) + ",,,N";
    return nmeaWithChecksum(payload);
}
