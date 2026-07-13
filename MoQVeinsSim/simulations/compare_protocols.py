#!/usr/bin/env python3
"""
Cross-protocol comparison table (RQ2).

Delivery is measured against the publisher's offered count, not the subscriber's own loss
scalars -- see analyze_sweep.py for why those cannot be trusted. Subscriber cars spawn over the
first 35 s, so they cannot receive what was published before they existed: ~62% is the delivery
ceiling, not 100%.

Latency and deadline-miss are averaged over the subscribers that received anything.
"""
import glob
import os
import re
import sys
from collections import defaultdict

DIRS = sys.argv[1:] or ["results"]


def load(path):
    txt = open(path).read()
    cfg = re.search(r"attr configname (\S+)", txt)
    scalars = defaultdict(dict)
    for mod, name, val in re.findall(r"^scalar (\S+) (\S+) (\S+)", txt, re.M):
        try:
            scalars[mod][name] = float(val)
        except ValueError:
            pass
    return (cfg.group(1) if cfg else os.path.basename(path)), scalars


def summarise(path):
    cfg, sc = load(path)
    pubs = {m: v for m, v in sc.items()
            if any(k.startswith("track[") and k.endswith(".objectsOffered") for k in v)}
    subs = {m: v for m, v in sc.items() if any(k.endswith(".objectsReceived") for k in v)}

    tracks = sorted({re.match(r"track\[(.+?)\]\.objectsOffered", k).group(1)
                     for v in pubs.values() for k in v
                     if re.match(r"track\[(.+?)\]\.objectsOffered", k)})

    rows = []
    for track in tracks:
        offered = sum(v.get(f"track[{track}].objectsOffered", 0) for v in pubs.values())
        if not offered:
            continue
        recv, lat, miss = [], [], []
        for v in subs.values():
            got = v.get(f"track[{track}].objectsReceived")
            if got is None:
                continue
            recv.append(got)
            if got > 0:
                if f"track[{track}].meanLatency" in v:
                    lat.append(v[f"track[{track}].meanLatency"])
                if f"track[{track}].deadlineMissRatio" in v:
                    miss.append(v[f"track[{track}].deadlineMissRatio"])
        if not recv:
            continue
        rows.append({
            "track": track.split("/")[-1],
            "offered": offered,
            "delivered": (sum(recv) / len(recv)) / offered,
            "latency": sum(lat) / len(lat) if lat else float("nan"),
            "miss": sum(miss) / len(miss) if miss else float("nan"),
        })
    return cfg, rows


results = {}
for d in DIRS:
    for f in glob.glob(os.path.join(d, "*.sca")):
        cfg, rows = summarise(f)
        if rows:
            results[cfg] = rows

ORDER = ["MOQ", "MOQ_SW", "MOQ_Partial", "MOQ_TCP", "MOQ_UDP", "MQTT_TCP", "MQTT_QUIC"]
keys = sorted(results, key=lambda c: ORDER.index(c) if c in ORDER else 99)

for track in ("BBox", "PCloud"):
    print(f"\n===== {track} =====")
    print(f"{'config':>14} | {'offered':>7} {'delivered':>9} | {'mean latency':>13} {'deadline miss':>13}")
    for cfg in keys:
        for r in results[cfg]:
            if r["track"] != track:
                continue
            lat = f"{r['latency']*1000:>10.0f}ms" if r["latency"] == r["latency"] else f"{'n/a':>12}"
            print(f"{cfg:>14} | {r['offered']:>7.0f} {r['delivered']*100:>8.1f}% "
                  f"| {lat:>13} {r['miss']*100:>12.1f}%")
