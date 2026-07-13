# MoQ partial reliability: the operating envelope

Urban grid, 1 publisher car, 7 subscriber cars, edge relay. 200 s runs.
`BBox` = 50 B / 100 ms, priority 0, deadline 100 ms, delivery timeout 200 ms (safety-critical).
`PCloud` = 300 KB / 200 ms, priority 1, deadline 500 ms, delivery timeout 1.0 s (bulk).

`DELIVERED` is measured against the publisher's offered count. Subscriber cars spawn over the
first 35 s of the run, so they cannot receive objects published before they existed: **~62% is
the ceiling**, not 100%. A track at ~62% is fully delivered; a track at 0.2% is annihilated.

---

## 1. Two defects fixed first

Neither was a parameter limit; both were ours, and both had to go before any sweep meant
anything.

**Publisher deadlock.** `flushSendBuffer` set `quicBlocked` from its own occupancy estimate, but
that flag is cleared only by QUIC's drain indication — and INET only fires a drain once its queue
has first risen *above* the low-water mark. Our queue peaked at 26042 B against a 26214 B mark,
so the drain never came and the publisher blocked itself for the rest of every run (8 of 412 BBox
objects sent). The estimate now only gates the write loop; `quicBlocked` is driven purely by the
real full/drain callbacks.

**Flow-control leak on reset (RFC 9000 §4.5).** `RESET_STREAM` released stream-level flow-control
credit for abandoned bytes but not connection-level credit, which is otherwise only advanced when
the application pops data — and abandoned bytes are never popped. Every reset therefore burned
connection window permanently. Not the cause of the stall, but it would have deadlocked any
run that reset often enough.

## 2. The dominant lever is queue depth, not shedding

Sweeping the QUIC flow-control window (with `sendQueueLimit` scaled with it):

| window | BBox latency | BBox miss | BBox delivered | PCloud delivered |
|---|---|---|---|---|
| 2 MB | 771 ms | 67.7% | 9.7% | 45.3% |
| 1 MB | 397 ms | 66.4% | 12.3% | 47.0% |
| 256 kB | 32 ms | **0.0%** | 17.5% | 0.4% |
| 128 kB | 30 ms | **0.0%** | 14.9% | 0.2% |
| 64 kB | 29 ms | **0.0%** | 16.3% | 0.2% |

BBox latency collapses ~25× (771 ms → 29 ms) purely by shrinking the transport buffer. A 2 MB
window is ~1.3 s of standing queue at the ~12 Mbps this link achieves, so no amount of
application-layer cleverness can meet a 100 ms deadline behind it. This is the bufferbloat the
MoQ draft warns about in §3.6.1.

## 3. But BBox delivery was still terrible — and that was a config bug

At every small window the safety track was still only ~15% delivered. Diagnosis: BBox's mean
send-buffer dwell was **34 ms**, far under its 200 ms timeout, so those objects had *not* aged
out. They were collateral damage. With `objectsPerGroup = 10`, ten BBox objects shared one stream
(one subgroup), and §10.4.3's reset abandons *the stream* — so each of 40 resets destroyed ~9
further objects. 291 of 411 BBox objects were lost this way.

Setting `objectsPerGroup = 1` on BBox (64 kB window):

| | offered | sent | shed | delivered | latency | deadline miss |
|---|---|---|---|---|---|---|
| **BBox** | 411 | **411** | **0** | **62.3%** (the ceiling) | **30 ms** | **0.0%** |
| PCloud | 208 | 4 | 199 | 0.2% | 879 ms | 100% |

This is the intended result: **the safety-critical track is fully delivered and 100% on time,
while the bulk track absorbs all the loss.** It also beats the fully reliable baseline at the
same window (`MOQ_SW` @64 kB: 60.9% delivered, 64 ms, 16.9% miss), so partial reliability is
adding value rather than merely trading delivery for latency.

**Design lesson (RQ1/RQ3):** subgroup size is a loss-amplification factor. Batching objects onto
a shared stream multiplies the cost of every reset by the group size. A track you intend to
protect must not share a subgroup stream with objects you are willing to abandon.

## 4. Relation to the research questions

- **RQ1 (design):** the MoQ side is now faithful — group/subgroup→stream, RESET_STREAM,
  delivery timeout over both buffered and in-flight objects, priority reaching the transport.
  The two defects above are design findings in their own right: an app must not infer transport
  backpressure, and reset granularity must match the unit you are willing to lose.
- **RQ3 (which V2X use cases benefit):** MoQ helps a **small, high-rate, latency-critical**
  stream sharing a congested uplink with **bulk** traffic — the BBox/PCloud split is exactly
  that shape, and BBox goes from 68% deadline-miss to 0%. It does **not** help if the transport
  queue is deep (§2), and it actively *hurts* the protected track if that track's objects are
  batched into shared subgroups (§3). The bulk track is not "degraded" but effectively
  **sacrificed** (0.2% delivered) — MoQ buys the safety track's deadline by giving up the
  point cloud almost entirely, which is the right trade for collision warning and the wrong one
  for, say, HD map upload.
- **RQ2:** untouched by this work. MQTT is still absent from the comparison set.

## 5. Open issue

`FLOW_CONTROL_ERROR` still kills some sweep points (`MOQ_SW` at 512/256/128 kB, `MOQ_Partial` at
512 kB), so the reliable baseline curve has holes. It predates this work. It should be diagnosed
before the MoQ-vs-MQTT-vs-TCP-vs-UDP comparison is run, or the baseline will be incomplete at
exactly the window sizes we now know matter most.
