# Project Chronos: Hardware-Layer GPS & Time Spoofing Defense

Most networks, Active Directory domains, and autonomous systems blindly trust the GPS data they receive. Because software firewalls only monitor network traffic (TCP/IP), they can't detect physical Radio Frequency (RF) spoofing. With a cheap SDR, an attacker can broadcast fake satellite signals to shift a server's time or spoof a drone's coordinates. 

This causes Kerberos authentication lockouts, breaks SIEM log correlation, and bypasses geofencing.

Project Chronos is a hardware-level firewall we're building for the ASYNC 2026 Hackathon. It sits inline between a GPS receiver and the host system to validate the incoming signal before it ever reaches the OS.

## Hardware Stack

Instead of trying to filter bad data in software, we cross-check two physical hardware domains: the easily-spoofed external RF signals vs. an isolated local quartz clock.

*   **MCU:** ESP32 Dev Board
*   **GPS Receiver:** u-blox NEO-6M (UART/PPS)
*   **Reference Clock:** DS3231 RTC (I2C)
*   **Hardware Kill-Switch:** 1-Channel 5V Relay

## How It Works

1.  **Data Ingestion:** The ESP32 continuously reads standard NMEA sentences and Pulse-Per-Second (PPS) hardware interrupts from the GPS module. 
2.  **Validation:** The firmware calculates time drift ($\Delta t$) and physical velocity (using the Haversine formula), comparing the incoming GPS data against our isolated RTC.
3.  **The Air-Gap:** If the GPS data violates basic physics (e.g., time jumps by 5 minutes, or a stationary server suddenly moves at 150 km/h), the ESP32 triggers the relay. This physically cuts the UART TX line, stopping the corrupted data dead in its tracks.

## Advanced Features (Stretch Goals)

To deal with more advanced attacks (like slow-drift spoofing), we are aiming to implement three extra features during the hackathon:

*   **TinyML Anomaly Detection:** We're setting up an Edge Impulse neural network on the ESP32 to monitor the Signal-to-Noise Ratio (SNR) variance in the `$GPGSV` stream. This lets us catch the behavioral RF signature of a ground-based transmitter before time or location even shifts.
*   **ESP-NOW Swarm:** By deploying multiple Chronos nodes, they can communicate locally via ESP-NOW. When an anomaly is detected, the swarm can theoretically cross-reference timestamps to triangulate the source of the fake RF signal.
*   **"Ghost-Feed" Fallback:** Cutting the data line stops the attack but causes a temporary DoS for the server. To fix this, once the relay trips, the ESP32 takes over the TX line and injects properly formatted, synthetic NMEA strings generated from the RTC time to keep the host server alive.

## Hackathon Status
Currently in the initial build phase. We are writing the C++ scaffolding for the Haversine math, drift thresholds, and the synthetic data generator prior to final breadboard assembly.