# MoQ for V2X over 5G: consolidated report

Answers to RQ1–RQ3, with the evidence base and its limits.

---

## Setup

**Two scenarios**, identical workload, 5G NR (Simu5G) with TR 38.901 propagation (LOS/NLOS path
loss, log-normal shadowing, Jakes fading), one publisher car, seven subscriber cars, relay/broker
on the edge server, 200 s runs.

| | **Urban** | **Highway** |
|---|---|---|
| road | 3×3 SUMO grid, 200 m edges, 50 km/h | straight 3 km corridor, 3 lanes, 120 km/h |
| vehicles | 8, departing 0–35 s | 8, departing 0–14 s (platoon) |
| gNodeBs | (156, 136) and (391, 313) m | x = 750 m and x = 2250 m, 100 m off the road |
| handover | none in practice | exactly one, mid-corridor |
| propagation | `URBAN_MACROCELL` | `RURAL_MACROCELL` |
| publisher lifetime | ~45 s | ~90 s |

**Workload** (both scenarios):
- `BBox` — 50 B every 100 ms, deadline **100 ms** (safety-critical: collision warning).
- `PCloud` — one LiDAR sweep per 200 ms as **8 × 37.5 KB segments**, deadline **500 ms** (bulk
  perception). Segment size follows EMP's measured 30–38 KB uploads and ETSI TS 103 324's
  "independently interpretable" CPM segments (`pointcloud-segmentation.md`).
- Offered load ≈ 12 Mbps, which saturates the uplink. Congestion is the point of the experiment.

**Statistics.** Every figure is the **mean of 8 seeds ± 95% CI half-width** (t-distribution).
Reproduce with `scripts/run-all.sh`.

**Delivery ceiling.** Subscriber cars spawn mid-run and cannot receive objects published before
they existed. **Urban ≈ 62% is full delivery; highway is lower still** because the publisher is on
the road twice as long. Compare configs against each other, not against 100%.

**Validity gates.** Every run must (a) reach the time limit and (b) report
`quicSendRejected = 0`. Both are enforced by `run-comparison.sh` and re-checked by `run-all.sh`.
This is not a formality — see §5.

---

## RQ1 — How can we design a system that integrates MoQ with V2X?

**Answered.** A working publisher → edge relay → subscriber MoQ system over 5G NR, integrated
with SUMO/Veins mobility, faithful to draft-ietf-moq-transport-14 on the mechanisms that matter
under congestion:

- **Object model**: tracks → groups → subgroups, with a subgroup mapped onto exactly one QUIC
  stream (§2.2); `objectsPerGroup` is the configurable group size.
- **Delivery timeout** (§9.2.1.2) applied to objects still buffered *and* to objects already
  written to QUIC, enforced by **`RESET_STREAM`** (§10.4.3), which INET's QUIC did not implement.
- **Priority** (§7.2) carried into QUIC's stream scheduler, not merely honoured in the app. QUIC
  itself defines no priority mechanism (RFC 9000 §2.3 leaves it to the implementation), so this
  required a new `PriorityScheduler`.

Three design lessons, each measured rather than assumed:

1. **Size the transport queue near the bandwidth-delay product.** Everything else is secondary
   (RQ3). A 2 MB window is ~1.3 s of standing queue at 12 Mbps; no application mechanism can meet
   a 100 ms deadline behind it.
2. **Subgroup size is a loss-amplification factor.** A reset abandons *the stream*, so batching a
   protected track's objects into a shared subgroup multiplies its loss by the group size: with
   `objectsPerGroup = 10`, 291 of 411 BBox objects were destroyed as collateral of 40 resets.
   A track you intend to protect must not share a subgroup with objects you are willing to abandon.
3. **An application must not infer transport backpressure.** Our publisher deadlocked itself by
   deducing "QUIC is full" from its own occupancy estimate, which only QUIC's drain signal could
   clear — and that signal never came.

## RQ2 — Does MoQ outperform MQTT (over QUIC and TCP), and plain TCP/UDP, in throughput and latency?

**Answered. MoQ wins decisively on latency; it does *not* win on throughput.**

### BBox — safety-critical, 100 ms deadline

