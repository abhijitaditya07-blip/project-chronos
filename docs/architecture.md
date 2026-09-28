# Architecture

Chronos is a stationary GNSS integrity gateway.

```text
NEO-6M
  │
  │ NMEA + 1PPS
  ▼
ESP32
  ├── NMEA parser
  ├── time/RTC checks
  ├── stationary position check
  ├── replay/freeze checks
  ├── slow residual tracking
  └── security state machine
           │
           ▼
        RELAY
       /     \
 GNSS → host   ESP32 holdover → host
```

The protected system is assumed not to move. That gives us a usable position reference without adding another sensor.
