# Glossary

Abbreviations and terms used across this repository's code, configuration and findings
documents. Every external standard is linked directly so any claim made against it can be
checked at source.

Where an "acronym" is officially only a name, that is stated — several of the protocols here
(QUIC, MQTT, INET) have expansions that are historical rather than normative, and writing them
out as though they were current is a small but avoidable error in a thesis.

---

## 1. Protocols and transport

| term | meaning |
|---|---|
| **MoQ** | Media over QUIC — the IETF working group |
| **MOQT** | Media over QUIC Transport — the protocol itself, [draft-ietf-moq-transport-14](https://datatracker.ietf.org/doc/draft-ietf-moq-transport/14/) (2 September 2025) |
| **MQTT** | Historically *MQ Telemetry Transport* (MQ = IBM's MQSeries). [OASIS MQTT v5.0](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html) treats "MQTT" as a name, not an acronym |
| **QUIC** | Originally *Quick UDP Internet Connections*; [RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html) defines QUIC as a name, expressly not an acronym |
| **TCP** | Transmission Control Protocol |
| **UDP** | User Datagram Protocol |
| **IP** | Internet Protocol |
| **SACK** | Selective Acknowledgment ([RFC 2018](https://www.rfc-editor.org/rfc/rfc2018.html)); loss recovery in [RFC 3517](https://www.rfc-editor.org/rfc/rfc3517.html) |
| **RTO** | Retransmission Timeout |
| **RTT** | Round-Trip Time |
| **BDP** | Bandwidth-Delay Product — capacity × RTT. The queue depth that keeps a link fully busy without adding standing delay; oversizing beyond it buys throughput nothing and costs latency directly |
| **MTU** | Maximum Transmission Unit |
| **HOL** | Head-of-Line (blocking) — a stalled item delaying everything queued behind it |
| **QoS** | Quality of Service. In MQTT, the 0/1/2 delivery-assurance levels ([v5.0 §4.3](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901234)); this project measures QoS 0 only |
| **FSPL** | Free-Space Path Loss |
| **DDS** | Data Distribution Service (Object Management Group) |
| **RTPS** | Real-Time Publish-Subscribe, DDS's wire protocol ([OMG DDSI-RTPS](https://www.omg.org/spec/DDSI-RTPS/)). Its `DATA_FRAG` submessage is the model for this project's UDP fragmentation |
| **IETF** | Internet Engineering Task Force |
| **RFC** | Request for Comments — an IETF standards-track or informational document |
| **OASIS** | Organization for the Advancement of Structured Information Standards — publishes MQTT |

## 2. MoQ data model and control

Defined in [draft-ietf-moq-transport-14](https://datatracker.ietf.org/doc/draft-ietf-moq-transport/14/);
section numbers refer to that draft.

| term | meaning |
|---|---|
| **Track** | a named stream of content; contains Groups (§2.1–2.4) |
| **Group** | a random-access point within a Track; contains Subgroups |
| **Subgroup** | an ordered run of Objects carried on **one** QUIC stream (§2.2). This mapping is what makes a stream reset a meaningful unit of loss |
| **Object** | the atomic unit of delivery, with an Object ID within its Subgroup |
| **Track Alias** | compact numeric identifier standing in for the full track name on the wire (§10.1). This project uses a *string* alias instead — see the framing-fidelity note in `MoqFraming.h` |
| **SUBGROUP_HEADER** | per-stream header carrying Track Alias, Group ID, Subgroup ID and Publisher Priority **once** per stream (§10.4.2) |
| **OBJECT_DATAGRAM** | datagram form carrying exactly one whole Object, with no fragmentation defined (§10.3.1) |
| **RESET_STREAM** | QUIC frame ([RFC 9000 §19.4](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.4)) abandoning a stream's in-flight bytes — MoQ's mechanism for dropping a stale Object (§10.4.3) |
| **DELIVERY_TIMEOUT** | per-track deadline past which an undelivered Object is abandoned (§9.2.1.2, §10.4.3) |
| **PUBLISH_DONE** | control message ending a subscription, carrying a status code (§9.12) |
| **TOO_FAR_BEHIND** | PUBLISH_DONE status `0x6` — the subscriber's queue exceeded the publisher's limit |
| **TRACK_ENDED** | PUBLISH_DONE status `0x2` — the track is no longer being published |

## 3. Simulation toolchain

| term | meaning |
|---|---|
| **OMNeT++** | Objective Modular Network Testbed in C++ — the discrete-event simulation kernel ([omnetpp.org](https://omnetpp.org/)) |
| **INET** | the INET Framework — OMNeT++'s TCP/IP and wireless model library ([inet.omnetpp.org](https://inet.omnetpp.org/)). A name, not an acronym |
| **Veins** | Vehicles in Network Simulation — couples OMNeT++ to SUMO ([veins.car2x.org](https://veins.car2x.org/)) |
| **Simu5G** | 5G NR model library for OMNeT++, successor to SimuLTE ([simu5g.org](https://simu5g.org/)) |
| **SUMO** | Simulation of Urban MObility — the road-traffic simulator ([sumo.dlr.de](https://sumo.dlr.de/)) |
| **NED** | NEtwork Description — OMNeT++'s topology and parameter language |
| **Cmdenv** | OMNeT++'s command-line (headless) runtime environment |
| **`.ini` / `.sca` / `.vec`** | configuration file / scalar results / vector results |
| **`.anf`** | OMNeT++ Analysis File — chart and dataset definitions |

## 4. 5G and radio (3GPP)

| term | meaning |
|---|---|
| **3GPP** | 3rd Generation Partnership Project |
| **TR / TS** | Technical Report / Technical Specification |
| **TR 38.901** | 3GPP channel models for 0.5–100 GHz ([spec archive](https://www.3gpp.org/ftp/Specs/archive/38_series/38.901/)). Table 7.4.1-1 holds the RMa path-loss equations |
| **NR** | New Radio — the 5G air interface |
| **gNodeB / gNB** | next generation NodeB — the 5G base station |
| **UE** | User Equipment — the mobile terminal (here, a vehicle) |
| **RAN** | Radio Access Network |
| **UPF** | User Plane Function — the 5G core's data-plane gateway |
| **PDCP** | Packet Data Convergence Protocol |
| **RLC** | Radio Link Control |
| **UM** | Unacknowledged Mode — RLC mode with no retransmission |
| **MAC** | Medium Access Control |
| **PHY** | Physical layer |
| **GTP-U** | GPRS Tunnelling Protocol, User plane — backhaul encapsulation |
| **Uu** | 3GPP designation for the UE-to-RAN radio interface. An interface name, not an acronym |
| **SINR** | Signal-to-Interference-plus-Noise Ratio |
| **LOS / NLOS** | Line of Sight / Non-Line of Sight |
| **RMa** | Rural Macro — TR 38.901 propagation scenario; used by the highway scenario |
| **UMa / UMi** | Urban Macro / Urban Micro; the urban grid scenario uses UMa |
| **PL** | Path Loss |
| **dBP** | break-point distance — where a path-loss model switches slope |
| **FDD** | Frequency Division Duplex |
| **RB** | Resource Block — the NR scheduling unit |
| **BLER** | Block Error Rate |
| **dB / dBm** | decibel (a ratio) / decibel relative to 1 milliwatt (an absolute power) |
| **MEC** | Multi-access Edge Computing — Simu5G modules, not used in this project |

## 5. V2X

| term | meaning |
|---|---|
| **V2X / V2I / V2V** | Vehicle-to-Everything / -Infrastructure / -Vehicle |
| **D2D** | Device-to-Device (sidelink). Not modelled here — this study is pure V2I over Uu |
| **ETSI** | European Telecommunications Standards Institute |
| **CPM** | Collective Perception Message ([ETSI TS 103 324](https://www.etsi.org/deliver/etsi_ts/103300_103399/103324/)) — the standardised sensor-sharing message the BBox track stands in for |
| **CAM** | Cooperative Awareness Message ([ETSI EN 302 637-2](https://www.etsi.org/deliver/etsi_en/302600_302699/30263702/)) |
| **LiDAR** | Light Detection and Ranging |

## 6. Method and results notation

| term | meaning |
|---|---|
| **RQ1 / RQ2 / RQ3** | Research Question 1–3 |
| **CI** | Confidence Interval — headline results are mean ± 95% CI over n = 5 seeds |
| **e2e** | end-to-end |
| **BBox** | bounding box — the 50 B / 100 ms deadline safety-critical track |
| **PCloud** | point cloud — the 37.5 kB × 8 objects / 200 ms, 500 ms deadline bulk track |

## 7. Configuration names in `omnetpp.ini`

Config identifiers compose a base protocol with suffixes.

| suffix | meaning |
|---|---|
| `_TCP` / `_UDP` | that transport substituted for QUIC |
| `_SW` | **small window** — bounded QUIC flow-control window. This is the *reliable* baseline: bounded window, **no** shedding |
| `_Partial` | partial reliability — bounded window **plus** delivery-timeout shedding |
| `_BDP` | window sized at the bandwidth-delay product (128 kB) |
| `_HW` | **highway** scenario (RMa propagation), as opposed to the urban grid |
| `_Window` | the window-size sweep |
| `_MultiPub` | multiple publishers |
| `_300` | 300 kB window variant |
| `_trace` | radio-layer vector recording enabled |

**Two naming hazards worth knowing before reading any results table.** `_SW` reads naturally as
"software" or "sliding window", but means *small window*, and it denotes the **reliable
baseline** — misreading it inverts the central comparison against `_Partial`. `_HW` reads
naturally as "hardware", but means *highway*. Expand both on first use in any prose that cites
a config by name.

---

## Sources

- [draft-ietf-moq-transport-14](https://datatracker.ietf.org/doc/draft-ietf-moq-transport/14/) — MoQ Transport; local copy at `skills/research-developer/constraints/moq-transport.txt`
- [OASIS MQTT v5.0](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html) — local copy at `design/mqtt-v5.0.txt`
- [RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html) — QUIC transport
- [RFC 8900](https://www.rfc-editor.org/rfc/rfc8900.html) — IP Fragmentation Considered Fragile
- [RFC 3517](https://www.rfc-editor.org/rfc/rfc3517.html) / [RFC 2018](https://www.rfc-editor.org/rfc/rfc2018.html) — SACK-based loss recovery
- [3GPP TR 38.901](https://www.3gpp.org/ftp/Specs/archive/38_series/38.901/) — channel models
- [ETSI TS 103 324](https://www.etsi.org/deliver/etsi_ts/103300_103399/103324/) — Collective Perception Service
