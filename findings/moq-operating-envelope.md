# MoQ operating envelope: two regimes, and the knee between them

Urban grid, 1 publisher car, 7 subscriber cars, edge relay. 200 s runs.
`BBox` = 50 B / 100 ms, priority 0, deadline 100 ms, delivery timeout 200 ms (safety-critical).
`PCloud` = one LiDAR sweep / 200 ms as 8 x 37.5 KB segments, deadline 500 ms, delivery timeout
1.0 s (bulk).

Sweeping the QUIC connection flow-control window, with `sendQueueLimit` scaled alongside it
(~window/8). `MOQ_Partial` = delivery-timeout shedding; `MOQ_SW` = same window, fully reliable
(no shedding). **Scheduler: default round-robin** — these configs do *not* enable the transport
priority scheduler (that is `*_Prio_Window`), so the behaviour below is pure bufferbloat, not
priority.

**Validity:** every completed run reports `quicSendRejected = 0`. Earlier versions of this sweep
were holed by `FLOW_CONTROL_ERROR` and distorted by silent write loss; both are fixed.

**Delivery ceiling:** subscriber cars spawn over the first 35 s and cannot receive what was
published earlier. **~62% is full delivery**, not 100% — compare configs to each other, not to 100.

**Seeds:** single seed per window point. The regime structure and the ~30x endpoint gap are not in
doubt; the *exact* knee position (±one window step) needs replication before it is a firm claim.

---

## 1. The two regimes

Reading down the window (`MOQ_Partial`, safety track first):

| window | BBox lat / miss / deliv | PCloud lat / miss / deliv / goodput | regime |
|---|---|---|---|
| 64 kB  | 31 ms / **0.0%** / 61% | 945 ms / 99.6% / 19% / 3.7 Mbps | safe |
| 128 kB | 40 ms / 1.5% / 62%     | 966 ms / 99.6% / 23% / 4.3 Mbps | safe |
| 256 kB | 49 ms / 3.1% / 62%     | 967 ms / 99.3% / 34% / 6.4 Mbps | safe |
| 300 kB | 46 ms / 4.5% / 62%     | 980 ms / 99.2% / 37% / **7.0 Mbps** | **edge** |
| 350 kB | 88 ms / **23%** / 61%  | 998 ms / 95% / 44% / 8.3 Mbps | **knee** |
| 400 kB | 192 ms / 43% / 60%     | 1432 ms / 92% / 44% / 8.0 Mbps | past |
| 450 kB | 308 ms / 51% / 58%     | 2172 ms / 59% / 51% / 8.4 Mbps | past |
| 512 kB | 438 ms / 75% / 62%     | 3642 ms / 65% / 55% / 9.1 Mbps | collapsed |
| 1 MB   | 431 ms / 71% / 56%     | 1728 ms / 64% / 51% / 9.3 Mbps | collapsed |
| 2 MB   | 955 ms / 82% / 61%     | 2727 ms / 71% / 57% / 9.8 Mbps | collapsed |

The sweep is **not** a smooth trade — it has two regimes divided by a knee:

- **Safe zone (window <= ~300 kB, single-seed).** BBox meets its 100 ms deadline (latency 31–49 ms,
  miss <= 4.5%). Within this zone, a *larger* window buys *more bulk throughput* — PCloud goodput
  climbs 3.7 -> 7.0 Mbps and its delivered fraction 19% -> 37% — at little cost to the safety track
  *in this single-seed sweep*. (The multi-seed validation in §6 is less forgiving: near the knee the
  run-to-run variance is large, so the *upper* end of this zone is not actually safe — 300 kB across
  5 seeds misses 9.5%, not 4.5%. The genuinely safe operating point is 128 kB, not 300 kB.)
- **The knee (~300–350 kB).** BBox deadline-miss jumps 4.5% -> 23% between 300 and 350 kB; mean
  latency crosses 100 ms between 350 and 400 kB. The safety track breaks here. Note the *miss ratio*
  turns before the *mean* does (350 kB is 88 ms mean but 23% miss — the tail is already blown), so
  the single-seed edge is ~300 kB — which §6 then shows is already too close to the knee to be safe
  across seeds.
- **Collapse (window >= ~400 kB).** BBox latency grows roughly linearly with the window
  (192 -> 308 -> 438 -> 955 ms); miss reaches 43–82%. Bulk goodput plateaus at ~9 Mbps, so past the
  knee you buy almost no throughput for a great deal of safety-track latency.

