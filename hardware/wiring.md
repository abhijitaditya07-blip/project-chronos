# Wiring (single definitive pin map)

Everything runs at **3.3 V**. This is the wiring the firmware in `firmware/chronos/chronos.ino` expects.
The prototype uses **no PPS, no OLED and no IMU**. A relay is optional (see the end).

## Pin map

| Part | Part pin | ESP32 pin | GPIO | Notes |
|---|---|---|---:|---|
| Power rails | red (+) rail | 3V3 | - | Never connect VIN/5V to this rail |
| Power rails | blue (-) rail | GND | - | Everything shares ground |
| NEO-6M GPS | VCC | 3V3 rail | - | Check your board's silkscreen for accepted voltage |
| NEO-6M GPS | GND | GND rail | - | |
| NEO-6M GPS | **TX** | RX2 | **16** | GPS sends, ESP32 receives (UART2) |
| NEO-6M GPS | RX | not connected | - | Not needed, firmware only listens |
| NEO-6M GPS | PPS | not connected | - | The prototype does not use PPS |
| DS3231 RTC | VCC | 3V3 rail | - | Powering at 3.3 V keeps SDA/SCL pull-ups at 3.3 V |
| DS3231 RTC | GND | GND rail | - | |
| DS3231 RTC | **SDA** | D21 | **21** | |
| DS3231 RTC | **SCL** | D22 | **22** | |
| Green LED | long leg via resistor | D25 | **25** | GPIO -> resistor -> LED long leg; short leg -> GND rail |
| Yellow LED | long leg via resistor | D26 | **26** | Same pattern |
| Red LED | long leg via resistor | D27 | **27** | Same pattern |
| LED resistors (x3) | in series | - | - | 220 ohm to 1 kohm |
| ACK button | diagonal leg A | D32 | **32** | Diagonal leg B -> GND rail; internal pull-up |
| SIM button | diagonal leg A | D33 | **33** | Diagonal leg B -> GND rail; internal pull-up |

### Pushing a 4-leg button into a breadboard
Two legs on the same side are usually joined inside the button. Always use **diagonal** (opposite-corner)
legs: one to the GPIO, one to GND. If a button reads "pressed" at idle, rotate it 90 degrees.

## ESP32 module check
GPIO16/17 are free on `ESP32-WROOM-32`. On `ESP32-WROVER` modules they are used for PSRAM, so the GPS pin
would have to change. Check the label on the metal shield.

## Power safety
- ESP32 GPIO pins are **not** 5 V tolerant. Never feed 5 V into any GPIO.
- Power the DS3231 from 3V3 so its I2C pull-ups never reach 5 V.
- Do not use GPIO 6-11 (flash). The firmware does not.
- Every LED needs a series resistor.
- Wire with USB unplugged.

## Optional relay (disabled by default: `HAS_RELAY = false` in the firmware)

The relay is **not required** for the prototype. It adds a physical data-path cut between the GPS and a
protected host input. It does **not** isolate RF.

| FROM | TO | GPIO |
|---|---|---:|
| Relay module VCC | ESP32 VIN (5 V from USB) | - |
| Relay module GND | GND rail | - |
| Relay module IN | ESP32 D23 | 23 |
| GPS TX | Relay COM terminal | - |
| GPS TX | ESP32 D16 (stays connected, so Chronos keeps monitoring) | 16 |
| Relay NO terminal | ESP32 D18 (stands in for the protected host's RX) | 18 |

- Leave NC unconnected. Wiring to NO means a reset or crash leaves the path **cut**.
- Most one-channel modules switch ON when IN is LOW. If yours is HIGH-trigger, set `RELAY_ACTIVE_LOW = false`.
- Run `tests/hardware/t6_relay` first. Host bytes must flow only while the relay is CLOSED.
- The relay follows the state: closed in TRUSTED and WARNING, open in INIT and UNSAFE.
- Power the relay from the laptop's USB through VIN. Do not power it from 3V3 or a GPIO.
