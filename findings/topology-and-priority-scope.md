# Topology scope: where MoQ's priority has authority, and where it does not

**Status: partly measured.** The publisher-count sweep (`PubScale_N{1..4}[_Prio]`, 5 seeds,
`results/pubscale/`) has been run and is the evidence for §3–§4. The uplink claim in §5 remains
**reasoned, not measured** — no experiment isolates it.

> **This document was rewritten after the sweep.** An earlier version claimed that MoQ's priority
> loses authority as soon as there is more than one publisher. That is wrong, and the measurement
> says so: at four publishers the priority scheduler cuts BBox latency from 538 ms to 178 ms and
> triples on-time delivery. The claim has been narrowed to the leg it actually applies to.

---

## 1. The setup we simulated

One or more publisher cars → **edge relay** (`server`, `MoqRelayApp`) → subscriber cars, over 5G NR
(Uu). This models **edge-assisted offload** — vehicle → edge → vehicles, the EMP (MobiCom '21)
architecture. It is **not** V2V cooperative perception, where every vehicle both publishes and
subscribes, and it is not sidelink.

Two legs, and they are not symmetric:

| leg | connections | who arbitrates |
|---|---|---|
| publisher car → relay (**uplink**) | one QUIC connection *per publisher*, on its own UE | the 5G MAC scheduler, between UEs |
| relay → subscriber car (**downlink**) | **one** QUIC connection per subscriber, carrying every publisher's tracks | the relay's MoQ session: app-level priority buffer + QUIC stream scheduler |

The bottleneck in every configuration we run is the **downlink**: offered load is ~82 Mbps against
the ~66 Mbps cell ceiling (~1.24×), and each object is fanned out to every subscriber.

## 2. The mechanism: session scope is per connection, and the relay is one endpoint

The draft is explicit — draft-ietf-moq-transport-14 §7:

> "MoQ priorities allow a subscriber and original publisher to influence the transmission order of
> Objects **within a session** in the presence of congestion."

Our implementation matches that scope. `PriorityScheduler` is constructed per QUIC connection and
arbitrates over that connection's stream map (`inet/transportlayer/quic/Connection.cc:73`).

**The consequence that the earlier version of this document missed:** "one session" is not the same
as "one publisher". The relay keeps **one socket per subscriber** (`subscriberSockets[sid]`,
`MoqRelayApp.cc:220`) and forwards *every* publisher's objects onto streams of that one connection,
into a send buffer keyed by object priority (`st.buffer[item.priority]`, `MoqRelayApp.cc:517`). So on
the downlink, all N publishers' BBox and PCloud objects land in a single MoQ session with a single
scheduler. Cross-publisher arbitration is squarely inside MoQ's scope there, at both the application
and the transport layer.

Multiple publishers at the application layer does **not** imply multiple sessions at the bottleneck.

## 3. Measured: priority does arbitrate across publishers

`PubScale_N{1..4}` holds total offered load constant (each publisher's PCloud object scaled 64000/N B,
so every N delivers 2.56 MB/s per subscriber) and varies only the publisher count. Subscribers are
always cars 4–7, so mobility and handover are identical across N. The `_Prio` variants re-run each
point with `**.quic.streamScheduler = "Priority"`. 5 seeds; aggregated by
`scripts/analyze_pubscale.py`.

| N | scheduler | BBox on-time | BBox miss | BBox mean latency |
|---|---|---|---|---|
| 1 | round-robin | 0.686 ± 0.005 | 0.8% ± 0.7 | 47 ± 2 ms |
| 2 | round-robin | 0.371 ± 0.043 | 48.5% ± 7.5 | 129 ± 10 ms |
| 3 | round-robin | 0.141 ± 0.012 | 82.6% ± 1.6 | 347 ± 37 ms |
| 4 | round-robin | 0.109 ± 0.003 | 87.1% ± 0.5 | 538 ± 147 ms |
| 1 | **priority** | 0.686 ± 0.005 | 0.8% ± 0.7 | 47 ± 2 ms |
| 2 | **priority** | 0.452 ± 0.073 | 33.0% ± 21.1 | 83 ± 32 ms |
| 3 | **priority** | 0.353 ± 0.018 | 51.0% ± 2.3 | 118 ± 9 ms |
| 4 | **priority** | 0.330 ± 0.034 | 55.3% ± 4.3 | 178 ± 74 ms |

(95% CIs over 5 seeds. On-time ratio = (received − misses) / expected; it is the headline because
miss ratio alone flatters a run that delivered almost nothing. On-time tops out at ~0.69 even at
N = 1 because subscribers join mid-run — see `ISSUES-AND-LIMITS.md` A2.6.)

**Priority is enforced and effective with multiple publishers.** At N = 4 it holds latency to 178 ms
where round-robin reaches 538 ms, and it roughly **triples** on-time delivery (0.330 vs 0.109). This
is the relay ordering four *different vehicles'* safety objects ahead of four different vehicles'
bulk objects, which is exactly the cross-publisher arbitration the earlier version of this document
claimed was impossible.

At N = 1 the two schedulers are identical, because a 128 kB (BDP-sized) queue already delivers BBox
on time regardless of send order — consistent with `moq-operating-envelope.md`: at the BDP the
*window*, not the scheduler, does the work.

## 4. What is left over, and why it cannot be attributed

Priority recovers most of the fan-in penalty, not all of it. At identical total load, N = 4 with
priority is still 55% miss / 178 ms against N = 1's 0.8% / 47 ms.

**We cannot say what causes the residual.** The sweep changes two things at once:

1. **streams per downlink connection** (2N) — more streams share one congestion window and one
   128 kB connection-level flow-control budget, so BBox's share shrinks even when its *order* is
   right; and
2. **publisher UEs and uplink connections** (N) — N independent radios contending at the MAC, which
   is the effect §5 is about.

The config comment in `omnetpp.ini:604` claims "the ONLY thing that changes with N is the number of
streams multiplexed onto each subscriber's downlink connection." **That is not accurate** — the
number of uplink connections and transmitting UEs changes with N too. Attributing the residual to
session scope would be over-reading the data; attributing it to stream dilution would be equally
unsupported.

## 5. The claim that survives: the uplink leg

**Reasoned, not measured.** On the publisher→relay leg, each vehicle has its own UE and its own QUIC
connection. Car A's safety BBox and car B's bulk PCloud are then in different stream maps, in
different connections, on different radios. Nothing in MoQ orders one against the other:

- MoQ's priority is session-scoped, per §7 above.
- QUIC's priority is per-connection by construction (RFC 9000 §2.3 leaves scheduling to the
  implementation, and an implementation schedules only the streams of a connection it owns).
