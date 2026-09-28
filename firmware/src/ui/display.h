#pragma once
#include <Adafruit_SSD1306.h>
#include "../models.h"

class ChronosDisplay {
public:
    bool begin();
    void update(ChronosState state, double integrity, long residual, double distance, const char* reason, bool holdover);
private:
    Adafruit_SSD1306 display_{128, 64, &Wire, -1};
};
