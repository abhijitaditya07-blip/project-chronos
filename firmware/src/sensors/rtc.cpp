#include "rtc.h"

bool ChronosRTC::begin() {
    ready_ = rtc_.begin();
    if (ready_) {
        lost_power_ = rtc_.lostPower();
        setFromBuildIfNeeded();
    }
    return ready_;
}

void ChronosRTC::setFromBuildIfNeeded() {
    if (lost_power_) {
        rtc_.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
}

DateTime ChronosRTC::now() {
    if (!ready_) return DateTime(2000, 1, 1, 0, 0, 0);
    return rtc_.now();
}


bool ChronosRTC::syncFromGnss(uint32_t epoch) {
    if (!ready_ || epoch < 946684800UL) return false;
    rtc_.adjust(DateTime(epoch));
    lost_power_ = false;
    return true;
}
