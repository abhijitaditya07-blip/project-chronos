// TEST 3 - Buttons (wired to GND, internal pull-up). Idle reads 1, pressed reads 0.
const int PIN_BTN_ACK = 32, PIN_BTN_SIM = 33;
int lastAck = -1, lastSim = -1;
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_ACK, INPUT_PULLUP);
  pinMode(PIN_BTN_SIM, INPUT_PULLUP);
  Serial.println("Press each button. Idle = 1, pressed = 0.");
}
void loop() {
  int a = digitalRead(PIN_BTN_ACK), s = digitalRead(PIN_BTN_SIM);
  if (a != lastAck) { Serial.printf("ACK (GPIO32): %s\n", a ? "released (1)" : "PRESSED (0)"); lastAck = a; }
  if (s != lastSim) { Serial.printf("SIM (GPIO33): %s\n", s ? "released (1)" : "PRESSED (0)"); lastSim = s; }
  delay(10);
}
