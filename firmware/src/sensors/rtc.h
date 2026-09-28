#pragma once

#include <RTClib.h>

class ChronosRTC {
public:
    bool begin();
    DateTime now();
    bool valid() const { return ready_; }
    bool lostPower() const { return lost_power_; }
    void setFromBuildIfNeeded();
    bool syncFromGnss(uint32_t epoch);

private:
    RTC_DS3231 rtc_;
    bool ready_ = false;
    bool lost_power_ = false;
};
