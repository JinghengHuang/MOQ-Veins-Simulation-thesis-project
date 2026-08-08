# Issues resolved, and limits of the project

Two lists. **Part A** is every substantive bug/defect fixed, with what it would have done to the
results had it not been. **Part B** is what the project does *not* establish — the material for a
Threats to Validity / Limitations section.

---

# Part A — Issues resolved

## A1. Bugs in third-party simulators (upstream defects)

These were not our code. Each silently corrupts results, and each was found by chasing an
implausible number rather than by anything failing loudly.

### A1.1 Simu5G: TR 38.901 rural path loss used Hz where the standard specifies GHz
`NrChannelModel_3GPP38_901::computeRuralMacro` passed `carrierFrequencyHz_` into the RMa `PL1`
term, which TR 38.901 Table 7.4.1-1 defines with f_c **in GHz**. A factor of 10⁹ inside a
`20·log10(…)` added a constant **+180 dB** of phantom path loss: the model returned 252–281 dB at
50–1000 m, where no radio link survives past ~160 dB.

- **Effect:** `RURAL_MACROCELL` was completely unusable — no packet ever got through. This blocked
  the *entire highway scenario*.
- **Evidence it was a bug:** the NLOS branch of the same function already used the GHz form, as did
  urban macro, urban micro and indoor; the corrected values track free-space loss within ~2 dB; and
  ns-3's `ThreeGppRmaPropagationLossModel::Pl1` computes the same term as `frequency / 1e9 / 3.0`.
- **Fixed:** `simu5g@efbfa4c6`.

### A1.2 INET QUIC: `RESET_STREAM` was not implemented at all
RFC 9000 §19.4 defines it; INET's QUIC had no such frame, no send/receive path, and no socket API.

- **Effect:** MoQ's delivery timeout (draft-ietf-moq-transport §10.4.3) *requires* a stream reset
  to abandon a stale object. Without it, partial reliability cannot be expressed at all.
- **Fixed:** `inet@eb2454c` (frame type, send/receive, retransmission, flow-control accounting,
  `QuicSocket::resetStream()` + callback).

### A1.3 INET QUIC: no stream priority scheduler
QUIC defines no priority mechanism of its own — RFC 9000 §2.3 explicitly leaves it to the
implementation and says one SHOULD be offered. INET shipped only round-robin.

- **Effect:** MoQ's send order (§7.2) stopped at our application buffer and never reached the wire;
  a 50 B safety message got exactly the same service as a 37.5 KB bulk segment.
- **Fixed:** `inet@eb2454c` (`PriorityScheduler` + per-send priority on `QuicSocket`).

### A1.4 INET QUIC: `RESET_STREAM` leaked connection-level flow-control credit
RFC 9000 §4.5 requires the receiver to account for abandoned bytes via `final_size`. Our reset path
released *stream*-level credit but not *connection*-level credit, which INET only advances when the
application pops data — and abandoned bytes are never popped.

- **Effect:** every reset permanently burned connection window. It never reopened, the sender's
  queue stopped draining, and the connection deadlocked. Any reset-heavy run would eventually stall.
- **Fixed:** `inet@2c1ce6f`.

