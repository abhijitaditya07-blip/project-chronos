#include "display.h"
#include "../config.h"
#include <Wire.h>

bool ChronosDisplay::begin() {
    if (!display_.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) return false;
    display_.clearDisplay();
    display_.setTextColor(SSD1306_WHITE);
    display_.setTextSize(1);
    display_.setCursor(0, 0);
    display_.println("CHRONOS");
    display_.println("GNSS integrity gateway");
    display_.display();
    return true;
}

void ChronosDisplay::update(ChronosState state, double integrity, long residual, double distance, const char* reason, bool holdover) {
    display_.clearDisplay();
    display_.setTextColor(SSD1306_WHITE);
    display_.setTextSize(1);
    display_.setCursor(0,0);
    display_.print("STATE: "); display_.println(stateName(state));
    display_.print("TRUST: "); display_.print((int)integrity); display_.println("/100");
    display_.print("DT: "); display_.print(residual); display_.println("ms");
    display_.print("POS: "); display_.print((int)distance); display_.println("m");
    display_.print("PATH: "); display_.println(holdover ? "HOLDOVER" : "GNSS");
    display_.print("WHY: "); display_.println(reason ? reason : "NONE");
    display_.display();
}
