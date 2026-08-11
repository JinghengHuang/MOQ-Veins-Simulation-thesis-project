# MoQ for V2X over 5G: consolidated report

Answers to RQ1–RQ3, with the evidence base and its limits.

Abbreviations, protocol terms and config-name suffixes are defined in
[`GLOSSARY.md`](GLOSSARY.md).

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

**Statistics.** Every figure is the **mean of 5 seeds ± 95% CI half-width** (t-distribution).
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

Two design lessons, each measured rather than assumed:

1. **Size the transport queue near the bandwidth-delay product.** Everything else is secondary
   (RQ3). A 2 MB window is ~1.3 s of standing queue at 12 Mbps; no application mechanism can meet
   a 100 ms deadline behind it.
2. **An application must not infer transport backpressure.** Our publisher deadlocked itself by
   deducing "QUIC is full" from its own occupancy estimate, which only QUIC's drain signal could
   clear — and that signal never came.

## RQ2 — Does MoQ outperform MQTT (over QUIC and TCP), and plain TCP/UDP, in throughput and latency?

**Answered. MoQ wins decisively on latency; it does *not* win on throughput.**

### BBox — safety-critical, 100 ms deadline

| config | **URBAN** latency / miss / goodput | **HIGHWAY** latency / miss / goodput |
|---|---|---|
| **MoQ partial, 128 kB** | **33 ± 3 ms** / **0.2 ± 0.4%** / 4.1 kbps | **113 ± 50 ms** / 20.5 ± 4.7% / 2.9 kbps |
| MoQ reliable, 128 kB | 39 ± 8 ms / 0.8 ± 1.5% / 4.1 kbps | 210 ± 95 ms / 26.7 ± 5.6% / 4.0 kbps *(ends at 63 s)* |
| MoQ / QUIC (default) | 1187 ± 311 ms / 79.8 ± 2.3% / 3.8 kbps | 1633 ± 1142 ms / 93.4 ± 2.3% / 3.9 kbps *(ends at 68 s)* |
| MQTT / QUIC | 2918 ± 127 ms / 90.0 ± 0.9% / 3.2 kbps | 8406 ± 1052 ms / 97.0 ± 1.0% / 1.2 kbps |
| MoQ / UDP | 1421 ± 371 ms / 93.2 ± 1.1% / 3.1 kbps | 3531 ± 1706 ms / 99.1 ± 0.3% / 1.5 kbps *(n = 4)* |
| MoQ / TCP | 4365 ± 857 ms / 91.3 ± 1.8% / 3.0 kbps | 8507 ± 1428 ms / 98.4 ± 0.6% / 1.3 kbps |
| MQTT / TCP | 3964 ± 793 ms / 94.8 ± 0.6% / 3.1 kbps | 8557 ± 979 ms / 98.1 ± 1.9% / 1.2 kbps *(n = 4)* |

*Rows marked "ends at N s" reach their send-buffer limit and terminate the subscription with
PUBLISH_DONE TOO_FAR_BEHIND (draft-14 §9.2.1.2) in all 5 seeds. The highway publisher is on the
road ~88 s, so those configs serve only ~70–75% of the available session; their latency and
goodput are measured over that shorter window, and their `delivered%` is computed against a
truncated offered count. See `moq-operating-envelope.md` §7.*

### PCloud — bulk, 500 ms deadline

| config | **URBAN** latency / miss / goodput | **HIGHWAY** latency / miss / goodput |
|---|---|---|
| MoQ partial, 128 kB | 951 ± 4 ms / 99.6% / 4.16 ± 0.21 Mbps | 1021 ± 61 ms / 99.3% / 2.19 ± 0.74 Mbps |
| MoQ reliable, 128 kB | **13 804 ± 1331 ms** / 99.7% / 5.65 Mbps | **9275 ± 1825 ms** / 99.1% / 4.04 Mbps *(ends at 63 s)* |
| MoQ / QUIC (default) | 3160 ± 106 ms / 73.8% / **9.54 ± 0.30 Mbps** | 9106 ± 1278 ms / 90.0% / 3.11 Mbps *(ends at 68 s)* |
| MQTT / QUIC | 2878 ± 131 ms / 65.8% / **9.66 ± 0.27 Mbps** | 8338 ± 1139 ms / 88.1% / 3.40 Mbps |
| MoQ / UDP | 1208 ± 508 ms / 55.1% / 7.88 Mbps | 2251 ± 300 ms / 85.1% / 2.55 Mbps *(n = 4)* |
| MoQ / TCP | 4330 ± 835 ms / 65.9 ± 10.4% / 8.84 ± 0.68 Mbps | 8462 ± 1393 ms / 89.0 ± 1.6% / 3.11 Mbps |
| MQTT / TCP | 3938 ± 778 ms / 61.0 ± 5.8% / 9.09 ± 0.35 Mbps | 8550 ± 925 ms / 87.1 ± 7.0% / 2.72 Mbps *(n = 4)* |

