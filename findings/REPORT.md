# MoQ for V2X over 5G: consolidated report

Answers to RQ1–RQ3, with the evidence base and its limits.

**Setup.** Urban 3×3 SUMO grid, 8 vehicles, 5G NR (Simu5G) with TR 38.901 propagation
(LOS/NLOS path loss, log-normal shadowing, Jakes fading), two gNodeBs with one handover mid-run.
One publisher car, seven subscriber cars, relay/broker on the edge server. 200 s runs.

**Workload.** Two tracks, both from the publisher car:
- `BBox` — 50 B every 100 ms, deadline **100 ms** (safety-critical, e.g. collision warning).
- `PCloud` — one LiDAR sweep per 200 ms, sent as **8 × 37.5 KB segments**, deadline **500 ms**
  (bulk perception). The segment size follows EMP's measured 30–38 KB uploads and ETSI TS 103 324's
  "independently interpretable" CPM segments — see `pointcloud-segmentation.md`.
- Offered load ≈ 12 Mbps, which saturates the uplink. Congestion is the point of the experiment.

**Statistics.** Every figure below is the **mean of 5 seeds ± the 95% CI half-width**
(t-distribution). Latency/miss/goodput are averaged over subscriber cars, then across seeds.
Reproduce with `scripts/run-comparison.sh` + `scripts/aggregate_seeds.py`.

**Validity gate.** All 35 runs report `quicSendRejected = 0`. This is not a formality: two earlier
versions of this comparison were wrong because INET's QUIC *silently discards* writes once its send
queue is full, and both MQTT and MoQ were at different times "winning" by throwing away most of the
offered load (see §5).

**Delivery ceiling.** Subscriber cars spawn over the first 35 s and cannot receive objects published
before they existed. **~62% is full delivery, not 100%.**

---

## RQ1 — How can we design a system that integrates MoQ with V2X?

**Answered.** A working publisher → edge relay → subscriber MoQ system over 5G NR, integrated with
SUMO/Veins mobility. The MoQ side is faithful to draft-ietf-moq-transport-14 on the mechanisms that
matter under congestion:

- **Object model**: tracks → groups → subgroups, with a subgroup mapped onto exactly one QUIC
  stream (§2.2), and `objectsPerGroup` as the configurable group size.
- **Delivery timeout** (§9.2.1.2) applied both to objects still buffered *and* to objects already
  written to QUIC, enforced by **`RESET_STREAM`** (§10.4.3).
- **Priority** (§7.2) carried down into QUIC's stream scheduler, not merely honoured in the app.

Three design lessons came out of building it, each measured rather than assumed:

1. **The transport queue must be sized near the bandwidth-delay product.** Everything else is
   secondary (see RQ3).
2. **Subgroup size is a loss-amplification factor.** A reset abandons *the stream*, so batching a
   protected track's objects into a shared subgroup multiplies its loss by the group size — with
   `objectsPerGroup = 10`, 291 of 411 BBox objects were destroyed as collateral of 40 resets.
   A track you intend to protect must not share a subgroup with objects you are willing to abandon.
3. **An application must not infer transport backpressure.** Our publisher deadlocked itself by
   deducing "QUIC is full" from its own occupancy estimate, which only QUIC's drain signal could
   clear — and that signal never came.

Building this required fixing **three genuine upstream bugs** (§6). Any of them would have silently
invalidated results.

## RQ2 — Does MoQ outperform MQTT (over QUIC and TCP), and plain TCP/UDP, in throughput and latency?

**Answered, with an important nuance: MoQ wins decisively on latency, but *not* on throughput.**

### BBox (safety-critical, 100 ms deadline)

| config | latency (ms) | deadline miss | delivered | goodput (kbps) |
|---|---|---|---|---|
| **MoQ partial, 128 kB window** | **33 ± 3** | **0.2% ± 0.4** | **62.2% ± 0.1** | 4.1 ± 0.0 |
| MoQ reliable, 128 kB window | 39 ± 8 | 0.8% ± 1.5 | 58.6% ± 7.8 | 4.1 ± 0.1 |
| MoQ / QUIC (default window) | 1246 ± 228 | 79.9% ± 2.5 | 62.0% ± 0.4 | 3.8 ± 0.1 |
| MQTT / QUIC | 2918 ± 127 | 90.0% ± 0.9 | 51.6% ± 0.4 | 3.2 ± 0.1 |
| MoQ / UDP | 1421 ± 371 | 93.2% ± 1.1 | 50.7% ± 1.1 | 3.1 ± 0.1 |
| MoQ / TCP | 22 289 ± 301 | 100% ± 0.0 | 12.4% ± 0.2 | 0.9 ± 0.0 |
| MQTT / TCP | 26 137 ± 96 | 100% ± 0.0 | 11.4% ± 0.3 | 0.9 ± 0.0 |

