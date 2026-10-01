// TEST 1 - ESP32 + Serial Monitor (115200 baud)
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Chronos test 1: ESP32 is alive");
  Serial.printf("Chip: %s, rev %d, %d MHz, flash %u KB\n",
                ESP.getChipModel(), (int)ESP.getChipRevision(),
                (int)ESP.getCpuFreqMHz(), (unsigned)(ESP.getFlashChipSize() / 1024));
}
void loop() {
  static unsigned long n = 0;
  Serial.printf("heartbeat %lu (uptime %lu s)\n", n++, millis() / 1000);
  delay(1000);
}
