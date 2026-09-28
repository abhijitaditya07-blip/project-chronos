# Wiring

## ESP32

| Signal | ESP32 |
|---|---:|
| NEO-6M TX → ESP32 RX | GPIO16 |
| NEO-6M 1PPS → ESP32 | GPIO27 |
| ESP32 holdover TX | GPIO4 |
| Relay control | GPIO5 |
| I2C SDA | GPIO21 |
| I2C SCL | GPIO22 |

The firmware opens two UARTs:

- UART2: GPS input on GPIO16/17
- UART1: holdover output on GPIO4

GPIO17 is not required by the current NEO-6M receive path but is reserved for GPS UART TX/configuration.

## Relay

```text
NEO-6M TX  -> Relay NO
ESP32 GPIO4 -> Relay NC
Relay COM    -> Protected host RX
```

The relay is normally kept on the GNSS side only after Chronos reaches `TRUSTED`.
At startup and quarantine it selects the ESP32 holdover path.

### Relay polarity

`RELAY_TRUSTED_LEVEL` and `RELAY_HOLDOVER_LEVEL` are in `firmware/src/config.h`.
Cheap relay modules vary, so verify the behavior with a multimeter before connecting the host.

## Power

Power I2C modules at **3.3 V** when the specific breakout supports it. Do not feed 5 V pull-ups directly into ESP32 I/O.

The NEO-6M breakout should be powered according to its regulator/board labeling.
