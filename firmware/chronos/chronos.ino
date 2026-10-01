/*
 * ============================================================================
 *  CHRONOS - GPS integrity monitor prototype
 *  ESP32 Dev Module + NEO-6M GPS + DS3231 RTC + 3 LEDs + 2 buttons
 * ============================================================================
 *  What it is:   an integrity-MONITORING / anomaly-DETECTION prototype.
 *                It cannot prove that GPS spoofing is happening. It flags GPS
 *                behaviour that is inconsistent with (a) an independent RTC and
 *                (b) the fact that this device is STATIONARY.
 *
 *  Libraries:    "RTClib" by Adafruit (Library Manager, accept dependencies).
 *  Serial:       115200 baud.
 *
 *  Wiring (must match the guide):
 *    GPS TX  -> GPIO16 (ESP32 RX2)      GPS RX -> not connected
 *    DS3231 SDA -> GPIO21, SCL -> GPIO22
 *    LEDs (each through a resistor):  green GPIO25, yellow GPIO26, red GPIO27
 *    Buttons (other leg to GND):      ACK GPIO32, SIM GPIO33
 *    Relay module: VCC -> ESP32 VIN(5V), GND -> GND, IN -> GPIO23
 *    Relay contacts: COM <- GPS TX, NO -> GPIO18 (protected/host side)
 *
 *  Buttons:
 *    ACK  short press : acknowledge a latched UNSAFE state (starts recovery)
 *    ACK  hold 3 s    : provision - copy GPS time into the RTC and re-learn the
 *                       fixed position (do this once, with a good GPS fix)
 *    SIM  short press : SELECT next simulated scenario (yellow blinks its number; nothing starts)
 *    SIM  hold 1.5 s  : START the selected scenario (only from TRUSTED), or CANCEL a running one
 *  Serial Monitor keys: 1-5 start simulation n, x cancel, a = ACK, p = provision, ? help
 * ============================================================================
 */

#include <Wire.h>
#include <RTClib.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
#endif

// ============================ PIN MAP =======================================
static const int PIN_GPS_RX     = 16;  // ESP32 RX2  <- GPS module TX
static const int PIN_GPS_TX     = 17;  // ESP32 TX2  -> GPS RX (NOT wired, not needed)
static const int PIN_SDA        = 21;  // DS3231 SDA
static const int PIN_SCL        = 22;  // DS3231 SCL
static const int PIN_LED_GREEN  = 25;
static const int PIN_LED_YELLOW = 26;
static const int PIN_LED_RED    = 27;
static const int PIN_BTN_ACK    = 32;  // to GND when pressed (internal pull-up)
static const int PIN_BTN_SIM    = 33;  // to GND when pressed (internal pull-up)
// ---- relay add-on (5 V relay module) ----
static const int  PIN_RELAY_IN      = 23;   // relay module IN
static const int  PIN_HOST_RX       = 18;   // ESP32 listens HERE = the protected side of the relay contact
static const int  PIN_HOST_TX_UNUSED = 19;  // required by the API, NOT wired
static const bool HAS_RELAY         = false; // <<< set to true ONLY after the relay is wired (guide: relay section)
static const bool RELAY_ACTIVE_LOW  = true; // most 1-channel modules: IN low = relay ON. Set false if yours is HIGH-trigger.

// ============================ TUNABLE THRESHOLDS ============================
// Change these here only. They are explained in Part 7 of the guide.
static const uint32_t GPS_BAUD          = 9600;
static const uint32_t RX_SILENT_MS      = 3000;   // no UART byte for this long = receiver failure
static const uint32_t NOFIX_MS          = 4000;   // alive but no valid fix for this long = signal loss
static const uint32_t BAD_WINDOW_MS     = 6000;   // window for counting bad sentences
static const int      BAD_COUNT_LIMIT   = 3;      // bad sentences inside window => BAD_NMEA
static const int      MIN_YEAR          = 2024;   // GPS date sanity range
static const int      MAX_YEAR          = 2099;
static const int      BASELINE_SAMPLES  = 20;     // good fixes averaged to learn our fixed position
static const int      BASE_MIN_SATS     = 4;
static const double   BASE_MAX_HDOP     = 5.0;
static const double   BASE_RESTART_M    = 100.0;  // unstable while learning -> start over
static const int      INIT_CLEAN_EPOCHS = 5;      // consecutive agreeing epochs to leave INIT
static const int      OFFSET_SUSP_S     = 2;      // |GPS-RTC| >= this = suspicious
static const int      OFFSET_HARD_S     = 5;      // |GPS-RTC| >= this = confirmed anomaly
static const int      TIME_STEP_S       = 2;      // |dGPS - dRTC| >= this between epochs
static const double   JUMP_M            = 40.0;   // 1 s position jump while stationary
static const double   DRIFT_M           = 60.0;   // distance from learned position: suspicious
static const double   FAR_M             = 250.0;  // distance from learned position: confirmed
static const int      SAT_JUMP_N        = 6;      // satellites-used change in 1 s (fix kept)
static const int      SNR_MIN_SATS      = 6;      // uniform-SNR heuristic needs this many sats
static const int      SNR_SPREAD_DB     = 3;      // max-min SNR at or below this = "too uniform"
static const uint32_t CLEAR_MS          = 20000;  // clean time needed WARNING -> TRUSTED
static const uint32_t SUSP_ESCALATE_MS  = 15000;  // suspicious this long -> UNSAFE
static const uint32_t FAULT_ESCALATE_MS = 30000;  // fault this long -> UNSAFE (auto-recovers)
static const uint32_t DEBOUNCE_MS       = 30;
static const uint32_t ACK_LONG_MS       = 3000;
static const uint32_t SIM_LONG_MS       = 1500;

