# Project Chronos: Hardware-Layer GPS Firewall

Most enterprise networks and autonomous systems have a massive blind spot: they trust incoming GPS serial data by default. Software firewalls only monitor network traffic, so if an attacker uses a cheap SDR to broadcast fake satellite signals, the host OS just accepts the corrupted NMEA strings. Time synchronization drifts, Kerberos authentication fails, and logs get corrupted.

Project Chronos is a hardware-based firewall we're building for ASYNC 2026. It sits directly on the UART wires between a GPS receiver and the host machine, validating the physics of the signal before it reaches the operating system.

## Hardware & BOM
We kept the component cost under $15 to make this viable as a cheap, deployable edge appliance.
* **MCU:** ESP32 Dev Board (Dual-core allows us to run floating-point math without blocking the I/O loop).
* **Reference Clock:** DS3231 I2C RTC (The ESP32's internal timer drifts too much, so we need an external quartz crystal for strict ground truth).
* **The Air-Gap:** 1-Channel 5V Relay.
* **Demo UI:** 0.96" SSD1306 OLED & Tactile Pushbuttons for injecting spoofed data during the pitch.

## How the Code Works
The firmware currently leverages high-speed mathematical thresholds to detect anomalies at the edge.
1. **Cross-Checking:** We read the NMEA strings and the physical PPS (Pulse-Per-Second) interrupt from the GPS and compare them against the isolated DS3231 clock.
2. **Threshold Math:** The ESP32 calculates absolute time drift ($\Delta t$) and physical velocity (using the Haversine formula).
3. **Hardware Kill-Switch:** If the incoming data violates physics (e.g., a stationary server suddenly moves at 150 km/h, or the time jumps 3 hours instantly), the ESP32 pulls the relay pin HIGH. This physically severs the copper UART TX line, dropping the spoofed data.
4. **Ghost-Feed Injection:** Tripping a relay causes a sudden loss of data, which can crash the host system. To prevent this, the ESP32 instantly takes over the TX line and starts injecting synthetic, properly formatted NMEA strings generated from the clean RTC time to keep the server stable.

## Running the Wokwi Simulation
We have set up an interactive hardware simulation in this repo using PlatformIO and Wokwi so you can test the logic without physical breadboarding.

1. Clone this repo and open it in VS Code with the **PlatformIO** and **Wokwi Simulator** extensions installed.
2. Build the project (PlatformIO will automatically grab the Adafruit OLED drivers from `platformio.ini`).
3. Open the command palette (`Ctrl+Shift+P`) and run `Wokwi: Start Simulator`.
4. **The Demo:** The OLED boots to `[SYSTEM SECURE]`. Press the red **INJECT SPOOF** button to simulate a localized SDR time-jump. The $\Delta t$ calculation will freeze on the inverted OLED, the relay will click over, and the system will latch into `>> AIR-GAP ENGAGED <<` until manually reset via the green button.

## Next Steps / Hackathon Stretch Goals
To push this beyond a simple kill-switch and turn it into an enterprise-grade security appliance, we are focusing on two major software additions for the ESP32:

* **Secure NTP Holdover (WiFi Failover):** The DS3231 RTC is accurate, but it will eventually drift during a prolonged attack. Since the ESP32 has onboard WiFi, we are adding a feature where, upon tripping the air-gap, the MCU connects to a secure enterprise NTP server. By dynamically disciplining the RTC over WiFi, our "Ghost-Feed" can maintain perfect synchronization indefinitely, even during a multi-day GPS jamming campaign.
* **Active Threat Intelligence (SIEM Integration):** Instead of just blocking the attack locally, the ESP32 will format the anomaly data (timestamp, $\Delta t$ drift, and the attacker's false coordinates) into a JSON payload. It will push this data over MQTT to an enterprise SIEM dashboard (like Splunk or Elastic), turning Chronos from a passive filter into an active localized threat sensor.