| config | **URBAN** latency / miss / goodput | **HIGHWAY** latency / miss / goodput |
|---|---|---|
| **MoQ partial, 128 kB** | **33 ± 3 ms** / **0.2 ± 0.4%** / 4.1 kbps | **97 ± 24 ms** / 18.2 ± 3.0% / 3.2 kbps |
| MoQ reliable, 128 kB | 39 ± 8 ms / 0.8 ± 1.5% / 4.1 kbps | 479 ± 268 ms / 29.3 ± 5.2% / 3.9 kbps |
| MoQ / QUIC (default) | 1246 ± 228 ms / 79.9 ± 2.5% / 3.8 kbps | 2111 ± 800 ms / 92.0 ± 0.7% / 3.7 kbps |
| MQTT / QUIC | 2918 ± 127 ms / 90.0 ± 0.9% / 3.2 kbps | 8297 ± 812 ms / 97.3 ± 0.7% / 1.3 kbps |
| MoQ / UDP | 1421 ± 371 ms / 93.2 ± 1.1% / 3.1 kbps | 3519 ± 846 ms / 99.0 ± 0.2% / 1.5 kbps |
| MoQ / TCP | 22 289 ± 301 ms / 100% / 0.9 kbps | 15 864 ± 1770 ms / 100% / 0.5 kbps |
| MQTT / TCP | 26 137 ± 96 ms / 100% / 0.9 kbps | 17 878 ± 2159 ms / 100% / 0.5 kbps |

### PCloud — bulk, 500 ms deadline

| config | **URBAN** latency / miss / goodput | **HIGHWAY** latency / miss / goodput |
|---|---|---|
| MoQ partial, 128 kB | 951 ± 4 ms / 99.6% / 4.21 ± 0.21 Mbps | 1041 ± 61 ms / 99.2% / 2.24 ± 0.38 Mbps |
| MoQ reliable, 128 kB | **13 804 ± 1331 ms** / 99.7% / 5.65 Mbps | **9333 ± 926 ms** / 99.1% / 3.94 Mbps |
| MoQ / QUIC (default) | 3291 ± 247 ms / 72.7% / **9.44 ± 0.16 Mbps** | 8163 ± 584 ms / 88.0% / 3.36 Mbps |
| MQTT / QUIC | 2878 ± 131 ms / 65.8% / **9.66 ± 0.27 Mbps** | 8265 ± 829 ms / 88.1% / 3.45 Mbps |
| MoQ / UDP | 1208 ± 508 ms / 55.1% / 7.88 Mbps | 2125 ± 224 ms / 83.6% / 2.55 Mbps |
| MoQ / TCP | 22 254 ms / 99.9% / 2.48 Mbps | 15 693 ms / 99.8% / 1.37 Mbps |
| MQTT / TCP | 26 081 ms / 100% / 2.49 Mbps | 17 695 ms / 100% / 1.32 Mbps |

### What the numbers say

**Latency: MoQ wins, decisively, in both scenarios.** On the safety track it is **88× faster than
MQTT/QUIC** and **~790× faster than either TCP variant** (urban), and **85× / 165×** faster
respectively on the highway. The CIs are far apart; this is not marginal.

**Throughput: MoQ does *not* beat MQTT.** On bulk goodput at a default window, MoQ/QUIC achieves
9.44 ± 0.16 Mbps against MQTT/QUIC's 9.66 ± 0.27 Mbps — MQTT is, if anything, marginally higher,
and on the highway they are indistinguishable (3.36 vs 3.45 Mbps). Worse, the bounded window that
buys the deadline **halves bulk goodput** (9.44 → 4.21 Mbps). **MoQ trades throughput for
timeliness; it does not provide both.**

**The gap decomposes into three layers**, all mechanism-attributable:
- **TCP is catastrophic** (16–26 s, 100% miss, ~4–12% delivered). A single ordered byte stream
  means a 37.5 KB PCloud segment head-of-line-blocks a 50 B BBox message, and no application
  protocol can fix that from above. This dominates everything else.
- **QUIC alone buys ~8–18×** by removing cross-stream head-of-line blocking. Still 12–29× short of
  the deadline.
- **The bounded window buys the remaining ~38×** (1246 → 33 ms, urban).

**Why MQTT cannot close the gap — three named mechanisms** (verified against MQTT v5.0; see
`mqtt-vs-moq.md`):
1. **No per-message priority.** Both topics share one connection; BBox queues behind PCloud.
2. **Message Expiry Interval is in whole seconds** (§3.3.2.3.3), so a 100 ms deadline is not even
   expressible — and it applies only *before onward delivery starts* ([MQTT-3.3.2-5], clock counting
   time **in the server** per [MQTT-3.3.2-6]). Our broker forwards on receipt, so
   `objectsExpired = 0` in every run: **MQTT's only discard mechanism is inert**, because the
   backlog lives in the transport where MQTT cannot reach it.
