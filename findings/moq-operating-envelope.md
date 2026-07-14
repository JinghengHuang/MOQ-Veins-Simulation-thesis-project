# MoQ operating envelope: what actually buys the deadline

Urban grid, 1 publisher car, 7 subscriber cars, edge relay. 200 s runs.
`BBox` = 50 B / 100 ms, priority 0, deadline 100 ms, delivery timeout 200 ms (safety-critical).
`PCloud` = one LiDAR sweep / 200 ms as 8 x 37.5 KB segments, deadline 500 ms, delivery timeout
1.0 s (bulk).

Sweeping the QUIC connection flow-control window, with `sendQueueLimit` scaled alongside it.
`MOQ_Partial` = delivery-timeout shedding; `MOQ_SW` = same window, fully reliable (no shedding).

**Validity:** all 12 runs report `quicSendRejected = 0`, and all 12 complete. Earlier versions of
this sweep were holed by `FLOW_CONTROL_ERROR` and distorted by silent write loss; both are fixed.

**Delivery ceiling:** subscriber cars spawn over the first 35 s and cannot receive what was
published earlier. **~62% is full delivery**, not 100%.

---

## 1. The sweep

### BBox (safety-critical, 100 ms deadline)

| window | `MOQ_Partial` latency / miss / delivered | `MOQ_SW` latency / miss / delivered |
|---|---|---|
| 2 MB | 955 ms / 81.6% / 59.6% | 1030 ms / 77.8% / 60.1% |
| 1 MB | 431 ms / 71.0% / 55.1% | 484 ms / 80.6% / 61.1% |
| 512 kB | 438 ms / 75.4% / 61.4% | 360 ms / 63.2% / 62.0% |
| 256 kB | 49 ms / 3.1% / 62.4% | 69 ms / 11.3% / 62.3% |
| 128 kB | 40 ms / 1.5% / 61.6% | 42 ms / 0.9% / 61.8% |
| **64 kB** | **31 ms / 0.0% / 60.7%** | 36 ms / 0.3% / 54.8% |

### PCloud (bulk, 500 ms deadline)

| window | `MOQ_Partial` latency / miss / delivered | `MOQ_SW` latency / miss / delivered |
|---|---|---|
| 2 MB | 2727 ms / 70.7% / 55.4% | 2587 ms / 72.1% / 53.4% |
| 512 kB | 3642 ms / 65.2% / 53.9% | 3641 ms / 57.8% / 51.3% |
| 256 kB | 967 ms / 99.3% / 33.2% | 8082 ms / 99.4% / 40.3% |
| 128 kB | 966 ms / 99.6% / 22.0% | **14 067 ms** / 99.7% / 28.1% |
| 64 kB | 945 ms / 99.6% / 18.7% | **13 634 ms** / 99.7% / 23.4% |

## 2. Queue depth is what buys the deadline — not shedding

BBox goes from **~81% deadline-miss at 2 MB to 0% at 64 kB**, a ~30x latency collapse
(955 ms -> 31 ms), purely by shrinking the transport buffer. A 2 MB window is ~1.3 s of standing
queue at the ~12 Mbps this link achieves, so no application-layer mechanism can meet a 100 ms
deadline behind it. This is the bufferbloat the MoQ draft warns about in §3.6.1.

**And the reliable baseline gets there too.** `MOQ_SW` at 128 kB reaches 42 ms / 0.9% miss —
statistically indistinguishable from `MOQ_Partial`'s 40 ms / 1.5%. **On the safety track,
delivery-timeout shedding adds almost nothing once the queue is shallow.** This corrects an
earlier claim in this file; the honest reading is that the window does nearly all the work.

Shedding helps only at the *margin* of the envelope: at 256 kB, `MOQ_Partial` reaches 3.1% miss
against `MOQ_SW`'s 11.3%, and at 64 kB it is the only config with a clean 0.0%.

## 3. What partial reliability actually buys: a bounded bulk track

The real difference is on PCloud, and it is dramatic:

| window | `MOQ_Partial` PCloud latency | `MOQ_SW` PCloud latency |
|---|---|---|
| 128 kB | **966 ms** | **14 067 ms** |
| 64 kB | **945 ms** | **13 634 ms** |

Once the window is shallow, the reliable baseline **cannot drop anything**, so the bulk backlog
simply queues: PCloud arrives **14 seconds** late. `MOQ_Partial` sheds stale segments and holds
PCloud at ~1 s — a **14x** reduction — at the cost of delivering less of it (22% vs 28%).

Both miss PCloud's 500 ms deadline ~99.6% of the time, so neither is *useful* for the bulk track
at these windows. But 1 s of bounded staleness is a fundamentally different failure mode from a
14 s unbounded backlog: the latter means the receiver is acting on 14-second-old perception data,
which is worse than having none.

**So the value of MoQ's delivery timeout is bounding staleness under overload, not protecting the
latency-critical track.** The window protects the latency-critical track.

## 4. The cost, stated plainly

Shrinking the window to meet BBox's deadline costs the bulk track:

- PCloud delivery falls from 55.4% (2 MB) to 18.7% (64 kB).
- The publisher sheds 916 of 1584 PCloud segments at 64 kB, and the relay a further 364.
- BBox delivery is untouched throughout (~60-62%, the ceiling), with **zero** BBox shed at every
  window from 512 kB down. The safety track is never sacrificed.

## 5. Relation to the research questions

- **RQ1 (design):** a MoQ-over-5G V2X design must size the transport buffer near the
  bandwidth-delay product. Partial reliability layered on a deep buffer achieves nothing —
  measured, not assumed (2 MB: 81.6% miss *with* shedding enabled).
- **RQ3 (which use cases benefit):** MoQ suits a **small, high-rate, latency-critical** stream
  sharing a congested uplink with **bulk** traffic. It delivers the safety track at 31 ms with 0%
  deadline miss and full delivery. The bulk track is **sacrificed** (18.7% delivered), which is
  the right trade for collision warning and the wrong one for HD-map upload. A workload with no
  deadline and a hard no-loss requirement should not use this configuration at all.
- **RQ2:** see `mqtt-vs-moq.md`. The relevant point from this sweep is that MoQ's advantage over
  MQTT is *conditional on the bounded window* — MoQ at a default window is no better than the
  reliable baseline, and loses to nothing but its own queue.

## 6. Caveats

The 512 kB and 1 MB points are noisy and not monotonic (e.g. `MOQ_Partial` BBox is 431 ms at 1 MB
but 438 ms at 512 kB, and `MOQ_SW` is *better* than `MOQ_Partial` at 512 kB). These are single
runs with a stochastic channel (log-normal shadowing, Jakes fading) and mid-run handovers; the
transition region between "bufferbloated" and "shallow" is where run-to-run variance is largest.
The endpoints (2 MB and 64-128 kB) are separated by an order of magnitude and are not in doubt,
but **any claim about the transition region needs repetitions with different seeds** before it
goes in the thesis.
