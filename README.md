# MoQ for V2X over 5G — simulation study

An OMNeT++ simulation comparing **Media over QUIC Transport (MoQ)** against **MQTT v5.0**, plain
**TCP** and **UDP** for vehicle-to-infrastructure communication over a congested 5G NR uplink.

Publisher (vehicle) → edge relay/broker → subscribers (vehicles), over Simu5G 5G NR, with SUMO
vehicle mobility fed in through Veins.

**Results and conclusions: [`findings/REPORT.md`](findings/REPORT.md).** Everything below is how to
reproduce them.

---

## Requirements

This project **requires forked versions** of INET, Simu5G and Veins. It will not build or produce
correct results against the upstream releases: the forks carry bug fixes without which the
simulation either does not run or silently produces wrong numbers (see
[Fork changes](#fork-changes)).

| component | repository | notes |
|---|---|---|
| OMNeT++ 6.3.0 | upstream | native install; **do not use `opp_env`** (its nix isolation hides system libs that `libINET.so` needs) |
| INET 4.6.0 | https://github.com/JinghengHuang/custom-inet | adds QUIC `RESET_STREAM`, a priority stream scheduler, and two flow-control fixes |
| Simu5G | https://github.com/JinghengHuang/custom-simu5g | fixes the TR 38.901 rural path-loss model |
| Veins 5.3.1 | https://github.com/JinghengHuang/custom-veins | INET 4.6.0 compatibility |
| SUMO 1.18+ | upstream | |

```bash
mkdir -p ~/thesiswork && cd ~/thesiswork
git clone https://github.com/JinghengHuang/custom-inet.git    inet-4.6.0
git clone https://github.com/JinghengHuang/custom-simu5g.git  simu5g-git
git clone https://github.com/JinghengHuang/custom-veins.git   veins-git
```

Paths are currently hard-coded to `~/thesiswork/{inet-4.6.0,simu5g-git,veins-git}` and
`~/omnetpp/omnetpp-6.3.0` in `scripts/`. Adjust those if you install elsewhere.

## Running

```bash
./scripts/start-services.sh          # starts veins_launchd (the TraCI broker); runs die at t=0 without it
./scripts/run-all.sh results 8       # build everything, run both scenarios x 8 seeds, verify, report
```

`run-all.sh` builds the three dependencies and MoQVeinsSim, runs the protocol comparison in both
scenarios plus the flow-control window sweep, **verifies every run**, and prints the aggregated
tables. Runs execute in parallel (default: half the cores); 56 runs take ~28 min on 32 cores.

Individual pieces:

```bash
./scripts/run-comparison.sh out 8 urban 16    # 7 configs x 8 seeds, urban, 16 in parallel
./scripts/run-comparison.sh out 8 highway 16  # same, highway
python3 scripts/aggregate_seeds.py out        # means with 95% CIs
```

## Verify your results before you believe them

Two failure modes in this stack are **silent** — they produce a plausible-looking result file and
no error. Both invalidated result tables during this project. `run-all.sh` checks for them and
**refuses to print results if either is present**; if you run simulations by hand, check them
yourself.

1. **`quicSendRejected` must be 0.** INET's QUIC *silently discards* application writes once its
   send queue is full — no block, no error. Discarded load **looks like good performance**: the
   data never enters the network, so it never queues, so latency looks excellent while most of the
   payload was binned at the sender. MQTT once "achieved" 34 ms and 0% deadline miss this way,
   while throwing away ~85% of its messages.

2. **The run must reach the time limit.** A simulation that aborts early still writes a `.sca`,
   and its partial data is indistinguishable from a legitimate "delivered nothing" outcome.

## Scenarios

Both run 8 vehicles; one publishes, the rest subscribe. Same workload, same ~12 Mbps offered load
(which saturates the uplink — congestion is the point).

- **Urban** (default): 3×3 SUMO grid, 200 m edges, 50 km/h, `URBAN_MACROCELL` propagation.
- **Highway** (`_HW` config suffix): straight 3 km, 3-lane corridor at 120 km/h, gNodeBs at ¼ and
  ¾ of the corridor so a vehicle hands over exactly once mid-run, `RURAL_MACROCELL` propagation.

**Workload** — two tracks from the publisher:
- `BBox` — 50 B every 100 ms, deadline 100 ms (safety-critical, e.g. collision warning).
- `PCloud` — one LiDAR sweep per 200 ms as 8 × 37.5 KB segments, deadline 500 ms (bulk perception).
  The segment size follows EMP's measured 30–38 KB uploads and ETSI TS 103 324's "independently
  interpretable" CPM segments — see [`findings/pointcloud-segmentation.md`](findings/pointcloud-segmentation.md).

**Key configs** (`MoQVeinsSim/simulations/omnetpp.ini`):

| config | what it is |
|---|---|
| `MOQ` | MoQ over QUIC, default flow-control window |
| `MOQ_SW` / `MOQ_Partial` | bounded window; reliable vs delivery-timeout shedding |
| `MOQ_TCP` / `MOQ_UDP` | the same MoQ application over TCP / UDP |
| `MQTT_TCP` / `MQTT_QUIC` | MQTT v5.0 (QoS 0), broker on the edge server |
| `*_HW` | the same, in the highway scenario |
| `MOQ_Partial_Window` / `MOQ_SW_Window` | flow-control window sweep |
| `MOQ_Partial_MultiPub` | 3 publishers (implemented, **not yet measured**) |

## Architecture

```
car[0] ──── MoQ publisher / MQTT publisher
   │
   │  5G NR uplink (Simu5G) — the bottleneck
   ▼
server ──── MoQ relay / MQTT broker      (edge, behind the UPF)
   │
   │  5G NR downlink, fanned out to every subscriber
   ▼
car[1..7] ─ MoQ subscribers / MQTT subscribers
```

Application code lives in `MoQVeinsSim/src/applications/{moq,mqtt}/`, shared framing and metrics in
`MoQVeinsSim/src/models/`. Both subscribers use the same `SubscriberStats.h`, so latency, loss and
deadline-miss are computed by identical code on both sides of the comparison.

**Veins is used only as the mobility feeder** (SUMO via TraCI). All application traffic goes over
5G NR via Simu5G, so the Veins 802.11p PHY — and its two-ray and obstacle-shadowing models — is not
on the data path. Propagation is Simu5G's `NrChannelModel_3GPP38_901` (3GPP TR 38.901: LOS/NLOS
path loss, log-normal shadowing, Jakes fading), declared explicitly in `omnetpp.ini` rather than
left to defaults.

## Fork changes

Why the upstream releases will not do:

**INET 4.6.0** ([custom-inet](https://github.com/JinghengHuang/custom-inet))
- **`RESET_STREAM` (RFC 9000 §19.4)** — not implemented upstream at all. MoQ requires it to abandon
  an object whose delivery timeout has expired (draft-ietf-moq-transport §10.4.3).
- **Priority stream scheduler** — QUIC defines no priority mechanism (RFC 9000 §2.3 leaves it to
  the implementation); INET shipped only round-robin, so MoQ's send order never reached the wire.
- **Flow-control fix (§4.5)** — `RESET_STREAM` did not release *connection-level* credit for
  abandoned bytes, so the window leaked shut on every reset.
- **Flow-control fix (§4.1, send side)** — send-side accounting was a running byte counter that
  decremented on loss and never re-charged retransmissions, so the sender overran the peer's window
  and the connection aborted with `FLOW_CONTROL_ERROR` at small windows (`inet@e846f96`).
- **Flow-control fix (§4.1, receive side)** — connection-level accounting added *every* received
  stream frame, so a retransmission was counted twice and the effective window decayed until the
  receiver aborted a legal sender with `FLOW_CONTROL_ERROR`. Harmless at a 1 GB window; fatal to the
  bounded-window configs every tuned result depends on (`inet@99fd4c1`).

Not a defect, but also carried by the fork:
- **API extension** — `Quic::getSendQueueLength()` exposes true send-queue occupancy, and the drain
  indication now fires on every downward crossing of the low-water mark rather than only after a
  full condition. An app pacing its writes never trips "full", so a drain-only-after-full signal
  never reaches it, and occupancy cannot be reconstructed from threshold crossings alone because
  bytes leave the queue on ACK, which the app never sees (`inet@31923b9`).

**Simu5G** ([custom-simu5g](https://github.com/JinghengHuang/custom-simu5g))
- **TR 38.901 rural path loss** — `computeRuralMacro` passed the carrier frequency in **Hz** into a
  term the standard specifies in **GHz**, adding a constant **+180 dB** of phantom path loss.
  `RURAL_MACROCELL` produced 252–281 dB and no radio link survived. This blocked the entire highway
  scenario.

**Veins 5.3.1** ([custom-veins](https://github.com/JinghengHuang/custom-veins))
- INET 4.6.0 compatibility.

## Known limitations

- A Simu5G handover race (`NrTxPdcpEntity::deliverPdcpPdu - destination must be a UE`) aborts a
  small number of highway runs. Deterministic per seed and independent of the protocol under test;
  affected runs are excluded and reported. **Not fixed.**
- MQTT is measured at QoS 0 only.
- The window sweep is single-seed.
- `MOQ_Partial_MultiPub` is implemented but unmeasured.

Full list of every issue resolved and every limit of the project:
[`findings/ISSUES-AND-LIMITS.md`](findings/ISSUES-AND-LIMITS.md). Threats to validity are also summarised in
§5 of [`findings/REPORT.md`](findings/REPORT.md).

## Repository layout

```
MoQVeinsSim/src/applications/moq/    MoQ publisher, relay, subscriber
MoQVeinsSim/src/applications/mqtt/   MQTT v5.0 publisher, broker, subscriber
MoQVeinsSim/src/models/              framing, track config, shared subscriber statistics
MoQVeinsSim/simulations/             omnetpp.ini, SUMO scenarios, analysis scripts
scripts/                             run-all.sh, run-comparison.sh, aggregate_seeds.py
findings/                            REPORT.md (start here), GLOSSARY.md, ISSUES-AND-LIMITS.md, supporting studies
design/                              the MoQ and MQTT specifications used as constraints
```