// ============================ TYPES =========================================
enum State : uint8_t { ST_INIT, ST_TRUSTED, ST_WARNING, ST_UNSAFE };
static const char* const STATE_NAME[4] = { "INIT", "TRUSTED", "WARNING", "UNSAFE" };

// Rules are grouped by class: FAULT (equipment/signal problem), SUSPICIOUS,
// CONFIRMED (breaks an invariant of our stationary + RTC model).
enum Rule : uint8_t {
  R_RX_SILENT, R_NO_FIX, R_RTC_FAULT,                                   // FAULT
  R_BAD_NMEA, R_DATE_BAD, R_FIX_FLAP, R_INCONSISTENT, R_SAT_JUMP,
  R_SNR_UNIFORM, R_TIME_STEP, R_TIME_OFFSET, R_POS_JUMP, R_POS_DRIFT,   // SUSPICIOUS
  R_TIME_BACKWARDS, R_TIME_OFFSET_HARD, R_POS_FAR,                      // CONFIRMED
  R_COUNT
};
static const char* const RULE_NAME[R_COUNT] = {
  "RX_SILENT", "NO_FIX", "RTC_FAULT",
  "BAD_NMEA", "DATE_BAD", "FIX_FLAP", "INCONSISTENT", "SAT_JUMP",
  "SNR_UNIFORM", "TIME_STEP", "TIME_OFFSET", "POS_JUMP", "POS_DRIFT",
  "TIME_BACKWARDS", "TIME_OFFSET_HARD", "POS_FAR"
};
static const uint32_t MASK_FAULT = (1UL << R_RX_SILENT) | (1UL << R_NO_FIX) | (1UL << R_RTC_FAULT);
static const uint32_t MASK_HARD  = (1UL << R_TIME_BACKWARDS) | (1UL << R_TIME_OFFSET_HARD) | (1UL << R_POS_FAR);
static const uint32_t MASK_ALL   = (1UL << R_COUNT) - 1UL;
static const uint32_t MASK_SUSP  = MASK_ALL & ~MASK_FAULT & ~MASK_HARD;

enum SimType : uint8_t {
  SIM_NONE, SIM_TIME_JUMP, SIM_POS_JUMP, SIM_SIGNAL_LOSS, SIM_NMEA_BURST, SIM_POS_DRIFT, SIM_COUNT
};
static const char* const SIM_NAME[SIM_COUNT] = {
  "none", "TIME JUMP (+37 s added to GPS clock)", "POSITION JUMP (~5.5 km)",
  "SIGNAL LOSS (fix dropped)", "MALFORMED NMEA BURST", "SLOW POSITION DRIFT (5 m/s)"
};
static const uint32_t SIM_DURATION_MS[SIM_COUNT] = { 0, 12000, 12000, 12000, 4000, 40000 };
static const char* const SIM_EXPECT[SIM_COUNT] = {
  "", "UNSAFE (red, latched, needs ACK)", "UNSAFE (red, latched, needs ACK)",
  "WARNING only (yellow), recovers by itself", "WARNING only (yellow), recovers by itself",
  "WARNING after ~15 s, then UNSAFE (red) after ~30 s"
};

struct Btn {
  int pin; bool raw; bool down; uint32_t changedAt; uint32_t downAt; bool longDone;
};

// ============================ GLOBAL STATE ==================================
static HardwareSerial GPSSerial(2);
static HardwareSerial HostSerial(1);   // reads the relay's output side (proof the data path is cut)
static RTC_DS3231 rtc;
static bool relayClosed = false;
static uint32_t hostBytesWindow = 0, hostRate = 0;
static bool rtcOk = false;          // DS3231 answered at boot
static bool rtcTimeValid = false;   // RTC holds a believable time (not lost power)

static State state = ST_INIT;
static bool latched = false;        // UNSAFE caused by anomaly: needs operator ACK
static uint32_t lastTickMs = 0, suspSince = 0, faultSince = 0, cleanSince = 0, lastMask = 0;

// rule engine
static uint32_t ruleUntil[R_COUNT];
static bool     ruleArmed[R_COUNT];
static uint8_t  ruleHits[R_COUNT];

// NMEA input
static char     lineBuf[110];
static uint8_t  lineLen = 0;
static bool     inLine = false;
static uint32_t lastByteMs = 0, lastValidSentenceMs = 0, lastBadPrintMs = 0;
static uint32_t bytesTotal = 0, goodSentences = 0, badSentences = 0;
static uint32_t badTimes[8];
static uint8_t  badIdx = 0;
static char*    fields[24];
static int      nFields = 0;

// latest GPS data
static bool     fixValid = false;
static uint32_t lastFixMs = 0, lastRmcMs = 0, lastGgaMs = 0;
static int      ggaQuality = 0, ggaSats = 0, prevSats = 0, prevQuality = 0;
static bool     havePrevGga = false;
static double   ggaHdop = 99.0;
static uint32_t fixLossTimes[4];
static uint8_t  fixLossIdx = 0;
static int      gsvMin = 999, gsvMax = 0, gsvCnt = 0;

// epoch analysis
static bool     epochValid = false, curDateOk = false, havePrev = false, prevDateOk = false;
static uint32_t curGpsUnix = 0, prevGpsUnix = 0, prevRtcUnix = 0;
static double   curLat = 0, curLon = 0, prevLat = 0, prevLon = 0, curDist = -1.0;
static int64_t  curOffset = 0;
static bool     skipContinuity = false;

// baseline / provisioning
static bool     baselineReady = false, needSync = true, syncRequested = false;
static int      baseN = 0, initClean = 0;
static double   sumLat = 0, sumLon = 0, baseLat = 0, baseLon = 0;

