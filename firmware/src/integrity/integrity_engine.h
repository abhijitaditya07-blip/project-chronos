#pragma once

#include "../models.h"
#include "geo.h"
#include <RTClib.h>

class IntegrityEngine {
public:
    void setReference(double lat, double lon, bool valid);
    bool hasReference() const { return ref_valid_; }
    double refLat() const { return ref_lat_; }
    double refLon() const { return ref_lon_; }

    IntegrityEvidence evaluate(const GnssFix& fix, const DateTime& rtc, uint32_t nowMs,
                               bool nmeaChecksumOk, bool ppsSeen);

    void resetHistory();

private:
    bool ref_valid_ = false;
    double ref_lat_ = 0.0;
    double ref_lon_ = 0.0;
    uint32_t last_epoch_ = 0;
    uint32_t last_payload_ms_ = 0;
    uint32_t last_packet_hash_ = 0;
    uint32_t repeated_count_ = 0;
    double drift_ewma_ = 0.0;
    int drift_sign_count_ = 0;
};
