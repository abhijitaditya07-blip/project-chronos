#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>

#include "config.h"
#include "models.h"
#include "gnss/gnss_parser.h"
#include "gnss/nmea_utils.h"
#include "sensors/rtc.h"
#include "integrity/integrity_engine.h"
#include "defense/state_machine.h"
#include "defense/holdover.h"
#include "ui/display.h"
#include "telemetry/telemetry.h"

HardwareSerial GPSSerial(2);   // UART2: NEO-6M
HardwareSerial HostSerial(1);  // UART1: relay NC -> host RX
GnssParser gnss;
ChronosRTC rtc;
IntegrityEngine integrity;
SecurityStateMachine security;
ChronosDisplay oled;
Preferences prefs;

volatile uint32_t pps_count = 0;
volatile uint32_t last_pps_us = 0;

GnssFix latestFix;
IntegrityEvidence latestEvidence;
DateTime latestRtc;

bool displayReady = false;
bool referenceValid = false;
double referenceLat = 0.0;
double referenceLon = 0.0;
double refLatSum = 0.0;
double refLonSum = 0.0;
uint32_t enrollmentCount = 0;
bool testMode = false;
String activeTest;
uint32_t lastOled = 0;
uint32_t lastTelemetry = 0;
uint32_t lastRtcUpdate = 0;

void IRAM_ATTR ppsISR() {
    pps_count++;
    last_pps_us = micros();
}

void saveReference() {
    prefs.putBool("ref_ok", referenceValid);
    prefs.putDouble("ref_lat", referenceLat);
    prefs.putDouble("ref_lon", referenceLon);
}

void clearReference() {
    referenceValid = false;
    referenceLat = referenceLon = 0.0;
    enrollmentCount = 0;
    refLatSum = refLonSum = 0.0;
    prefs.putBool("ref_ok", false);
    integrity.setReference(0, 0, false);
}

void loadReference() {
    referenceValid = prefs.getBool("ref_ok", false);
    referenceLat = prefs.getDouble("ref_lat", 0.0);
    referenceLon = prefs.getDouble("ref_lon", 0.0);
    integrity.setReference(referenceLat, referenceLon, referenceValid);
}

void enrollReference(const GnssFix& fix) {
    if (referenceValid || !fix.has_position || !fix.valid) return;
    refLatSum += fix.lat;
    refLonSum += fix.lon;
    enrollmentCount++;
    if (enrollmentCount >= ENROLL_SAMPLES) {
        referenceLat = refLatSum / enrollmentCount;
        referenceLon = refLonSum / enrollmentCount;
        referenceValid = true;
        integrity.setReference(referenceLat, referenceLon, true);
        saveReference();
    }
}

void injectTestFix(GnssFix& fix, const DateTime& now) {
    static uint32_t driftSeconds = 0;
    static GnssFix replayFix;
    static bool replayInitialized = false;

    if (activeTest == "NORMAL") {
        fix = latestFix;
        fix.valid = true; fix.has_time = true; fix.has_position = true; fix.has_speed = true;
        fix.epoch = now.unixtime();
        fix.lat = referenceValid ? referenceLat : 12.9716;
        fix.lon = referenceValid ? referenceLon : 77.5946;
        fix.speed_kmh = 0.0;
        fix.last_update_ms = millis();
        fix.packet_hash = 0x1000 + (millis() / 1000);
    } else if (activeTest == "TEST_TIME_JUMP") {
        fix = latestFix;
        fix.valid = true; fix.has_time = true; fix.has_position = true;
        fix.epoch = now.unixtime() + 10800;
        fix.last_update_ms = millis();
        fix.packet_hash = millis() / 500;
    } else if (activeTest == "TEST_SLOW_DRIFT") {
        driftSeconds = millis() / 1000;
        fix = latestFix;
        fix.valid = true; fix.has_time = true; fix.has_position = true;
        fix.epoch = now.unixtime() + driftSeconds;
        fix.last_update_ms = millis();
        fix.packet_hash = millis() / 500;
    } else if (activeTest == "TEST_TELEPORT") {
        fix = latestFix;
        fix.valid = true; fix.has_time = true; fix.has_position = true;
        fix.epoch = now.unixtime();
        fix.lat = referenceValid ? referenceLat + 1.0 : 13.9716;
        fix.lon = referenceValid ? referenceLon + 1.0 : 78.5946;
        fix.last_update_ms = millis();
        fix.packet_hash = millis() / 500;
    } else if (activeTest == "TEST_REPLAY") {
        if (!replayInitialized) { replayFix = latestFix; replayInitialized = true; }
        fix = replayFix;
        fix.valid = true; fix.has_time = true; fix.has_position = true;
        fix.last_update_ms = millis();
    } else if (activeTest == "TEST_FREEZE") {
        fix = latestFix;
        fix.valid = true;
        fix.last_update_ms = millis();
    } else if (activeTest == "TEST_BLACKOUT" || activeTest == "TEST_BAD_NMEA") {
        fix = GnssFix{};
    }
}