## 2. Why the knee exists (mechanism)

The flow-control window caps how much data QUIC keeps outstanding — in its send queue and committed
to the radio (RAN). **That is the depth of the standing queue both tracks share.**

- **Before the knee — shallow queue.** The committed queue is shallow; shedding keeps the app send
  buffer bounded. The 50 B BBox objects interleave (round-robin) and traverse a shallow queue, so
  they reach the wire inside 100 ms. A larger window admits more PCloud before flow-control
  backpressure, so less is shed and more is delivered — throughput rises toward capacity.
- **After the knee — deep queue (bufferbloat).** The window now permits a PCloud backlog deep
  enough that draining it exceeds BBox's deadline. Critically, that backlog sits **downstream of
  where shedding can act**: once PCloud bytes are committed to QUIC / the radio they cannot be
  retroactively dropped, and round-robin does not let BBox jump ahead of bytes already in the RAN.
  BBox is trapped behind a deep committed queue, so its latency scales with the window. This is
  exactly the ~520 ms of BBox latency the bufferbloat analysis places "in QUIC's send queue and the
  RAN, which shedding cannot touch."

**One knob, opposing effects:** the deeper queue that carries more bulk throughput is the same queue
the safety track must wait behind. The knee is where that shared queue first grows deeper than the
safety deadline.

The knee sits at ~300–350 kB — **4–5x above the ~75 kB bandwidth-delay product**. That gap is what
shedding and BBox's tiny size buy: they keep the safety track alive well past the naive BDP before
bufferbloat wins.

## 3. Window buys the deadline; shedding extends the safe edge

The safety track is rescued almost entirely by the *window*, not by shedding. At 128 kB the reliable
baseline reaches **42 ms / 0.9% miss**, statistically indistinguishable from `MOQ_Partial`'s
40 ms / 1.5%. Shrinking the window does the work.

But the completed reliable curve shows shedding **extends the safe edge by about one window step**:

| window | `MOQ_Partial` BBox miss | `MOQ_SW` BBox miss |
|---|---|---|
| 128 kB | 1.5% | 0.9% |
| 256 kB | **3.1%** | **11.3%** |
| 300 kB | **4.5%** | **12.5%** |

At 256–300 kB the reliable baseline has already started missing (11–12%) while shedding holds the
line (3–4%). So the reliable config's safe envelope tops out around 128–256 kB; shedding pushes it
to ~300 kB. Near the operating point they agree; approaching the knee, shedding earns headroom.

## 4. What partial reliability actually buys — and what it costs

**It bounds bulk staleness.** Once the window is shallow the reliable baseline cannot drop anything,
so the PCloud backlog queues for **13–14 seconds**; `MOQ_Partial` sheds stale segments and holds it
to ~1 s — a **~14x** reduction. Both miss PCloud's 500 ms deadline ~99% of the time, so neither is
*useful* for the bulk track at these windows, but 1 s of bounded staleness is a different failure
mode from acting on 14-second-old perception.

**It costs most of the bulk track.** Shedding delivers *less* PCloud than the reliable baseline:

| window | `MOQ_Partial` PCloud delivered | `MOQ_SW` PCloud delivered |
|---|---|---|
| 128 kB | **22.6%** | 59.7% |
| 300 kB | 37.4% | 60.7% |

At the 128 kB operating point partial reliability delivers only ~23% of PCloud — **it sheds roughly
three-quarters of the bulk track** (see §5 on where those objects go). The reliable baseline delivers
~60%, but 14 s late. This is the trade in one line: **`MOQ_Partial` sacrifices bulk *quantity* for
bulk *freshness*, and both for safety-track latency.** Right for collision warning; wrong for HD-map
upload.

## 5. Where shed PCloud goes

Shedding drops objects two ways, and a shed object is **never delivered** to the relay or
subscribers:

1. **Age-based DELIVERY_TIMEOUT (standard MoQ, draft-14 §10.4.3).** An object still in the app send
   buffer past its 1.0 s timeout is dropped *before* being handed to QUIC — never sent
   (`objectsShedStale`). An object that has *already* been handed to QUIC and then times out causes
   its subgroup **stream to be RESET** (`resetAfterSend`): the reset discards the bytes still queued
   in QUIC, stops retransmission, and tells the receiver to drop the partial object. A subgroup is
   one stream carrying several objects, so a reset takes the rest of that subgroup with it.
