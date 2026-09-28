# Chronos

### Hardware-layer GNSS integrity gateway

Chronos sits between a GNSS receiver and a stationary host system and checks the navigation data before the host gets it.

```text
GNSS ──> Chronos ──> Host
           │
        verify
           │
      pass / isolate
```

The idea is pretty simple: a GNSS packet can have a valid checksum and still contain bad information. Chronos uses an independent RTC plus GNSS consistency checks to decide whether the stream should still be trusted.

Built for the **ASYNC 2026 Cybersecurity & Defense** track.

## Hardware

| Qty | Component |
|---:|---|
| 1 | ESP32 DevKit |
| 1 | NEO-6M GPS + antenna |
| 1 | DS3231 RTC |
| 1 | 1-channel relay |
| 1 | 0.96" SSD1306 OLED |
| 1 | Breadboard |
| 1 set | Jumper wires |
| 1 | USB cable/power |

No IMU, SDR or custom PCB is required.

## What it checks

- NMEA checksum / syntax
- GNSS time vs DS3231
- stationary-position changes
- stale/replayed packets
- frozen GNSS data
- persistent time drift
- GNSS loss

When the stream is not trusted, the relay switches the host to the ESP32 holdover path.

## Attack lab

The Python simulator includes:

- time jump
- slow drift
- position teleport
- replay
- freeze
- malformed NMEA
- GNSS blackout

The dashboard shows the reason for the decision instead of just saying `ATTACK`.

## Run the simulator

```bash
cd simulator
python3 server.py
```

Open:

```text
http://127.0.0.1:8080
```

No third-party Python packages are required.

## Firmware

Open the repo in PlatformIO and build the ESP32 environment:

```bash
pio run
```

Upload:

```bash
pio run -t upload
```

Serial monitor:

```bash
pio device monitor -b 115200
```

Serial test commands:

```text
NORMAL
TEST_TIME_JUMP
TEST_SLOW_DRIFT
TEST_TELEPORT
TEST_REPLAY
TEST_FREEZE
TEST_BAD_NMEA
TEST_BLACKOUT
ENROLL
SYNC_RTC
CLEAR_REF
```

Test commands are clearly marked as test mode; they are not RF spoofing.

## Wiring

See [`hardware/wiring.md`](hardware/wiring.md).

The relay selects between:

```text
Normal:
NEO-6M ──> Host

Quarantine:
ESP32 holdover ──> Host
```

## Project structure

```text
chronos/
├── firmware/
├── simulator/
├── dashboard/
├── tests/
├── hardware/
├── docs/
└── wokwi/
```

## Limitations

Chronos is a prototype integrity/containment layer. It cannot cryptographically prove that a physically plausible GNSS signal is authentic, and it is not a replacement for high-end resilient-PNT hardware.