void handleCommand(String cmd) {
    cmd.trim();
    if (cmd == "NORMAL") {
        testMode = false; activeTest = ""; security.reset(); return;
    }
    if (cmd == "CLEAR_REF" || cmd == "ENROLL") {
        clearReference(); security.reset(); return;
    }
    if (cmd == "SYNC_RTC") {
        if (latestFix.has_time && latestFix.valid) rtc.syncFromGnss(latestFix.epoch);
        return;
    }
    if (cmd.startsWith("TEST_")) {
        activeTest = cmd; testMode = true; security.reset();
    }
}

void readCommands() {
    static String cmd;
    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\n') { handleCommand(cmd); cmd = ""; }
        else if (c != '\r' && cmd.length() < 48) cmd += c;
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, RELAY_HOLDOVER_LEVEL); // safe startup
    pinMode(GPS_PPS_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(GPS_PPS_PIN), ppsISR, RISING);

    GPSSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    HostSerial.begin(9600, SERIAL_8N1, -1, HOST_TX_PIN);
    gnss.begin(GPSSerial);
    rtc.begin();

    prefs.begin("chronos", false);
    loadReference();
    displayReady = oled.begin();
    security.reset();
    latestRtc = rtc.now();

    Serial.println("CHRONOS_READY");
}

void loop() {
    readCommands();
    const uint32_t now = millis();

    bool gotNewFix = false;
    if (!testMode) {
        gnss.update();
        GnssFix parsed;
        if (gnss.consumeFix(parsed)) {
            latestFix = parsed;
            enrollReference(latestFix);
            gotNewFix = true;
        }
    }

    if (now - lastRtcUpdate >= RTC_PERIOD_MS) {
        latestRtc = rtc.now();
        lastRtcUpdate = now;
    }

    // If the RTC lost power, allow one explicit GNSS bootstrap sync.
    // After that, the RTC runs independently.
    if (rtc.lostPower() && latestFix.valid && latestFix.has_time) {
        rtc.syncFromGnss(latestFix.epoch);
        latestRtc = rtc.now();
    }

    GnssFix evaluationFix = latestFix;
    if (testMode) {
        injectTestFix(evaluationFix, latestRtc);
        gotNewFix = true;
    } else if (!latestFix.last_update_ms || now - latestFix.last_update_ms > PACKET_TIMEOUT_MS) {
        evaluationFix = GnssFix{};
        gotNewFix = true;
    }

    const bool ppsSeen = (micros() - last_pps_us) < 1500000UL && pps_count > 0;
    const bool checksumOk = !gnss.hadChecksumFailure();

    if (gotNewFix) {
        latestEvidence = integrity.evaluate(evaluationFix, latestRtc, now, checksumOk, ppsSeen);
        gnss.clearChecksumFailure();
    }

    if (!referenceValid && !testMode) {
        security.reset();
    } else {
        security.update(latestEvidence);
    }

    bool quarantine = security.state() == ChronosState::QUARANTINE ||
                      security.state() == ChronosState::HOLDOVER ||
                      security.state() == ChronosState::RECOVERY ||
                      security.state() == ChronosState::SUSPECT;

    if (!rtc.valid()) quarantine = true;
    if (!evaluationFix.valid || !evaluationFix.has_time) quarantine = true;

    digitalWrite(RELAY_PIN, quarantine ? RELAY_HOLDOVER_LEVEL : RELAY_TRUSTED_LEVEL);

    // Host sees either the real GNSS stream (relay NO) or this controlled holdover stream (relay NC).
    if (quarantine && now - lastTelemetry >= TELEMETRY_PERIOD_MS) {
        HostSerial.println(makeHoldoverSentence(latestRtc, referenceLat, referenceLon, referenceValid));
    }

    if (now - lastOled >= OLED_PERIOD_MS && displayReady) {
        oled.update(security.state(), latestEvidence.integrity, latestEvidence.time_residual_ms,
                    latestEvidence.distance_m, security.lastReason(), quarantine);
        lastOled = now;
    }

    if (now - lastTelemetry >= TELEMETRY_PERIOD_MS) {
        Serial.println(telemetryJson(security.state(), latestEvidence, evaluationFix, latestRtc,
                                     referenceValid, referenceLat, referenceLon,
                                     quarantine, security.lastReason(), testMode));
        lastTelemetry = now;
    }

    delay(2);
}
