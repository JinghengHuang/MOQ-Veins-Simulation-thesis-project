#!/usr/bin/env python3
"""
Per-track delivery analysis measured against the publisher's OFFERED count.

Why not use the subscriber's own lossRatio / gapLossRatio scalars:

  * lossRatio counts from object 0, so every object published before a (SUMO-spawned)
    subscriber joined counts as lost. It overstates loss badly.
  * gapLossRatio only counts objects missing BETWEEN the first and last object a subscriber
    actually received. A track that is shed almost in its entirety therefore has no internal
    gaps and scores a perfect 0% loss -- which is exactly backwards, and it silently hid the
    fact that PCloud was being annihilated in the small-window sweep runs.

The only honest denominator is what the publisher offered while the subscriber was subscribed.
We approximate it with the publisher's per-track objectsOffered, and report delivery ratio
(received / offered) alongside the shed/sent breakdown, so a track that is dropped wholesale
can never masquerade as lossless.
"""
import glob
import os
import re
import sys
from collections import defaultdict

SCA_DIR = sys.argv[1] if len(sys.argv) > 1 else "results"


def parse(path):
    txt = open(path).read()
    cfg = re.search(r"attr configname (\S+)", txt)
    win = re.search(r"itervar win (\S+)", txt)
    scalars = defaultdict(dict)
    for mod, name, val in re.findall(r"^scalar (\S+) (\S+) (\S+)", txt, re.M):
        try:
            scalars[mod][name] = float(val)
        except ValueError:
            pass
    return (cfg.group(1) if cfg else "?", win.group(1) if win else "-", scalars)


def track_names(scalars):
    names = set()
    for mod, vals in scalars.items():
        for k in vals:
            m = re.match(r"track\[(.+?)\]\.objectsOffered$", k)
            if m:
                names.add(m.group(1))
    return sorted(names)


def analyse(path):
    cfg, win, sc = parse(path)
    pubs = {m: v for m, v in sc.items() if any(k.startswith("track[") and k.endswith(".objectsOffered") for k in v)}
    subs = {m: v for m, v in sc.items() if any(k.endswith(".objectsReceived") for k in v)}
    relay = next((v for m, v in sc.items() if "server.app" in m), {})

    out = []
    for track in track_names(sc):
        offered = sum(v.get(f"track[{track}].objectsOffered", 0) for v in pubs.values())
        sent = sum(v.get(f"track[{track}].objectsSent", 0) for v in pubs.values())
        pub_shed = sum(v.get(f"track[{track}].objectsShedStale", 0) for v in pubs.values())
        relay_shed = relay.get(f"track[{track}].objectsShedStale", 0)

        # Per-subscriber delivery, averaged over subscribers that actually subscribed.
        recv, lat, miss, n = [], [], [], 0
        for v in subs.values():
            if f"track[{track}].objectsReceived" not in v:
                continue
            n += 1
            recv.append(v[f"track[{track}].objectsReceived"])
            if f"track[{track}].meanLatency" in v:
                lat.append(v[f"track[{track}].meanLatency"])
            if f"track[{track}].deadlineMissRatio" in v:
                miss.append(v[f"track[{track}].deadlineMissRatio"])
        if not n or not offered:
            continue
        mean_recv = sum(recv) / n
        # Delivery ratio against what the publisher offered: the metric that cannot be gamed
        # by a track being dropped wholesale.
        delivery = mean_recv / offered
        out.append({
            "track": track, "offered": offered, "sent": sent,
            "pub_shed": pub_shed, "relay_shed": relay_shed,
            "recv": mean_recv, "delivery": delivery,
            "lat": sum(lat) / len(lat) if lat else float("nan"),
            "miss": sum(miss) / len(miss) if miss else float("nan"),
        })
    return cfg, win, out


WIN_ORDER = ["2MB", "1MB", "512kB", "256kB", "128kB", "64kB", "-"]

results = defaultdict(list)
for f in glob.glob(os.path.join(SCA_DIR, "*.sca")):
    cfg, win, rows = analyse(f)
    if rows:
        results[cfg].append((win, rows))

for cfg in sorted(results):
    print(f"\n===== {cfg} =====")
    print(f"{'window':>7} {'track':>8} | {'offered':>7} {'sent':>5} {'pubShed':>7} {'relayShed':>9} "
          f"| {'recv/sub':>8} {'DELIVERED':>9} | {'latency':>8} {'miss':>6}")
    for win, rows in sorted(results[cfg], key=lambda r: WIN_ORDER.index(r[0]) if r[0] in WIN_ORDER else 99):
        for r in rows:
            short = r["track"].split("/")[-1]
            print(f"{win:>7} {short:>8} | {r['offered']:>7.0f} {r['sent']:>5.0f} {r['pub_shed']:>7.0f} "
                  f"{r['relay_shed']:>9.0f} | {r['recv']:>8.1f} {r['delivery']*100:>8.1f}% "
                  f"| {r['lat']*1000:>6.0f}ms {r['miss']*100:>5.1f}%")
