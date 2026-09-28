#include "integrity_engine.h"
#include "../config.h"
#include <math.h>

static uint32_t dateTimeEpoch(const DateTime& dt) { return dt.unixtime(); }

void IntegrityEngine::setReference(double lat, double lon, bool valid) {
    ref_lat_ = lat;
    ref_lon_ = lon;
    ref_valid_ = valid;
}

void IntegrityEngine::resetHistory() {
    last_epoch_ = 0;
    last_payload_ms_ = 0;
    repeated_count_ = 0;
    drift_ewma_ = 0.0;
    drift_sign_count_ = 0;
}

IntegrityEvidence IntegrityEngine::evaluate(const GnssFix& fix, const DateTime& rtc, uint32_t nowMs,
                                            bool nmeaChecksumOk, bool ppsSeen) {
    IntegrityEvidence e;
    e.nmea_ok = nmeaChecksumOk;
    e.pps_seen = ppsSeen;

    if (!fix.has_time && !fix.has_position) {
        e.freshness_ok = false;
        e.integrity = 20.0;
        e.primary_reason = "NO_VALID_GNSS";
        e.hard_fail = true;
        return e;
    }

    uint32_t rtcEpoch = dateTimeEpoch(rtc);
    if (fix.has_time) {
        long long residual = static_cast<long long>(fix.epoch) * 1000LL - static_cast<long long>(rtcEpoch) * 1000LL;
        e.time_residual_ms = static_cast<long>(residual);

        long absResidual = labs(e.time_residual_ms);
        if (absResidual > HARD_TIME_RESIDUAL_MS) {
            e.rtc_ok = false;
            e.hard_fail = true;
            e.primary_reason = "TIME_RESIDUAL_HIGH";
        } else if (absResidual > SUSPECT_TIME_RESIDUAL_MS) {
            e.rtc_ok = false;
            if (e.primary_reason == String("NONE").c_str()) e.primary_reason = "TIME_RESIDUAL_HIGH";
        }

        if (last_epoch_ != 0 && fix.epoch < last_epoch_) {
            e.replay_ok = false;
            e.hard_fail = true;
            e.primary_reason = "TIMESTAMP_REGRESSION";
        }
        if (fix.packet_hash != 0 && fix.packet_hash == last_packet_hash_) {
            repeated_count_++;
        } else {
            repeated_count_ = 0;
        }
        last_packet_hash_ = fix.packet_hash;
        if (repeated_count_ >= 3) {
            e.freeze_ok = false;
            e.replay_ok = false;
            e.hard_fail = true;
            e.primary_reason = "REPLAY_OR_FREEZE";
        }
        last_epoch_ = fix.epoch;
    }

    if (fix.last_update_ms > 0 && nowMs - fix.last_update_ms > PACKET_TIMEOUT_MS) {
        e.freshness_ok = false;
        e.hard_fail = true;
        e.primary_reason = "STALE_GNSS";
    }

    if (fix.has_position) {
        if (fabs(fix.lat) > 90.0 || fabs(fix.lon) > 180.0) {
            e.position_ok = false;
            e.hard_fail = true;
            e.primary_reason = "INVALID_COORDINATES";
        }
        if (ref_valid_) {
            e.distance_m = haversineMeters(ref_lat_, ref_lon_, fix.lat, fix.lon);
            if (e.distance_m > HARD_POSITION_RADIUS_M) {
                e.position_ok = false;
                e.hard_fail = true;
                e.primary_reason = "POSITION_TELEPORT";
            } else if (e.distance_m > STATIONARY_RADIUS_M) {
                e.position_ok = false;
                if (String(e.primary_reason) == "NONE") e.primary_reason = "POSITION_OUT_OF_BOUNDS";
            }
        }
    }

    // Lightweight slow-drift detector. It accumulates persistent residual direction.
    if (fix.has_time) {
        double residual = static_cast<double>(e.time_residual_ms);
        drift_ewma_ = 0.2 * residual + 0.8 * drift_ewma_;
        if (fabs(drift_ewma_) > 60.0) {
            drift_sign_count_++;
        } else if (drift_sign_count_ > 0) {
            drift_sign_count_--;
        }
        if (drift_sign_count_ >= 5) {
            e.rtc_ok = false;
            if (String(e.primary_reason) == "NONE") e.primary_reason = "PERSISTENT_TIME_DRIFT";
        }
    }

    // Score is intentionally simple and explainable.
    double score = 100.0;
    if (!e.nmea_ok) score -= 35;
    if (!e.freshness_ok) score -= 35;
    if (!e.rtc_ok) score -= 25;
    if (!e.position_ok) score -= 25;
    if (!e.replay_ok) score -= 40;
    if (!e.freeze_ok) score -= 30;
    score = constrain(score, 0.0, 100.0);
    e.integrity = score;

    if (String(e.primary_reason) == "NONE") {
        if (!e.nmea_ok) e.primary_reason = "NMEA_CHECKSUM";
        else if (!e.freshness_ok) e.primary_reason = "STALE_GNSS";
        else if (!e.rtc_ok) e.primary_reason = "TIME_RESIDUAL_HIGH";
        else if (!e.position_ok) e.primary_reason = "POSITION_OUT_OF_BOUNDS";
        else if (!e.replay_ok) e.primary_reason = "REPLAY_DETECTED";
    }
    return e;
}
