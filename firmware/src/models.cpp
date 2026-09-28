#include "models.h"

const char* stateName(ChronosState state) {
    switch (state) {
        case ChronosState::STARTUP: return "STARTUP";
        case ChronosState::TRUSTED: return "TRUSTED";
        case ChronosState::SUSPECT: return "SUSPECT";
        case ChronosState::QUARANTINE: return "QUARANTINE";
        case ChronosState::HOLDOVER: return "HOLDOVER";
        case ChronosState::RECOVERY: return "RECOVERY";
    }
    return "UNKNOWN";
}
