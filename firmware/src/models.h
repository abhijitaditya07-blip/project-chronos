#pragma once

#include <Arduino.h>
#include <stdint.h>

enum class ChronosState : uint8_t {
    STARTUP,
    TRUSTED,
    SUSPECT,
    QUARANTINE,
    HOLDOVER,
    RECOVERY
};

struct GnssFix {
    bool valid = false;
    bool has_time = false;
    bool has_position = false;
    bool has_speed = false;
    uint32_t epoch = 0;
    double lat = 0.0;
    double lon = 0.0;
    double speed_kmh = 0.0;
    uint32_t last_update_ms = 0;
    uint32_t packet_hash = 0;
};

struct IntegrityEvidence {
    bool nmea_ok = true;
    bool freshness_ok = true;
    bool rtc_ok = true;
    bool position_ok = true;
    bool replay_ok = true;
    bool freeze_ok = true;
    bool pps_seen = false;
    bool hard_fail = false;
    double integrity = 100.0;
    long time_residual_ms = 0;
    double distance_m = 0.0;
    const char* primary_reason = "NONE";
};

const char* stateName(ChronosState state);