### What the numbers say

**Latency: MoQ wins, decisively, in both scenarios.** On the safety track it is **88× faster than
MQTT/QUIC** and **120–132× faster than the TCP variants** (urban); on the highway the corresponding
factors are **74×** and **75–76×**. The CIs are far apart; this is not marginal.

**Throughput: MoQ does *not* beat MQTT.** On bulk goodput at a default window, MoQ/QUIC achieves
9.54 ± 0.30 Mbps against MQTT/QUIC's 9.66 ± 0.27 Mbps — the CIs overlap, so the two are
indistinguishable, and on the highway MQTT is nominally ahead (3.40 vs 3.11 Mbps, also
overlapping). Worse, the bounded window that buys the deadline **more than halves bulk goodput**
(9.54 → 4.16 Mbps). **MoQ trades throughput for timeliness; it does not provide both.**

**The gap decomposes into three layers**, all mechanism-attributable:
- **TCP loses on latency, not on throughput** (4.4 s urban / 8.6 s highway; 91–95% and 98% miss).
  A single ordered byte stream means a 37.5 KB PCloud segment head-of-line-blocks a 50 B BBox
  message, and no application protocol can fix that from above. **The signature is direct: TCP
  delivers BBox at essentially its own PCloud's latency — 4365 vs 4330 ms urban, 8507 vs 8462 ms
  highway — because the safety track moves at the bulk track's pace when they share one ordered
  stream. QUIC decouples them (urban 1187 vs 3160 ms; highway 1633 vs 9106 ms). Meanwhile bulk
  throughput is at near-parity (urban PCloud 8.84 Mbps over TCP against 9.54 Mbps over QUIC), so
  this is not a capacity deficit — the ordered stream costs latency specifically.** This is HOL
  blocking measured rather than inferred, and it is what remains after the advertised-window
  artifact was fixed; the earlier "22–26 s, 100% miss, ~12% delivered" figures were that artifact,
  not TCP.
- **QUIC alone buys less than expected** — 3.7× for MoQ and only 1.4× for MQTT (urban); on the
  highway 5.2× for MoQ and essentially nothing for MQTT (8557 → 8406 ms). Removing cross-stream
  HOL blocking helps only a protocol that uses multiple streams, and MQTT does not.
- **The bounded window buys the remaining ~36×** (1187 → 33 ms, urban), and is where the deadline
  is actually won.

**Why plain UDP also trails QUIC** (the comparison the three layers above omit). The clean
measurement is the per-track in-range loss ratio, because `BBox` is 125 B — **one datagram, never
fragmented** — while `PCloud` is 32. Comparing the two isolates congestion control from
reassembly:

| highway, in-range loss | BBox (1 datagram) | PCloud (32 datagrams) |
|---|---|---|
| MoQ / UDP | **58.6%** | 77.5% |
| MoQ / QUIC | 12.1% | 33.7% |
| MoQ / TCP | 31.8% | 32.3% |

**UDP's deficit is the absence of congestion control, not the absence of reassembly robustness.**
It loses 58.6% of its *smallest, single-datagram* messages where QUIC loses 12.1% — a gap that
exists before any object is fragmented. Uncontrolled sending into the ~66 Mbps cell manufactures
queue-drop that UDP then cannot recover.

All-or-nothing reassembly is real and adds a **second, smaller** penalty: survival falls from
41.4% (BBox) to 22.5% (PCloud), a factor of **~1.8×**, not 32×. The independence model
`P(delivered) ≈ (1−p)³²` is badly wrong here — at the observed p = 0.586 it predicts ~10⁻¹² PCloud
survival against 22.5% measured. **Fragment losses are strongly correlated**: an object's 32
datagrams are written back-to-back and are dropped together by a single queue-overflow event, so
they largely share fate. That correlation is itself a finding — burst loss is what makes
application-layer fragmentation survivable at all at this loss rate.

