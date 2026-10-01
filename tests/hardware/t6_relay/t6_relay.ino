// TEST 6 - Relay + data-path cut. Wire as in the guide (relay IN = GPIO23, COM <- GPS TX, NO -> GPIO18).
// Every 5 s the relay toggles. GPS bytes must ALWAYS flow (GPIO16 tap).
// "host" bytes (GPIO18, after the relay contact) must flow ONLY while the relay is CLOSED.
#include <driver/gpio.h>
const int PIN_RELAY_IN = 23, PIN_GPS_RX = 16, PIN_HOST_RX = 18, PIN_HOST_TX_UNUSED = 19;
const bool RELAY_ACTIVE_LOW = true;        // set false for a HIGH-trigger module
HardwareSerial GPSSerial(2);
HardwareSerial HostSerial(1);
bool closed = false;
unsigned long lastToggle = 0, lastReport = 0, gpsBytes = 0, hostBytes = 0;

void relaySet(bool c) {
  closed = c;
  if (RELAY_ACTIVE_LOW) { if (c) { pinMode(PIN_RELAY_IN, OUTPUT); digitalWrite(PIN_RELAY_IN, LOW); } else pinMode(PIN_RELAY_IN, INPUT); }
  else { pinMode(PIN_RELAY_IN, OUTPUT); digitalWrite(PIN_RELAY_IN, c ? HIGH : LOW); }
}
void setup() {
  relaySet(false);
  Serial.begin(115200);
  GPSSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, 17);
  HostSerial.begin(9600, SERIAL_8N1, PIN_HOST_RX, PIN_HOST_TX_UNUSED);
  gpio_pullup_en((gpio_num_t)PIN_HOST_RX);
  Serial.println("Relay test: you should hear a click every 5 s.");
}
void loop() {
  while (GPSSerial.available()) { GPSSerial.read(); gpsBytes++; }
  while (HostSerial.available()) { HostSerial.read(); hostBytes++; }
  if (millis() - lastToggle >= 5000) { lastToggle = millis(); relaySet(!closed); }
  if (millis() - lastReport >= 1000) {
    lastReport = millis();
    Serial.printf("relay=%s | GPS bytes/s=%lu | HOST bytes/s=%lu\n", closed ? "CLOSED" : "OPEN  ", gpsBytes, hostBytes);
    gpsBytes = 0; hostBytes = 0;
  }
}
