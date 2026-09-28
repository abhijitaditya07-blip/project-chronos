#pragma once

#include "../models.h"
#include "../integrity/integrity_engine.h"

class SecurityStateMachine {
public:
    ChronosState state() const { return state_; }
    void reset();
    bool update(const IntegrityEvidence& e);
    const char* lastReason() const { return last_reason_; }

private:
    ChronosState state_ = ChronosState::STARTUP;
    uint32_t bad_count_ = 0;
    uint32_t good_count_ = 0;
    const char* last_reason_ = "STARTUP";
};