### PCloud (bulk, 500 ms deadline)

| config | latency (ms) | deadline miss | delivered | goodput (Mbps) |
|---|---|---|---|---|
| MoQ partial, 128 kB window | **951 ± 4** | 99.6% ± 0.0 | 21.6% ± 1.0 | 4.21 ± 0.21 |
| MoQ reliable, 128 kB window | **13 804 ± 1331** | 99.7% ± 0.1 | 27.2% ± 2.7 | 5.65 ± 0.38 |
| MoQ / QUIC (default window) | 3291 ± 247 | 72.7% ± 2.9 | 55.7% ± 1.9 | **9.44 ± 0.16** |
| MQTT / QUIC | 2878 ± 131 | 65.8% ± 4.9 | 51.5% ± 0.4 | **9.66 ± 0.27** |
| MoQ / UDP | 1208 ± 508 | 55.1% ± 7.5 | 43.1% ± 0.8 | 7.88 ± 0.29 |
| MoQ / TCP | 22 254 ± 123 | 99.9% ± 0.0 | 12.5% ± 0.4 | 2.48 ± 0.07 |
| MQTT / TCP | 26 081 ± 114 | 100% ± 0.0 | 11.3% ± 0.4 | 2.49 ± 0.06 |

### What the numbers say

**Latency: MoQ wins, decisively.** Only MoQ over QUIC with a bounded window meets the 100 ms
safety deadline (33 ms, 0.2% miss). It is **88× faster than MQTT/QUIC**, **43× faster than
MoQ/UDP**, and **~790× faster than either TCP variant** on the safety track. The CIs are far apart;
this is not a marginal result.

**Throughput: MoQ does *not* beat MQTT.** On bulk goodput at a default window, MoQ/QUIC achieves
9.44 ± 0.16 Mbps against MQTT/QUIC's 9.66 ± 0.27 Mbps — MQTT is, if anything, *marginally higher*.
**MoQ's advantage is latency, not capacity.** Worse, the bounded window that buys the deadline
**halves bulk goodput** (9.44 → 4.21 Mbps). MoQ trades throughput for timeliness; it does not
provide both.

**The gap decomposes into three layers**, all mechanism-attributable rather than "MoQ is faster":

- **TCP is catastrophic** (22–26 s, 100% miss, ~12% delivered, ~2.5 Mbps). A single ordered byte
  stream means a 37.5 KB PCloud segment head-of-line-blocks a 50 B BBox message, and no application
  protocol can fix that from above. This dominates every other effect.
- **QUIC alone buys ~8–18×** (26.1 s → 2.9 s for MQTT; 22.3 s → 1.2 s for MoQ) by removing
  cross-stream head-of-line blocking. Still 12–29× short of the deadline.
- **The bounded window buys the remaining ~38×** (1246 ms → 33 ms).

**Why MQTT cannot close the gap — three named mechanisms** (all verified against the MQTT v5.0
spec, see `mqtt-vs-moq.md`):
1. **No per-message priority.** Both topics share one connection; BBox queues behind PCloud.
2. **Message Expiry Interval is in whole seconds** (§3.3.2.3.3), so a 100 ms deadline is not even
   expressible — and it only applies *before onward delivery starts* ([MQTT-3.3.2-5], with the
   clock counting time **in the server** per [MQTT-3.3.2-6]). Our broker forwards on receipt, so
   `objectsExpired = 0` in every run: **MQTT's only discard mechanism is inert**, because the
   backlog lives in the transport where MQTT cannot reach it.
3. **No `RESET_STREAM` equivalent.** Once delivery starts, the message must complete however stale.
   MQTT converts congestion into *unbounded latency*; MoQ converts it into *chosen loss*.

**What MQTT is good at, and worth saying:** over QUIC it delivered ~51% of both tracks with no
protocol-level loss and marginally the best bulk goodput. It is late, not lossy. For telemetry with
no deadline and a hard no-loss requirement, MQTT's unbounded buffering is a **feature**.

## RQ3 — Which V2X use cases are boosted by MoQ?

**Answered: those with a small, high-rate, latency-critical stream sharing a congested uplink with
bulk traffic — and only those.**

MoQ delivers the safety track at **33 ms with 0.2% deadline miss and full delivery**, while the
bulk track is **sacrificed** (21.6% delivered, and it misses its own 500 ms deadline 99.6% of the
time). This is the right trade for **collision warning / cooperative awareness**, and the wrong one
for **HD-map upload or bulk perception offload**, where MoQ's benefit is negative — a workload with
no deadline and a no-loss requirement should not use this configuration at all.

