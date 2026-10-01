// TEST 2 - LEDs. Each LED lights alone for 1 s in order: green, yellow, red.
const int PIN_LED_GREEN = 25, PIN_LED_YELLOW = 26, PIN_LED_RED = 27;
const int leds[3] = { PIN_LED_GREEN, PIN_LED_YELLOW, PIN_LED_RED };
const char* names[3] = { "GREEN (GPIO25)", "YELLOW (GPIO26)", "RED (GPIO27)" };
void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 3; i++) pinMode(leds[i], OUTPUT);
}
void loop() {
  for (int i = 0; i < 3; i++) {
    Serial.printf("ON: %s\n", names[i]);
    digitalWrite(leds[i], HIGH);
    delay(1000);
    digitalWrite(leds[i], LOW);
  }
}