### A1.5 INET QUIC: send-side flow control decremented on loss (`FLOW_CONTROL_ERROR`)
RFC 9000 §4.1 charges flow control against the **highest offset sent**, so a retransmission consumes
no new credit. INET instead kept a running byte counter that was *decremented* on loss and never
re-charged when the data was resent (its own source carried the TODO: *"How to count retransmitted
data?"*).

- **Effect:** after enough radio losses the sender credited itself window it had already spent, sent
  past the receiver's advertised limit, and the receiver aborted the connection with
  `FLOW_CONTROL_ERROR`. This killed multiple runs at small flow-control windows — precisely the
  operating point the study depends on, so the reliable baseline had holes exactly where it mattered.
- **Fixed:** `inet@e846f96`.

## A2. Bugs in our own application code

### A2.1 Publisher deadlocked itself by inferring transport backpressure
`flushSendBuffer` set `quicBlocked` from its *own* occupancy estimate, but that flag is only ever
cleared by QUIC's drain indication — and INET only fires a drain once its queue has first risen
*above* the low-water mark. Our queue peaked at 26042 B against a 26214 B mark, just under it.

- **Effect:** the drain never came and the publisher blocked itself for the remainder of every run
  (8 of 412 objects sent).
- **Lesson (design finding):** an application must not infer transport backpressure; it must be told.
- **Fixed:** `d867ca9`.

### A2.2 Per-call instead of per-event byte accounting → 3468 silently discarded writes
`flushSendBuffer` runs once per object, and a group burst emits 8 objects in one event. Each call
reset its in-flight byte tally to zero while QUIC still reported its stale pre-event queue length,
so one event could hand QUIC ~8× its limit — and everything past the limit was **silently
discarded**.

- **Effect:** MoQ appeared to achieve **27 ms / 3% deadline miss** — because it was throwing most of
  the load away. Caught only because MoQ's delivery ratio (31.5%) was implausibly *worse* than
  MQTT's (51.1%) for a 50-byte track.
- **Fixed:** `27f2b77`.

### A2.3 Same class of bug in the MQTT publisher
The MQTT publisher wrote straight to QUIC with `socketMsgRejected` as a no-op.

- **Effect:** MQTT/QUIC appeared to achieve **34 ms / 0% deadline miss** — while discarding ~85% of
  its messages. The broker had received 911 of ~10 000 published.
- **Fixed:** in `af497d3` and hardened in `60fd3b1`.

### A2.4 Delivery timeout scoped only to the app buffer
The timeout inspected objects still in our send buffer, but not objects already written to QUIC.
With a deep transport buffer an object leaves the app buffer long before it ages out.

- **Effect:** the timeout never fired in the default configuration — the mechanism was inert exactly
  where it was needed.
- **Fixed:** `a43d4a4` (periodic sweep over written-but-outstanding objects, resetting their stream).

### A2.5 Subgroup size amplified loss on the protected track
A `RESET_STREAM` abandons *the stream*, i.e. the whole subgroup. With `objectsPerGroup = 10`, ten
BBox objects shared one stream, so one stale object destroyed nine others.

- **Effect:** **291 of 411** BBox objects lost as collateral of 40 resets — the mechanism was
  destroying the very track it was meant to protect.
- **Lesson (design finding):** a track you intend to protect must not share a subgroup with objects
  you are willing to abandon.
- **Fixed:** `5d23916`.

### A2.6 Metrics that could not see the loss they were measuring
`lossRatio` counted from object 0, charging every subscriber for objects published before its car
spawned. `gapLossRatio` (added to fix that) counted only gaps *between* the first and last object
received — so a track dropped almost in its entirety showed **0% loss**.

- **Effect:** a PCloud track at 0.5% delivered was reported as lossless. This actively misled the
  analysis until delivery was re-measured against the publisher's offered count.
- **Fixed:** `2a75ee1`, and `scripts/aggregate_seeds.py` now measures delivery against `objectsOffered`.

### A2.7 MQTT expiry measured from the wrong clock
We computed Message Expiry from the *publisher's* timestamp, which includes network transit.
MQTT v5.0 [MQTT-3.3.2-6] counts time waiting **in the server**.

- **Effect:** unfairly expired 474 messages MQTT would legitimately have delivered.
- **Fixed:** in `af497d3`.

### A2.8 MQTT transport buffer sized for the BDP instead of the session
MQTT never sheds, so its backlog grows with *session length × excess offered load*. The highway
publisher tenders ~135 MB over its ~90 s on the road; the 100 MB buffer overflowed.

- **Effect:** silent discards on the highway (`quicSendRejected = 2`) — small, but the same failure
  class.
- **This is also a finding, not just a bug:** MoQ runs the same workload in a **128 kB** buffer.
  MQTT's memory requirement is unbounded in principle.
- **Fixed:** `60fd3b1`.

### A2.9 A non-conformant dropping policy stood in for a specified one
The publisher and relay each bounded their send buffer with a **priority-ordered eviction**: on
overflow, drop the oldest object of the lowest-priority track. That is not a MoQ behaviour. Priority
governs transmission *order* only (draft-14 §7.2), and the draft's only dropping primitive is the
age-based DELIVERY_TIMEOUT. The effect was that a track configured with **no** delivery timeout —
which §9.2.1.2 says delivers every object — became quietly lossy instead of failing loudly, and did
so in a priority-aware way that happened to protect the safety track.

- **Effect:** on the highway the "reliable" baselines silently shed ~35% of the bulk track
  (`MOQ_SW_BDP` ~1,250 objects/run, `MOQ_QUIC` similar). Urban never triggered it, so the headline
  urban results were unaffected. Compounding this, collateral from an overflow-triggered stream
  reset was counted under `objectsShedStale`, the counter used to report *conformant* shedding —
  1,809 objects across the three affected configs, including 667 in a config with no delivery
  timeout configured at all, where that counter should have been unreachable.
- **Fixed:** the accounting split in `26817f2`; the mechanism replaced in `8478887` with the
  teardown the draft actually specifies — PUBLISH_DONE with **TOO_FAR_BEHIND** (§9.2.1.2, code in
  §9.12), applied at both hops, with the relay propagating TRACK_ENDED downstream.
- **This is a finding, not just a bug:** with the conformant behaviour, reliable MoQ **cannot
  sustain the highway workload for the publisher's time on the road** (~88 s: 3 km at 33.3 m/s). It
  terminates in all 5 seeds — 63.0 ± 2.1 s (`MOQ_SW_BDP`), 68.2 ± 1.9 s (`MOQ_QUIC`),
  65.4 ± 3.6 s (`MOQ_SW_BDP_300`) — losing the last quarter to third of the session and delivering
  fewer safety objects than partial reliability (1680 ± 443 vs 2173 ± 461). Partial reliability
  produces for the full window; its single teardown (seed 4, 88.8 s) lands *after* production
  finished at 88.0 s and costs nothing. Urban never reaches the limit in any config or seed — at
  ~41 s of road time the backlog cannot reach 2000 objects. Survival time is roughly linear in
  `sendBufferLimit`, so quote it with the buffer size; the robust claim is the ordering, not the
  seconds. See `moq-operating-envelope.md` §7.
- **Metric hazard this introduced:** `delivered%` is `objectsReceived / objectsOffered`, and a
  terminated config stops offering — so the denominator truncates and the ratio flatters whichever
  config gave up earliest (`MOQ_QUIC` reports 67.5% BBox delivered against `MOQ_Partial_BDP`'s
  35.2%, on 66.4 s of production against 88.0 s). Compare absolute counts, not ratios, whenever
  survival times differ.

## A3. Methodology defects (how results were being validated)

### A3.1 No silent-loss gate
INET's QUIC discards writes when its send queue fills — no block, no error, just a callback an app
may ignore. **Discarded load looks like good performance**: the data never enters the network, so it
never queues, so latency looks excellent. This produced **two entirely bogus result tables** (A2.2,
A2.3), neither of which failed loudly.

- **Fixed:** every run now gates on `quicSendRejected == 0`; `run-all.sh` refuses to print results
  otherwise.

### A3.2 No completion check → crashed runs counted as data
A simulation that aborts early still writes a plausible `.sca`. Two highway runs that died at
t≈16–19 s were folded in as "delivered nothing" data points, and the aggregator then dropped them
from latency/miss — **survivor bias**, silently flattering TCP/UDP.

- **Fixed:** runs must reach the time limit; aborted runs are quarantined as `.sca.aborted`.

### A3.3 Single-run results on a stochastic channel
The channel model has log-normal shadowing and Jakes fading, and vehicles hand over mid-run.
Single runs cannot support a claim.

- **Effect:** with n=1 we credited delivery-timeout shedding for the deadline result. With 5 seeds
  and CIs, shedding and the reliable baseline are **statistically indistinguishable** on the safety
  track (33 ± 3 ms / 0.2 ± 0.4% vs 39 ± 8 ms / 0.8 ± 1.5%). The conclusion was wrong.
- **Fixed:** `01b7e11` — all headline results are 5 seeds, mean ± 95% CI.

### A3.4 Scenario named "Highway" was an urban grid
The network was called `Highway` but the SUMO scenario was a 3×3 grid with 200 m edges at 50 km/h.
The README also claimed two-ray interference and obstacle shadowing — Veins 802.11p PHY features
that are **not on the data path** (Veins is only the mobility feeder; all traffic goes over 5G NR).

- **Fixed:** `9b8ab87` — a real highway scenario was built, the propagation model and gNodeB
  positions were declared explicitly rather than inherited (they had been coming from the *icon
  coordinates in the NED display string*), and the false README claim was removed.

---

# Part B — Limits of the project

What the study does **not** establish. These belong in a Threats to Validity section.

## B1. Scenario and workload scope

| limit | detail |
|---|---|
| **The headline deadline result is urban-only** | MoQ meets the 100 ms safety deadline in the urban grid (33 ± 3 ms, 0.2% miss) but **not on the highway** (102 ± 42 ms, **19.2% miss**). The protocol *ordering* holds in both; the absolute claim does not. This is the single most important caveat. |
| **The bulk track is sacrificed, not served** | Protecting BBox necessarily starves PCloud: at the operating window MoQ delivers only ~23% of PCloud, and PCloud misses its own 500 ms deadline ~99% of the time at **every** window where BBox is safe — the link is over capacity for *both* tracks, so no configuration serves both. MoQ's value is therefore **directing the unavoidable loss by policy** (safety protected, bulk degraded gracefully and controllably), *not* serving the workload — where TCP HOL-blocks both and a deep-buffer QUIC drowns the safety stream. Right for a safety stream + degradable bulk; a use case needing the full point cloud *on time* needs more capacity (spectrum, fewer subscribers, lower bulk rate), not a better protocol. See [`moq-operating-envelope.md`](moq-operating-envelope.md). |
| **Two scenarios, both synthetic** | 8 vehicles each. No dense-traffic, multi-cell, or mixed-mobility case. |
| **One workload shape** | One publisher, two tracks (small/critical + bulk). The RQ3 claim rests on this being representative of the pattern it describes. |
| **Priority's scope: measured on the downlink, unevaluated on the uplink** | MoQ's priority is session-scoped (draft §7) — scoped to one *connection*, not to one publisher. The relay serves each subscriber over a single session carrying every publisher's tracks, so on the bottleneck downlink priority arbitrates across publishers, and the sweep shows it working: at 4 publishers under constant total load it holds BBox latency to **178 ms against round-robin's 538 ms** and roughly **triples on-time delivery** (0.330 vs 0.109). It does **not** restore single-publisher performance — 55% of deadlines are still missed (87% without) — but the sweep varies streams-per-connection and transmitting-UE count together, so the residual cannot be attributed to either. On the publisher→relay **uplink** each vehicle holds its own connection on its own radio and arbitration falls to the 5G MAC scheduler, which is unaware of MoQ priorities; that leg is not the bottleneck here and no experiment isolates it. See the REPORT "RQ3 (extended)" section and [`topology-and-priority-scope.md`](topology-and-priority-scope.md). |
| **Load scaling: measured to 4 publishers, urban only** | `PubScale_N{1..4}` (round-robin and priority, 5 seeds each) were run — BBox degrades super-linearly with publisher count (Chart 8). Still a limit: single scenario (urban), single window (128 kB), and only up to 4 publishers. |
| **MQTT at QoS 0 only** | QoS 1 and 2 unmeasured. Defensible — over TCP, QoS 0 already inherits reliable delivery, and QoS 2's four-way handshake is not used in V2X telemetry — but it is a scope limit, not a result. |
| **Window sweep is single-seed** | The 128 kB operating point, which every tuned result depends on, is asserted from one run per window. Endpoints (2 MB vs 64–128 kB) differ by an order of magnitude and are safe; the 512 kB–1 MB transition region is noisy, non-monotonic, and **no claim is made about it**. |

## B2. Known defects not fixed

| limit | detail |
|---|---|
| **Simu5G handover crash** | `NrTxPdcpEntity::deliverPdcpPdu - destination must be a UE` — a handover/attach race in Simu5G's Binder. Aborts a small number of highway runs; **deterministic per seed** and **independent of the protocol under test** (it struck `MOQ_TCP` seed 3 and `MOQ_UDP` seed 4 by chance of draw). Affected runs are excluded and reported (n = 7 instead of 8 for those rows). Judged not cost-effective to fix. |

## B3. Modelling assumptions (deliberate simplifications)

| assumption | consequence |
|---|---|
| **Idealised backhaul** | 10 G Ethernet from gNodeB → UPF → server, so the **radio is the only bottleneck**. Deliberate — it isolates the phenomenon under study — but it means no result speaks to backhaul-constrained deployments. |
| **Zero relay processing delay** | The edge relay/broker forwards instantaneously. No compute cost is modelled. |
| **Perfect clock synchronisation** | Latency is measured on the global simulation clock. No clock skew. |
| **Zero-filled payloads, no codec** | Objects carry no real content and have **no inter-object dependencies**, so a lost object causes no error propagation. **Loss is a transport-level ratio, not a perceived-quality claim.** |
| **No background/cross traffic** | The MoQ/MQTT tracks are the only load on the cell. |
| **No D2D/sidelink** | Pure V2I over Uu. |
| **`fcsMode`/`crcMode = "declared"`** | No bit-error corruption is computed; frames are marked correct/incorrect analytically (standard INET practice). |
| **Delivery ceiling ≈ 62% (urban)** | Subscriber cars spawn mid-run and cannot receive objects published before they existed. **Compare configs against each other, not against 100%.** |
| **Highway tail: far-end coverage collapse + handover ping-pong** | Past gNodeB2 a vehicle's SINR falls toward the noise floor and the serving cell oscillates between the two gNodeBs (~23 switches/run; confirmed by a `servingCell`/SINR trace, seed 0). This is load-bearing on connection-oriented transports specifically: the single publisher's *uplink* QUIC connection stalls when it enters this region (~48.5 s), starving **all** subscribers at once, whereas connectionless UDP trickles on. Read highway results as a good-coverage window plus a coverage-loss tail — the tail reflects handover instability, not steady behaviour. The *initial* mid-corridor handover succeeds; only the far-end region ping-pongs. Not a UDP-vs-QUIC confound (it truncates QUIC's window yet QUIC still wins). |

## B4. Protocol-fidelity gaps (things the spec has that we do not implement)

MoQ (draft-ietf-moq-transport-14):

| not implemented | why it does not affect the results |
|---|---|
| `FETCH` / repair, relay caching, `MAX_CACHE_DURATION` | For deadline-bound V2X data, a repair path that delivers *after* the deadline is worthless. Their absence is a **consequence of the workload**, not a gap. |
| Session lifecycle: `SETUP`, `SUBSCRIBE_UPDATE`, `UNSUBSCRIBE`, `PUBLISH_DONE`, error paths, `GOAWAY`, `TRACK_STATUS` | Setup/teardown machinery; none changes steady-state behaviour under congestion. Note `SUBSCRIBE_UPDATE`'s absence does mean **priorities and timeouts are static for a session**. |
| Namespace discovery (`SUBSCRIBE_NAMESPACE`) | Subscribers are configured with the publisher's namespace a priori. |
| Object status codes, subscription filters | Loss is inferred from object-ID gaps; subscriptions effectively "start from now". |
| Datagram forwarding preference | Our UDP config is a *custom* protocol, not MoQ-over-datagram. |
| MoQT wire format (varint headers) | We use a custom fixed-width framing. **Do not make byte-overhead claims from this model.** |
| Subgroups carrying layered/temporal structure | One subgroup per group. Group order and the §7.2 tie-break steps 3–4 are therefore never exercised. |

MQTT (v5.0): QoS 2, retained messages, wills, session state, auth, topic aliases, wildcard
subscriptions. None affects steady-state latency or throughput.

**MQTT-over-QUIC is not standardised.** We carry the MQTT byte stream over a single QUIC stream in
place of TCP — the accepted approach — so MQTT gains QUIC's loss recovery but still cannot exploit
independent streams. That limitation is inherent to MQTT's framing, not to our implementation.

## B5. Reproduction hazards (for anyone re-running this)

1. **The three forks are mandatory.** Upstream INET/Simu5G will not produce correct results — see
   A1. In particular, stock Simu5G cannot run the highway scenario at all.
2. **`quicSendRejected` must be 0** on every run, or the numbers are meaningless (A3.1).
3. **Every run must reach the time limit**, or crashed simulations enter the dataset as data (A3.2).
4. **Do not use `opp_env`** — its nix isolation hides system libraries that `libINET.so` requires.

`scripts/run-all.sh` enforces 2 and 3 and refuses to print results otherwise.

---

## Glossary

| | |
|---|---|
| **BBox** / **PCloud** | the 50 B / 100 ms safety track and the 37.5 kB x 8 / 500 ms bulk track |
| **BDP** | Bandwidth-Delay Product — capacity x RTT; the queue depth that keeps a link busy without adding standing delay |
| **MOQ_SW** | "small window" config — bounded QUIC flow-control window, **reliable** baseline (no shedding); `_BDP` sizes that window at the BDP |
| **RMa** | Rural Macro — TR 38.901 propagation scenario, used by the highway scenario |
| **SINR** | Signal-to-Interference-plus-Noise Ratio |
| **NED** | NEtwork Description — OMNeT++'s topology and parameter language |

Full list: [`GLOSSARY.md`](GLOSSARY.md)
