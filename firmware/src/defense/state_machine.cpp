#include "state_machine.h"
#include "../config.h"

void SecurityStateMachine::reset() {
    state_ = ChronosState::STARTUP;
    bad_count_ = 0;
    good_count_ = 0;
    last_reason_ = "STARTUP";
}

bool SecurityStateMachine::update(const IntegrityEvidence& e) {
    ChronosState old = state_;
    if (e.hard_fail || e.integrity < 30.0) {
        state_ = ChronosState::QUARANTINE;
        bad_count_ = 0;
        good_count_ = 0;
    } else if (e.integrity < 70.0) {
        if (state_ == ChronosState::TRUSTED || state_ == ChronosState::STARTUP || state_ == ChronosState::RECOVERY) {
            bad_count_++;
            if (bad_count_ >= SUSPECT_CONFIRM_SAMPLES) state_ = ChronosState::SUSPECT;
        }
        good_count_ = 0;
    } else {
        bad_count_ = 0;
        good_count_++;
        if (state_ == ChronosState::STARTUP && good_count_ >= 3) state_ = ChronosState::TRUSTED;
        else if (state_ == ChronosState::SUSPECT && good_count_ >= 5) state_ = ChronosState::TRUSTED;
        else if (state_ == ChronosState::RECOVERY && good_count_ >= RECOVERY_GOOD_SAMPLES) state_ = ChronosState::TRUSTED;
        else if (state_ == ChronosState::QUARANTINE) state_ = ChronosState::RECOVERY;
    }

    if (state_ == ChronosState::QUARANTINE || state_ == ChronosState::SUSPECT) {
        if (e.primary_reason) last_reason_ = e.primary_reason;
    }
    return state_ != old;
}