// simulation
static SimType  simActive = SIM_NONE;
static uint32_t simStartMs = 0, simEndMs = 0, lastInjectMs = 0;
static uint8_t  simSelected = 1;      // which scenario the SIM button will start
static uint32_t selBlinkStart = 0;
static uint8_t  selBlinkCount = 0;

static Btn btnAck = { PIN_BTN_ACK, false, false, 0, 0, false };
static Btn btnSim = { PIN_BTN_SIM, false, false, 0, 0, false };

// ============================ SMALL HELPERS =================================
static int64_t iabs64(int64_t v) { return v < 0 ? -v : v; }

static double distM(double la1, double lo1, double la2, double lo2) {
  // equirectangular approximation: accurate to well under 1 % for tens of km
  double dLat = (la2 - la1) * 111320.0;
  double dLon = (lo2 - lo1) * 111320.0 * cos(la1 * M_PI / 180.0);
  return sqrt(dLat * dLat + dLon * dLon);
}

static bool isNumStr(const char* s) {            // digits with at most one '.'
  if (!s || !*s) return false;
  int dots = 0;
  for (; *s; s++) {
    if (*s == '.') { if (++dots > 1) return false; }
    else if (!isdigit((unsigned char)*s)) return false;
  }
  return true;
}
static int toInt(const char* s, int def) {
  if (!s || !*s) return def;
  for (const char* p = s; *p; p++) if (!isdigit((unsigned char)*p)) return def;
  return atoi(s);
}
static bool digitsN(const char* s, int n) {
  if (strlen(s) < (size_t)n) return false;
  for (int i = 0; i < n; i++) if (!isdigit((unsigned char)s[i])) return false;
  return true;
}
static int d2(const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); }

// ============================ RULE ENGINE ===================================
static bool ruleActive(Rule r, uint32_t now) {
  return ruleArmed[r] && (int32_t)(ruleUntil[r] - now) > 0;
}
static uint32_t activeMask(uint32_t now) {
  uint32_t m = 0;
  for (uint8_t r = 0; r < R_COUNT; r++) if (ruleActive((Rule)r, now)) m |= (1UL << r);
  return m;
}
static void clearRules() {
  for (uint8_t r = 0; r < R_COUNT; r++) { ruleArmed[r] = false; ruleUntil[r] = 0; ruleHits[r] = 0; }
}
// Mark a rule active for holdMs. Logs once when it becomes active.
static void raiseRule(Rule r, uint32_t holdMs) {
  if (state == ST_INIT) return;                  // INIT only learns; nothing is judged yet
  uint32_t now = millis();
  if (!ruleActive(r, now)) {
    const char* cls = (MASK_FAULT & (1UL << r)) ? "FAULT" : ((MASK_HARD & (1UL << r)) ? "CONFIRMED" : "SUSPICIOUS");
    Serial.printf("[EVENT] %s (%s)%s\n", RULE_NAME[r], cls,
                  simActive != SIM_NONE ? "  <-- during SIMULATED anomaly" : "");
  }
  ruleArmed[r] = true;
  ruleUntil[r] = now + holdMs;
}
// Debounced condition: must be true for `need` consecutive evaluations.
static void cond(Rule r, bool c, uint8_t need, uint32_t holdMs) {
  if (c) { if (ruleHits[r] < 255) ruleHits[r]++; if (ruleHits[r] >= need) raiseRule(r, holdMs); }
  else ruleHits[r] = 0;
}
static void describeActive(char* out, size_t n, uint32_t now) {
  out[0] = 0;
  bool first = true;
  for (uint8_t r = 0; r < R_COUNT; r++) {
    if (!ruleActive((Rule)r, now)) continue;
    size_t l = strlen(out);
    snprintf(out + l, n - l, "%s%s", first ? "" : ",", RULE_NAME[r]);
    first = false;
  }
  if (first) snprintf(out, n, "-");
}

// ============================ STATE MACHINE =================================
static void changeState(State s, const char* why) {
  Serial.printf("\n>>> STATE %s -> %s | %s\n\n", STATE_NAME[state], STATE_NAME[s], why);
  state = s;
  suspSince = 0; faultSince = 0; cleanSince = 0;
}
static void resetLearning() {
  baselineReady = false; baseN = 0; sumLat = 0; sumLon = 0; initClean = 0;
  havePrev = false; curDist = -1.0; needSync = true;
}

static void stateTick(uint32_t now) {
  uint32_t m = activeMask(now);
  lastMask = m;
  bool hard = (m & MASK_HARD) != 0;
  bool susp = (m & MASK_SUSP) != 0;
  bool fault = (m & MASK_FAULT) != 0;
  int suspCount = __builtin_popcount(m & MASK_SUSP);
  char why[120];

  if (susp)  { if (!suspSince)  suspSince = now; }  else suspSince = 0;
  if (fault) { if (!faultSince) faultSince = now; } else faultSince = 0;
  if (m) cleanSince = 0; else if (!cleanSince) cleanSince = now;

  switch (state) {
    case ST_INIT:
      break;                                       // handled in analyzeEpoch()
    case ST_TRUSTED:
      if (hard) { latched = true; describeActive(why, sizeof(why), now); changeState(ST_UNSAFE, why); }
      else if (m) { describeActive(why, sizeof(why), now); changeState(ST_WARNING, why); }
      break;
    case ST_WARNING:
      if (hard) { latched = true; describeActive(why, sizeof(why), now); changeState(ST_UNSAFE, why); }
      else if (suspSince && now - suspSince >= SUSP_ESCALATE_MS) {
        latched = true; changeState(ST_UNSAFE, "suspicious behaviour sustained > 15 s");
      } else if (suspCount >= 3) {
        latched = true; changeState(ST_UNSAFE, "3+ independent suspicious indicators at once");
      } else if (faultSince && now - faultSince >= FAULT_ESCALATE_MS) {
        latched = false; changeState(ST_UNSAFE, "GPS unusable (fault) > 30 s - blocked, will auto-recover");
      } else if (!m && cleanSince && now - cleanSince >= CLEAR_MS) {
        changeState(ST_TRUSTED, "20 s with no indicators");
      }
      break;
    case ST_UNSAFE:
      if (hard) latched = true;
      if (!latched && !m) changeState(ST_WARNING, "fault cleared - verifying for 20 s");
      break;
  }
}

