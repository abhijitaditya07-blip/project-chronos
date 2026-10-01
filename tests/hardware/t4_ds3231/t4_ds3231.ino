// TEST 4 - DS3231 detection + time. Needs library "RTClib" by Adafruit.
#include <Wire.h>
#include <RTClib.h>
RTC_DS3231 rtc;
void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(21, 22);                       // SDA = GPIO21, SCL = GPIO22
  Serial.println("Scanning I2C bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X%s\n", addr,
                    addr == 0x68 ? "  <- DS3231 clock chip" : (addr == 0x57 ? "  <- module's EEPROM (normal)" : ""));
      found++;
    }
  }
  if (!found) Serial.println("  NOTHING found - check wiring (see guide Part 10)");
  if (!rtc.begin()) { Serial.println("rtc.begin() FAILED - DS3231 not answering"); return; }
  Serial.printf("lostPower flag: %s (YES is normal on first use / new battery)\n", rtc.lostPower() ? "YES" : "no");
}
void loop() {
  DateTime t = rtc.now();
  Serial.printf("RTC %04d-%02d-%02d %02d:%02d:%02d   chip temp %.2f C\n",
                t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second(), rtc.getTemperature());
  delay(1000);
}
