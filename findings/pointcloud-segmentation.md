# Should PCloud be one 300 KB object, or a group of segments?

> **SUPERSEDED IN PART — read this first.** The numbers below were measured before two later
> fixes: (a) the MoQ publisher was silently discarding writes once QUIC's send queue overran
> (`quicSendRejected = 3468`), which flattered its latency, and (b) PCloud was still one 300 KB
> object in some runs. The *direction* of every conclusion here still holds and was re-confirmed,
> but the absolute figures should be taken from `mqtt-vs-moq.md`, which is the current,
> silent-loss-gated comparison. Specifically, MoQ at a 128 kB window measures 37 ms / 0.7% miss
> (not 30 ms / 0.0%), and at the default window ~1030 ms / 77% miss.


## The question

Our bulk track models a LiDAR point cloud as **one 300 KB MoQ Object**. With
`objectsPerGroup = 1` that means one object per group per stream, so the Group/Subgroup layer
collapses to an identity mapping and does no work. Before deciding whether that layer earns its
place, we should know what real V2X systems actually do.

## What the literature says: point clouds are chunked, and partial data is usable

**EMP (MobiCom '21) partitions the frame and uploads it chunk by chunk.** This is the primary
source, and it is explicit about *why*.

> "The partitioning is necessary for point cloud data as the size of a single frame can be large
> and there may not be enough bandwidth to upload full frames from all vehicles to the edge in
> time. **Uploading chunk by chunk allows the edge to leverage partial point cloud data if
> available.**"
> — [@zhang2021emp], §3.1 (System Overview, Data Plane)

That last sentence is the crux: **partial delivery of a point cloud is useful**, not worthless.
It is the property our monolithic model destroys.

**The measured chunk size is 30–38 KB.**

> "each vehicle only needs to upload 30–38KB for each frame. The data transmission can be
> finished within ∼23ms over LTE."
> — [@zhang2021emp], §5.2 (End-to-end Performance)

Table 1 of the same section gives the per-vehicle shared data size as 29.4–38.8 KB across 2–6
vehicle setups, against a **raw point cloud frame of ~2.0 MB**. So a frame is reduced ~50% by
partitioning, then compressed, and uploaded in ~30–38 KB pieces.

**The V2X standard segments too, into independently usable units.**

> "Depending on its size, data selected for inclusion is assembled into complementary data
> segments, **each representing an independently interpretable CPM**, following the assembly
> process specified in clause 6.1.3."
> — [@etsi103324], clause 6.1.2.1 (CPM generation events), NOTE

"Independently interpretable" means a receiver can act on one segment without the rest of the
frame — the same property EMP relies on.

**Conclusion: a monolithic 300 KB object is not representative.** Real systems send ~30–38 KB
segments, specifically so that partial delivery still yields usable perception.

## Why this matters for our results

Our sweep shows the monolithic model produces all-or-nothing behaviour: in `MOQ_Partial`, PCloud
is delivered at **0.1–2.3%** at every window size. The object is too large to clear its deadline,
so it is abandoned nearly every time. There is no middle ground, because the unit of loss *is*
the whole frame — which is exactly the situation the literature designs around.

Segmenting PCloud into a **Group of 8 Objects of 37.5 KB** (37.5 KB sits inside EMP's measured
30–38 KB band) changes this qualitatively:

- Delivering 4 of 8 segments yields *partial perception coverage* rather than nothing. Partial
  reliability becomes **graceful degradation** instead of a binary drop.
- It is the case MoQ's subgroup design targets: a subgroup carries the ordered segments of one
  frame, and `RESET_STREAM` on delivery-timeout abandons **the tail** of a stale frame while the
  segments already delivered stay usable — precisely §10.4.3's intent. This is the configuration
  in which the Group/Subgroup layer stops being a no-op.

## Applied, and measured

One Group per LiDAR sweep, containing 8 Objects of 37.5 KB: `packetSize: 37500B`,
`objectsPerGroup: 8`, `sendInterval: 0.2s` (the *group* period). The publisher now emits a whole
Group per interval, so both the offered load **and the burst shape** are preserved — segmentation
granularity is the only variable. Verified: `bytesOffered` = 62 400 000 B and `offeredRate` =
12.35 Mbps for PCloud, identical to the monolithic runs.

Both configurations at a 128 kB flow-control window, `MOQ_Partial`:

| | monolithic (1 x 300 KB) | segmented (8 x 37.5 KB) |
|---|---|---|
| BBox delivered | 61.7% | 49.1% |
| BBox latency | 31 ms | 32 ms |
| BBox deadline miss | **0.0%** | **0.0%** |
| **PCloud delivered** | **0.2%** | **18.6%** |
| PCloud latency | 856 ms | 836 ms |

**PCloud delivery rises from 0.2% to 18.6% — roughly 90x — while BBox keeps 0% deadline miss.**
This is the graceful degradation the literature describes: partial frames now arrive and are
usable, instead of whole frames being abandoned. It is the first configuration in which MoQ's
Group/Subgroup layer does real work rather than acting as an identity mapping.

Honest caveat: BBox *delivery* fell from 61.7% to 49.1%. Its deadline compliance is untouched
(0% miss, 32 ms) and the publisher still sent 410/410 objects with zero shedding, so the loss is
downstream congestion caused by the extra PCloud data that now succeeds. This is a genuine
trade-off, not a free win, and it should be reported as one.

For reference, the reliable baseline (`MOQ_SW`) at the same window delivers more PCloud (24.4%)
but at 11.4 s mean latency with a 99% deadline miss — i.e. it delivers bulk data that is far too
late to be useful, and misses 5.9% of BBox deadlines in the process.

---

## Bibliography

```bibtex
@inproceedings{zhang2021emp,
  author    = {Zhang, Xumiao and Zhang, Anlan and Sun, Jiachen and Zhu, Xiao and
               Guo, Y. Ethan and Qian, Feng and Mao, Z. Morley},
  title     = {{EMP}: Edge-assisted Multi-vehicle Perception},
  booktitle = {Proceedings of the 27th Annual International Conference on Mobile
               Computing and Networking (MobiCom '21)},
  year      = {2021},
  pages     = {545--558},
  publisher = {ACM},
  address   = {New Orleans, LA, USA},
  doi       = {10.1145/3447993.3483242}
}

@techreport{etsi103324,
  author      = {{ETSI}},
  title       = {Intelligent Transport Systems (ITS); Vehicular Communications;
                 Basic Set of Applications; Collective Perception Service;
                 Release 2},
  number      = {ETSI TS 103 324 V2.1.1},
  institution = {European Telecommunications Standards Institute},
  year        = {2023},
  month       = jun,
  url         = {https://www.etsi.org/deliver/etsi_ts/103300_103399/103324/02.01.01_60/ts_103324v020101p.pdf}
}

@article{qiu2021autocast,
  author  = {Qiu, Hang and Huang, Pohan and Asavisanu, Namo and Liu, Xiaochen and
             Psounis, Konstantinos and Govindan, Ramesh},
  title   = {{AutoCast}: Scalable Infrastructure-less Cooperative Perception for
             Distributed Collaborative Driving},
  journal = {arXiv preprint arXiv:2112.14947},
  year    = {2021},
  url     = {https://arxiv.org/abs/2112.14947}
}
```
