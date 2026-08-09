# Groups, subgroups and RESET_STREAM — what was implemented

**Scope: this is an implementation record, not a results document.** The measurements it
originally carried were taken under a configuration this project no longer uses — a monolithic
300 KB PCloud object, a 2 MB flow-control window, and `objectsPerGroup = 10` on BBox — and have
been removed. Results now live in [`moq-operating-envelope.md`](moq-operating-envelope.md) and
[`delivery-timeout-enforcement.md`](delivery-timeout-enforcement.md).

Each track now maps onto its own subgroup and therefore its own stream, which is the point of
having a priority at all: streams exist so that tracks can be scheduled and abandoned
independently. Batching a protected track's objects into a shared subgroup defeats that by
construction, so the loss-amplification effect the old measurements characterised is designed out
rather than tuned around.

---

## 1. What was implemented

**MoQ object model (draft-14 §2.2–2.4).** Objects now carry `groupId` and `subgroupId`, and a
Subgroup maps onto exactly one QUIC stream. A per-track `objectsPerGroup` sets the group size;
BBox uses 1, so every safety object owns its stream and can never be abandoned as collateral of
another object's reset. Previously every object of a track shared one long-lived stream and
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
- **RQ3 (which V2X use cases are boosted):** these mechanisms are what makes the RQ3 trade
  expressible at all; the measured answer is in [`moq-operating-envelope.md`](moq-operating-envelope.md).
- **RQ2 (vs MQTT/TCP/UDP):** not addressed by this change. See [`mqtt-vs-moq.md`](mqtt-vs-moq.md).

## 3. Where the results went

The measurements originally reported here (reset counts, per-car BBox delivery, and the
publisher's shed/reset accounting) were taken under the obsolete configuration described at the
top and have been removed rather than restated: the workload, the window and the group size have
all changed since, so the numbers do not describe the system as it now stands.

The one finding from that work that survives is **that delivery-timeout shedding cannot repair a
bufferbloated transport** — applied to a deep queue it converts lateness into loss without
removing the lateness, because the queue refills as fast as it drains. That finding is now carried
with current evidence by [`moq-operating-envelope.md`](moq-operating-envelope.md), whose 2 MB row
shows 82% deadline miss *with* shedding enabled, and it is the basis of the RQ1 design lesson that
queue depth must be fixed before partial reliability can help.

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
