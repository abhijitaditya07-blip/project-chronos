#pragma once

// Hardware pins. Change only here if your board/wiring differs.
constexpr int GPS_RX_PIN = 16;
constexpr int GPS_TX_PIN = 17;
constexpr int HOST_TX_PIN = 4;
constexpr int GPS_PPS_PIN = 27;
constexpr int I2C_SDA_PIN = 21;
constexpr int I2C_SCL_PIN = 22;
constexpr int RELAY_PIN = 5;

// Many Arduino relay modules are active HIGH; some are active LOW.
// Flip these two values if your module behaves the opposite way.
constexpr uint8_t RELAY_TRUSTED_LEVEL = HIGH;
constexpr uint8_t RELAY_HOLDOVER_LEVEL = LOW;

constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t RTC_ADDR = 0x68;

// Detection tuning.
constexpr double STATIONARY_RADIUS_M = 75.0;
constexpr double HARD_POSITION_RADIUS_M = 500.0;
constexpr long HARD_TIME_JUMP_MS = 5000;
constexpr long SUSPECT_TIME_RESIDUAL_MS = 100;
constexpr long HARD_TIME_RESIDUAL_MS = 2000;
constexpr uint32_t PACKET_TIMEOUT_MS = 5000;
constexpr uint32_t RECOVERY_GOOD_SAMPLES = 8;
constexpr uint32_t SUSPECT_CONFIRM_SAMPLES = 3;
constexpr uint32_t ENROLL_SAMPLES = 20;

// Loop periods.
constexpr uint32_t OLED_PERIOD_MS = 500;
constexpr uint32_t TELEMETRY_PERIOD_MS = 500;
constexpr uint32_t RTC_PERIOD_MS = 1000;