2. **Send-buffer overflow eviction** (a finite-buffer safeguard, *not* a MoQ mechanism, reported
   separately as `quicShed`): when the app buffer overflows it evicts the oldest object of the
   lowest-priority track (PCloud first), resetting its stream if it had begun transmitting.

At the tight operating window most PCloud takes path 1 and is dropped in the app buffer before it is
ever sent, because QUIC is backpressured and the buffer fills faster than it drains. So yes — **at
128 kB the majority of PCloud is never sent** (~77% shed). That is by design: partial reliability
protects the safety track by sacrificing the bulk track. The point-cloud segmentation into 8
independently-usable 37.5 KB segments is what makes this tolerable — the ~23% that *is* delivered is
still a usable (sparser) point cloud, not a corrupt blob.

## 6. The 128 kB choice, validated against 300 kB

The single-seed sweep (§1) suggested any window in 64–300 kB meets the deadline, with 300 kB looking
like the top of a "safe zone" (46 ms / 4.5% miss). **A 5-seed re-run of both operating points
refutes that** — the single-seed 300 kB point was an optimistic draw:

| operating point | urban BBox miss (5-seed) | urban BBox latency | urban PCloud goodput |
|---|---|---|---|
| **128 kB (canonical)** | **0.2 ± 0.4%** | 33 ± 3 ms | 4.2 Mbps |
| 300 kB (tested) | **9.5 ± 4.6%** | 90 ± 82 ms | 6.9 Mbps |

Across seeds, 300 kB misses the safety deadline ~9.5% of the time with an enormous latency CI
(±82 ms, straddling the 100 ms line) — because 300 kB sits one step below the ~350 kB knee, and radio
fading tips individual seeds over it. 128 kB, well below the knee, is stable (0.2 ± 0.4%). The same
holds on the highway (128 kB: 19% miss; 300 kB: 36%).

So the choice is **not** a free safety-vs-throughput trade among equals. **300 kB buys ~2.7 Mbps of
bulk goodput at the cost of a safety track that misses ~1 in 10 deadlines with wild variance — not
viable for a safety-critical stream.** 128 kB is the operating point: BDP-anchored (~75 kB BDP), far
from the knee, and the only tested window that holds the deadline with a tight CI. (The 64 kB extreme
is safer still, 0.0%, but starves the bulk track at 3.7 Mbps; 128 kB is the balance.)

This is also a clean methodology lesson (A3.3 in ISSUES-AND-LIMITS): the single-seed sweep's 4.5%
became 9.5 ± 4.6% across five seeds — fine claims about the near-knee region cannot rest on one run.
See the "operating point: 128 kB vs 300 kB" chart (both 5-seed).

## 7. Relation to the research questions

- **RQ1 (design):** a MoQ-over-5G V2X design must size the transport buffer near the BDP. Partial
  reliability layered on a deep buffer achieves nothing — measured, not assumed (2 MB: 82% miss
  *with* shedding enabled).
- **RQ3 (which use cases benefit):** MoQ suits a **small, high-rate, latency-critical** stream
  sharing a congested uplink with **bulk** traffic. It delivers the safety track at ~31 ms / 0% miss
  while sacrificing the bulk track — right for collision warning, wrong for HD-map upload. A workload
  with no deadline and a hard no-loss requirement should not use this configuration.
- **RQ2:** see `mqtt-vs-moq.md`. MoQ's advantage over MQTT is *conditional on the bounded window* —
  at a default window it is no better than the reliable baseline, and loses to nothing but its own
  queue.

## 8. Caveats

Single seed per window point. The knee runs (350/400/450 kB) fill the transition region the earlier
version flagged as unmeasured, and they are monotonic and consistent — but the *exact* knee position
(300 vs 350 kB) and the residual non-monotonicity at 512 kB–2 MB (stochastic channel: log-normal
shadowing, Jakes fading, mid-run handover) still need repetitions with different seeds before any
fine claim about the transition goes in the thesis. The reliable curve is complete for
64–300 kB + 1 MB; its 512 kB and 2 MB points did not finish (no-shedding backlogs run so slowly they
hit the wall-clock limit — itself a finding about unbounded reliable buffering).
