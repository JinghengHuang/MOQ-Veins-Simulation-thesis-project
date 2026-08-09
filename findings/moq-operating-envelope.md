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

**It costs about a fifth of bulk delivery.** Shedding delivers less PCloud than the reliable
baseline, but far less than an earlier version of this document claimed. From the 5-seed urban
runs at each operating point (mean ± 95% CI):

| window | `MOQ_Partial` PCloud delivered | `MOQ_SW` PCloud delivered | relative cost |
|---|---|---|---|
| 128 kB | **21.4 ± 1.0%** | 27.2 ± 2.7% | −21% |
| 300 kB | 35.3 ± 3.6% | 42.6 ± 1.4% | −17% |

So the trade is **~1 s of staleness instead of ~14 s, for about a fifth of the bulk objects** —
not the near-total sacrifice previously reported. Stated in one line: **`MOQ_Partial` gives up a
modest share of bulk *quantity* to bound bulk *staleness*, and bounds the sender's memory as a
side effect** (see [`delivery-timeout-enforcement.md`](delivery-timeout-enforcement.md)). That is
a considerably better bargain for collision warning than the earlier framing suggested, and it is
still the wrong one for HD-map upload, where the missing fifth is the whole point of the transfer.

> **Correction.** This section previously reported `MOQ_SW` delivering 59.7% and 60.7% of PCloud
> at 128 kB and 300 kB, against `MOQ_Partial`'s 22.6% and 37.4% — implying shedding cost roughly
> two-thirds of the bulk track. The `MOQ_SW` figures were wrong by about 2×; the single-seed
> sweep, the 5-seed `_BDP` runs and the 5-seed `_BDP_300` runs all agree on ~28% and ~43%. The
> direction of the effect is unchanged; its magnitude was overstated.

## 5. Where shed PCloud goes

There is exactly **one** way an object is dropped, and a shed object is **never delivered** to the
relay or subscribers:

1. **Age-based DELIVERY_TIMEOUT (standard MoQ, draft-14 §9.2.1.2, mechanism in §10.4.3).** An object
   still in the app send buffer past its 1.0 s timeout is dropped *before* being handed to QUIC —
   never sent (`objectsShedStale`). An object that has *already* been handed to QUIC and then times
   out causes its subgroup **stream to be RESET** (`resetAfterSend`): the reset discards the bytes
   still queued in QUIC, stops retransmission, and tells the receiver to drop the partial object. A
   subgroup is one stream carrying several objects, so a reset takes the rest of that subgroup with
   it.

Earlier versions of this model had a second path — a priority-ordered eviction on send-buffer
overflow, reported as `quicShed`. **It has been removed.** It was not a MoQ mechanism: priority
governs transmission *order* only (§7.2), and dropping the lowest-priority object made a track with
no DELIVERY_TIMEOUT — one the draft says delivers every object — quietly lossy. The draft specifies
this case directly (§9.2.1.2): at its resource limit the publisher **MAY terminate the subscription
with TOO_FAR_BEHIND**, which is what the model now does. See §7.

At the tight operating window most PCloud takes path 1 and is dropped in the app buffer before it is
ever sent, because QUIC is backpressured and the buffer fills faster than it drains. Measured at the
128 kB operating point over 5 seeds, of the PCloud objects the publisher generates:

| | never sent | of which, shed on timeout |
|---|---|---|
| urban | **58.5 ± 1.7%** | 56.3 ± 1.7% |
| highway | **88.4 ± 1.5%** | 87.2 ± 1.5% |

So a majority of PCloud never reaches the wire, and on the highway the great majority does not.
That is by design: partial reliability protects the safety track by sacrificing the bulk track, and
it sacrifices more of it exactly where the link is worse.

Two things this figure is *not*. It is not the same as the delivered fraction (21.4% urban) — the
remaining gap is the late-join delivery ceiling and downstream loss, not publisher shedding. And it
is not directly comparable to `MOQ_SW`, which sheds nothing at the publisher and instead converts
the same excess into ~14 s of queueing delay. The point-cloud segmentation into 8 independently
usable 37.5 KB segments is what makes any of this tolerable: what is delivered is a sparser point
cloud, not a corrupt blob.

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

## 7. What a track with NO delivery timeout does at the resource limit

DELIVERY TIMEOUT is optional — draft-14 §9.2.1.2 says the parameter "MAY appear", and spells out the
absent case: "if neither the subscriber nor publisher specifies DELIVERY TIMEOUT, all Objects in the
track matching the subscription filter are delivered as indicated by their Group Order and
Priority." That is exactly the `MOQ_SW_*` / `MOQ_QUIC` reliable baseline, so both arms of the
comparison are conformant modes of operation, differing only in one optional parameter.