- The arbitration that actually happens is in the **5G MAC scheduler**, which allocates resource
  blocks between UEs and knows nothing of MoQ object priorities.

So a vehicle's safety message can queue behind another vehicle's point cloud *on the way up*, and
MoQ has no mechanism to prevent it. Closing that requires the network to know the priority — i.e.
**RAN-level QoS**: mapping the safety track to a distinct 5QI / QoS flow with a guaranteed bit rate.
That is a 5G mechanism, not a MoQ one, and orthogonal to what this thesis measures.

In our runs this leg is not the bottleneck (the downlink is), so its contribution is expected to be
small here — which is another reason the §4 residual should not be read as evidence for it.

## 6. The experiment that would isolate it

Vary stream count **without** varying UE count: one publisher emitting 2N tracks at the same total
load (`PubScale_T{2,4,6,8}`, say), so the downlink still carries 2N streams but only one radio is
transmitting.

- If BBox degradation tracks `PubScale_N{1..4}`, the residual is **stream dilution inside the
  downlink connection**, and the uplink/session-scope story contributes nothing measurable here.
- If it is materially flatter, the difference is the **inter-UE** effect of §5, and the gap
  quantifies it.

That is falsifiable either way, which is the point of writing it down.

## 7. What to say in the thesis

Scoping sentence (mandatory):

> This work models edge-assisted offload (vehicle → edge → vehicles), not V2V cooperative
> perception. A vehicle's application traffic shares one QUIC connection to the relay, and the relay
> serves each subscriber over a single connection carrying every publisher's tracks.

The result, stated with its scope:

> MoQ's priority is scoped to a session (draft-ietf-moq-transport-14 §7) — that is, to one
> connection, not to one publisher. Because the edge relay serves each subscriber over a single
> session carrying every publisher's tracks, priority arbitrates across publishers on the
> bottleneck downlink, and the measurements confirm it: at four publishers under constant total
> load, the priority scheduler holds safety-track latency to 178 ms against round-robin's 538 ms and
> triples on-time delivery. It does not restore single-publisher performance (55% of deadlines are
> still missed), but the sweep varies stream count and transmitting-UE count together and so cannot
> attribute the residual. What MoQ demonstrably *cannot* order is traffic on the uplink leg, where
> each vehicle holds a separate connection on a separate radio and arbitration falls to the 5G MAC
> scheduler; protecting one vehicle's safety data from another's bulk data on that leg would require
> RAN-level QoS (a distinct 5QI), not a MoQ mechanism.

## 8. Related

- Sidelink / true V2V mesh (PC5) is a **different system**, with a different bottleneck and radio
  model. D2D is disabled in this project. Not a scenario variant — a different thesis.
- `REPORT.md` §"RQ3 (extended)" and §5 (threats to validity); `ISSUES-AND-LIMITS.md` §B1.
- `moq-operating-envelope.md` for why the 128 kB window, not the scheduler, carries N = 1.
- Chart 8 (`PubScale_BBox_vs_publishers`) in `MOQ.anf`.

---

## Glossary

| | |
|---|---|
| **BBox** / **PCloud** | the 50 B / 100 ms safety track and the 37.5 kB x 8 / 500 ms bulk track |
| **BDP** | Bandwidth-Delay Product — capacity x RTT |
| **D2D** | Device-to-Device (sidelink); not modelled — this study is pure V2I over Uu |
| **RAN** | Radio Access Network |
| **EMP** | *Edge-assisted Multi-vehicle Perception*, MobiCom '21 |

Full list: [`GLOSSARY.md`](GLOSSARY.md)
