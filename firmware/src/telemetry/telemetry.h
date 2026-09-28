#pragma once
#include "../models.h"
#include <RTClib.h>

String telemetryJson(ChronosState state, const IntegrityEvidence& e, const GnssFix& fix,
                     const DateTime& rtc, bool refValid, double refLat, double refLon,
                     bool holdover, const char* reason, bool testMode);