static void onAckShort() {
  if (state == ST_UNSAFE && latched) {
    if (simActive != SIM_NONE) { Serial.println("[ACK] refused: simulation still running (wait, or hold SIM to cancel)"); return; }
    latched = false;
    clearRules();
    changeState(ST_WARNING, "operator ACK - 20 s clean window required");
  } else if (state == ST_UNSAFE) {
    Serial.println("[ACK] UNSAFE is a fault, not a latched anomaly: it clears itself when GPS data is healthy");
  } else {
    Serial.println("[ACK] nothing to acknowledge");
  }
}
static void onAckLong() {
  if (!rtcOk) { Serial.println("[PROVISION] refused: RTC not found"); return; }
  if (simActive != SIM_NONE) { Serial.println("[PROVISION] refused: simulation running"); return; }
  if (!fixValid || millis() - lastFixMs > 2500) { Serial.println("[PROVISION] refused: need a current valid GPS fix"); return; }
  syncRequested = true;
  Serial.println("[PROVISION] will copy GPS time into RTC at the next GPS epoch and re-learn position");
}

// ============================ NMEA PARSING ==================================
static void noteBad(const char* why) {
  uint32_t now = millis();
  badSentences++;
  badTimes[badIdx] = now ? now : 1;
  badIdx = (badIdx + 1) & 7;
  if (now - lastBadPrintMs > 2000) {
    Serial.printf("[NMEA] rejected sentence: %s\n", why);
    lastBadPrintMs = now;
  }
}
static int recentBad(uint32_t now) {
  int c = 0;
  for (uint8_t i = 0; i < 8; i++) if (badTimes[i] && now - badTimes[i] <= BAD_WINDOW_MS) c++;
  return c;
}
static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
static bool nmeaChecksumOk(const char* s) {
  if (s[0] != '$') return false;
  uint8_t cs = 0;
  const char* p = s + 1;
  for (; *p && *p != '*'; p++) {
    if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E) return false;   // non-printable
    cs ^= (uint8_t)*p;
  }
  if (*p != '*' || hexVal(p[1]) < 0 || hexVal(p[2]) < 0 || p[3] != 0) return false;
  return cs == (uint8_t)((hexVal(p[1]) << 4) | hexVal(p[2]));
}
static void splitFields(char* s) {
  nFields = 0;
  fields[nFields++] = s;
  for (char* p = s; *p && nFields < 24; p++) if (*p == ',') { *p = 0; fields[nFields++] = p + 1; }
}
static const char* fld(int i) { return i < nFields ? fields[i] : ""; }

static bool parseTime(const char* s, int& h, int& m, int& sec) {
  if (!digitsN(s, 6)) return false;
  h = d2(s); m = d2(s + 2); sec = d2(s + 4);
  return h < 24 && m < 60 && sec < 60;
}
static bool parseDate(const char* s, int& d, int& mo, int& y) {
  if (strlen(s) != 6 || !digitsN(s, 6)) return false;
  d = d2(s); mo = d2(s + 2); y = 2000 + d2(s + 4);
  return d >= 1 && d <= 31 && mo >= 1 && mo <= 12;
}
static bool parseCoord(const char* s, const char* hemi, bool isLat, double& out) {
  if (!isNumStr(s)) return false;
  char h = hemi[0];
  if (isLat ? (h != 'N' && h != 'S') : (h != 'E' && h != 'W')) return false;
  const char* dot = strchr(s, '.');
  int intDigits = dot ? (int)(dot - s) : (int)strlen(s);
  if (intDigits != (isLat ? 4 : 5)) return false;         // ddmm.mmmm / dddmm.mmmm
  double v = atof(s);
  int deg = (int)(v / 100.0);
  double mins = v - deg * 100.0;
  if (mins >= 60.0) return false;
  if (deg > (isLat ? 90 : 180)) return false;
  double dd = deg + mins / 60.0;
  if (h == 'S' || h == 'W') dd = -dd;
  out = dd;
  return true;
}

static void recordFixLoss(uint32_t now) {
  fixLossTimes[fixLossIdx] = now ? now : 1;
  fixLossIdx = (fixLossIdx + 1) & 3;
  int c = 0;
  for (uint8_t i = 0; i < 4; i++) if (fixLossTimes[i] && now - fixLossTimes[i] <= 120000UL) c++;
  if (c >= 3) raiseRule(R_FIX_FLAP, 30000);
}

