# Groups, subgroups and RESET_STREAM — what it changed, and an unwelcome result

Scenario: urban grid, 1 publisher car, 7 subscriber cars, relay on the edge server.
`BBox` = 50 B / 100 ms, priority 0, deadline 100 ms, delivery timeout 200 ms (safety-critical).
`PCloud` = 300 KB / 200 ms, priority 1, deadline 500 ms, delivery timeout 1.0 s (bulk).
Full-length runs (200 s), default 2 MB QUIC flow-control window.

---

## 1. What was implemented

**MoQ object model (draft-14 §2.2–2.4).** Objects now carry `groupId` and `subgroupId`, and a
Subgroup maps onto exactly one QUIC stream. A per-track `objectsPerGroup` sets the group size —
BBox uses 10 (1 s of data per stream), PCloud uses 1 (each point cloud is an independent
random-access frame). Previously every object of a track shared one long-lived stream and
`groupId` was hardcoded to 0.

**RESET_STREAM (RFC 9000 §19.4).** INET's QUIC did not implement this frame at all. Added it:
frame type, send/receive paths, retransmission on loss, final-size flow-control accounting, and
a socket API + callback. This is what MoQ §10.4.3 requires when an object exceeds its delivery
timeout.

**Priority in the transport (RFC 9000 §2.3, MoQ §7.2).** QUIC has no priority mechanism of its
own and leaves scheduling to the implementation; INET only had round-robin, so MoQ priority
stopped at our app buffer and never reached the wire. Added a `PriorityScheduler` plus a
per-send priority API.

**Delivery timeout applied to written-but-undelivered objects.** The timeout previously only
inspected the app send buffer. With a deep transport buffer an object leaves that buffer long
before it ages out, so the timeout never fired. A periodic sweep now also resets the subgroup
stream of objects already handed to QUIC.

## 2. Relation to the research questions

- **RQ1 (design a system integrating MoQ with V2X):** this is the MoQ side of the design made
  faithful. Without subgroup→stream mapping and RESET_STREAM, MoQ's partial reliability cannot
  be expressed at all, and priority is inert below the application.
- **RQ3 (which V2X use cases are boosted):** the result below speaks directly to this, and it is
  a caution rather than a boost.
- **RQ2 (vs MQTT/TCP/UDP):** not addressed by this change. See §5.

## 3. Data

Mechanism works: the resets fire and are received.

| | `MOQ_SW` (reliable) | `MOQ_Partial` (timeout shedding) |
|---|---|---|
| publisher subgroup stream resets | 0 | **242** |
| relay `objectsResetByPublisher` | 0 | **242** |

But the outcome on the safety-critical track is bad:

| car[5], BBox (deadline 100 ms) | `MOQ_SW` | `MOQ_Partial` |
|---|---|---|
| objects received | 206 | **46** |
| mean latency | 1.086 s | 0.994 s |
| deadline-miss ratio | 85.0% | **82.6%** |

Publisher accounting, `MOQ_Partial` (of 410 BBox objects offered):

| | BBox | PCloud |
|---|---|---|
| offered | 410 | 206 |
| sent | 94 | 204 |
| shed stale (before first byte) | 316 | 0 |
| reset after send | 41 | 201 |

**Reading:** delivery-timeout shedding destroyed the BBox track (206 → 46 objects delivered, a
78% drop) and bought essentially nothing in return — mean latency fell 1.086 s → 0.994 s and the
deadline-miss ratio barely moved, 85.0% → 82.6%. PCloud was reset almost in its entirety (201 of
204 sent).

## 4. Why — and it is not a defect in the mechanism

The mechanism is doing exactly what the spec says. The problem is what it is being applied to.

BBox's end-to-end latency (~1.0 s) is roughly **5× its 200 ms delivery timeout**, and PCloud's
(~4.6 s) is ~5× its 1.0 s timeout. Essentially *every* object ages out. The timeout therefore
does not select stale objects from fresh ones — it condemns all of them. Shedding converts
lateness into loss without removing the lateness, because the latency lives in the transport and
RAN queues (the 2 MB flow-control window is ~1.3 s of standing queue at the ~12 Mbps this link
achieves) and the queue refills as fast as it is drained.

A second effect amplifies the damage on BBox: with `objectsPerGroup = 10`, resetting one
subgroup's stream abandons up to 9 further objects with it. That is why 41 resets produce 316
additional shed objects — consistent with §10.4.3, which abandons the stream, not the object.

**The finding, stated plainly:** MoQ's delivery timeout cannot repair a bufferbloated transport.
It is a mechanism for discarding *the minority of objects that are stale*, and it presupposes
that the queue is shallow enough that most objects are not. Applied to a deep queue it degrades
into near-total loss with no latency benefit. This is consistent with the draft's own warning in
§3.6.1 (bufferbloat causes "head-of-line blocking and latency, even when there is no packet
loss"), and it implies queue depth must be fixed *before* partial reliability can help.

## 5. Open issues and suggested course of action

**Blocker: the shallow-buffer regime stalls the publisher.** The natural contrast run (256 kB
window, 32 kB send-queue limit) — where the timeout *should* shine — does not produce usable
data: of 412 BBox objects offered, only 8 were sent, ~370 remained stuck in the send buffer, and
subscribers received nothing. Until this is understood we cannot evaluate MoQ's partial
reliability in the regime where it is supposed to work. I would investigate this next; it is
also the same regime where `MOQ_SW` previously died with `FLOW_CONTROL_ERROR`, so the two are
plausibly the same underlying defect in the send path.

**Options for the thesis framing** (your call):

- **(a) Report it as a negative/conditional result.** "MoQ partial reliability is necessary but
  not sufficient: it requires a transport queue sized near the BDP, otherwise it trades delivery
  for nothing." This is defensible and interesting, and it directly answers RQ3 by bounding when
  MoQ helps. It needs the shallow-buffer stall fixed to show the positive half of the contrast.
- **(b) Tune the timeouts to the achieved latency** rather than to the application deadline
  (e.g. BBox timeout 1.5 s). This would make shedding selective again, but it concedes that the
  100 ms deadline is unreachable in this configuration, which is arguably the real finding.
- **(c) Reduce `objectsPerGroup` for BBox to 1** to stop one reset destroying ten objects. Cheap,
  and worth doing regardless, but it does not address the underlying lateness.

**Gap against RQ2:** the comparison set is currently MoQ vs TCP vs UDP. RQ2 explicitly names
**MQTT, over both QUIC and TCP**, and no MQTT implementation exists in this codebase. That is a
substantial piece of missing work and should be scoped deliberately.

---

## Glossary

| | |
|---|---|
| **BBox** / **PCloud** | the 50 B / 100 ms safety track and the 37.5 kB x 8 / 500 ms bulk track |
| **BDP** | Bandwidth-Delay Product — capacity x RTT |
| **MOQ_SW** | "small window" config — bounded flow-control window, **reliable** baseline (no shedding) |
| **MOQ_Partial** | bounded window **plus** delivery-timeout shedding |
| **RAN** | Radio Access Network |

Full list: [`GLOSSARY.md`](GLOSSARY.md)