The draft also says what happens when that promise cannot be kept. Two sentences later: "If a
subscriber fails to consume Objects at a sufficient rate, causing the publisher to exceed its
resource limits, the publisher MAY terminate the subscription with error TOO_FAR_BEHIND", defined in
§9.12 as "the publisher's queue of objects to be sent to the given subscriber exceeds its
implementation defined limit". `sendBufferLimit` (2000 objects) is that limit.

**The relevant baseline is the publisher's time on the road, not the 200 s sim limit.** The highway
publisher traverses 3 km at 33.3 m/s, so it is present for **~88 s**; the urban one for **~41 s**.
A config that never terminates produces for that whole window and then the vehicle leaves.

**Measured (5 seeds, highway, buffer limit 2000 objects):**

| config | terminates | produced | BBox delivered | BBox latency | BBox miss | PCloud latency |
|---|---|---|---|---|---|---|
| `MOQ_Partial_BDP` (timeout) | **0/5** | **88.0 s** | **2184 ± 522** | 113 ± 50 ms | 20.5 ± 4.7% | 1.02 ± 0.06 s |
| `MOQ_SW_BDP` (reliable) | **5/5, 63.0 ± 2.1 s** | 61.1 s | 1680 ± 443 | 210 ± 95 ms | 26.7 ± 5.6% | 9.28 ± 1.83 s |
| `MOQ_QUIC` (reliable, 2 MB) | **5/5, 68.2 ± 1.9 s** | 66.4 s | 3137 ± 259 | 1633 ± 1142 ms | 93.4 ± 2.3% | 9.11 ± 1.28 s |
| `MOQ_SW_BDP_300` (reliable) | **5/5, 65.4 ± 3.6 s** | 63.5 s | — | — | — | — |
| `MOQ_Partial_BDP_300` (timeout) | 0/5 | 88.3 s | — | — | — | — |

(BBox delivered = objects summed over the 7 subscribers.)

The three reliable configs end the subscription in **every** seed, losing roughly the last quarter to
third of the publisher's road time. Partial reliability never terminates, in either scenario, and
produces for the full window. (Before the delivery-timeout enforcement fix it terminated once, in
highway seed 4 at 88.8 s; with shedding now driven by object age its send buffer is bounded at ~40
objects and the limit is never approached — see `delivery-timeout-enforcement.md`.)

**Urban never reaches the limit** — 0/5 for every config. At ~41 s of road time the backlog has no
time to reach 2000 objects. So the result is scenario-specific: reliable MoQ sustains this workload
in the urban grid and cannot sustain it on the highway.

**Do not compare `delivered%` across configs with different survival times.** It is
`objectsReceived / objectsOffered`, and a config that terminates stops offering — so the denominator
truncates and the ratio *flatters* the config that gave up. `MOQ_QUIC` reports 67.5% delivered
against `MOQ_Partial_BDP`'s 35.2%, while having produced 66.4 s of content against 88.0 s. Use the
absolute counts above, or normalise by road time.

**Read the survival time with its buffer size attached.** It is roughly
`sendBufferLimit / (offered rate − drain rate)`, i.e. close to linear in a parameter chosen for
memory safety, not measured. A larger buffer postpones the teardown; it does not prevent it, because
the offered rate exceeds the drain rate throughout. What is robust to the buffer choice is the
**ordering** — partial reliability produces for the whole road time and reliable does not — not the
specific seconds. A longer road would *not* now expose partial reliability: its occupancy is a fixed
point at $\sum_i \lambda_i T_i \approx 42$ objects, independent of session length.

One caveat on the reliable arm's *latency* numbers: they look better than the previous
eviction-based implementation reported (210 ms vs 479 ms) precisely because the session now ends at
63 s instead of dribbling out a growing backlog. Latency and survival must be read together; neither
alone describes the behaviour.

## 8. Relation to the research questions

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

## 9. Caveats

Single seed per window point. The knee runs (350/400/450 kB) fill the transition region the earlier
version flagged as unmeasured, and they are monotonic and consistent — but the *exact* knee position
(300 vs 350 kB) and the residual non-monotonicity at 512 kB–2 MB (stochastic channel: log-normal
shadowing, Jakes fading, mid-run handover) still need repetitions with different seeds before any
fine claim about the transition goes in the thesis. The reliable curve is complete for
64–300 kB + 1 MB; its 512 kB and 2 MB points did not finish (no-shedding backlogs run so slowly they
hit the wall-clock limit — itself a finding about unbounded reliable buffering).

---

## Glossary

| | |
|---|---|
| **BBox** / **PCloud** | the 50 B / 100 ms safety track and the 37.5 kB x 8 / 500 ms bulk track |
| **BDP** | Bandwidth-Delay Product — capacity x RTT; `_BDP` configs size the window at it (128 kB) |
| **MOQ_SW** | "small window" config — bounded flow-control window, **reliable** baseline (no shedding) |
| **MOQ_Partial** | bounded window **plus** delivery-timeout shedding |
| **CI** | Confidence Interval (95%) |

Full list: [`GLOSSARY.md`](GLOSSARY.md)