// ---- one validated 1 Hz "epoch": valid fix with time, date and position ----
static void analyzeEpoch(uint32_t nowMs, int year, uint32_t gpsUnix, double lat, double lon) {
  epochValid = true;
  curGpsUnix = gpsUnix; curLat = lat; curLon = lon;
  curDateOk = (year >= MIN_YEAR && year <= MAX_YEAR);
  if (!rtcOk) return;

  if (syncRequested) {                                 // operator-approved provisioning
    syncRequested = false;
    if (curDateOk && simActive == SIM_NONE) {
      rtc.adjust(DateTime(gpsUnix));
      rtcTimeValid = true;
      resetLearning();
      clearRules();
      latched = false;
      if (state != ST_INIT) changeState(ST_INIT, "re-provisioning: RTC set from GPS, re-learning position");
      else Serial.println("[PROVISION] RTC set from GPS time; re-learning position");
      return;
    }
    Serial.println("[PROVISION] refused: GPS date not plausible");
  }

  uint32_t rtcUnix = rtc.now().unixtime();
  int64_t offset = curDateOk ? (int64_t)gpsUnix - (int64_t)rtcUnix : 0;
  curOffset = offset;
  bool skip = skipContinuity;
  skipContinuity = false;

  cond(R_DATE_BAD, !curDateOk, 1, 5000);

  if (state == ST_INIT) {
    // ---- learning phase: average position, check RTC agrees with GPS ----
    bool timeAgree = rtcTimeValid && curDateOk && iabs64(offset) < OFFSET_SUSP_S;
    needSync = !timeAgree;
    bool good = (nowMs - lastGgaMs) < 2500 && ggaQuality > 0 && ggaSats >= BASE_MIN_SATS && ggaHdop <= BASE_MAX_HDOP;
    if (!baselineReady && good) {
      if (baseN >= 5 && distM(lat, lon, sumLat / baseN, sumLon / baseN) > BASE_RESTART_M) {
        baseN = 0; sumLat = 0; sumLon = 0;               // fix still settling: restart
      }
      sumLat += lat; sumLon += lon; baseN++;
      if (baseN >= BASELINE_SAMPLES) {
        baseLat = sumLat / baseN; baseLon = sumLon / baseN; baselineReady = true;
        Serial.printf("[INIT] position baseline locked: %.6f, %.6f (%d fixes)\n", baseLat, baseLon, baseN);
      }
    }
    if (baselineReady) curDist = distM(lat, lon, baseLat, baseLon);
    if (baselineReady && timeAgree) initClean++; else initClean = 0;
    if (initClean >= INIT_CLEAN_EPOCHS) {
      clearRules(); latched = false;
      changeState(ST_TRUSTED, "baseline locked, RTC and GPS agree");
    }
  } else {
    // ---- monitoring phase ----
    if (curDateOk) {
      if (havePrev && prevDateOk && !skip) {
        int64_t dGps = (int64_t)gpsUnix - (int64_t)prevGpsUnix;
        int64_t dRtc = (int64_t)rtcUnix - (int64_t)prevRtcUnix;
        int64_t diff = dGps - dRtc;
        if (iabs64(diff) >= TIME_STEP_S) raiseRule(R_TIME_STEP, 10000);
        if (dGps < 0 && diff <= -TIME_STEP_S) raiseRule(R_TIME_BACKWARDS, 10000);
      }
      cond(R_TIME_OFFSET,      iabs64(offset) >= OFFSET_SUSP_S, 3, 3000);
      cond(R_TIME_OFFSET_HARD, iabs64(offset) >= OFFSET_HARD_S, 3, 3000);
    }
    if (baselineReady) {
      curDist = distM(lat, lon, baseLat, baseLon);
      if (havePrev && !skip && distM(prevLat, prevLon, lat, lon) >= JUMP_M) raiseRule(R_POS_JUMP, 10000);
      cond(R_POS_DRIFT, curDist >= DRIFT_M, 3, 3000);
      cond(R_POS_FAR,   curDist >= FAR_M,   3, 3000);
    }
  }
  prevGpsUnix = gpsUnix; prevRtcUnix = rtcUnix; prevLat = lat; prevLon = lon;
  prevDateOk = curDateOk; havePrev = true;
}

static void handleRMC() {
  uint32_t now = millis();
  lastRmcMs = now;
  char status = fld(2)[0];
  if (status != 'A' && status != 'V') { noteBad("RMC status field invalid"); return; }
  bool simLoss = (simActive == SIM_SIGNAL_LOSS);
  if (status == 'V' || simLoss) {                      // receiver says: no valid fix
    if (fixValid) {
      fixValid = false;
      Serial.printf("[GPS] fix LOST%s\n", simLoss ? " (SIMULATED)" : "");
      recordFixLoss(now);
    }
    return;
  }
  int h, mi, s, d, mo, y; double lat, lon;
  if (!parseTime(fld(1), h, mi, s) || !parseDate(fld(9), d, mo, y) ||
      !parseCoord(fld(3), fld(4), true, lat) || !parseCoord(fld(5), fld(6), false, lon)) {
    noteBad("RMC claims fix but fields are invalid");
    return;
  }
  if (!fixValid) { fixValid = true; Serial.println("[GPS] fix acquired / recovered"); }
  lastFixMs = now;

  uint32_t gpsUnix = DateTime(y, mo, d, h, mi, s).unixtime();
  // ---- SIMULATED anomalies are injected here, after parsing, before analysis ----
  if (simActive == SIM_TIME_JUMP) gpsUnix += 37;
  if (simActive == SIM_POS_JUMP)  lat += 0.05;
  if (simActive == SIM_POS_DRIFT) lat += (5.0 * (double)(now - simStartMs) / 1000.0) / 111320.0;
  analyzeEpoch(now, y, gpsUnix, lat, lon);
}

