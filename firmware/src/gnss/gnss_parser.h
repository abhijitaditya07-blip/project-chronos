#pragma once

#include <TinyGPSPlus.h>
#include "../models.h"

class GnssParser {
public:
    void begin(HardwareSerial& serial);
    void update();
    bool consumeFix(GnssFix& out);
    uint32_t lastValidPacketMs() const { return last_valid_packet_ms_; }
    bool lastSentenceChecksumOk() const { return last_checksum_ok_; }
    bool hadChecksumFailure() const { return checksum_failure_; }
    void clearChecksumFailure() { checksum_failure_ = false; }

private:
    HardwareSerial* serial_ = nullptr;
    TinyGPSPlus gps_;
    String line_;
    GnssFix pending_;
    bool new_fix_ = false;
    bool last_checksum_ok_ = true;
    bool checksum_failure_ = false;
    uint32_t last_valid_packet_ms_ = 0;
    void processLine(const String& line);
};
