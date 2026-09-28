import math
from datetime import datetime, timezone


def checksum(payload: str) -> str:
    value = 0
    for c in payload:
        value ^= ord(c)
    return f"{value:02X}"


def wrap(payload: str) -> str:
    return f"${payload}*{checksum(payload)}"


def rmc(epoch: int, lat: float, lon: float, speed_kmh: float = 0.0, valid: bool = True) -> str:
    dt = datetime.fromtimestamp(epoch, tz=timezone.utc)
    time_str = dt.strftime("%H%M%S") + ".00"
    date_str = dt.strftime("%d%m%y")
    lat_abs = abs(lat)
    lon_abs = abs(lon)
    lat_deg = int(lat_abs)
    lon_deg = int(lon_abs)
    lat_min = (lat_abs - lat_deg) * 60.0
    lon_min = (lon_abs - lon_deg) * 60.0
    lat_field = f"{lat_deg:02d}{lat_min:07.4f}"
    lon_field = f"{lon_deg:03d}{lon_min:07.4f}"
    lat_hemi = "N" if lat >= 0 else "S"
    lon_hemi = "E" if lon >= 0 else "W"
    knots = speed_kmh / 1.852
    payload = (
        f"GPRMC,{time_str},{'A' if valid else 'V'},{lat_field},{lat_hemi},"
        f"{lon_field},{lon_hemi},{knots:.1f},000.0,{date_str},,,N"
    )
    return wrap(payload)