static void handleGGA() {
  uint32_t now = millis();
  int q = toInt(fld(6), -1);
  int sats = toInt(fld(7), 0);
  if (q < 0 || q > 8) { noteBad("GGA quality field invalid"); return; }
  lastGgaMs = now;
  ggaHdop = isNumStr(fld(8)) ? atof(fld(8)) : 99.0;
  if (simActive == SIM_SIGNAL_LOSS) { q = 0; sats = 0; }
  if (havePrevGga && prevQuality > 0 && q > 0 && abs(sats - prevSats) >= SAT_JUMP_N) raiseRule(R_SAT_JUMP, 10000);
  prevQuality = q; prevSats = sats; havePrevGga = true;
  ggaQuality = q; ggaSats = sats;
}

static void handleGSV() {
  int total = toInt(fld(1), 0), msgNum = toInt(fld(2), 0);
  if (total < 1 || msgNum < 1 || msgNum > total) { noteBad("GSV header invalid"); return; }
  if (msgNum == 1) { gsvMin = 999; gsvMax = 0; gsvCnt = 0; }
  for (int k = 0; k < 4; k++) {
    int snr = toInt(fld(7 + 4 * k), 0);
    if (snr > 0) { if (snr < gsvMin) gsvMin = snr; if (snr > gsvMax) gsvMax = snr; gsvCnt++; }
  }
  if (msgNum == total) {
    cond(R_SNR_UNIFORM, gsvCnt >= SNR_MIN_SATS && (gsvMax - gsvMin) <= SNR_SPREAD_DB, 5, 3000);
  }
}

static void processLine(char* line) {
  uint32_t now = millis();
  if (!nmeaChecksumOk(line)) { noteBad("bad checksum / malformed"); return; }
  goodSentences++;
  lastValidSentenceMs = now;
  char work[112];
  strncpy(work, line + 1, sizeof(work) - 1);
  work[sizeof(work) - 1] = 0;
  char* star = strchr(work, '*');
  if (star) *star = 0;
  splitFields(work);
  const char* id = fld(0);
  if (strlen(id) != 5) return;                         // proprietary sentences ignored
  if (!strcmp(id + 2, "RMC")) handleRMC();
  else if (!strcmp(id + 2, "GGA")) handleGGA();
  else if (!strcmp(id + 2, "GSV")) handleGSV();
}

// Non-blocking UART reader: assembles lines starting with '$' ending in CR/LF.
static void gpsPump() {
  while (GPSSerial.available()) {
    char c = (char)GPSSerial.read();
    lastByteMs = millis();
    bytesTotal++;
    if (c == '$') {
      if (inLine && lineLen > 1) noteBad("truncated sentence");
      lineLen = 0; lineBuf[lineLen++] = c; inLine = true;
    } else if (inLine) {
      if (c == '\r' || c == '\n') {
        lineBuf[lineLen] = 0;
        if (lineLen > 1) processLine(lineBuf);
        inLine = false; lineLen = 0;
      } else if (lineLen < sizeof(lineBuf) - 1) {
        lineBuf[lineLen++] = c;
      } else {
        noteBad("sentence too long");
        inLine = false; lineLen = 0;
      }
    }
  }
}

// ============================ RULES THAT RUN EVERY SECOND ===================
static bool rtcPing() {
  Wire.beginTransmission(0x68);
  return Wire.endTransmission() == 0;
}
static void watchdogRules(uint32_t now) {
  bool silent = (now - lastByteMs) > RX_SILENT_MS;
  if (silent) raiseRule(R_RX_SILENT, 2500);
  if (!silent && lastFixMs != 0 && (now - lastFixMs) > NOFIX_MS) raiseRule(R_NO_FIX, 2500);
  cond(R_BAD_NMEA, recentBad(now) >= BAD_COUNT_LIMIT, 1, 2500);
  if (!silent && bytesTotal > 0 && (now - lastValidSentenceMs) > 4000) raiseRule(R_BAD_NMEA, 2500);  // wrong baud / noise
  bool rmcFresh = lastRmcMs != 0 && (now - lastRmcMs) < 2500;
  bool ggaFresh = lastGgaMs != 0 && (now - lastGgaMs) < 2500;
  if (rmcFresh && ggaFresh) {
    bool ggaFix = ggaQuality > 0;
    cond(R_INCONSISTENT, (fixValid != ggaFix) || (ggaFix && ggaSats < 3), 3, 3000);
  }
  cond(R_RTC_FAULT, !rtcOk || !rtcPing(), 2, 2500);
}

// ============================ SIMULATION (labelled, not real) ===============
static void startSim(uint8_t idx) {
  if (idx < 1 || idx >= SIM_COUNT) return;
  if (simActive != SIM_NONE) { Serial.println("[SIM] a simulation is already running"); return; }
  if (state != ST_TRUSTED) { Serial.println("[SIM] only from TRUSTED (green). ACK / wait for recovery first."); return; }
  simActive = (SimType)idx;
  simSelected = idx;
  simStartMs = millis();
  simEndMs = simStartMs + SIM_DURATION_MS[idx];
  lastInjectMs = 0;
  Serial.printf("\n########## [SIMULATED ANOMALY] %s - %lu s ##########\n"
                "########## Expected outcome: %s ##########\n"
                "########## This is injected in software. NOT a real GPS attack. ##########\n\n",
                SIM_NAME[idx], (unsigned long)(SIM_DURATION_MS[idx] / 1000), SIM_EXPECT[idx]);
}
static void endSim(bool cancelled) {
  Serial.printf("\n########## [SIMULATED ANOMALY] %s %s ##########\n\n", SIM_NAME[simActive],
                cancelled ? "CANCELLED" : "ended");
  simActive = SIM_NONE;
  skipContinuity = true;                               // ending the simulation is not itself an event
}
// Short press on SIM: choose the NEXT scenario (nothing is started). Yellow LED blinks its number.
static void selectNextSim() {
  if (simActive != SIM_NONE) { Serial.println("[SIM] a simulation is running: hold SIM 1.5 s to cancel it"); return; }
  simSelected = (simSelected % (SIM_COUNT - 1)) + 1;
  selBlinkStart = millis();
  selBlinkCount = simSelected;
  Serial.printf("[SIM] SELECTED %d: %s -> expected: %s. HOLD SIM 1.5 s to START.\n",
                (int)simSelected, SIM_NAME[simSelected], SIM_EXPECT[simSelected]);
}
static void simHousekeeping(uint32_t now) {
  if (simActive == SIM_NONE) return;
  if ((int32_t)(now - simEndMs) >= 0) { endSim(false); return; }
  if (simActive == SIM_NMEA_BURST && now - lastInjectMs >= 500) {
    lastInjectMs = now;
    char bad[] = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00";  // checksum deliberately wrong
    Serial.println("[SIM] injecting malformed NMEA sentence into the parser");
    processLine(bad);
  }
}

