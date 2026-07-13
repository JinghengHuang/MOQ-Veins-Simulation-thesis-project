# Should PCloud be one 300 KB object, or a group of segments?

## The question

Our bulk track models a LiDAR point cloud as **one 300 KB MoQ Object**. With
`objectsPerGroup = 1` that means one object per group per stream, so the Group/Subgroup layer
collapses to an identity mapping and does no work. Before deciding whether that layer earns its
place, we should know what real V2X systems actually do.

## What the literature says: point clouds are segmented, not sent whole

**Raw point clouds are partitioned before transmission.** EMP partitions the point cloud and
transmits **non-overlapping segments of 30–38 KB each**, with up to six such segments carried
over a V2I channel — reported in
[AutoCast (arXiv:2112.14947)](https://arxiv.org/pdf/2112.14947), which contrasts this with
sending per-object point clouds (~200 points, 38.4 kbits) as an order-of-magnitude cheaper
alternative.

**The V2X standard itself segments.** ETSI TS 103 324 (Collective Perception Service) assembles
CPM data into *complementary data segments, each representing an independently interpretable
CPM*, using the `messageSegmentInfo` component when more than one CPM is assembled from the data
selected for transmission
([ETSI TS 103 324 V2.1.1](https://www.etsi.org/deliver/etsi_ts/103300_103399/103324/02.01.01_60/ts_103324v020101p.pdf)).
The key phrase is **independently interpretable**: a receiver can use a segment on its own,
without the rest of the frame.

**Sending raw sensor data whole is regarded as the expensive extreme.** The cooperative-perception
survey [arXiv:2310.03525](https://arxiv.org/html/2310.03525v4) classifies early fusion (raw point
cloud) as "high-fidelity information but requiring substantial communication bandwidth", with
intermediate and late fusion progressively cheaper. Deployment reports go further: infrastructure
nodes transmit compact object-level output rather than raw streams, because raw transmission over
5G "incurs multi-second latency and more than 60% packet loss, whereas compact object-level
outputs achieve sub-40 ms delivery with zero loss"
([CoInfra, arXiv:2507.02245](https://arxiv.org/pdf/2507.02245)).

**Conclusion: a monolithic 300 KB object is not representative.** Real systems send ~30–38 KB
independently-usable segments.

## Why this matters for our results

Our own sweep shows the monolithic model produces all-or-nothing behaviour: in `MOQ_Partial`,
PCloud is delivered at **0.1–2.3%** at every window size. The object is simply too large to clear
its deadline, so it is abandoned essentially every time. There is no middle ground, because the
unit of loss *is* the whole frame.

Segmenting PCloud into a **Group of 8 Objects of ~37.5 KB** changes this qualitatively:

- It matches EMP's measured segment size and ETSI's segmentation model.
- Each segment is independently interpretable, so delivering 4 of 8 segments yields *partial
  perception coverage* rather than nothing. Partial reliability becomes **graceful degradation**
  instead of a binary drop.
- It is the case MoQ's subgroup design actually targets: a subgroup carries the ordered segments
  of one frame, and `RESET_STREAM` on delivery-timeout abandons **the tail** of a stale frame
  while the segments already delivered remain usable. That is precisely §10.4.3's intent, and it
  is the configuration in which the Group/Subgroup layer stops being a no-op.

## Recommendation

Model PCloud as one Group per LiDAR sweep containing 8 Objects of 37.5 KB
(`packetSize: 37500B`, `sendInterval: 0.2s` per sweep, `objectsPerGroup: 8`), keeping the offered
load identical (300 KB per 200 ms) so the comparison against the current results stays fair.

This is a workload change, not new machinery — the group/subgroup and reset code already
supports it. Expected effect, to be measured rather than assumed: PCloud moves off 0% delivery
onto a graceful-degradation curve, while BBox keeps its 0% deadline-miss protection.
