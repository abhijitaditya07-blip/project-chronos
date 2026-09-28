import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "simulator"))

from attack_engine import AttackEngine
from nmea import wrap


class SimulatorTests(unittest.TestCase):
    def test_checksum(self):
        msg = wrap("GPRMC,123519,A,4807.038,N,01131.000,E,0.0,0.0,230394,,,N")
        self.assertTrue(msg.startswith("$GPRMC"))
        self.assertEqual(msg.count("*"), 1)

    def test_normal(self):
        e = AttackEngine()
        s = e.sample(1700000000)
        self.assertTrue(s.valid)
        self.assertTrue(s.nmea.startswith("$GPRMC"))

    def test_teleport_changes_position(self):
        e = AttackEngine()
        e.set_mode("position_teleport")
        s = e.sample(1700000000)
        self.assertGreater(abs(s.lat-e.base_lat), 1)

    def test_blackout(self):
        e = AttackEngine()
        e.set_mode("blackout")
        s = e.sample(1700000000)
        self.assertFalse(s.valid)
        self.assertEqual(s.nmea, "")

    def test_slow_drift_moves_time(self):
        e = AttackEngine()
        e.set_mode("slow_drift")
        a = e.sample(1700000000)
        b = e.sample(1700000001)
        self.assertGreater(b.epoch, a.epoch)


if __name__ == "__main__":
    unittest.main()
