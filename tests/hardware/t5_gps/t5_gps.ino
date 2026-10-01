// TEST 5 - GPS UART / NMEA. GPS TX -> GPIO16. Serial Monitor at 115200.
HardwareSerial GPSSerial(2);
unsigned long bytesTotal = 0, lastReport = 0, dollars = 0;
void setup() {
  Serial.begin(115200);
  delay(500);
  GPSSerial.setRxBufferSize(1024);
  GPSSerial.begin(9600, SERIAL_8N1, 16, 17);   // baud 9600, RX = GPIO16, TX = GPIO17 (unused)
  Serial.println("Raw GPS output below. Expect lines starting with $GPRMC, $GPGGA, $GPGSV ...");
}
void loop() {
  while (GPSSerial.available()) {
    char c = GPSSerial.read();
    bytesTotal++;
    if (c == '$') dollars++;
    Serial.write(c);                           // echo everything to the Serial Monitor
  }
  if (millis() - lastReport > 5000) {
    lastReport = millis();
    Serial.printf("\n--- %lu bytes, %lu sentences in %lu s ---\n", bytesTotal, dollars, millis() / 1000);
    if (bytesTotal == 0) Serial.println("--- NO DATA: check GPS TX->GPIO16, GPS power, common GND ---");
  }
}
