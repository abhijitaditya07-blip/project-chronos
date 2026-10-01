# Chronos

### Hardware-layer GNSS integrity gateway (prototype)

![CI](https://github.com/abhijitaditya07-blip/project-chronos/actions/workflows/ci.yml/badge.svg)

Chronos sits after a GPS receiver and checks the receiver's output before an application relies on it.
It compares GPS time with an independent DS3231 real-time clock, compares GPS position with the fixed
spot where the device was installed, and publishes an explicit trust state: **TRUSTED**, **WARNING**
(suspect) or **UNSAFE** (quarantine).

> **Scope, stated plainly.** Chronos is an integrity-monitoring / anomaly-detection prototype. It cannot
> prove that GPS spoofing is happening, a careful spoofer can evade it, and it does not isolate RF.
> Anomalies in the demo are **simulated in software**; no GPS signals are transmitted.

Built for the **ASYNC 2026 Cybersecurity & Defense** track.

---

## Problem statement

Systems that navigate or synchronise on GNSS (ships, aircraft, drones, networks, power and logging
equipment) mostly trust whatever the receiver reports. Civil GPS signals are not authenticated, and
jamming and spoofing incidents are being reported at sea and in the air (see references 8 and 9).

The core problem is that **valid is not the same as true**. A receiver locked onto a counterfeit signal
still outputs well-formed NMEA sentences with correct checksums, a valid fix flag and plausible numbers.
Nothing between the receiver and the application asks whether those numbers should be believed.
Receiver-level detection exists on some modern modules (reference 3), but many deployed systems use
commodity or sealed receivers that cannot be swapped, and the application can only trust or distrust a
black box.

## Use case and purpose

Chronos is for stationary systems that consume GPS position or time from a receiver they cannot modify
and have no independent way to question, for example a fixed-site timing or logging node, an edge gateway,
or a field sensor whose installed location is known (these are *potential* uses; nothing here is
deployed). Its purpose is to put a verification step at the boundary between an untrusted external sensor
and the application: it reads the receiver's ordinary output, checks it against two witnesses the radio
signal cannot set (a battery-backed clock and the device's own installed position), separates equipment
faults from suspicious behaviour, and outputs an explicit trust state with a reason, so the application can
keep using GPS, treat it with caution, or ignore it and fall back to something else. It turns "GPS is true
unless it breaks" into "GPS is a sensor that must keep earning trust", while being honest that it reduces
blind trust rather than proving authenticity.

## What is implemented

| Status | Item |
|---|---|
| Implemented, tested | NMEA parsing (`RMC`, `GGA`, `GSV`) with checksum and range validation; GPS-vs-RTC comparison; position baseline learned at install; 16 detection rules; state machine with latch and 20 s recovery; LEDs; Serial log; five labelled simulated anomalies; printed `GATE` decision |
| Optional, off by default | Relay that physically cuts the GPS data line to a protected input (`HAS_RELAY` in the firmware); needs extra hardware, see `hardware/wiring.md` |
| Not implemented | PPS / sub-second timing, holdover output, dedicated frozen-position or replay rule, IMU, second receiver, RF-level jamming detection, a downstream application consuming the trust state |

## Architecture

```
GNSS satellites  (civil signal, unauthenticated)
        |
   NEO-6M receiver
        |  raw NMEA text, UART2, 9600 baud, GPIO16
        v
+--------------------------------------------------------------+
| ESP32  /  CHRONOS                                            |
|                                                              |
|  line assembler -> checksum + range checks -> RMC/GGA/GSV    |
|        |                                                     |
|        +--> epoch analysis <---- DS3231 RTC (I2C 21/22)      |
|        |      GPS-RTC offset, time step, position vs         |
|        |      installed baseline, jump                       |
|        +--> watchdogs: silence, no fix, malformed rate,      |
|               RTC health, fix flapping, sentence consistency |
|                         |                                    |
|                  rule engine (debounced, time-held flags)    |
|                         |                                    |
|        state machine:  INIT -> TRUSTED <-> WARNING -> UNSAFE |
+-------------------------|------------------------------------+
                          v
        LEDs (GPIO 25/26/27)  +  Serial log  +  GATE flag
                          v
        Downstream application  (enabled by the design, not built)
```

**How it works**

1. **Parse and validate.** The ESP32 assembles NMEA lines itself (no GPS library), verifies the XOR checksum
   and rejects truncated, oversized, non-printable or out-of-range data, counting every reject.
2. **Compare against independent witnesses.** Once per GPS epoch it reads the DS3231 and computes
   GPS-minus-RTC and how far GPS time moved versus the RTC. It also measures distance from the position
   averaged over 20 good fixes at install, and from the previous fix.
3. **Run the rules.** 16 rules, grouped as *fault*, *suspicious* or *confirmed*, each with a debounce and a
   hold time, raise flags (see the table below).
4. **Decide.** The state machine turns active flags into a state. Equipment faults (no data, no fix) heal
   themselves; suspected anomalies latch until a person presses ACK and then need 20 clean seconds.
5. **Publish.** LEDs, a Serial line with the reason, and `GATE=PASS / CAUTION / BLOCK`.

**State machine**

| State | LEDs | Meaning |
|---|---|---|
| INIT | yellow blinking | learning position, waiting for fix or RTC sync |
| TRUSTED | green | no indicators; `GATE=PASS` |
| WARNING (suspect) | yellow | a fault or suspicious indicator is active; `GATE=CAUTION` |
| UNSAFE (quarantine) | red (solid = latched anomaly, fast blink = fault) | GPS not trusted; `GATE=BLOCK` |

WARNING becomes UNSAFE on any confirmed rule, suspicious indicators lasting 15 s, three or more at once, or
a fault lasting 30 s. UNSAFE from an anomaly needs ACK; UNSAFE from a fault clears itself.

**Why a DS3231.** It is a battery-backed, temperature-compensated clock (datasheet accuracy about 2 ppm,
roughly 0.17 s per day) whose time does not come from any radio signal. It is a *witness*, not a precision
reference: here it is read to 1 s resolution and there is no PPS.

### Detection rules

| Rule | Class | Trigger (defaults in `chronos.ino`) |
|---|---|---|
| `RX_SILENT` | fault | no UART byte for 3 s |
| `NO_FIX` | fault | receiver alive, no valid fix for 4 s |
| `RTC_FAULT` | fault | DS3231 not answering |
| `BAD_NMEA` | suspicious | 3 bad sentences in 6 s, or bytes with no valid sentence for 4 s |
| `DATE_BAD` | suspicious | GPS year outside 2024-2099 |
| `FIX_FLAP` | suspicious | 3 fix losses in 120 s |
| `INCONSISTENT` | suspicious | RMC and GGA disagree on fix, or fix with fewer than 3 satellites |
| `SAT_JUMP` | suspicious | satellites used change by 6 or more in 1 s while fix holds |
| `SNR_UNIFORM` | suspicious | 6+ satellites within 3 dB of each other for 5 cycles (weak heuristic) |
| `TIME_STEP` | suspicious | GPS time moves 2 s differently from the RTC between epochs |
| `TIME_OFFSET` | suspicious | GPS minus RTC at least 2 s for 3 epochs |
| `POS_JUMP` | suspicious | 40 m or more between consecutive 1 Hz fixes |
| `POS_DRIFT` | suspicious | 60 m or more from installed position for 3 epochs |
| `TIME_BACKWARDS` | confirmed | GPS clock moves backward and 2 s or more off versus RTC |
| `TIME_OFFSET_HARD` | confirmed | GPS minus RTC at least 5 s for 3 epochs |
| `POS_FAR` | confirmed | 250 m or more from installed position for 3 epochs |

"Confirmed" means the data broke an invariant of Chronos's model (independent clock, stationary device).
It is not proof of an attack. Thresholds are engineering choices for a bench prototype; tune them with your
own logs.

## Hardware

ESP32 DevKit (WROOM), NEO-6M GPS with antenna, DS3231 RTC module, 3 LEDs with resistors, 2 push buttons,
breadboard, jumper wires, USB cable. **No PPS, OLED or IMU is used.** An optional 5 V relay module is
supported but disabled by default. Full pin map and safety notes: [`hardware/wiring.md`](hardware/wiring.md).

| Signal | ESP32 GPIO |
|---|---:|
| GPS TX to ESP32 RX2 | 16 |
| DS3231 SDA / SCL | 21 / 22 |
| LEDs green / yellow / red | 25 / 26 / 27 |
| Buttons ACK / SIM | 32 / 33 |

## How to run

**Option A: PlatformIO (VS Code)**

1. Install VS Code and the *PlatformIO IDE* extension. Make sure the ESP32 shows up as a COM/serial port
   (install the CP210x or CH340 USB driver if it does not, and use a data-capable USB cable).
2. Open this repository folder in VS Code.
3. Build and upload:
   ```
   pio run
   pio run -t upload
   ```
4. Serial monitor (115200 baud):
   ```
   pio device monitor -b 115200
   ```

**Option B: Arduino IDE**

1. Add the ESP32 board package (Espressif, via Boards Manager) and install the **RTClib** library by Adafruit.
2. Open `firmware/chronos/chronos.ino`, choose **ESP32 Dev Module** and your port, upload, and open the Serial
   Monitor at **115200**.

**First run (once, with a GPS fix, antenna at a window or outdoors)**

1. Boot: the LEDs flash green, yellow, red, then the state is **INIT** (yellow blinking). A first fix can take
   1 to 15 minutes.
2. Serial shows `learning position n/20`.
3. If the RTC was never set, yellow blinks fast and Serial says `HOLD ACK 3 s`. Hold the **ACK** button for 3 s
   to copy GPS time into the RTC. Do this in a place and moment you trust: Chronos treats it as ground truth.
4. After about 25 s of agreement the state becomes **TRUSTED** (green).

**Controls**

| Control | Action |
|---|---|
| ACK short press | acknowledge a latched UNSAFE (starts the 20 s recovery check) |
| ACK hold 3 s | provision: set RTC from GPS and re-learn the installed position |
| SIM short press | select the next simulated anomaly (yellow blinks its number, nothing starts) |
| SIM hold 1.5 s | start the selected simulation (from TRUSTED only), or cancel a running one |
| Serial keys | `1` to `5` start a simulation directly, `x` cancel, `a` ACK, `p` provision, `?` help |

## How to test

**1. Host tests (no hardware needed).** Compiles the real firmware against small Arduino/RTClib stubs and
drives it with a scripted fake GPS and RTC: normal run, provisioning, all five simulations, receiver silence,
uniform-signal heuristic and a real 300 m step.

```
bash tests/host/run_tests.sh
```

Needs only `g++`. Expected: `ALL HOST TESTS PASSED`. These tests do not cover wiring, ESP32 timing or real GPS.

**2. Hardware bring-up sketches.** Test each part alone, in order, before the full firmware. Each sketch is in
`tests/hardware/<name>/`:

| Sketch | Checks | Pass looks like |
|---|---|---|
| `t1_serial` | ESP32 and Serial | `heartbeat 0, 1, 2...` at 115200 |
| `t2_led` | LEDs | green, yellow, red in turn |
| `t3_buttons` | buttons | `PRESSED (0)` on press, `released (1)` on release |
| `t4_ds3231` | RTC | device at `0x68`, seconds count up |
| `t5_gps` | GPS UART | readable `$GPRMC`, `$GPGGA` lines (a fix shows `A`) |
| `t6_relay` | optional relay | host bytes flow only while relay is CLOSED |

**3. Simulated anomalies on the device.** Start each from green. Simulations are injected in software and
labelled `[SIMULATED ANOMALY]`.

| Key | Simulation | Expected result |
|---|---|---|
| `1` | GPS clock +37 s | UNSAFE (red, latched) within seconds |
| `2` | position jump about 5.5 km | UNSAFE (red, latched) within seconds |
| `3` | signal loss | WARNING only, recovers by itself |
| `4` | malformed NMEA burst | WARNING only, recovers by itself |
| `5` | slow drift 5 m/s | WARNING after about 15 s, UNSAFE after about 30 s |

After 1, 2 or 5: wait for `ended`, press **ACK**, then wait 20 s of clean data for green. ACK is refused while
a simulation is still running.

**4. Real faults (no simulation).** Unplug the GPS TX wire: expect `RX_SILENT`, yellow, then red after 30 s,
recovering on its own when reconnected. Unclip the antenna: `NO_FIX`. Remove a DS3231 wire: `RTC_FAULT`.

**5. Keep a log.** Save the Serial output of each test with what you did, what you expected and what
happened. For a stronger evidence table, also record seconds from injection to red for each simulation, and
the number of false alarms over a 30 to 60 minute outdoor run.

## Repository structure

```
project-chronos/
├── firmware/chronos/chronos.ino   firmware (Arduino/PlatformIO)
├── hardware/wiring.md             pin map, power safety, optional relay
├── tests/
│   ├── host/                      PC tests of detection logic + stubs
│   └── hardware/                  bring-up sketches t1..t6
├── platformio.ini
├── .github/workflows/ci.yml
├── simulator/  dashboard/  wokwi/  docs/   (see note)
└── README.md
```

**Note.** `simulator/`, `dashboard/`, `wokwi/` and `docs/` come from an earlier exploration of the same idea
that assumed a relay, OLED and PPS. They are not part of the tested hardware prototype and may use different
rules and thresholds than the firmware.

## Limitations

- A spoofer that keeps time within about 2 s and position within about 60 m of reality will not be caught.
- No PPS and 1 s RTC resolution: this is not precision timing.
- Position checks assume the device does not move. Moving platforms would need an IMU or velocity limits.
- Trust on first use: the RTC and position baseline come from the first good GPS fix. If GPS is already
  spoofed at provisioning, Chronos inherits it.
- Quarantine is a decision. Without the optional relay nothing is disconnected; with it, only the data line
  is cut. RF is never isolated.
- DS3231 drift is small but real; re-provision after weeks of operation.
- Thresholds are untuned for your site; poor sky view raises false alarms.

## References

1. Microchip, "New BlueSky GNSS Firewall From Microsemi Provides Secure, Continuous Timing Integrity in GPS-Denied Environments" - https://microchip.com/en-us/about/news-releases/products/new-bluesky-gnss-firewall-from-microsemi-provides-secure-continuous-timing-integrity-in-gps-denied-environments
2. Microchip BlueSky GNSS Firewall product page - https://www.microchip.com/en-us/products/clock-and-timing/systems/bluesky-gnss-firewall
3. u-blox, NEO-M9N Product Summary (receiver-level jamming and spoofing detection) - https://content.u-blox.com/sites/default/files/NEO-M9N_ProductSummary_UBX-19027207.pdf
4. GPS World, "Galileo OSNMA authentication service now operational" - https://www.gpsworld.com/galileo-osnma-authentification-service-now-operational/
5. European GNSS Service Centre, "Celebrating one year of Galileo OSNMA" - https://www.gsc-europa.eu/node/7832
6. Khanafseh, Roshan, Langel, Chan, Joerger, Pervan, "GPS spoofing detection using RAIM with INS coupling", IEEE/ION PLANS 2014 - https://experts.arizona.edu/en/publications/gps-spoofing-detection-using-raim-with-ins-coupling/
7. "GNSS spoofing detection using IMU and odometer consistency", Sensors 2018, 18(5), 1305 - https://dx.doi.org/10.3390/s18051305
8. GPS World, "How to Defeat Harmful GPS/GNSS Interference: A Roadmap for Action" (ICAO, ITU and IMO joint warning, March 2025) - https://www.gpsworld.com/how-to-defeat-harmful-gps-gnss-interference-a-roadmap-for-action/
9. gCaptain, "GPS Interference Off California Offers Warning for Global Shipping" (29 January 2026 event) - https://gcaptain.com/gps-interference-off-california-offers-warning-for-global-shipping/
10. Component and tooling documentation: NMEA 0183 sentence formats; DS3231 datasheet (Analog Devices / Maxim); u-blox 6 / NEO-6 data sheet; Adafruit RTClib - https://github.com/adafruit/RTClib ; Arduino-ESP32 core - https://github.com/espressif/arduino-esp32 ; PlatformIO - https://platformio.org

Items 1 to 9 were checked against their pages while preparing this README (7 was checked only by its search
snippet, so confirm its exact title). Item 10 is general documentation named without a link check.