3. **No `RESET_STREAM` equivalent.** Once delivery starts the message must complete, however stale.
   MQTT converts congestion into *unbounded latency*; MoQ converts it into *chosen loss*.

**A fourth, found late and worth its own line: MQTT's buffer must be sized for the whole session.**
Because it never sheds, its backlog grows with *session length × excess offered load*. The highway
publisher tenders ~135 MB over its ~90 s on the road; a 100 MB buffer overflowed and QUIC began
silently discarding writes. MoQ runs the same workload in a **128 kB** buffer — a ~10³× difference
in memory footprint, and a real operational drawback of MQTT on long-lived congested links.

**What MQTT is good at.** Over QUIC it delivers with no protocol-level loss and marginally the best
bulk goodput. It is *late*, not *lossy*. For telemetry with no deadline and a hard no-loss
requirement, MQTT's unbounded buffering is a **feature**.

## RQ3 — Which V2X use cases are boosted by MoQ?

**Answered: a small, high-rate, latency-critical stream sharing a congested uplink with bulk
traffic — and only that, and only in the urban scenario.**

**Urban:** MoQ delivers BBox at **33 ms with 0.2% deadline miss and full delivery**, while PCloud
is **sacrificed** (21.6% delivered, missing its own deadline 99.6% of the time). Right trade for
collision warning; wrong trade for HD-map upload.

**Highway: MoQ no longer meets the deadline** (97 ± 24 ms mean, but **18.2% miss**). The ordering
is unchanged — MoQ is still 85× faster than MQTT/QUIC — but the 100 ms target is not reliably met.
The highway differs from the urban grid in several ways at once, and this two-scenario design cannot
cleanly separate their contributions. The dominant observable driver is geometry: the ~1.5 km cells
keep vehicles near the cell edge for much of the run, where path loss is high and SINR low, so the
same ~12 Mbps offered load is more overloaded — *despite* the more favourable rural-macrocell LOS,
which if anything mitigates it. A publisher that lives twice as long (and so tenders twice the data)
compounds this. A mid-run handover may add further disruption, but handover was not instrumented in
these runs, so its contribution is unquantified.

**This is the single most important caveat in the report: "MoQ meets the 100 ms safety deadline"
is an urban-scenario claim, not a general one.**

Two boundary conditions, both measured:

**(a) The bounded window buys the deadline — not partial reliability.** Urban: the fully reliable
MoQ baseline at the same 128 kB window reaches 39 ± 8 ms / 0.8 ± 1.5% miss, **statistically
indistinguishable** from partial reliability's 33 ± 3 ms / 0.2 ± 0.4% (CIs overlap). Layering
delivery-timeout shedding on a *deep* buffer achieves nothing (≈80% miss at 2 MB even with shedding
enabled). This corrects an earlier claim of ours.

*(On the highway the two do separate — 97 ms / 18.2% vs 479 ms / 29.3% — so under harsher
congestion shedding does begin to matter for the safety track. Worth noting, but the urban result
is the one that constrains the design.)*

**(b) What partial reliability actually buys is a bounded bulk track.** With a shallow window the
reliable baseline cannot drop anything, so the bulk backlog simply queues: PCloud arrives
**13.8 s ± 1.3 s** late (urban) / **9.3 s ± 0.9 s** (highway). Shedding holds it at **951 ms** /
**1041 ms** — a **14× / 9× reduction** — at the cost of delivering less of it. Both miss PCloud's
deadline, so neither is *useful* for bulk data; but 1 s of bounded staleness and a 14 s unbounded
backlog are different failure modes. Acting on 14-second-old perception data is arguably worse than
having none.

**So: the window protects the latency-critical track; the delivery timeout bounds staleness under
overload.** Two distinct contributions, and they should be reported as such.

## 5. Threats to validity

*(Full list of resolved issues and project limits: [`ISSUES-AND-LIMITS.md`](ISSUES-AND-LIMITS.md).)*

- **Scenario dependence.** The headline deadline result holds in urban and fails on the highway.
  Both are reported; neither generalises to arbitrary V2X deployments.
- **One workload shape.** One publisher, two tracks. `MOQ_Partial_MultiPub` (3 publishers) is
  implemented but **was not run** — no load-scaling evidence.
- **The single-publisher topology is the most favourable case for MoQ's priority mechanism.**
  MoQ priority is scoped to a session (draft §7), so it orders one sender's streams. With many
  vehicles the contention is *between* senders, arbitrated by the 5G MAC scheduler, which knows
  nothing of MoQ priorities — MoQ could not protect one vehicle's safety data from another's
  bulk data. Reasoned, not measured: [`topology-and-priority-scope.md`](topology-and-priority-scope.md).
