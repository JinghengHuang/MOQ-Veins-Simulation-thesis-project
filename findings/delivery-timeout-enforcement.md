# Enforcing DELIVERY_TIMEOUT by age, not by transmit opportunity

**Status: measured.** Highway and urban, 5 seeds each, `MOQ_Partial_BDP` at the 128 kB operating
point. The occupancy traces are single-seed (seed 0) time series; the aggregate figures are 5-seed
means with 95% CIs.

---

## 1. What was done

`MoqPublisherApp` tested an object's delivery timeout only inside `flushSendBuffer`, whose loop
exits as soon as QUIC reports backpressure:

```cpp
while (!quicBlocked && sendBufferCount > 0) {
    if (quicSendQueueLength() + quicBytesThisEvent >= quicSendQueueLimit) break;
```

Two consequences. Staleness was tested only for objects that reached the **head** of the
priority-ordered queue, and only while the transport was **accepting data** — so under heavy
congestion, exactly when the backlog is growing, shedding stopped entirely.

Added `sweepSendBufferTimeouts()`, driven by the existing `deliveryTimeoutCheckInterval` timer
(50 ms). It walks every priority queue and drops each object past its own timeout, resetting the
subgroup stream for any already partially sent
([draft-14 §10.4.3](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-14#section-10.4.3)).
Tracks configured with no timeout are skipped, so the reliable baseline stays lossless by
construction.

## 2. Relation to the research questions

- **RQ3 (what partial reliability buys).** Establishes that delivery-timeout shedding **bounds the
  sender's memory**, which the previous implementation did not demonstrate. Reliable mode has no
  such bound: its buffer grows as session length × excess offered load until it must abandon the
  subscriber (`moq-operating-envelope.md` §7). This is the same formula as the MQTT buffer finding
  in `ISSUES-AND-LIMITS.md` A2.8 — one mechanism explains both.
- **RQ1 (design).** An age-based discard must be driven by a timer, not by the transmit path.
  Enforcing it opportunistically makes it fail under precisely the congestion it exists to handle.
  Same class of defect as A2.4, which scoped the timeout to the app buffer and left it inert when
  the transport buffer was deep.

## 3. Data

### Send-buffer occupancy, max per 10 s bucket (seed 0)

| | t=0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 |
|---|---|---|---|---|---|---|---|---|---|---|
| urban partial | 45 | 54 | 48 | 54 | 47 | – | – | – | – | – |
| highway partial **before** | 54 | 55 | 89 | 120 | 141 | 400 | 520 | 1020 | 1520 | 1530 |
| highway partial **after** | 54 | 55 | 50 | 50 | 50 | 50 | 50 | 50 | 50 | 50 |
| highway reliable (unchanged) | 194 | 465 | 730 | 1055 | 1421 | 1905 | 2001 | 0 | 0 | 0 |

Highway partial before the fix shows a knee, not a linear ramp: flat to ~20 s, then runaway. That
is the sweep starving as the transport blocks more often. After the fix it is flat for the whole
session and indistinguishable from urban.

### Steady-state occupancy matches theory

For an age-based discard the backlog converges to the objects still younger than their timeout:

$$B_{ss} = \sum_i \lambda_i T_i$$

With $\lambda_{\text{BBox}} = 10\ \text{s}^{-1},\ T_{\text{BBox}} = 0.2\ \text{s}$ and
$\lambda_{\text{PCloud}} = 40\ \text{s}^{-1},\ T_{\text{PCloud}} = 1.0\ \text{s}$:

$$B_{ss} = (10)(0.2) + (40)(1.0) = 42\ \text{objects}$$

Measured mean occupancy after the fix: **42.4** (highway), **39.1** (urban). The agreement across
two congestion levels is the evidence that occupancy is now set by the timeout rather than by the
link.

Without a timeout the backlog has no fixed point and grows at the excess rate:

$$\frac{dB}{dt} = \lambda_{\text{offered}} - \lambda_{\text{drain}}
\quad\Longrightarrow\quad
t_{\text{terminate}} \approx \frac{B_{\text{limit}}}{\lambda_{\text{offered}} - \lambda_{\text{drain}}}$$

### Aggregate effect, `MOQ_Partial_BDP`, 5 seeds

| metric | urban | highway before | highway after |
|---|---|---|---|
| PCloud residual in buffer at end | 38 ± 1 | 1053 | **40 ± 0** |
| PCloud shed on timeout | 932 (56%) | 2066 (58%) | **3093 ± 58 (87%)** |
| subscriptions terminated | 0/5 | 1/5 (88.8 s) | **0/5** |
| BBox latency | 33 ± 3 ms | 102 ± 42 ms | 113 ± 50 ms |
| BBox deadline miss | 0.2 ± 0.4% | 19.2 ± 2.4% | 20.5 ± 4.7% |
| PCloud latency | 951 ± 4 ms | 1045 ± 111 ms | 1021 ± 61 ms |

**Reported honestly: the safety track did not improve.** Highway BBox latency moved 102 → 113 ms and
miss 19.2% → 20.5%. The CIs overlap heavily, so this is not a significant change in either
direction — but it is not an improvement, and should not be presented as one. What the fix buys is
memory boundedness and the elimination of the teardown, paid for by shedding more bulk (58% → 87%
of PCloud). Urban is unchanged (33 ± 3 ms / 0.2 ± 0.4%).

For context, the comparison against reliable mode on the highway is unaffected by this and still
favours partial reliability on every safety-relevant axis:

| highway | session | BBox delivered | BBox latency | BBox miss |
|---|---|---|---|---|
| `MOQ_Partial_BDP` | full ~88 s road time | **2184 ± 522** | **113 ± 50 ms** | **20.5 ± 4.7%** |
| `MOQ_SW_BDP` | ends 63.0 ± 2.1 s | 1680 ± 443 | 210 ± 95 ms | 26.7 ± 5.6% |

## 4. Suggested course of action

**Re-runs still outstanding.** Every config carrying `objectDeliveryTimeout` is affected. Done:
`MOQ_Partial_BDP` and `MOQ_Partial_BDP_300`, both scenarios, 5 seeds. Not yet done:

- `PubScale_N{1..4}[_Prio]` — 40 runs; feeds Chart 8 and REPORT.md "RQ3 (extended)".
- `MOQ_Partial_Window` — 6 window points, single seed; feeds §1 of `moq-operating-envelope.md`.

**How to pitch it in the thesis — two options:**

1. *As a design lesson (recommended).* Fold into RQ1 alongside A2.4 and the backpressure lesson:
   "an application-level discard policy must be driven by object age on a timer, because binding it
   to transmit opportunities makes it inert under congestion." Costs a paragraph, and it generalises
   beyond MoQ.
2. *As a measured property of partial reliability.* Put the $B_{ss} = \sum \lambda_i T_i$ result in
   RQ3 as the quantitative statement of what shedding buys — a sender memory bound that is
   independent of session length and congestion, against reliable mode's unbounded growth. Stronger
   claim, but it rests on this fix being correct, so it should cite the occupancy traces.

They are not exclusive; (1) belongs in the implementation/design narrative and (2) in results.

**A caveat to keep.** The bound is on the *application* send buffer only. Objects already handed to
QUIC sit below the app, where only `RESET_STREAM` reaches them and the RAN queue reaches nothing —
see `moq-partial-reliability.md` §5, which places ~0.53 s of BBox latency below the app. Bounding
the app buffer does not bound end-to-end latency.

## 5. Glossary

| Term | Meaning |
|---|---|
| **BBox** | Safety-critical track: 50 B / 100 ms, 100 ms deadline, 200 ms delivery timeout |
| **PCloud** | Bulk track: LiDAR sweep / 200 ms as 8 × 37.5 KB segments, 500 ms deadline, 1.0 s delivery timeout |
| **DELIVERY_TIMEOUT** | MoQ parameter type 0x02; age past which a publisher stops forwarding an object and MUST reset its stream |
| **TOO_FAR_BEHIND** | PUBLISH_DONE status 0x6; the sender's queue for a subscriber exceeded its implementation-defined limit |
| **Send buffer** | Application-level priority-ordered queue in `MoqPublisherApp`, ahead of QUIC's send queue |
| **Backpressure** | QUIC refusing further writes once its send queue is full |
| **Lazy sweep** | The defect fixed here: staleness tested only at the queue head, only while the transport accepted data |
| **BDP** | Bandwidth-delay product; ~75 kB on this link, the basis for the 128 kB operating point |
| **$B_{ss}$** | Steady-state send-buffer occupancy, in objects |
| **$\lambda_i,\ T_i$** | Offered object rate and delivery timeout of track $i$ |

## 6. References

- [draft-ietf-moq-transport-14](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-14),
  2 September 2025 —
  [§9.2.1.2 DELIVERY TIMEOUT](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-14#section-9.2.1.2),
  [§9.12 PUBLISH_DONE](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-14#section-9.12),
  [§10.4.3 Closing Subgroup Streams](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-14#section-10.4.3)
- [RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html) —
  [§2.3 Stream prioritization](https://www.rfc-editor.org/rfc/rfc9000.html#section-2.3),
  [§19.4 RESET_STREAM](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.4)
- Related in-repo: `moq-operating-envelope.md` §7, `moq-partial-reliability.md` §5,
  `ISSUES-AND-LIMITS.md` A2.4 and A2.8

---

## Glossary

| | |
|---|---|
| **BBox** / **PCloud** | the 50 B / 100 ms safety track and the 37.5 kB x 8 / 500 ms bulk track |
| **DELIVERY_TIMEOUT** | per-track deadline past which an undelivered object is abandoned (draft-14 §9.2.1.2, §10.4.3) |
| **`sweepSendBufferTimeouts()`** | the timer-driven sweep added here; walks every priority queue and drops objects past their own timeout |
| **transmit opportunity** | the old enforcement point — inside `flushSendBuffer`, which exits as soon as QUIC reports backpressure, so shedding stopped under exactly the congestion it exists to handle |
| **send-buffer occupancy** | objects held in the publisher's priority-ordered app buffer; the quantity the fix bounds |
| **B_ss = Σ λᵢTᵢ** | steady-state occupancy for an age-based discard — arrival rate × timeout, summed over tracks; 42 objects here |
| **`MOQ_Partial_BDP`** | bounded 128 kB window **plus** delivery-timeout shedding |
| **`MOQ_SW_BDP`** | bounded 128 kB window, **reliable** baseline (no shedding) |
| **TOO_FAR_BEHIND** | PUBLISH_DONE status 0x6 — publisher ends a subscription at its resource limit (draft-14 §9.12) |
| **CI** | Confidence Interval — 95%, n = 5 seeds |

Full list: [`GLOSSARY.md`](GLOSSARY.md)
