# MoQ partial reliability under 5G V2X congestion — findings

Scenario: one publisher car sends two tracks through a relay to ~7 subscriber cars.
`BBox` = 50 B every 100 ms, priority 0, deadline 100 ms (safety-critical).
`PCloud` = 300 KB every 200 ms, priority 1, deadline 500 ms (bulk).
Configs: `MOQ_SW` (bounded QUIC window, fully reliable) vs `MOQ_Partial` (same window +
MOQ delivery-timeout shedding: BBox 200 ms, PCloud 1.0 s).

---

## 1. MOQT has no priority-based shedding (spec)

We assumed the standard let a sender drop low-priority objects to protect high-priority ones.
It does not. In draft-ietf-moq-transport-14 (2 September 2025):

- **Priorities (§7, scheduling algorithm in §7.2)** govern *transmission order only* — subscriber
  priority, then publisher priority, then group order, then object id.
- **DELIVERY TIMEOUT (§9.2.1.2)** is the only dropping primitive: it is **age-based** and applies
  **uniformly regardless of priority**. On expiry the publisher MUST reset the transport stream
  (the obligation is stated in §9.2.1.2; the reset mechanism and its error code are in §10.4.3).

**This was acted on.** The model did carry a priority-ordered send-buffer eviction for a time; it
has been removed in favour of the teardown the draft actually specifies for a full queue —
PUBLISH_DONE with TOO_FAR_BEHIND (§9.2.1.2, code in §9.12). See `moq-operating-envelope.md` §7.

Consequence: a priority-eviction heuristic would be a *departure* from MOQT, not conformance to
it. Track protection has to emerge from priority *scheduling* plus age-based shedding, or not at
all. Everything below stays inside the standard.

## 2. The original result was inverted, and it was our bug, not the spec's

Before the fix, `MOQ_Partial` shed **10 BBox and 0 PCloud** — it dropped the track it was meant to
protect. Root cause chain:

- `MoqPublisherApp::doSendQuic` handed each 300 KB object to QUIC in **one** `socket.send()`.
- INET QUIC's `sendQueueLimit` is **connection-wide** and its admission is **binary**: over the
  limit it sets `acceptDataFromApp = false` and **silently drops every subsequent write**, on all
  streams (`Connection::newStreamData`).
- With `sendQueueLimit = 100 KB`, one 300 KB object overshoots 3× and locks *every* stream out —
  BBox included — until ACKs drag occupancy below the low-water mark (80 KB): ≈150–200 ms at the
  ~12 Mbps this link achieves. That is exactly BBox's 200 ms delivery timeout, so BBox aged out
  and was shed, while PCloud (1.0 s timeout, never blocked by anything) never tripped its own.

What was *not* wrong: INET QUIC already round-robins between streams per stream-frame
(`RRScheduler`), so a small BBox write on its own stream is **not** head-of-line blocked behind
PCloud's bytes. The transport was fine; the app was stuffing it.

**Fix:** write objects to QUIC in 16 KB chunks, paced against QUIC's true send-queue occupancy, so
the backlog stays in the app's priority-ordered buffer — where priority and delivery timeouts can
act. This is MOQT's own sender model.

## 3. Three latent bugs found on the way

- **The app could not observe QUIC's queue.** The existing `estQueueBytes` estimate only ever
  *grew*: bytes leave QUIC's queue on ACK, which the app never sees. It self-corrected only by
  accident, because every 300 KB object overshot the limit and forced a full/drain cycle. Chunking
  removed the overshoot and the publisher gated itself into a **permanent stall** (1 PCloud + 2
  BBox objects sent, total). Fixed by exposing real occupancy from the transport
  (`Quic::getSendQueueLength`) and making the drain indication fire on every downward crossing of
  the low-water mark, not only after a full condition.
- **`QuicSocket::send()` is asynchronous.** QUIC does not enqueue the bytes until it processes the
  message in a *later event*, so reading the queue right after a write returns a stale value. A
  first attempt at pacing read a flat ~79 KB while pushing far past the limit → **2703 rejected
  (silently dropped) writes**. Fixed by counting bytes written within the current event.
- **The relay was silently losing data.** Its `socketMsgRejected` was an empty override — rejected
  writes were not even counted — and its forward queue was a plain FIFO that dropped the *oldest*
  object regardless of track. Now priority-ordered, paced, and counted.

**Framing landmine (unfixed, load-bearing invariant):** `MoqFraming::tryParse` trusts a 4-byte
length prefix with no sync word and no bounds check. A *partially* written object followed by more
data on the same stream desyncs the receiver permanently and can drive an out-of-bounds read. The
code therefore only ever sheds objects that have **not started transmitting** (`sentOffset == 0`).
Implementing MOQT's RESET_STREAM properly would need real chunk framing — INET QUIC has no
RESET_STREAM at all.

Post-fix gates (both configs): `quicSendRejected = 0` on publisher and relay; peak QUIC occupancy
99 108 B against a 100 000 B limit; no stall (410/410 BBox, 202/207 PCloud reach QUIC); relay
shedding lands **entirely on PCloud** (21 objects, 0 BBox).

## 4. Partial reliability works, and the benefit grows with congestion

Offered-load sweep, demand = 7 subscribers × PCloud × 8 / 0.2 s. **5 seeds per point**
(mean ± stdev); link saturates around 65 Mbps.

