#pragma once
#include <RTClib.h>
#include <Arduino.h>

String makeHoldoverSentence(const DateTime& now, double lat, double lon, bool havePosition);