// ============================ RELAY GATE ====================================
// Contact wiring is NORMALLY OPEN: unpowered / reset / crashed = data path CUT (fail-closed).
// Active-low modules are driven "open-drain style": LOW = relay on, INPUT (released) = relay off.
// Releasing the pin (instead of driving HIGH) is what makes a 5 V module switch off reliably from 3.3 V.
static void relaySet(bool closed) {
  if (!HAS_RELAY) return;
  if (RELAY_ACTIVE_LOW) {
    if (closed) { pinMode(PIN_RELAY_IN, OUTPUT); digitalWrite(PIN_RELAY_IN, LOW); }
    else        { pinMode(PIN_RELAY_IN, INPUT); }
  } else {
    pinMode(PIN_RELAY_IN, OUTPUT);
    digitalWrite(PIN_RELAY_IN, closed ? HIGH : LOW);
  }
  if (closed != relayClosed) {
    relayClosed = closed;
    Serial.printf("[GATE] relay %s - GPS data path to host is now %s\n",
                  closed ? "CLOSED" : "OPEN", closed ? "CONNECTED" : "PHYSICALLY CUT");
  }
}
// Pass GPS to the host in TRUSTED and WARNING (flagged); cut it in INIT and UNSAFE.
static void applyGate() {
  if (!HAS_RELAY) return;
  bool want = (state == ST_TRUSTED || state == ST_WARNING);
  if (want != relayClosed) relaySet(want);
}
static void hostPump() {
  if (!HAS_RELAY) return;
  while (HostSerial.available()) { HostSerial.read(); hostBytesWindow++; }
}

// ============================ BUTTONS / LEDS / STATUS =======================
// returns 0 = nothing, 1 = short press (on release), 2 = long press (while held)
static int pollBtn(Btn& b, uint32_t longMs) {
  bool raw = (digitalRead(b.pin) == LOW);
  uint32_t now = millis();
  int ev = 0;
  if (raw != b.raw) { b.raw = raw; b.changedAt = now; }
  if (now - b.changedAt >= DEBOUNCE_MS && raw != b.down) {
    b.down = raw;
    if (b.down) { b.downAt = now; b.longDone = false; }
    else if (!b.longDone) ev = 1;
  }
  if (b.down && !b.longDone && now - b.downAt >= longMs) { b.longDone = true; ev = 2; }
  return ev;
}

static void updateLeds(uint32_t now) {
  bool g = false, y = false, r = false;
  bool slow = ((now / 500) & 1) == 0;                  // 1 Hz blink
  bool fast = ((now / 125) & 1) == 0;                  // 4 Hz blink
  if (selBlinkCount && state != ST_UNSAFE) {           // yellow blinks N times = scenario N selected
    uint32_t el = now - selBlinkStart;
    if (el >= (uint32_t)selBlinkCount * 300 + 200) selBlinkCount = 0;
    else {
      digitalWrite(PIN_LED_GREEN, LOW); digitalWrite(PIN_LED_RED, LOW);
      digitalWrite(PIN_LED_YELLOW, (((el / 150) & 1) == 0 && (el / 300) < selBlinkCount) ? HIGH : LOW);
      return;
    }
  }
  switch (state) {
    case ST_INIT:    y = needSync ? fast : slow; r = (!rtcOk) && slow; break;
    case ST_TRUSTED: g = true; break;
    case ST_WARNING: y = true; g = (lastMask == 0) && slow; break;   // green blink = recovering
    case ST_UNSAFE:  r = latched ? true : fast; break;
  }
  digitalWrite(PIN_LED_GREEN,  g ? HIGH : LOW);
  digitalWrite(PIN_LED_YELLOW, y ? HIGH : LOW);
  digitalWrite(PIN_LED_RED,    r ? HIGH : LOW);
}

