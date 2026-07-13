# Protocol comparison: MoQ vs MQTT vs TCP vs UDP (RQ2)

Urban grid, 1 publisher car, 7 subscriber cars, edge relay/broker, 200 s runs. Identical
scenario, workload, offered load and burst shape across every config; only the protocol changes.

`BBox` = 50 B / 100 ms, deadline 100 ms (safety-critical).
`PCloud` = one LiDAR sweep per 200 ms as 8 x 37.5 KB segments, deadline 500 ms (bulk).

The MQTT and MoQ subscribers share one statistics implementation (`SubscriberStats.h`), so
latency, loss and deadline-miss are computed by the same code on both sides.

**Delivery ceiling:** subscriber cars spawn over the first 35 s and cannot receive what was
published before they existed. **~62% is full delivery, not 100%.**

**Validity gate:** every run below reports `quicSendRejected = 0`. This matters — see §5.

---

## 1. Results

### BBox (safety-critical, 100 ms deadline)

| config | delivered | mean latency | deadline miss |
|---|---|---|---|
| **MoQ partial reliability, 128 kB window** | **62.3%** (ceiling) | **37 ms** | **0.7%** |
| MoQ / QUIC (default 1 GB window) | 62.1% | 1030 ms | 76.8% |
| MoQ / QUIC, bounded 2 MB window | 60.1% | 1030 ms | 77.8% |
| MoQ partial reliability, 2 MB window | 59.6% | 955 ms | 81.6% |
| MQTT / QUIC | 51.1% | 2784 ms | 89.2% |
| MoQ / UDP | 49.4% | 1319 ms | 93.2% |
| MoQ / TCP | 12.4% | 22 148 ms | 100% |
| MQTT / TCP | 11.6% | 26 137 ms | 100% |

### PCloud (bulk, 500 ms deadline)

| config | delivered | mean latency | deadline miss |
|---|---|---|---|
| MoQ partial reliability, 128 kB window | 22.1% | 956 ms | 99.6% |
| MoQ / QUIC (default) | 57.4% | 3639 ms | 69.7% |
| MoQ partial reliability, 2 MB window | 55.4% | 2727 ms | 70.7% |
| MQTT / QUIC | 51.1% | 2742 ms | 60.5% |
| MoQ / UDP | 43.5% | 1077 ms | 51.5% |
| MoQ / TCP | 12.7% | 22 198 ms | 99.9% |
| MQTT / TCP | 11.5% | 26 051 ms | 100% |

## 2. The headline is narrower than "MoQ wins"

**Exactly one configuration in the matrix meets the safety deadline: MoQ with partial reliability
*and* a bounded transport buffer (0.7% miss at 37 ms).** Everything else — including MoQ over QUIC
at its default window — misses 77-100% of BBox deadlines.

MoQ at a default window performs essentially the same as the fully reliable baseline (1030 ms,
~77% miss). The protocol's mechanisms only pay off once the transport queue is shallow enough
that delivery-timeout shedding can actually reach the backlog. **Partial reliability is necessary
but not sufficient; it must be paired with a queue near the bandwidth-delay product.** That is
the bufferbloat result (see `moq-operating-envelope.md`), now confirmed across the whole matrix
rather than inferred from one protocol.

The gap decomposes cleanly into three layers:

- **TCP is catastrophic** (22-26 s, 100% miss, ~12% delivered). One ordered byte stream means a
  37.5 KB PCloud segment head-of-line-blocks a 50 B BBox message, and neither MoQ nor MQTT can
  do anything about it from above.
- **QUIC alone buys ~8-20x** (26.1 s -> 2.8 s for MQTT; 22.1 s -> 1.0 s for MoQ) by removing
  cross-stream head-of-line blocking. Still 10-28x short of the 100 ms deadline.
- **MoQ's application-layer mechanisms buy the remaining ~30x**, but only in the bounded-window
  configuration.

## 3. Why MQTT cannot close the gap — three named mechanisms

**(a) No per-message priority.** MQTT has none. Both topics share one connection, so BBox queues
behind PCloud. MoQ carries each subgroup on its own QUIC stream and passes publisher priority
down to the transport's stream scheduler.

**(b) Expiry cannot express the deadline, and cannot reach the backlog.** The Message Expiry
Interval is a four-byte integer **in seconds** (v5.0 §3.3.2.3.3), so a 100 ms deadline is not
expressible; 1 s is the protocol's minimum. And [MQTT-3.3.2-5] only requires the broker to delete
a message that expires *before onward delivery starts*, with [MQTT-3.3.2-6] confirming the clock
counts time waiting **in the server**. Our broker forwards on receipt, so `objectsExpired = 0` in
every run: **MQTT's only discard mechanism is inert**, because the backlog lives in the transport
where MQTT cannot reach it. MoQ's `DELIVERY_TIMEOUT` is in milliseconds and, via `RESET_STREAM`,
reaches data already handed to QUIC.

**(c) No way to abandon in-flight data.** MQTT has no `RESET_STREAM` counterpart. Once delivery
starts, the message must be delivered in full however stale. MQTT converts congestion into
unbounded latency; MoQ converts it into *chosen* loss on the track that can absorb it.

## 4. What MQTT is good at — worth saying

Over QUIC, MQTT delivered ~51% of both tracks with no protocol-level loss: it loses nothing, it
is merely late. For telemetry with no deadline and a hard no-loss requirement, MQTT's unbounded
buffering is a **feature**. MoQ's advantage exists only where a deadline exists, and it is paid
for in bulk-track loss (PCloud falls to 22% delivered in the winning config, and effectively never
meets its own 500 ms deadline). That is the right trade for collision warning and the wrong one
for HD-map upload — which is the direct answer to RQ3.

## 5. Validity: the silent-loss gate

An earlier version of this comparison was **wrong**, and the way it was wrong is worth recording.

INET's QUIC silently discards writes once its send queue is full. Both the MQTT publisher and
(later) the MoQ publisher were overrunning that limit and losing data invisibly:

- MQTT/QUIC first appeared to achieve 34 ms and 0% deadline miss — because ~85% of the offered
  load was being thrown away at the sender. The broker had received 911 of ~10 000 messages.
- MoQ then appeared to achieve 27 ms and 3% miss — because it was discarding 3468 writes. It was
  caught only because MoQ's delivery ratio (31.5%) was implausibly *worse* than MQTT's (51.1%)
  for a 50-byte track.

Root cause in both: the flush routine ran once per object, and a group burst emits several objects
in one event, so a per-call byte tally let each call overshoot QUIC's limit afresh while the
transport still reported its stale pre-event queue length.

**Every config in this table now reports `quicSendRejected = 0`.** Any future run must be gated on
that scalar: it is what distinguishes a real result from a protocol that merely looks fast because
it discarded the load.

## 6. Caveat on MQTT-over-QUIC

MQTT-over-QUIC is **not standardised**. We carry the MQTT byte stream over a single QUIC stream in
place of TCP, which is the accepted approach. MQTT therefore gains QUIC's loss recovery but still
uses one ordered stream — it cannot exploit QUIC's independent streams, because MQTT's framing
assumes a single ordered byte stream. That limitation is inherent to MQTT, not to our
implementation.

MQTT clients are given a large transport send buffer (100 MB). MQTT has no backpressure signal to
the application and no shedding, so it relies on the transport to buffer, exactly as over TCP. A
bounded QUIC send queue would not model MQTT — it would convert MQTT's buffering into invisible
loss.
