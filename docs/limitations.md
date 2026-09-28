# Limitations

1. The NEO-6M exposes relatively coarse NMEA time information. Sub-second drift detection is much better when the receiver exposes a usable 1PPS output.
2. A stationary reference is an assumption of the prototype.
3. NMEA-level checks cannot prove that a physically plausible GNSS signal is authentic.
4. Relay behavior and timing depend on the specific relay module.
5. The holdover stream is explicitly marked invalid; it is not presented as real GNSS.
