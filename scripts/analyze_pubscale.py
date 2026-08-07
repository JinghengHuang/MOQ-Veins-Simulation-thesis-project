#!/usr/bin/env python3
"""Aggregate the publisher-count sweep (results/pubscale).

For each config (PubScale_N{1..4}[_Prio]) we pool every BBox / PCloud track instance
(4 subscribers x N publishers) within a seed into a seed-level mean, then report
mean +/- 95% CI across the 5 seeds -- seeds are the independent replications.

Primary metric: on-time delivery ratio = (received - deadlineMisses) / expected.
deadlineMissRatio (= misses / received) is reported too, but on its own it flatters a
run that delivered almost nothing, so on-time-ratio is the headline.
"""
import glob, re, math, sys
from collections import defaultdict

ROOT = sys.argv[1] if len(sys.argv) > 1 else "MoQVeinsSim/simulations/results/pubscale"

# t_0.975 for small dof (n-1): index by n
TCRIT = {2: 12.706, 3: 4.303, 4: 3.182, 5: 2.776, 6: 2.571}

def parse_sca(path):
    """Return {(module,pub,track): {metric: value}} for one .sca.

    Keyed by the RECORDING module too: every subscriber records the same
    track[car[0]/BBox] namespace, so keying by (pub,track) alone would collide all
    subscribers onto one entry (only the last-written survives)."""
    d = defaultdict(dict)
    for line in open(path):
        m = re.match(r'scalar (\S+) track\[(car\[\d+\])/(\w+)\]\.(\w+) ([\d.eE+-]+)', line)
        if m:
            mod, pub, track, metric, v = m.groups()
            d[(mod, pub, track)][metric] = float(v)
    return d

def seed_means(sca):
    """Per-seed mean of each metric over all subscriber instances of BBox and PCloud."""
    d = parse_sca(sca)
    acc = {t: defaultdict(list) for t in ("BBox", "PCloud")}
    for (mod, pub, track), mv in d.items():
        if track not in acc:
            continue
        # subscriber instances only (they alone record objectsExpected)
        if mv.get("objectsExpected", 0.0) <= 0:
            continue
        rec = mv.get("objectsReceived", 0.0)
        exp = mv.get("objectsExpected", 0.0)
        miss = mv.get("deadlineMisses", 0.0)
        if exp > 0:
            acc[track]["onTimeRatio"].append((rec - miss) / exp)
            acc[track]["deliveredRatio"].append(rec / exp)
        if "deadlineMissRatio" in mv:
            acc[track]["deadlineMissRatio"].append(mv["deadlineMissRatio"])
        if "meanLatency" in mv:
            acc[track]["meanLatency"].append(mv["meanLatency"])
        if "maxLatency" in mv:
            acc[track]["maxLatency"].append(mv["maxLatency"])
    out = {}
    for track, md in acc.items():
        out[track] = {k: (sum(v) / len(v) if v else float("nan")) for k, v in md.items()}
    return out

def mean_ci(xs):
    xs = [x for x in xs if not math.isnan(x)]
    n = len(xs)
    if n == 0:
        return (float("nan"), float("nan"))
    mu = sum(xs) / n
    if n == 1:
        return (mu, 0.0)
    sd = math.sqrt(sum((x - mu) ** 2 for x in xs) / (n - 1))
    ci = TCRIT.get(n, 2.776) * sd / math.sqrt(n)
    return (mu, ci)

def collect(cfg):
    per_seed = defaultdict(lambda: defaultdict(list))  # track -> metric -> [per-seed means]
    scas = sorted(glob.glob(f"{ROOT}/{cfg}/seed*/{cfg}.sca"))
    for sca in scas:
        sm = seed_means(sca)
        for track, md in sm.items():
            for k, v in md.items():
                per_seed[track][k].append(v)
    return per_seed, len(scas)

CONFIGS = [("PubScale_N1", 1, "RR"), ("PubScale_N2", 2, "RR"),
           ("PubScale_N3", 3, "RR"), ("PubScale_N4", 4, "RR"),
           ("PubScale_N1_Prio", 1, "Prio"), ("PubScale_N2_Prio", 2, "Prio"),
           ("PubScale_N3_Prio", 3, "Prio"), ("PubScale_N4_Prio", 4, "Prio")]

def fmt(mc):
    mu, ci = mc
    if math.isnan(mu):
        return "    n/a    "
    return f"{mu:5.3f}±{ci:4.3f}"

print(f"root: {ROOT}\n")
hdr = f"{'sched':5} {'N':>2} {'seeds':>5} | {'BBox onTime':>12} {'BBox miss':>11} {'BBox meanLat':>13} {'BBox maxLat':>12} | {'PCloud onTime':>13} {'PCloud meanLat':>14}"
print(hdr); print("-" * len(hdr))
last_sched = None
for cfg, N, sched in CONFIGS:
    ps, nseeds = collect(cfg)
    if sched != last_sched and last_sched is not None:
        print()
    last_sched = sched
    b, p = ps.get("BBox", {}), ps.get("PCloud", {})
    print(f"{sched:5} {N:>2} {nseeds:>5} | "
          f"{fmt(mean_ci(b.get('onTimeRatio', []))):>12} "
          f"{fmt(mean_ci(b.get('deadlineMissRatio', []))):>11} "
          f"{fmt(mean_ci(b.get('meanLatency', []))):>13} "
          f"{fmt(mean_ci(b.get('maxLatency', []))):>12} | "
          f"{fmt(mean_ci(p.get('onTimeRatio', []))):>13} "
          f"{fmt(mean_ci(p.get('meanLatency', []))):>14}")
