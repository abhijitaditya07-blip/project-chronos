#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define RELAY_PIN 5
#define LED_PIN 19
#define BTN_CLEAN 12
#define BTN_SPOOF 14

const int MAX_DRIFT_MS = 50;
unsigned long rtc_time = 0;
unsigned long gps_time = 0;
long drift = 0;

bool system_locked = false; 
long last_bad_drift = 0; // Stores the anomaly value for the incident log

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BTN_CLEAN, INPUT_PULLUP);
    pinMode(BTN_SPOOF, INPUT_PULLUP);
    digitalWrite(RELAY_PIN, LOW); 
    digitalWrite(LED_PIN, LOW);

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 20);
    display.println("PROJECT CHRONOS");
    display.println("Initializing...");
    display.display();
    delay(1500);
}

void updateDisplay(bool airgap, long display_drift) {
    display.clearDisplay();
    
    if (airgap) {
        // Cinematic Alert Mode: Invert entire screen to white
        display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
        display.setTextColor(BLACK);
    } else {
        display.setTextColor(WHITE);
    }

    display.setCursor(5, 5);
    display.println("--- CHRONOS SOC ---");
    
    display.setCursor(5, 20);
    display.print("RTC: "); display.println(rtc_time);
    display.setCursor(5, 30);
    display.print("GPS: "); display.println(gps_time);
    
    display.setCursor(5, 45);
    display.print("Drift: "); 
    display.print(display_drift);
    display.println(" ms");

    display.setCursor(5, 55);
    if (airgap) {
        display.println(">> AIR-GAP ENGAGED <<");
    } else {
        display.println("   [SYSTEM SECURE]   ");
    }
    display.display();
}

void loop() {
    rtc_time = millis();
    
    // 1. Read feeds and add realistic sensor jitter
    if (digitalRead(BTN_SPOOF) == LOW) {
        gps_time = rtc_time + 3450 + random(10, 45); // Spoofed jump + jitter
    } else {
        gps_time = rtc_time + random(0, 3); // Clean clock sync with natural micro-fluctuations
    }

    drift = abs((long)(gps_time - rtc_time));

    // 2. Hardware-Layer Threat Detection
    if (drift > MAX_DRIFT_MS) {
        system_locked = true; 
        last_bad_drift = drift; // Freeze the massive drift spike for the UI
    }

    // 3. Manual Engineer Reset
    if (digitalRead(BTN_CLEAN) == LOW) {
        system_locked = false; 
        last_bad_drift = 0;
    }

    // 4. Actuate Defenses
    if (system_locked) {
        digitalWrite(RELAY_PIN, HIGH); // Trip relay
        digitalWrite(LED_PIN, HIGH);   // Ignite physical warning LED
        updateDisplay(true, last_bad_drift);
    } else {
        digitalWrite(RELAY_PIN, LOW); // Secure
        digitalWrite(LED_PIN, LOW);
        updateDisplay(false, drift);
    }

    delay(100); 
}