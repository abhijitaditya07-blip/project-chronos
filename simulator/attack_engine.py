from dataclasses import dataclass
from typing import List
from nmea import rmc


@dataclass
class Sample:
    epoch: float
    lat: float
    lon: float
    speed_kmh: float
    nmea: str
    valid: bool = True


class AttackEngine:
    def __init__(self, lat=12.9716, lon=77.5946):
        self.base_lat = lat
        self.base_lon = lon
        self.mode = "normal"
        self.tick = 0
        self.drift_ms = 0.0
        self.replay_buffer: List[Sample] = []
        self.last_sample: Sample | None = None

    def set_mode(self, mode: str):
        new_mode = mode.lower()
        if new_mode != self.mode:
            self.replay_buffer.clear()
        self.mode = new_mode
        if self.mode != "slow_drift":
            self.drift_ms = 0.0
        self.tick = 0

    def sample(self, epoch: float) -> Sample:
        self.tick += 1
        lat, lon = self.base_lat, self.base_lon
        speed = 0.0
        valid = True
        attack_epoch = epoch

        if self.mode == "time_jump":
            attack_epoch += 3 * 3600
        elif self.mode == "slow_drift":
            self.drift_ms += 150.0
            attack_epoch += self.drift_ms / 1000.0
        elif self.mode == "position_teleport":
            lat += 5.0
            lon += 5.0
        elif self.mode == "freeze":
            if self.replay_buffer:
                return self.replay_buffer[-1]
        elif self.mode == "malformed_nmea":
            n = rmc(int(epoch), lat, lon)
            return Sample(epoch, lat, lon, 0.0, n[:-2] + "00", True)
        elif self.mode == "blackout":
            return Sample(epoch, lat, lon, 0.0, "", False)

        msg = rmc(int(attack_epoch), lat, lon, speed, valid)
        sample = Sample(attack_epoch, lat, lon, speed, msg, valid)
        if self.mode == "replay":
            if not self.replay_buffer:
                self.replay_buffer.append(sample)
            return self.replay_buffer[0]
        if len(self.replay_buffer) > 8:
            self.replay_buffer.pop(0)
        self.replay_buffer.append(sample)
        self.last_sample = sample
        return sample