| PCloud | demand | BBox deadline-miss SW → Partial | BBox mean latency SW → Partial | PCloud gap-loss SW → Partial |
|---|---|---|---|---|
| 50 KB | 14 Mbps | 0.028 → 0.028 ±0.01 | 0.049 → 0.049 s | 0.000 → 0.000 |
| 100 KB | 28 Mbps | 0.436 → 0.436 ±0.02 | 0.082 → 0.082 s | 0.000 → 0.000 |
| 150 KB | 42 Mbps | 0.285 → 0.284 ±0.02 | 0.099 → 0.099 s | 0.000 → 0.000 |
| 200 KB | 56 Mbps | 0.612 → 0.596 ±0.02 | 0.224 → 0.211 s | 0.000 → 0.004 |
| 250 KB | 70 Mbps | 0.802 → 0.765 ±0.04 | 0.449 → 0.420 s | 0.000 → 0.068 |
| **300 KB** | **84 Mbps** | **0.840 → 0.706** ±0.09 | **0.543 → 0.371 s** | 0.000 → 0.214 |

The intended trade is clean and **monotonic in load**: the more congested the link, the more
partial reliability buys. At full saturation BBox deadline-miss falls **84.0% → 70.6%** and BBox
mean latency falls **0.543 s → 0.371 s (−32%)** — paid for with PCloud loss that rises exactly in
step (0 → 21.4%). Below saturation the configs are identical, which is correct: nothing goes
stale, so nothing is shed.

Shedding engages only as the link saturates (Partial, mean objects): publisher sheds 4.6 BBox /
0.2 PCloud at 200 KB, rising to 19.5 / 6.2 at 300 KB; relay sheds 1.4 → 150, essentially all
PCloud. `quicSendRejected = 0` at every point — no silent loss anywhere.

Note the reliable baseline's `gapLossRatio` is **0.000 at every load**: `MOQ_SW` really is lossless,
and *all* of `MOQ_Partial`'s loss is deliberate. That is the cleanest possible statement of the
trade.

**Retracted:** single-seed data suggested a congestion collapse (throughput falling 66 → 60 Mbps
past saturation). With 5 seeds it does not survive — `MOQ_SW` throughput rises and flattens
(63.4 → 66.5 Mbps). `MOQ_Partial` delivers *less* raw throughput at saturation (61.1 Mbps) simply
because it is intentionally dropping bulk data. There is no collapse.

## 5. The structural limit: the app can only shed what it still holds

Of BBox's 0.64 s mean end-to-end latency at 300 KB, the publisher's app buffer accounts for
0.067 s and the relay's for 0.048 s. The remaining **~0.53 s sits below the app** — in QUIC's send
queue and the 5G radio stack (PDCP/RLC/MAC), a FIFO the application cannot reorder or drop from.

This bounds what app-level partial reliability can ever achieve: its effectiveness depends on
*where the bottleneck queue forms*. Pull the transport buffer shallower and more of the backlog
becomes sheddable; leave it deep and MoQ's delivery timeouts are shouting at a queue they cannot
reach. We chose to report this rather than tune it away.

## 6. Measurement fixes (both applied)

- **`lossRatio` was contaminated by late join.** It is computed from object 0, so a SUMO car that
  spawns mid-run counted every object published before it existed as "lost" — ~38% apparent loss
  even at 14 Mbps on an idle network. Added **`gapLossRatio`** (holes *between* the first and last
  object a subscriber actually saw). The new metric shows the fully-reliable baseline at **0.000
  loss at every load**, which proves the old ~38% was pure artifact. Use `gapLossRatio`.

  | PCloud | old `lossRatio` | `gapLossRatio` |
  |---|---|---|
  | 50 KB | 0.380 | 0.000 |
  | 150 KB | 0.387 | 0.000 |
  | 250 KB | 0.418 | 0.068 |
  | 300 KB | 0.493 | 0.214 |

- **One replication per point was not enough.** Now 5 seeds per point (`repeat = 5`,
  `seed-set = ${repetition}`). This changed conclusions: it killed the apparent congestion collapse
  (§4) and turned a noisy-looking BBox curve into a clean monotonic one.

## 7. Open: the 100 KB anomaly

BBox deadline-miss is **non-monotonic between 100 KB and 150 KB** — 0.436 at 100 KB but 0.285 at
150 KB, i.e. *more* offered load produces *fewer* deadline misses. With 5 seeds the stdev is only
±0.02, so this is **systematic, not noise**, and it is unexplained. Both configs show it
identically, so it is not a shedding effect — it is something in the transport or radio (plausibly
a QUIC pacing/cwnd interaction, or handover timing coupling with the 0.2 s send interval).

Deliberately left unexplained for now — **carry it into the Discussion chapter** rather than the
results: state it as an observed non-monotonicity in the operating region below saturation, note
that it is seed-stable and config-independent (so not an artifact of shedding), and offer the
transport/radio interaction as the likely cause. A reviewer will spot the kink in the curve, so it
is better named than glossed.

## Other open items

- One `Sweep_Partial` run at 300 KB produced no subscriber scalars (4 seeds, not 5, at that point).
  Worth re-running to confirm it is not a systematic failure at max load.
- Chart BBox deadline-miss vs offered load with error bars.
- If more BBox protection is wanted, the remaining spec-faithful lever is a **priority scheduler**
  in QUIC: INET ships only round-robin, and `IScheduler` already has the hook
  (`// TODO: create Scheduler depending on ned file parameter`). MOQT §7.2 send-order would be a
  genuine contribution — but see §5: it cannot touch the radio queue either.
