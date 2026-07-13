# MoQ vs MQTT v5.0 (RQ2)

Urban grid, 1 publisher car, 7 subscriber cars, edge relay/broker, 200 s runs. Identical
scenario, identical workload, identical offered load and burst shape; only the application
protocol changes. Topics map onto tracks (`car[0]/BBox`, `car[0]/PCloud`).

`BBox` = 50 B / 100 ms, deadline 100 ms (safety-critical).
`PCloud` = one LiDAR sweep per 200 ms as 8 x 37.5 KB segments, deadline 500 ms (bulk).

The MQTT and MoQ subscribers share one statistics implementation (`SubscriberStats.h`), so
"latency", "loss" and "deadline miss" are computed by the same code on both sides.

---

## 1. What was implemented

MQTT v5.0 publisher, broker and subscriber (`src/applications/mqtt/`): CONNECT/CONNACK,
PUBLISH/PUBACK, SUBSCRIBE/SUBACK, DISCONNECT, QoS 0 and 1, and the Message Expiry Interval.
Out of scope, as none affect steady-state latency or throughput: QoS 2, retained messages, wills,
session state, auth, topic aliases, wildcards.

Both topics share one MQTT connection, as a real deployment would. MQTT provides no per-message
priority, so this is not a modelling choice we made — it is the protocol.

## 2. Results

Safety-critical BBox track, at subscriber car[5]:

| protocol | mean latency | deadline miss | messages delivered |
|---|---|---|---|
| MQTT over TCP | **28.1 s** | **100%** | 42 |
| MQTT over QUIC | **3.66 s** | **86.2%** | 203 |
| MoQ partial reliability (128 kB window) | **0.032 s** | **0.0%** | 201 |

Bulk PCloud track:

| protocol | mean latency | deadline miss |
|---|---|---|
| MQTT over TCP | 28.0 s | 100% |
| MQTT over QUIC | 3.61 s | 69.9% |
| MoQ partial reliability | 0.84 s | 83.1% |

Sanity gates: `quicSendRejected = 0` on both MQTT publisher and broker (no silent loss), and the
broker received 2283 of 2289 published messages over QUIC (99.7%), so nothing is being hidden by
discarded writes.

**MoQ delivers the safety track ~110x faster than MQTT-over-QUIC and ~880x faster than
MQTT-over-TCP, and is the only one of the three that meets the 100 ms deadline at all.**

## 3. Why — three named mechanisms, not "MoQ is faster"

**(a) No per-message priority.** MQTT has none. Both topics share one connection, so a 37.5 KB
PCloud segment sits ahead of a 50 B BBox message and delays it. MoQ carries each subgroup on its
own QUIC stream and passes the publisher priority down to the transport's stream scheduler, so
BBox is never queued behind bulk data.

**(b) Expiry cannot express the deadline, and cannot reach the backlog.** The Message Expiry
Interval is a *four-byte integer in seconds* (v5.0 §3.3.2.3.3), so a 100 ms deadline is not
expressible: 1 s is the smallest value the protocol permits. Worse, [MQTT-3.3.2-5] only requires
the broker to delete a message if it expires *before onward delivery starts*; [MQTT-3.3.2-6]
confirms the clock is time waiting **in the server**. Our broker forwards on receipt, so nothing
ever expires — `objectsExpired = 0` in both runs. MQTT's one discard mechanism is inert here,
because the backlog is in the transport, where MQTT has no way to reach it. MoQ's
`DELIVERY_TIMEOUT` is in milliseconds and, via `RESET_STREAM`, can abandon data already handed to
QUIC.

**(c) No way to abandon in-flight data.** MQTT has no counterpart to `RESET_STREAM`. Once the
broker starts sending, the message must be delivered in full, however stale. Everything MQTT
publishes is eventually delivered — 28 s late over TCP. It converts congestion into unbounded
latency, whereas MoQ converts it into *chosen* loss on the track that can absorb it.

**QUIC alone buys about 8x** (28.1 s → 3.66 s) by removing TCP's head-of-line blocking across the
connection. But it is still 36x short of the 100 ms deadline, because the remaining problem is
the application protocol, not the transport. This separates the two contributions cleanly.

## 4. Relation to the research questions

- **RQ2 (does MoQ perform better than MQTT over QUIC/TCP, and general TCP/UDP):** yes, decisively,
  on the latency-critical track, and the gap is attributable to three specific, nameable
  mechanisms rather than to raw speed. Note also *what MQTT is good at*: it delivered essentially
  everything (no loss), which is the right behaviour for telemetry that must not be dropped.
- **RQ3 (which use cases benefit):** MoQ's advantage appears only where a deadline exists. For a
  workload with no deadline and a hard no-loss requirement, MQTT's unbounded buffering is a
  feature, not a defect.

## 5. Caveat to state in the thesis

MQTT-over-QUIC is **not standardised**. We carry the MQTT byte stream over a single QUIC stream
in place of TCP, which is the accepted approach in the literature, but it means MQTT gains QUIC's
loss recovery and connection-level improvements while still using one ordered stream — it cannot
exploit QUIC's independent streams, because MQTT's framing assumes a single ordered byte stream.
That limitation is inherent to MQTT, not an artifact of our implementation.

We also had to give the MQTT clients a large transport send buffer (100 MB). MQTT has no
backpressure signal to the application and no shedding, so it relies on the transport to buffer,
exactly as it does over TCP. With a bounded QUIC send queue, INET's QUIC silently discards writes
once full, which would have made MQTT look artificially good by throwing load away. This is
documented in the config.
