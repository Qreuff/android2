import json
import random
from datetime import datetime, timedelta

route_coords = [
    (55.030199, 82.920430),
    (55.035500, 82.896600),
    (55.041500, 82.920100),
    (55.058600, 82.877900),
    (55.030400, 82.919800),
    (55.016400, 82.943300),
    (55.014200, 82.934300),
    (54.848300, 83.106600),
]

def interpolate_coords(coord1, coord2, t):
    lat = coord1[0] + (coord2[0] - coord1[0]) * t
    lon = coord1[1] + (coord2[1] - coord1[1]) * t
    return lat, lon

def generate_signal_strength(quality="good"):
    if quality == "good":
        rsrp = random.randint(-80, -60)
        rsrq = random.randint(-10, -3)
        rssi = random.randint(-70, -50)
    elif quality == "medium":
        rsrp = random.randint(-100, -81)
        rsrq = random.randint(-15, -11)
        rssi = random.randint(-90, -71)
    else:
        rsrp = random.randint(-120, -101)
        rsrq = random.randint(-20, -16)
        rssi = random.randint(-110, -91)
    return rsrp, rsrq, rssi

data = []
start_time = datetime(2024, 3, 25, 10, 0, 0)
base_ip = "10.0.2.15"

for i in range(20000):
    segment = (i // 200) % (len(route_coords) - 1)
    t = (i % 200) / 200.0
    
    lat, lon = interpolate_coords(route_coords[segment], route_coords[segment + 1], t)
    
    lat += random.uniform(-0.0005, 0.0005)
    lon += random.uniform(-0.0005, 0.0005)
    
    if segment < 2:
        quality = "good"
    elif segment < 4:
        quality = "medium"
    else:
        quality = "poor"
    
    rsrp, rsrq, rssi = generate_signal_strength(quality)
    time_offset = timedelta(seconds=random.randint(2, 5))
    current_time = start_time + time_offset * i
    
    entry = {
        "altitude": random.uniform(140, 160),
        "device_id": "sdk_gphone64_arm64",
        "frequency": random.choice([1800, 2600, 3500]),
        "ip_address": base_ip,
        "latitude": round(lat, 6),
        "longitude": round(lon, 6),
        "network_type": random.choice(["LTE", "5G", "4G"]),
        "provider": "gps",
        "rsrp": rsrp,
        "rsrq": rsrq,
        "rssi": rssi,
        "time": int(current_time.timestamp() * 1000)
    }
    data.append(entry)

with open("location.json", "w", encoding="utf-8") as f:
    for entry in data:
        f.write(json.dumps(entry, ensure_ascii=False) + "\n")

print(f"Сгенерировано {len(data)} записей")