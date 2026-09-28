# Attack matrix

| Scenario | Evidence | Expected response |
|---|---|---|
| Time jump | GNSS vs RTC residual | QUARANTINE |
| Slow drift | persistent residual | SUSPECT → QUARANTINE |
| Position teleport | distance from fixed reference | QUARANTINE |
| Replay | repeated/stale timestamp/message | QUARANTINE |
| GPS freeze | repeated packets/no progress | QUARANTINE |
| Bad NMEA | checksum/syntax | QUARANTINE |
| Blackout | no valid GNSS | HOLDOVER |
| Normal | consistent data | TRUSTED |