Two boundary conditions matter, and both are measured:

**(a) The bounded window is what buys the deadline — not partial reliability.** The fully reliable
MoQ baseline at the same 128 kB window reaches 39 ± 8 ms / 0.8% ± 1.5 miss, **statistically
indistinguishable** from partial reliability's 33 ± 3 ms / 0.2% ± 0.4 (the CIs overlap). Layering
delivery-timeout shedding on a *deep* buffer achieves nothing: at 2 MB the miss rate is ~80% even
with shedding enabled. This corrects an earlier claim of ours and is the single most important
design finding.

**(b) What partial reliability actually buys is a bounded bulk track.** With a shallow window the
reliable baseline cannot drop anything, so the bulk backlog simply queues: PCloud arrives
**13.8 s ± 1.3 s** late. Shedding holds it at **951 ms ± 4 ms** — a **14× reduction** — at the cost
of delivering less of it (21.6% vs 27.2%). Both miss PCloud's deadline, so neither is *useful* for
bulk data at this window; but 1 s of bounded staleness and a 14 s unbounded backlog are different
failure modes. Acting on 14-second-old perception data is arguably worse than having none.

**So: the window protects the latency-critical track; the delivery timeout bounds staleness under
overload.** These are two distinct contributions and should be reported as such.

## 5. Threats to validity

- **One scenario.** All numbers are from the urban grid. A highway scenario (3 km corridor,
  120 km/h, one handover) and a multi-publisher load-scaling config are both implemented but were
  **not run** in this comparison. Generalisation beyond the urban case is unsupported.
- **One workload shape.** One publisher, two tracks. The RQ3 claim rests on the BBox/PCloud split
  being representative of the "small critical + bulk" pattern.
- **The window-sweep transition region is noisy.** Between 512 kB and 1 MB the ordering is not
  monotonic and the reliable baseline sometimes beats partial reliability. The endpoints differ by
  an order of magnitude and are not in doubt, but no claim should be made about the transition.
- **Simulation, not deployment.** Idealised 10 G backhaul (so the radio is the only bottleneck, by
  design), zero relay processing delay, perfect clock sync, zero-filled payloads with no codec and
  no inter-object dependency. Loss is a transport-level ratio, not a perceived-quality claim.
- **MQTT-over-QUIC is not standardised.** We carry the MQTT byte stream over a single QUIC stream
  in place of TCP — the accepted approach — so MQTT gains QUIC's loss recovery but still cannot
  exploit independent streams. That limitation is inherent to MQTT's framing, not to our code.

## 6. Upstream bugs found and fixed

Each of these silently corrupts results, and each was found by chasing an implausible number rather
than by anything failing loudly.

| where | bug | effect |
|---|---|---|
| **Simu5G** | `NrChannelModel_3GPP38_901::computeRuralMacro` fed carrier frequency in **Hz** into a TR 38.901 term specified in **GHz** | constant **+180 dB** of phantom path loss; `RURAL_MACROCELL` produced 252–281 dB and no link survived. Cross-checked against ns-3's reference implementation |
| **INET QUIC** | `RESET_STREAM` released stream-level but not **connection-level** flow-control credit for abandoned bytes (RFC 9000 §4.5) | connection window leaked shut on every reset, deadlocking any reset-heavy run |
| **INET QUIC** | send-side flow control was a running byte counter that **decremented on loss** and never re-charged retransmissions (RFC 9000 §4.1 charges the *highest offset sent*) | sender gradually credited itself window it had already spent, overran the peer's limit, and the connection aborted with `FLOW_CONTROL_ERROR` at small windows |

Plus two of our own: the publisher backpressure deadlock, and a per-call (rather than per-event)
byte tally that let a group burst overshoot QUIC's send-queue limit, causing **3468 silently
discarded writes**.

**Recommendation for any future work on this codebase: gate every run on `quicSendRejected = 0`.**
It is the single check that distinguishes a real result from a protocol that merely looks fast
because it discarded the load.

## 7. Suggested next steps

1. **Run the highway scenario and the multi-publisher scaling config.** Both exist; neither has
   been measured. This is the cheapest way to strengthen external validity.
2. **Sweep the window with seeds** to place the BDP operating point with a CI, rather than
   asserting 128 kB from a single-seed sweep.
3. **Consider reporting the reliable bounded-window config as the headline**, with partial
   reliability as the mechanism that bounds bulk staleness. That is what the data supports, and it
   is a more defensible story than "MoQ's shedding wins".
