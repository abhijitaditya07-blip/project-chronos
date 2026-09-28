#include "telemetry.h"
#include <Arduino.h>

String telemetryJson(ChronosState state, const IntegrityEvidence& e, const GnssFix& fix,
                     const DateTime& rtc, bool refValid, double refLat, double refLon,
                     bool holdover, const char* reason, bool testMode) {
    String out = "{";
    out += "\"state\":\"" + String(stateName(state)) + "\",";
    out += "\"integrity\":" + String(e.integrity, 1) + ",";
    out += "\"gnss_valid\":" + String(fix.valid ? "true" : "false") + ",";
    out += "\"gnss_time\":" + String(fix.has_time ? fix.epoch : 0) + ",";
    out += "\"rtc_time\":" + String((unsigned long)rtc.unixtime()) + ",";
    out += "\"time_residual_ms\":" + String(e.time_residual_ms) + ",";
    out += "\"distance_from_reference_m\":" + String(e.distance_m, 2) + ",";
    out += "\"packet_age_ms\":" + String(fix.last_update_ms ? millis() - fix.last_update_ms : 999999UL) + ",";
    out += "\"relay\":\"" + String(holdover ? "HOLDOVER" : "GNSS") + "\",";
    out += "\"holdover\":" + String(holdover ? "true" : "false") + ",";
    out += "\"test_mode\":" + String(testMode ? "true" : "false") + ",";
    out += "\"reference_valid\":" + String(refValid ? "true" : "false") + ",";
    out += "\"reference_lat\":" + String(refLat, 6) + ",";
    out += "\"reference_lon\":" + String(refLon, 6) + ",";
    out += "\"reason\":\"" + String(reason ? reason : "NONE") + "\",";
    out += "\"nmea_ok\":" + String(e.nmea_ok ? "true" : "false") + ",";
    out += "\"freshness_ok\":" + String(e.freshness_ok ? "true" : "false") + ",";
    out += "\"rtc_ok\":" + String(e.rtc_ok ? "true" : "false") + ",";
    out += "\"position_ok\":" + String(e.position_ok ? "true" : "false") + ",";
    out += "\"replay_ok\":" + String(e.replay_ok ? "true" : "false") + ",";
    out += "\"freeze_ok\":" + String(e.freeze_ok ? "true" : "false") + ",";
    out += "\"pps_seen\":" + String(e.pps_seen ? "true" : "false");
    out += "}";
    return out;
}
