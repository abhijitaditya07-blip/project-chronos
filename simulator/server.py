#!/usr/bin/env python3
import json
import math
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from attack_engine import AttackEngine

ROOT = Path(__file__).resolve().parent.parent
DASHBOARD = ROOT / "dashboard" / "index.html"

state_lock = threading.Lock()
engine = AttackEngine()
state = {
    "state": "TRUSTED",
    "integrity": 100.0,
    "mode": "normal",
    "gnss_time": int(time.time()),
    "rtc_time": int(time.time()),
    "time_residual_ms": 0,
    "distance_from_reference_m": 0.0,
    "packet_age_ms": 0,
    "relay": "GNSS",
    "holdover": False,
    "reason": "NONE",
    "nmea_ok": True,
    "freshness_ok": True,
    "rtc_ok": True,
    "position_ok": True,
    "replay_ok": True,
    "freeze_ok": True,
    "incoming_nmea": "",
    "outgoing_nmea": "",
    "history": [],
    "events": [],
}


def distance_m(a_lat, a_lon, b_lat, b_lon):
    r = 6371000.0
    p1, p2 = math.radians(a_lat), math.radians(b_lat)
    dp = math.radians(b_lat-a_lat)
    dl = math.radians(b_lon-a_lon)
    x = math.sin(dp/2)**2 + math.cos(p1)*math.cos(p2)*math.sin(dl/2)**2
    return r * 2 * math.atan2(math.sqrt(x), math.sqrt(1-x))


def checksum_ok(sentence: str) -> bool:
    if not sentence.startswith("$") or "*" not in sentence:
        return False
    body, supplied = sentence[1:].split("*", 1)
    if len(supplied) < 2:
        return False
    value = 0
    for ch in body:
        value ^= ord(ch)
    return supplied[:2].upper() == f"{value:02X}"


def detector(sample, now):
    reasons = []
    time_residual = int((sample.epoch - now) * 1000)
    d = distance_m(engine.base_lat, engine.base_lon, sample.lat, sample.lon)
    nmea_ok = checksum_ok(sample.nmea)
    position_ok = d <= 75
    hard = False
    score = 100.0

    if not sample.valid:
        reasons.append("NO_GNSS_DATA")
        score -= 45
        hard = True

    if not nmea_ok:
        reasons.append("NMEA_CHECKSUM")
        score -= 45
        hard = True

    if abs(time_residual) > 2000:
        reasons.append("TIME_RESIDUAL_HIGH")
        score -= 30
        hard = True
    elif abs(time_residual) > 100:
        reasons.append("TIME_RESIDUAL_HIGH")
        score -= 15

    if engine.mode == "slow_drift":
        if engine.drift_ms > 300:
            reasons.append("PERSISTENT_TIME_DRIFT")
            score -= min(45.0, engine.drift_ms / 15.0)
        if engine.drift_ms > 1200:
            hard = True

    if d > 500:
        reasons.append("POSITION_TELEPORT")
        score -= 35
        hard = True
    elif not position_ok:
        reasons.append("POSITION_OUT_OF_BOUNDS")
        score -= 20

    replay = False
    freeze = False
    if engine.mode in ("replay", "freeze"):
        if engine.last_sample is not None and sample.nmea == engine.last_sample.nmea:
            engine.repeat_count = getattr(engine, "repeat_count", 0) + 1
        else:
            engine.repeat_count = 0
        if engine.repeat_count >= 2:
            replay = engine.mode == "replay"
            freeze = engine.mode == "freeze"
            reasons.append("REPLAY_DETECTED" if replay else "GPS_FREEZE")
            score -= 40
            hard = True
    else:
        engine.repeat_count = 0

    if engine.mode == "replay" and engine.replay_buffer:
        reasons.append("REPLAY_DETECTED")
        replay = True
        score -= 30
        hard = True

    score = max(0.0, min(100.0, score))
    if hard or score < 30:
        new_state = "QUARANTINE"
    elif score < 70:
        new_state = "SUSPECT"
    else:
        new_state = "TRUSTED"
    return score, new_state, list(dict.fromkeys(reasons)), time_residual, d, nmea_ok, position_ok, replay, freeze


def worker():
    while True:
        now = time.time()
        sample = engine.sample(now)
        with state_lock:
            score, sec_state, reasons, residual, d, nmea_ok, position_ok, replay, freeze = detector(sample, now)
            engine.last_sample = sample
            state["state"] = sec_state
            state["integrity"] = score
            state["mode"] = engine.mode
            state["gnss_time"] = int(sample.epoch) if sample.valid else 0
            state["rtc_time"] = int(now)
            state["time_residual_ms"] = residual
            state["distance_from_reference_m"] = round(d, 2)
            state["packet_age_ms"] = 0 if sample.valid else 999999
            state["relay"] = "HOLDOVER" if sec_state != "TRUSTED" else "GNSS"
            state["holdover"] = sec_state != "TRUSTED"
            state["reason"] = reasons[0] if reasons else "NONE"
            state["nmea_ok"] = nmea_ok
            state["freshness_ok"] = sample.valid
            state["rtc_ok"] = abs(residual) <= 100
            state["position_ok"] = position_ok
            state["replay_ok"] = not replay
            state["freeze_ok"] = not freeze
            state["incoming_nmea"] = sample.nmea
            state["outgoing_nmea"] = (rmc := sample.nmea) if sec_state == "TRUSTED" else rmc_from_holdover(now, engine.base_lat, engine.base_lon)
            state["history"].append({"t": time.strftime("%H:%M:%S"), "integrity": score, "drift": residual})
            state["history"] = state["history"][-60:]
            if reasons:
                event = {"time": time.strftime("%H:%M:%S"), "state": sec_state, "mode": engine.mode, "reasons": reasons}
                if not state["events"] or state["events"][-1]["reasons"] != reasons:
                    state["events"].append(event)
                    state["events"] = state["events"][-20:]
        time.sleep(1)


def rmc_from_holdover(epoch, lat, lon):
    from nmea import rmc
    return rmc(int(epoch), lat, lon, 0.0, False) + ""


class Handler(BaseHTTPRequestHandler):
    def send_json(self, payload):
        body = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/":
            body = DASHBOARD.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(body)
        elif parsed.path == "/telemetry":
            with state_lock:
                self.send_json(state)
        elif parsed.path == "/set_mode":
            mode = parse_qs(parsed.query).get("mode", ["normal"])[0]
            engine.set_mode(mode)
            engine.repeat_count = 0
            engine.last_sample = None
            self.send_json({"ok": True, "mode": mode})
        elif parsed.path == "/reset":
            engine.set_mode("normal")
            self.send_json({"ok": True})
        else:
            self.send_error(404)

    def log_message(self, *_):
        return


if __name__ == "__main__":
    threading.Thread(target=worker, daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", 8080), Handler)
    print("Chronos dashboard: http://127.0.0.1:8080")
    server.serve_forever()