- **MQTT QoS 0 only.** QoS 1 and 2 were not measured. Defensible (over TCP, QoS 0 already gets
  reliable delivery from the transport, and QoS 2's four-way handshake is not used in V2X
  telemetry) but it is a scope limit, not a result.
- **Two highway runs aborted** with a Simu5G bug
  (`NrTxPdcpEntity::deliverPdcpPdu - destination must be a UE`, a handover/attach race in the
  Binder). Deterministic per seed and **independent of the protocol under test** — it struck
  `MOQ_TCP` (seed 3) and `MOQ_UDP` (seed 4) by chance of draw. They are excluded and reported
  (n = 7 instead of 8 for those two rows); the bug is **not fixed**.
- **Window sweep is single-seed.** Endpoints (2 MB vs 64–128 kB) differ by an order of magnitude
  and are safe; the 512 kB–1 MB transition region is noisy and no claim is made about it.
- **Simulation, not deployment.** Idealised 10 G backhaul (so the radio is the only bottleneck, by
  design), zero relay processing delay, perfect clock sync, zero-filled payloads with no codec and
  no inter-object dependency. Loss is a transport-level ratio, not a perceived-quality claim.
- **MQTT-over-QUIC is not standardised.** We carry the MQTT byte stream over a single QUIC stream
  in place of TCP — the accepted approach — so MQTT gains QUIC's loss recovery but still cannot
  exploit independent streams. That limitation is inherent to MQTT's framing, not to our code.

## 6. Upstream bugs found and fixed

Each silently corrupts results, and each was found by chasing an implausible number rather than by
anything failing loudly.

| where | bug | effect |
|---|---|---|
| **Simu5G** | `computeRuralMacro` fed carrier frequency in **Hz** into a TR 38.901 term specified in **GHz** | constant **+180 dB** of phantom path loss; `RURAL_MACROCELL` produced 252–281 dB and no link survived. Cross-checked against ns-3's reference implementation. **This blocked the entire highway scenario.** |
| **INET QUIC** | `RESET_STREAM` released stream-level but not **connection-level** flow-control credit for abandoned bytes (RFC 9000 §4.5) | the connection window leaked shut on every reset, deadlocking any reset-heavy run |
| **INET QUIC** | send-side flow control was a running byte counter that **decremented on loss** and never re-charged retransmissions (RFC 9000 §4.1 charges the *highest offset sent*) | sender credited itself window it had already spent, overran the peer's limit, and the connection aborted with `FLOW_CONTROL_ERROR` at small windows |

Plus two of our own: the publisher backpressure deadlock, and a per-call (rather than per-event)
byte tally that let a group burst overshoot QUIC's send-queue limit, causing **3468 silently
discarded writes**.

### The silent-loss gate — the methodological lesson

INET's QUIC **silently discards** writes once its send queue is full: no block, no error, just a
callback an application may ignore. **Discarded load looks like good performance** — the data never
enters the network, so it never queues, so latency looks excellent while the payload was binned at
the sender.

This produced **two entirely bogus result tables** during this project:
- MQTT/QUIC first "achieved" 34 ms and 0% deadline miss — while discarding ~85% of its messages.
- MoQ then "achieved" 27 ms — while discarding 3468 writes.

Neither failed loudly. Both were caught only by noticing a delivery ratio that did not add up. A
third failure mode (a simulation that *aborts early* still writes a plausible-looking `.sca`) was
caught later, after two crashed runs had already been folded in as data.

**Gate every run on: (a) it reached the time limit, and (b) `quicSendRejected == 0`.** Both checks
are now enforced in `run-comparison.sh` and re-verified by `run-all.sh`, which refuses to print
results if any run fails either.

## 7. Reproducing

```
./scripts/run-all.sh results 8      # build, both scenarios x 8 seeds, sweep, verify, report
```
Runs are executed in parallel (default: half the cores); 56 runs take ~28 min on 32 cores.

## 8. Suggested next steps

1. **Run the multi-publisher scaling config.** It exists and is unmeasured; it is the cheapest
   remaining way to test whether the RQ3 claim survives higher offered load.
2. **Seed the window sweep** so the BDP operating point can be stated with a CI rather than
   asserted from one run.
3. **Consider leading with the bounded-window result**, and presenting partial reliability as the
   mechanism that bounds *bulk* staleness. That is what the data supports, and it is more defensible
   than "MoQ's shedding wins" — which the urban CIs do not support.