**Loss recovery — not the stream abstraction itself — separates UDP from the rest**: stream-less
TCP matches QUIC on the bulk track (32.3% vs 33.7% loss; 66.6% vs 66.2% delivered, pooled). On the
safety track TCP falls behind QUIC (31.8% vs 12.1%) for the separate reason established above —
head-of-line blocking, not loss.

*Denominator caveat.* Do not quote "UDP delivers 22.1%" against TCP/QUIC's ~66% as a like-for-like
comparison. `objectsExpected` derives from the highest object ID that arrives, and the
connection-oriented configs backlog so badly that their subscribers never reach past object ~880,
where a UDP subscriber — which drops rather than queues — reaches ~2577. TCP and QUIC deliver most
of a *short prefix* of the track; UDP delivers a scattered fraction of a **3× longer span**. Both
ratios are individually correct and they do not measure the same thing. The loss-ratio table above
is the comparison to cite.

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
is **sacrificed** (21.4% delivered, missing its own deadline 99.6% of the time). Right trade for
collision warning; wrong trade for HD-map upload.

### Workload grounding — where the object sizes come from

The result depends on the *shape* of the workload, so the two object sizes are anchored to
standards and measurement rather than chosen for convenience.

**`BBox` = 50 B models one perceived object, not a whole message.** Two independent sources agree
on the size of the CPM field that describes a single detected object. ETSI's analysis of the
Collective Perception Service gives the Perceived Object Container "an average size of about
**31 bytes** for each object included in the message"
([ETSI TR 103 562](https://www.etsi.org/deliver/etsi_tr/103500_103599/103562/02.01.01_60/tr_103562v020101p.pdf));
Thandavarayan et al. model it as a fixed "**Perceived Object Container (35 Bytes)** … per detected
object" ([JNCA 2023](https://doi.org/10.1016/j.jnca.2023.103655), §5.1). 50 B is that container plus
framing overhead, and sits comfortably above both.

A *complete* CPM is several times larger: its mandatory "ITS PDU header, Management Container and
Originating Vehicle Container" total **121 Bytes**, before any object is described, and ETSI
segments CPMs above a 1 100 byte threshold. For a full safety message the same paper sets CAM size
at **350 bytes**.

**This matters for interpreting the result.** The safety track is ~0.03% of offered bytes, so
protecting it is nearly free — that is *why* priority costs the bulk track so little. Had BBox been
modelled as a whole 350 B CAM rather than a single perceived object, the size asymmetry would fall
from ~750× to ~107×. Still large enough that the conclusion holds, but the margin is narrower than
the headline figure suggests, and the asymmetry — not MoQ itself — is doing much of the work.

**`PCloud` = 8 × 37.5 KB per sweep — segment size is sourced; the total is a deliberate load
choice.** The 37.5 KB segment sits inside EMP's measured 30–38 KB per-vehicle upload
[@zhang2021emp], and the segmentation into independently usable units follows ETSI TS 103 324's
"independently interpretable CPM" segments [@etsi103324]. The **number** of segments is not from
EMP: EMP partitions the scene across vehicles so each uploads only its share, whereas this publisher
sends a full sweep. The 300 KB total is set to place offered load ~1.3× above cell capacity, because
congestion is the phenomenon under study. It is consistent with a compressed full frame — EMP reports
a ~2.0 MB raw frame, and roadside LiDAR ranges "from 5-25 MB per frame"
([Zimmer et al., 2024](https://arxiv.org/abs/2405.01750)) — but it is a load setting, not a fidelity
claim, and should be read as one.

**What this claim is, precisely: MoQ *directs* the unavoidable loss by policy — it does not serve
the workload.** The link is over capacity for both tracks, so no configuration serves both; every
window that protects BBox starves PCloud (PCloud misses its 500 ms deadline ~99% of the time
wherever BBox is safe — see `moq-operating-envelope.md`). MoQ's contribution is that the *operator*,
not the queue mechanics, decides which track survives contention: the safety stream is protected and
the bulk degrades gracefully and controllably — where TCP head-of-line-blocks both and a deep-buffer
QUIC drowns the safety stream. A workload that needs the full point cloud *on time* is not served by
a better protocol; it needs more capacity (spectrum, fewer subscribers, or a lower bulk rate).

**Highway: MoQ no longer meets the deadline** (113 ± 50 ms mean, but **20.5% miss**). The ordering
is unchanged — MoQ is still 74× faster than MQTT/QUIC — but the 100 ms target is not reliably met.
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

*(On the highway the two separate, and the interesting difference is not latency. The highway
publisher is on the road ~88 s (3 km at 33.3 m/s), which bounds the session. Partial reliability
produces for that whole window (88.0 s) at 113 ± 50 ms / 20.5 ± 4.7% miss. The reliable baseline
reaches its send-buffer limit and **terminates the subscription at 63.0 ± 2.1 s in all 5 seeds**
with PUBLISH_DONE TOO_FAR_BEHIND (draft-14 §9.2.1.2), producing only 61.1 s — so its 210 ± 95 ms /
26.7 ± 5.6% is measured over ~70% of the available session, and it delivers fewer BBox objects
overall (1680 ± 443 vs 2184 ± 522). Under harsher congestion the reliable mode does not merely
degrade — it stops serving. Urban never reaches the limit in any config or seed, so the urban result
still constrains the design. See `moq-operating-envelope.md` §7.)*

**(b) What partial reliability actually buys is a bounded bulk track.** With a shallow window the
reliable baseline cannot drop anything, so the bulk backlog simply queues: PCloud arrives
**13.8 s ± 1.3 s** late (urban) / **9.3 s ± 1.8 s** (highway). Shedding holds it at **951 ms** /
**1021 ms** — a **14× / 9× reduction** — at the cost of delivering less of it. Both miss PCloud's
deadline, so neither is *useful* for bulk data; but 1 s of bounded staleness and a 14 s unbounded
backlog are different failure modes. Acting on 14-second-old perception data is arguably worse than
having none.

On the highway there is a further cost the latency figure hides: that unbounded backlog is what
drives the reliable baseline into its resource limit, ending the subscription at 63 s. Bounding the
backlog is therefore not only about freshness — it is what keeps the session alive at all.

**So: the window protects the latency-critical track; the delivery timeout bounds staleness under
overload.** Two distinct contributions, and they should be reported as such.

### Which use cases, stated against the standard catalogue

One workload shape was measured, so "which use cases" is answered by establishing **what properties
a workload must have** and then classifying the standard catalogue against them. 3GPP groups
enhanced V2X into four categories ([TR 22.886](https://www.3gpp.org/ftp/Specs/archive/22_series/22.886/),
with quantified latency, reliability, payload and data-rate requirements per scenario in
[TS 22.186](https://www.3gpp.org/ftp/Specs/archive/22_series/22.186/)).

MoQ's mechanisms help a workload where **all four** hold:

1. the latency-critical stream is **small relative to the competing traffic** (so priority is nearly
   free rather than zero-sum);
2. the competing traffic is **loss-tolerant and independently interpretable** (so shedding degrades
   it rather than corrupting it);
3. deadlines are **known a priori and static** for the session;
4. the transport queue **can be bounded near the BDP**.

| eV2X category (TR 22.886) | shape | verdict |
|---|---|---|
| **Extended sensors** | small CPM-like safety stream + bulk perception | **measured here** — all four hold; this is the case the report answers |
| **Vehicle platooning** | frequent small messages, little bulk to shed | properties (1)–(2) barely apply: with no bulk track there is nothing to sacrifice. The BDP-sized window still helps; the delivery timeout has little to act on. **Inferred, untested.** |
| **Advanced driving** | coordination messages alongside sensor sharing | plausibly satisfies all four, depending on the sensor payload. **Inferred, untested.** |
| **Remote driving** | large uplink video *is* the latency-critical stream | **violates (1)**. There is no bulk track to sacrifice — shedding would attack the critical stream itself. Predicted unsuitable, on structural grounds. |

Two honesty notes. **Only the first row is measured**; the others are classified by matching their
TS 22.186 requirements against the four properties, which is reasoning, not evidence. And the tested
shape is the **favourable** case for MoQ — small-critical-plus-large-tolerant is precisely what its
mechanisms are designed for — so this result is closer to an upper bound on MoQ's benefit than to a
representative sample of V2X.

A further restriction cuts across all four rows: the workload here has **no inter-object
dependencies** (§5), so shed objects cause no error propagation. Any codec with inter-frame
prediction, or any incremental map update, violates that and would degrade far worse than the
loss ratios here suggest.

## RQ3 (extended) — Does MoQ's priority hold as load scales across publishers?

MoQ's priority is scoped to a session — that is, to one *connection*, not to one publisher
(draft §7). Because the edge relay serves each subscriber over a single session carrying every
publisher's tracks, that scope still spans all publishers on the bottleneck downlink. To test how
far priority carries as load spreads across senders, cars 0..N−1 each publish their own
BBox + PCloud (N = 1..4) at the 128 kB operating point (urban), the remaining cars subscribe to all
of them, and the run is repeated under QUIC's default **round-robin** scheduler and our
**PriorityScheduler**. 5 seeds; all runs passed the gate.

| publishers | round-robin BBox lat / miss | priority BBox lat / miss |
|---|---|---|
| 1 | 48 ± 5 ms / 1.6% | 49 ± 7 ms / 1.9% |
| 2 | 133 ± 15 ms / 49% | 92 ± 45 ms / 33% |
| 3 | 353 ± 37 ms / 83% | 121 ± 10 ms / 52% |
| 4 | 538 ± 147 ms / 87% | 180 ± 80 ms / 55% |

1. **The safety track degrades super-linearly with publisher count.** Going 1→4 publishers (4×
   offered safety load) drives round-robin BBox latency 48→538 ms (~11×) and miss 1.6→87%. Each new
   publisher adds two streams; the 50 B BBox waits behind more 37.5 KB PCloud per round, and because
   the connection is throughput-capped the backlogs compound rather than merely add.
2. **The PriorityScheduler does real work** — it roughly halves both latency and miss at every N ≥ 2
   (at 4 publishers, 180 vs 538 ms and 55% vs 87%). At N = 1 the two schedulers are identical,
   because a shallow 128 kB queue already delivers BBox on time regardless of order — consistent with
   the operating-envelope finding that at the BDP the *window*, not the scheduler, does the work.
3. **But priority does not restore single-publisher performance.** Even with it, four publishers
   miss 55% of BBox deadlines. The residual cannot be attributed from this sweep: N varies *both*
   the streams multiplexed onto each subscriber's downlink connection (2N, sharing one congestion
   window and one 128 kB flow-control budget) and the number of transmitting UEs / uplink
   connections (N). Stream dilution and inter-UE MAC contention are therefore confounded.

This **measures** how far MoQ's priority carries. On the bottleneck downlink it arbitrates across
publishers and does substantial work; it does not hold the safety track at the single-publisher
level once load is spread across senders. The leg where MoQ demonstrably has *no* authority — the
publisher→relay uplink, where each vehicle holds a separate connection on a separate radio — is not
the bottleneck here and is not isolated by this sweep. (Urban, 128 kB, single scenario. Chart 8;
`topology-and-priority-scope.md` §4 records the confound, §6 the experiment that would resolve it.)

## 5. Threats to validity

*(Full list of resolved issues and project limits: [`ISSUES-AND-LIMITS.md`](ISSUES-AND-LIMITS.md).)*

- **Scenario dependence.** The headline deadline result holds in urban and fails on the highway.
  Both are reported; neither generalises to arbitrary V2X deployments.
- **One workload shape.** Two tracks per publisher; only the publisher count is varied, at constant
  total load (`PubScale_N{1..4}`). `MOQ_Partial_MultiPub` (3 publishers at full rate) is implemented
  but **was not run** — it moves offered load and sender count together.
- **MoQ's priority is not evaluated on the uplink leg.** Priority is scoped to a session, i.e. to
  one connection (draft §7) — not to one publisher. The relay serves each subscriber over a single
  session carrying every publisher's tracks, so on the bottleneck downlink priority *does* arbitrate
  across publishers, and the sweep measures it doing so. On the publisher→relay uplink each vehicle
  holds its own connection on its own radio, arbitration falls to the 5G MAC scheduler, and MoQ has
  no mechanism there — but that leg is not the bottleneck in these runs and no experiment isolates
  it. Reasoned, not measured:
  [`topology-and-priority-scope.md`](topology-and-priority-scope.md) §5.
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
./scripts/run-all.sh results 5      # build, both scenarios x 5 seeds, sweep, verify, report
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

---

## References

Sources cited above, beyond the protocol specifications (draft-ietf-moq-transport-14,
OASIS MQTT v5.0, RFC 9000) listed in [`GLOSSARY.md`](GLOSSARY.md).

```bibtex
@inproceedings{zhang2021emp,
  author    = {Zhang, Xumiao and Zhang, Anlan and Sun, Jiachen and Zhu, Xiao and
               Guo, Y. Ethan and Qian, Feng and Mao, Z. Morley},
  title     = {{EMP}: Edge-assisted Multi-vehicle Perception},
  booktitle = {Proc. 27th Annual Int. Conf. on Mobile Computing and Networking (MobiCom '21)},
  year      = {2021}, pages = {545--558}, publisher = {ACM},
  doi       = {10.1145/3447993.3483242}
}

@techreport{etsi103324,
  author      = {{ETSI}},
  title       = {Intelligent Transport Systems ({ITS}); Vehicular Communications; Basic Set of
                 Applications; Collective Perception Service; Release 2},
  institution = {ETSI}, number = {TS 103 324}
}

@techreport{etsi103562,
  author      = {{ETSI}},
  title       = {Intelligent Transport Systems ({ITS}); Vehicular Communications; Basic Set of
                 Applications; Analysis of the Collective Perception Service ({CPS}); Release 2},
  institution = {ETSI}, number = {TR 103 562}, year = {2019},
  note        = {Perceived Object Container averages ~31 bytes per object},
  url         = {https://www.etsi.org/deliver/etsi_tr/103500_103599/103562/02.01.01_60/tr_103562v020101p.pdf}
}

@article{thandavarayan2023scalable,
  author  = {Thandavarayan, Gokulnath and Sepulcre, Miguel and Gozalvez, Javier and
             Coll-Perales, Baldomero},
  title   = {Scalable cooperative perception for connected and automated driving},
  journal = {Journal of Network and Computer Applications},
  volume  = {216}, pages = {103655}, year = {2023},
  note    = {Perceived Object Container 35 bytes; mandatory headers/containers 121 bytes; CAM 350 bytes},
  doi     = {10.1016/j.jnca.2023.103655}
}

@article{thandavarayan2020cpm,
  author  = {Thandavarayan, Gokulnath and Sepulcre, Miguel and Gozalvez, Javier},
  title   = {Generation of Cooperative Perception Messages for Connected and Automated Vehicles},
  journal = {IEEE Transactions on Vehicular Technology}, year = {2020},
  note    = {ITS PDU header + Management + Station Data containers ~121 bytes; preprint arXiv:1908.11151},
  url     = {https://arxiv.org/abs/1908.11151}
}

@misc{zimmer2024pointcompress3d,
  author = {Zimmer, Walter and Pranamulia, Ramandika and Zhou, Xingcheng and
            Liu, Mingyu and Knoll, Alois C.},
  title  = {{PointCompress3D}: A Point Cloud Compression Framework for Roadside {LiDARs}
            in Intelligent Transportation Systems},
  year   = {2024}, url = {https://arxiv.org/abs/2405.01750}
}

@techreport{3gpp22886,
  author      = {{3GPP}},
  title       = {Study on enhancement of {3GPP} support for {5G} {V2X} services},
  institution = {3GPP}, number = {TR 22.886},
  url         = {https://www.3gpp.org/ftp/Specs/archive/22_series/22.886/}
}

@techreport{3gpp22186,
  author      = {{3GPP}},
  title       = {Enhancement of {3GPP} support for {V2X} scenarios; Stage 1},
  institution = {3GPP}, number = {TS 22.186},
  url         = {https://www.3gpp.org/ftp/Specs/archive/22_series/22.186/}
}
```

*Fill in the year and Release/version for the 3GPP and ETSI entries from the revision actually
consulted — citing a 3GPP spec without a Release is a common review flag.*

---

## Glossary

| | |
|---|---|
| **BBox** / **PCloud** | the 50 B / 100 ms safety track and the 37.5 kB x 8 / 500 ms bulk track |
| **BDP** | Bandwidth-Delay Product — capacity x RTT |
| **CI** | Confidence Interval — headline results are mean +/- 95% CI over n = 5 seeds |
| **HOL** | Head-of-Line (blocking) |
| **EMP** | *Edge-assisted Multi-vehicle Perception*, MobiCom '21 — source of the 30-38 kB chunk size |
| **RMa** / **UMa** | Rural Macro / Urban Macro — TR 38.901 propagation scenarios |

Full list: [`GLOSSARY.md`](GLOSSARY.md)
