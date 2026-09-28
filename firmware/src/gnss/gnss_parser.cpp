#include "gnss_parser.h"
#include "nmea_utils.h"
#include <RTClib.h>

void GnssParser::begin(HardwareSerial& serial) {
    serial_ = &serial;
    line_.reserve(128);
}

void GnssParser::update() {
    if (!serial_) return;
    while (serial_->available()) {
        char c = static_cast<char>(serial_->read());
        if (c == '\n') {
            processLine(line_);
            line_ = "";
        } else if (c != '\r') {
            if (line_.length() < 127) line_ += c;
            else line_ = "";
        }
    }
}

static uint32_t hashLine(const String& s) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < s.length(); ++i) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

void GnssParser::processLine(const String& raw) {
    if (raw.length() < 8) return;
    String line = raw;
    if (!line.startsWith("$")) return;

    last_checksum_ok_ = validNmeaChecksum(line);
    if (!last_checksum_ok_) {
        checksum_failure_ = true;
        return;
    }

    for (size_t i = 0; i < line.length(); ++i) gps_.encode(line[i]);
    last_valid_packet_ms_ = millis();

    bool changed = false;
    if (gps_.location.isUpdated() || gps_.time.isUpdated() || gps_.speed.isUpdated()) {
        pending_.valid = gps_.location.isValid();
        pending_.has_position = gps_.location.isValid();
        pending_.has_time = gps_.date.isValid() && gps_.time.isValid();
        pending_.has_speed = gps_.speed.isValid();
        if (pending_.has_position) {
            pending_.lat = gps_.location.lat();
            pending_.lon = gps_.location.lng();
        }
        if (pending_.has_speed) pending_.speed_kmh = gps_.speed.kmph();
        if (pending_.has_time) {
            DateTime dt(
                gps_.date.year(), gps_.date.month(), gps_.date.day(),
                gps_.time.hour(), gps_.time.minute(), gps_.time.second());
            pending_.epoch = dt.unixtime();
        }
        pending_.last_update_ms = millis();
        pending_.packet_hash = hashLine(line);
        new_fix_ = true;
        changed = true;
    }
    (void)changed;
}

bool GnssParser::consumeFix(GnssFix& out) {
    if (!new_fix_) return false;
    out = pending_;
    new_fix_ = false;
    return true;
}