static void printStatus(uint32_t now) {
  char gpsT[12] = "--:--:--", rtcT[12] = "--:--:--", flags[120], note[80] = "";
  if (epochValid && curDateOk && now - lastFixMs < 3000) {
    DateTime g(curGpsUnix);
    snprintf(gpsT, sizeof(gpsT), "%02d:%02d:%02d", g.hour(), g.minute(), g.second());
  }
  if (rtcOk) {
    DateTime t = rtc.now();
    snprintf(rtcT, sizeof(rtcT), "%02d:%02d:%02d", t.hour(), t.minute(), t.second());
  }
  describeActive(flags, sizeof(flags), now);
  if (state == ST_INIT) {
    if (now - lastByteMs > RX_SILENT_MS)      snprintf(note, sizeof(note), "NO GPS DATA: check GPS TX->GPIO16, power, baud");
    else if (!rtcOk)                          snprintf(note, sizeof(note), "RTC NOT FOUND: check SDA/SCL wiring");
    else if (!fixValid)                       snprintf(note, sizeof(note), "waiting for GPS fix (antenna near a window/outdoors)");
    else if (!baselineReady)                  snprintf(note, sizeof(note), "learning position %d/%d", baseN, BASELINE_SAMPLES);
    else if (needSync)                        snprintf(note, sizeof(note), "RTC != GPS: HOLD ACK 3 s to sync RTC to GPS");
    else                                      snprintf(note, sizeof(note), "verifying %d/%d", initClean, INIT_CLEAN_EPOCHS);
  } else if (state == ST_WARNING && lastMask == 0 && cleanSince) {
    snprintf(note, sizeof(note), "recovering %lus/%lus", (unsigned long)((now - cleanSince) / 1000), (unsigned long)(CLEAR_MS / 1000));
  } else if (state == ST_UNSAFE && latched) {
    snprintf(note, sizeof(note), "LATCHED: press ACK when safe");
  }
  const char* gate = (state == ST_TRUSTED) ? "PASS" : (state == ST_WARNING ? "CAUTION" : "BLOCK");
  char dist[16] = "n/a";
  char rel[48] = "";
  if (HAS_RELAY) snprintf(rel, sizeof(rel), "relay=%-6s host=%3luB/s ", relayClosed ? "CLOSED" : "OPEN", (unsigned long)hostRate);
  if (curDist >= 0) snprintf(dist, sizeof(dist), "%.1fm", curDist);
  Serial.printf("[%5lus] %-7s GATE=%-7s %s| fix=%c sats=%d hdop=%.1f | GPS %s RTC %s off=%+lds | dist=%s | flags=%s | sim=%s sel=%d%s%s\n",
                (unsigned long)(now / 1000), STATE_NAME[state], gate,
                rel,
                fixValid ? 'A' : 'V', ggaSats, ggaHdop, gpsT, rtcT, (long)curOffset,
                dist, flags,
                simActive == SIM_NONE ? "-" : "SIMULATED", (int)simSelected, note[0] ? " | " : "", note);
}

static void printHelp() {
  Serial.println("Keys: 1 time-jump  2 position-jump  3 signal-loss  4 nmea-burst  5 drift  x cancel-sim  a ACK  p provision  ? help");
}

// ============================ ARDUINO ENTRY POINTS ==========================
void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_LED_GREEN, OUTPUT); pinMode(PIN_LED_YELLOW, OUTPUT); pinMode(PIN_LED_RED, OUTPUT);
  relaySet(false);                                   // FIRST: make sure the data path starts CUT
  pinMode(PIN_BTN_ACK, INPUT_PULLUP); pinMode(PIN_BTN_SIM, INPUT_PULLUP);
  const int leds[3] = { PIN_LED_GREEN, PIN_LED_YELLOW, PIN_LED_RED };   // power-on LED self test
  for (int i = 0; i < 3; i++) { digitalWrite(leds[i], HIGH); delay(250); digitalWrite(leds[i], LOW); }

  Serial.println("\n=== CHRONOS GPS integrity monitor (prototype) ===");
  Serial.println("Anomaly DETECTION only - cannot prove spoofing. Stationary device assumed.");

  Wire.begin(PIN_SDA, PIN_SCL);
  rtcOk = rtc.begin();
  if (rtcOk) {
    bool lost = rtc.lostPower();
    rtcTimeValid = !lost && rtc.now().year() >= MIN_YEAR;
    Serial.printf("[RTC] DS3231 found. lostPower=%s, time=%s\n", lost ? "YES" : "no", rtcTimeValid ? "plausible" : "NOT VALID (needs provisioning)");
  } else {
    Serial.println("[RTC] DS3231 NOT FOUND - check SDA(GPIO21)/SCL(GPIO22)/3V3/GND");
  }

  GPSSerial.setRxBufferSize(1024);
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  if (HAS_RELAY) {
    HostSerial.setRxBufferSize(512);
    HostSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_HOST_RX, PIN_HOST_TX_UNUSED);
#if defined(ARDUINO_ARCH_ESP32)
    gpio_pullup_en((gpio_num_t)PIN_HOST_RX);         // idle-high when the relay is open
#endif
  }
  lastByteMs = millis();
  lastValidSentenceMs = millis();
  clearRules();
  printHelp();
  Serial.printf("[SIM] selected %d: %s -> expected: %s. Short-press SIM to change, HOLD to start.\n", (int)simSelected, SIM_NAME[simSelected], SIM_EXPECT[simSelected]);
}

void loop() {
  uint32_t now = millis();
  gpsPump();
  hostPump();
  applyGate();

  int e = pollBtn(btnAck, ACK_LONG_MS);
  if (e == 1) onAckShort(); else if (e == 2) onAckLong();
  e = pollBtn(btnSim, SIM_LONG_MS);
  if (e == 1) selectNextSim();                          // short press = choose
  else if (e == 2) { if (simActive != SIM_NONE) endSim(true); else startSim(simSelected); }   // hold = start / cancel

  while (Serial.available()) {                          // optional keyboard control from Serial Monitor
    char c = (char)Serial.read();
    if (c >= '1' && c <= '5') startSim((uint8_t)(c - '0'));
    else if (c == 'x' && simActive != SIM_NONE) endSim(true);
    else if (c == 'a') onAckShort();
    else if (c == 'p') onAckLong();
    else if (c == '?') printHelp();
  }

  simHousekeeping(now);

  if (now - lastTickMs >= 1000) {
    lastTickMs = now;
    hostRate = hostBytesWindow; hostBytesWindow = 0;
    watchdogRules(now);
    stateTick(now);
    printStatus(now);
  }
  updateLeds(now);
}
