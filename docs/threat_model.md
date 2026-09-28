# Threat model

## Attacker can

- manipulate GNSS information seen by the receiver
- replay previously valid GNSS data
- introduce time offsets or gradual drift
- corrupt NMEA data
- cause GNSS data loss

## Chronos does not claim to

- cryptographically prove GNSS authenticity
- detect every physically plausible spoofing attack
- replace high-end resilient-PNT receivers
- infer RF-layer spoofing from NMEA alone in every case

Chronos is a prototype integrity/containment layer. Its value is that bad or suspicious navigation data can be stopped before it reaches the protected host when an observable inconsistency exists.
