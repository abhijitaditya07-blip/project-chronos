#pragma once

#include <Arduino.h>

bool validNmeaChecksum(const String& sentence);
String sentenceType(const String& sentence);
String formatRmc(uint32_t epoch, double lat, double lon, bool valid, double speedKmh);
String nmeaWithChecksum(const String& payload);
