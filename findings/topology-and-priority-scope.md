# Topology scope: why a single publisher flatters MoQ

**Status: reasoned, not measured.** No experiment was run for this. It is recorded so the
limitation is stated deliberately rather than discovered by a reviewer.

---

## The setup we actually simulated

One publisher car → edge relay → seven subscriber cars, over 5G NR (Uu). Both tracks (`BBox`,
`PCloud`) travel **on a single QUIC connection** from the publisher to the relay.

This models **edge-assisted offload** — vehicle → edge → vehicles — which is a legitimate and
well-cited architecture (EMP, MobiCom '21, is exactly this). It is **not** V2V cooperative
perception, where every vehicle both publishes and subscribes.

The single publisher was chosen for simplicity. It is not a neutral choice: **it happens to be the
case in which MoQ's priority mechanism looks best.**

## The mechanism: MoQ's priority is scoped to a session

The draft is explicit. draft-ietf-moq-transport-14 §7:

> "MoQ priorities allow a subscriber and original publisher to influence the transmission order of
> Objects **within a session** in the presence of congestion."

And on ordering beyond that scope (§7.2):

> "This algorithm does not provide a well-defined ordering for objects that belong to different
> subscriptions or FETCH responses, but have the same subscriber and publisher priority. The
> ordering in those cases is implementation-defined."

Our implementation matches that scope exactly. The `PriorityScheduler` is constructed **per QUIC
connection** and arbitrates over that connection's stream map
(`inet/transportlayer/quic/Connection.cc:73`, over `&streamMap`). It can order *this* sender's
streams against each other. It has no visibility of, and no authority over, any other sender.

## What that means with one publisher vs many

**One publisher (what we simulated).** `BBox` and `PCloud` share one QUIC connection, so they are
streams in the *same* stream map. The priority scheduler can genuinely preempt the bulk track for
the safety track. The mechanism is fully exercised, and it works — this is why MoQ's send order
reaches the wire at all.

**Many publishers (not simulated).** Each vehicle opens **its own QUIC connection** to the relay.
Car A's safety `BBox` and car B's bulk `PCloud` are then in **different stream maps, in different
connections, on different UEs**. Nothing in MoQ can order one against the other:

- MoQ's priority is session-scoped, per the quote above.
- QUIC's priority is per-connection by construction (RFC 9000 §2.3 leaves scheduling to the
  implementation, and an implementation only schedules the streams of a connection it owns).
- The arbitration that *actually* happens is in the **5G MAC scheduler**, which allocates resource
  blocks between UEs and knows nothing about MoQ object priorities.

So in a realistic multi-vehicle deployment, **a vehicle's safety-critical message can queue behind
another vehicle's point cloud, and MoQ has no mechanism to prevent it.** The protection we measured
is protection *against your own bulk traffic*, not against the fleet's.

## Why this is a real limitation, not a modelling artifact

It is not something a better MoQ implementation could fix. It is a property of where the priority
mechanism lives: at the application layer, inside one session. Closing it requires the *network* to
know about the priority — i.e. **RAN-level QoS**: mapping the safety track to a distinct 5QI / QoS
flow with a guaranteed bit rate, so the MAC scheduler itself favours it. That is a 5G mechanism,
not a MoQ one, and it is orthogonal to everything this thesis measures.

This also composes with the other main finding. We showed that the **bounded transport queue**, not
delivery-timeout shedding, is what buys the deadline. Both of those levers are *per-sender*: each
vehicle can bound its own queue and shed its own stale objects. Neither helps when the contention
is *between* vehicles for radio resources. The per-sender levers are necessary but, in a fleet, not
sufficient.

## What an experiment would look like, if it is ever run

The existing `MOQ_Partial_MultiPub` config **cannot be used as-is**: it promotes cars 0–2 to
publishers at the full per-publisher rate, tripling an offered load that already exceeds capacity.
The result would be a degenerate table in which every protocol misses every deadline — informative
about nothing.

The experiment that would isolate the effect is:

- **Hold total offered load constant** and vary only how many senders produce it. E.g. 12 Mbps from
  1 publisher, vs 8 publishers at 1.5 Mbps each, with the same BBox/PCloud split per publisher.
- The only variable is then **whether contention is within a connection or between connections**.
- **Prediction (unverified):** BBox deadline-miss rises with publisher count even at constant total
  load, because MoQ's priority can no longer arbitrate the contention. If BBox performance is
  roughly flat in publisher count, this reasoning is wrong and the RAN scheduler is doing more work
  than expected.

That prediction is falsifiable, which is the point of writing it down.

## What to say in the thesis

Minimum (scoping sentence, mandatory):

> This work models edge-assisted offload (vehicle → edge → vehicles), not V2V cooperative
> perception. All application traffic from a vehicle shares one QUIC connection.

Recommended (the limitation, stated with its mechanism):

> MoQ's priority is scoped to a session (draft-ietf-moq-transport-14 §7), so it arbitrates between
> the streams of one sender. With a single publisher — the topology evaluated here — that is
> sufficient to protect the safety track from the same vehicle's bulk traffic. In a multi-vehicle
> deployment, contention is between vehicles, resolved by the 5G MAC scheduler, which is unaware of
> MoQ priorities. MoQ therefore cannot protect one vehicle's safety data from another vehicle's
> bulk data; doing so would require RAN-level QoS (a distinct 5QI for the safety track). The
> single-publisher topology evaluated here is thus the most favourable case for MoQ's priority
> mechanism, and results should not be extrapolated to fleet-scale contention without that caveat.

## Related

- Sidelink / true V2V mesh (PC5) is a **different system**, with a different bottleneck and radio
  model. D2D is disabled in this project. Not a scenario variant — a different thesis.
- See also `REPORT.md` §5 (threats to validity) and `ISSUES-AND-LIMITS.md` §B1.
