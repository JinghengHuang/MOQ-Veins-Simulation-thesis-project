#!/usr/bin/env python3
"""
Aggregate the protocol comparison over seed-sets: mean +/- 95% CI per config and track.

Metrics (all measured identically for MoQ and MQTT via SubscriberStats.h):
  latency   - mean end-to-end latency, subscriber-side
  miss      - deadline-miss ratio
  delivered - objects received / objects the publisher offered. Subscriber cars spawn over the
              first 35 s and cannot receive what was published earlier, so ~62% is the ceiling.
  goodput   - subscriber-side throughput, i.e. payload bits actually delivered per second

Per seed, a metric is first averaged over the subscriber cars, then across seeds we report the
mean and the half-width of the 95% CI (t-distribution, n-1 dof).

Usage: aggregate_seeds.py <result-dir>
"""
import glob
import math
import os
import re
import sys
from collections import defaultdict

# t(0.975, dof) for small samples; index by degrees of freedom.
T95 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365,
       8: 2.306, 9: 2.262, 10: 2.228}

ORDER = ["MOQ_Partial_BDP", "MOQ_SW_BDP", "MOQ_QUIC", "MQTT_QUIC",
         "MOQ_UDP", "MOQ_TCP", "MQTT_TCP"]


def parse(path):
    scalars = defaultdict(dict)
    with open(path) as fh:
        for line in fh:
            m = re.match(r"scalar (\S+) (\S+) (\S+)", line)
            if not m:
                continue
            try:
                scalars[m.group(1)][m.group(2)] = float(m.group(3))
            except ValueError:
                pass
    return scalars


def per_seed(path):
    """One seed -> {track: {metric: value}}, averaged over subscribers."""
    sc = parse(path)
    pubs = {m: v for m, v in sc.items()
            if any(k.startswith("track[") and k.endswith(".objectsOffered") for k in v)}
    subs = {m: v for m, v in sc.items() if any(k.endswith(".objectsReceived") for k in v)}

    tracks = {re.match(r"track\[(.+?)\]\.objectsOffered", k).group(1)
              for v in pubs.values() for k in v
              if re.match(r"track\[(.+?)\]\.objectsOffered", k)}

    out = {}
    for track in tracks:
        offered = sum(v.get(f"track[{track}].objectsOffered", 0) for v in pubs.values())
        if not offered:
            continue
        recv, lat, miss, gp = [], [], [], []
        for v in subs.values():
            got = v.get(f"track[{track}].objectsReceived")
            if got is None:
                continue
            recv.append(got)
            if got > 0:
                for key, acc in ((".meanLatency", lat), (".deadlineMissRatio", miss),
                                 (".throughput", gp)):
                    if f"track[{track}]{key}" in v:
                        acc.append(v[f"track[{track}]{key}"])
        if not recv:
            continue
        mean = lambda xs: sum(xs) / len(xs) if xs else float("nan")
        out[track.split("/")[-1]] = {
            "latency": mean(lat),
            "miss": mean(miss),
            "delivered": (sum(recv) / len(recv)) / offered,
            "goodput": mean(gp),
        }
    return out


def ci95(xs):
    xs = [x for x in xs if x == x]  # drop NaN
    n = len(xs)
    if n == 0:
        return float("nan"), float("nan")
    mu = sum(xs) / n
    if n < 2:
        return mu, float("nan")
    sd = math.sqrt(sum((x - mu) ** 2 for x in xs) / (n - 1))
    return mu, T95.get(n - 1, 1.96) * sd / math.sqrt(n)


root = sys.argv[1] if len(sys.argv) > 1 else "results"
data = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))  # cfg -> track -> metric -> [vals]
seeds = set()

for seed_dir in sorted(glob.glob(os.path.join(root, "seed*"))):
    seeds.add(os.path.basename(seed_dir))
    for sca in glob.glob(os.path.join(seed_dir, "*.sca")):
        cfg = os.path.basename(sca)[:-4]
        for track, metrics in per_seed(sca).items():
            for k, v in metrics.items():
                data[cfg][track][k].append(v)

print(f"seed dirs: {len(seeds)}")
print("n = completed runs contributing to each row. Runs that aborted early are excluded by")
print("run-comparison.sh (renamed .sca.aborted) and must be reported, not silently dropped.\n")
for track in ("BBox", "PCloud"):
    print(f"===== {track} =====")
    print(f"{'config':>17} | {'latency (ms)':>18} | {'deadline miss':>17} | "
          f"{'delivered':>15} | {'goodput (kbps)':>17} | n")
    keys = sorted(data, key=lambda c: ORDER.index(c) if c in ORDER else 99)
    for cfg in keys:
        if track not in data[cfg]:
            continue
        m = data[cfg][track]
        lat, lat_ci = ci95(m["latency"])
        miss, miss_ci = ci95(m["miss"])
        dev, dev_ci = ci95(m["delivered"])
        gp, gp_ci = ci95(m["goodput"])
        n = len(m["latency"])
        print(f"{cfg:>17} | {lat*1000:>8.0f} +/- {lat_ci*1000:<6.0f} | "
              f"{miss*100:>6.1f}% +/- {miss_ci*100:<5.1f} | "
              f"{dev*100:>5.1f}% +/- {dev_ci*100:<4.1f} | "
              f"{gp/1000:>8.1f} +/- {gp_ci/1000:<5.1f} | {n}")
    print()
