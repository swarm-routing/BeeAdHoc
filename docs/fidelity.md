# BeeAdHoc: fidelity sheet

What the source specifies, what the implementation does, and where they differ.
Every row cites its source (`[KEY] §x`, `Listing N`, `Fig. N`, `ch. N`), or
says `repo choice: <reason>`, or `unknown`. Checked by `swarm-routing/.github`
`tools/fidelity/fidelity.py check`.

**Provenance.** The rows were moved from AntHocNet's
`ns3/baselines/adaptive/README.md`, sections "BeeAdHoc — fidelity sheet" and
"Shared plumbing (all four arms)", at `swarm-routing/AntHocNet` commit
`4fe1400c98d8a1e2f053ced6b6580df3b27f1124` (the file last changed in
`f3fccdf926db1be35ae0083a37016866f309e33c`, 2026-11-07). That file is
documentation (GPL-2.0-only), as this one is, so the text moved as-is apart
from the reshaping into the org's tables (golden rule 2). Every locator was
re-checked against the report while moving it; the corrections are listed under
*Differences from AntHocNet's `beeadhoc` arm*. Nothing here is copied from the
arm's code, which stays GPL-2.0-only and never reaches `core/` (golden rule 12).

**Implementation** describes AntHocNet's `beeadhoc` arm
(`ns3/baselines/adaptive/model/beeadhoc-routing-protocol.{h,cc}`), the only
implementation so far. `core/` is not written yet; it follows the rows that are
settled and the repo choices, and waits on the open ones.

**Status** says how each row stands against the source (#13):

- `as written; settled (<locator>)`: the source gives it, and the arm follows it;
- `deviation: …` / `not implemented: …`: the arm departs from it, with the reason
  (and what `core/` follows instead, where that is decided);
- `repo choice (maintainer decision 2026-11-10, <issue>): …`: the source leaves it
  open, and the maintainer chose the value or reading, pre-registered on that
  issue (golden rule 11). Unless the row says otherwise, it is the arm's value;
- `unknown: unresolved …`: the cited section does not settle it, and no issue owns it yet;
- `unknown: open #4 …` / `unknown: open #5 …`: the cited section does not settle
  it, and that issue will (golden rule 11: the reading is pre-registered there,
  not chosen here).

Parameters carry the same status at the start of their Notes.

## Sources

| Key | Citation | Access |
|---|---|---|
| PG460 | M. Farooq, H. F. Wedde (instructors), J. Fischer, M. Kowalski, M. Langhans, N. Range, C. Schletter, R. Tarak, M. Tchatcheu, C. Timm, F. Volmering, S. Werner, K. Wang, *BeeAdHoc: an Efficient, Secure and Scalable Routing Framework for Mobile AdHoc Networks*, Project Group 460 Final Report, University of Dortmund, 2006, hdl 2003/22405 (TU Dortmund Eldorado; no DOI). The source of every row below | in hand |
| W05 | H. F. Wedde, M. Farooq, T. Pannenbaecker, B. Vogel, C. Mueller, J. Meth, R. Jeruschkat, *BeeAdHoc: an energy efficient routing algorithm for mobile ad hoc networks inspired by bee behavior*, Proc. GECCO 2005, ACM, pp. 153–160, DOI 10.1145/1068009.1068034 (authors, year, venue and pages agree across Crossref, Semantic Scholar and OpenAlex). The citation of record; **not held** (ACM 403, no open copy). No row cites it (#5) | institution |
| WF05 | H. F. Wedde, M. Farooq, *The wisdom of the hive applied to mobile ad-hoc networks*, Proc. IEEE Swarm Intelligence Symposium (SIS 2005), pp. 341–348, DOI 10.1109/SIS.2005.1501641 (authors, venue and pages agree across the three indexes). **Not held**; may carry the multi-protocol evaluation (#5). No row cites it | institution |
| F06 | M. Farooq, *From the Wisdom of the Hive to Intelligent Routing in Telecommunication Networks*, PhD dissertation, University of Dortmund, 2006, hdl 2003/22186. Mostly BeeHive on fixed networks; BeeAdHoc appears only as the MANET prototype (§1.3). Not a BeeAdHoc specification; no row cites it | in hand |
| PG439 | H. F. Wedde, M. Farooq (instructors) et al., *BEEhive: an Energy-Aware Scheduling and Routing Framework*, Project Group 439 Final Report, University of Dortmund, 2004, hdl 2003/21407. PG 460's predecessor (BeeHive); it does not specify the Linux module. No row cites it | in hand |

## Which version of BeeAdHoc

[PG460] describes more than one BeeAdHoc:

- **ns-2 simulation** (ch. 3), with entrance, packing floor and dance floor;
- **formal multi-agent model** (ch. 4), with foragers, packers, swarms and scouts;
- **scalability model** (ch. 6), "Next R Hop Routing" for routes past ten hops;
- **Linux kernel module** (chs. 12–13, constants in §14.3.2).

This repository follows the **Linux kernel module**, decided in #16
(2026-11-09, option A): it is the only version specified down to its constants,
and AntHocNet's arm follows it, so the arm's history (the v270a anchor) applies.
Packers and dance-floor recruitment (ch. 4) are out of scope; if they come
later, they are a default-off mechanism with its own A/B. Every row below
belongs to the Linux module unless it starts with `ch. 3:`.

One caution for the anchor: the evaluation in ch. 7 is an **ns-2** study
([PG460] §7.1.4), so the BeeAdHoc it measures is an ns-2 implementation, not
the kernel module this sheet follows. The report does not say which ns-2
version (ch. 3 or ch. 6) ch. 7 ran. See *Anchor candidate*.

## Mechanisms

| Mechanism | Source | Implementation | Status |
|---|---|---|---|
| Strict source routing: the source writes the whole route into the packet; intermediate nodes follow it | [PG460] §12.1.2, Fig. 12.6 | A `BeeRouteTrailer` (route, 24-bit info field, an optional piggybacked forager) on every data packet; the destination strips it before local delivery | deviation: a trailer, not an IP option. The application header stays first, so the harness classifies flows unchanged. Its bytes are on the air: 4 per route address + 4 per piggybacked address + 8 (two 24-bit info fields and 2 bytes of counts), the piggyback's info field included even when no forager rides along. The harness books them outside `nrl_bytes`, in `##INPKT##` and `nrlBytesIncl` (AntHocNet #808) |
| A packet this node originates gets its route on the way out (OUTPUT chain) | [PG460] §12.1.3 | Never routed in `RouteOutput`: the base bounces it through loopback (AODV's deferred-route pattern), and `OnLoopedBack` puts the trailer onto IP's own copy and sends it at once when a forager exists; otherwise it is held for a scout | as written; settled ([PG460] §12.1.3). The loopback is ns-3 plumbing (AntHocNet #800 fix: adding the trailer to the application's packet made `OnOffApplication` re-send it) |
| Longest route: 10 hops, from the 40-byte IP options budget | [PG460] §12.1.2, Listing 13.5 | `MaxHops` 10 | repo choice (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): 10 hops, the value §12.1.2 states, as the arm. The doubt does not bind here, since the route rides in a trailer, not an IP option. Kept as a note on the kernel module: §12.1.2's own budget holds 8 four-byte route addresses, and Listing 13.5 rejects any route whose options (`IPT_BEEADHOC_OPTIONLENGTH` + 4 · route_length − 1, padded to 4) push the IP header past 15 words. That constant has no value in the report, so whether a 10-hop route fit the kernel module is not settled |
| Each forwarding host shifts the 24-bit info field left 3 bits and writes its 3-bit battery level (FORWARD chain) | [PG460] §12.1.2, §13.2.2, Listing 13.7 | Same, at every intermediate hop; the battery is the harness's energy accounting (`--energyJ`), read through an energy probe (AntHocNet #801) | as written; settled ([PG460] Listing 13.7) for the shift and the writers on the FORWARD chain. Which endpoint, if any, also writes a value is the open row below |
| Battery level → 3-bit value | [PG460] Listing 13.7 | ≤ 9 % → 0, (9, 14] → 1, > 65 % → 7; levels 2–6 split (14, 65] into five equal bins of 10.2 % | as written; settled ([PG460] Listing 13.7) for levels 0, 1 and 7. repo choice (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)) for levels 2–6: the listing elides them (`[.....]`); the arm's equal bins are kept, (14, 24.2] → 2, (24.2, 34.4] → 3, (34.4, 44.6] → 4, (44.6, 54.8] → 5, (54.8, 65] → 6. Accounting only: a node at 0 % keeps transmitting. At the default 5000 J no node falls below about 79 % in a 900 s run, so every level reads 7 |
| A node without battery data reports a full battery | [PG460] §13.1.2, §13.2.2 | `--energyJ=0` → every node reports 7 | as written; settled ([PG460] §13.1.2: without the ACPI patch a machine "will allways claim that its battery is full") |
| Only energy is measured in the kernel module; delay and throughput are not collected | [PG460] §13.2.2 | One forager type (energy) | as written; settled ([PG460] §13.2.2: "only able to collect energy data in reality"). See the three-type store below |
| The forager store keeps three types (energy, delay, throughput); a returned scout is put once per type; a packet's type comes from its IP TOS field | [PG460] §13.2.2, §13.3, Listing 13.28, Listing 13.19 | One store, energy only | deviation: the arm keeps one type. `core/` keeps the three types, as the kernel module (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): with only energy measured the three copies carry the same info, but each type array holds its own copies and dance counts, so one store would change the forager supply |
| INPUT chain: every delivered packet is a forager that can find its way back to its source | [PG460] §12.1.3, §13.2.2, §13.7.1 | The destination stores the reversed route as a 'foreign' forager for the source | as written; settled ([PG460] §12.1.3, §13.7.1) |
| Rating (`forager_rate_lifetime`): direct neighbour → `MAX_DANCES`; otherwise ((MAXHOPS − (route_length − 1)) · min · avg + 1) / 44, integer arithmetic | [PG460] §13.3, Listing 13.15 | Same | as written; settled ([PG460] Listing 13.15) for the formula itself |
| What `route_length` counts | [PG460] Listing 13.15, Listing 13.19 | Links | as written; settled (Listing 13.19 with Listing 13.15, recorded on #4 2026-10-30): the dump's route `29 28 30 25` has 4 links and the foragers show 5–6 dances after one or two, against 7 computed for length 4 |
| Which per-hop values the rating reads (intermediate nodes only, or one endpoint too) | [PG460] §12.1.2, Listing 13.15, Listing 13.19, Listing 13.27 | The intermediate nodes' values only | unknown: open #4 (and #27) for which node supplies the L-th value. Settled for the count (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/BeeAdHoc/issues/4#issuecomment-6756879325): the listings over the prose): on an L-link route the rating reads L values, min and average over all L (Listing 13.15 reads `route_length` values; a scout's info is assembled from `info[0]` to `info[nh]`, nh + 1 values, on a route of nh + 1 links, Listing 13.27, Listing 13.28); §12.1.2's "excluding source and destination hop" (L − 1 values) is set aside. Deviation: the arm reads L − 1. Still open: the L − 1 forwarders write theirs (Listing 13.7), but the listings do not show the L-th: Listing 13.26 elides where a scout's `info[]` is filled, and a data packet's field starts at 0 (Listing 13.6) and is written only on FORWARD, which read literally rates every multi-hop data forager 0, against the dump's `Info: fff`, `Dances: 5`–`6` (Listing 13.19). The `Info: ffffff` on the opt-type 2 foragers of the same route is unexplained too (the rating reads only the low 4). Inert while every level reads 7 (AntHocNet #801); to settle before an energy anchor |
| The rating is capped at `MAX_DANCES` | [PG460] Listing 13.15 | Capped (`CapDances`, default on) | deviation: the arm caps by default. Settled: no cap ([PG460] Listing 13.15, Listing 13.16; maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/BeeAdHoc/issues/4#issuecomment-6756879325)): Listing 13.15 returns the expression, Listing 13.16 stores it as returned, Listing 13.17 only decrements, and `nr_dances` is a `u8` (Listing 13.12). With every level 7: direct neighbour 8, then 10, 8, 7, 6, 5, 4, 3 for 2 to 8 links, so a cap would bind only at 2 links. `core/` follows the listing (the arm's `CapDances=false`); AntHocNet #806 measured no difference. Open on #4: on 9 and 10 links the listing as printed rates 0, since the 24-bit info field holds 8 values (§12.1.2; masked to 24 bits in Listing 13.27) and Listing 13.15 reads the 9th and 10th as 0, so such a route never dances; whether `core/` keeps that is not settled |
| Forager choice: a random forager of the type; still dancing (dances left, younger than `FORAGER_DANCETIME`) → copied, dances − 1; otherwise removed; older than `FORAGER_LIFETIME` → discarded unless it is the last | [PG460] §13.3, Listing 13.17 | Same (`ForagerStore::Take`) | as written; settled ([PG460] Listing 13.17) |
| 'Own' foragers are used first, 'foreign' ones when none are left | [PG460] §13.7.1 | Same | as written; settled ([PG460] §13.7.1) |
| With two-way traffic the foreign foragers ride home piggybacked (on the acknowledgements, for TCP) | [PG460] §13.7.1 | One foreign forager piggybacked per data packet toward its owner | as written; settled ([PG460] §13.7.1) for the mechanism; how many per packet is a parameter row (not given) |
| Swarm trigger: foreign foragers > `SWARMSIZE` and own < `SWARMSIZE_OWN` | [PG460] §13.7.2, Listing 13.30 | Same | as written; settled ([PG460] Listing 13.30) |
| Swarm packet: `SWARMSIZE` foreign foragers in one source-routed packet, routes reversed by the sender, unpacked into the array at the creator node | [PG460] §13.7.3, §13.7.5, Listing 13.33, Listing 13.35 | Same, but the routes are reversed at home | deviation: the report reverses each forager's route at the node that builds the swarm (§13.7.3); the arm reverses on arrival. Same result if both ends agree; recorded so `core/` follows the report |
| Scout: broadcast, each node appends itself; a seen list of (source, destination, id) drops repeats; the destination unicasts the scout back along the reversed route (mode 2); the source turns it into foragers | [PG460] §13.5, Listing 13.21, Listing 13.26, Listing 13.28 | Same | as written; settled ([PG460] §13.5) |
| Scout relays are jittered | [PG460] §3.4 | U[0, 10 ms) per relay | repo choice (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): U[0, 10 ms), as the arm. ch. 3: the ns-2 version adds "a small random delayer" to scout relays (§3.4), with no value; the Linux module, on real radios, has none. An ns-3 port needs one (synchronised relays collide, as §3.4 reports) |
| Expanding ring search: no scout back within `SCOUTRESENDRATE` → another scout with a new id and a new TTL | [PG460] §13.5, Listing 13.25 | Resend after 500 ms, TTL 2 → 4 → 8 → 10, two rounds at 10, then the held packets are dropped | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/BeeAdHoc/issues/5#issuecomment-6756882422)): the arm's ring, TTL 2 → 4 → 8 → 10, two rounds at 10, then the held packets are dropped. §13.5 gives "a small TTL" and no growth or give-up rule. Its "decreased TTL" for the second scout is recorded as a likely typo for "increased": it contradicts "expanding ring search" in the same paragraph. The starting TTL decides the arm's delay (AntHocNet #806), so an anchor states it |
| No route yet: the packet is stored "for a short time" while a scout is out | [PG460] §12.1.3 | Held in the base's queue (shared plumbing: 64 packets, 30 s) | as written; settled ([PG460] §12.1.3) for holding; the limits are parameters (not given) |
| Garbage collection: destinations without traffic for `MAXAGE` seconds are removed; a timer frees all foragers after `MAXAGE` | [PG460] §13.3, Listing 13.14 | No separate age-out; the lifetime rule discards on use | deviation: the arm has no `MAXAGE` collector (the report gives no value); the lifetime rule in Listing 13.17 bounds what is used, not what is stored. `core/` has the collector (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): `MAXAGE` 30 s and `GARBAGE_PROBAB` 1 (Parameters) |
| A failed next link | unknown | The packet is dropped (booked as a route drop); every forager over that link is removed (`RemoveForagersOnLinkFailure`, default on); the source picks another route for its own next packet | unknown: unresolved. Chs. 12–13 describe no link-failure handling or salvage; the removal is the arm's choice, measured in AntHocNet #806 (no significant effect) |
| ch. 3: dance floor, packing floor, entrance; ch. 4: packers, swarm leader / recruiter | [PG460] ch. 3, ch. 4 | Not implemented | not implemented: out of scope by #16; the Linux module replaces them with the forager array and the swarm trigger |
| ch. 6 scalability model (Next R Hop Routing) | [PG460] ch. 6 | Not implemented | not implemented: a separate extension, not part of the Linux module |
| BeeGuard / security | [PG460] ch. 8, ch. 9, ch. 10, ch. 11 | Not implemented | not implemented: out of scope here (BeeAdHoc#9 tracks the secure variants) |

## Parameters

| Parameter | Value | Source | Notes |
|---|---|---|---|
| `IPT_BEEADHOC_MAX_DANCES` | 8 | [PG460] §14.3.2 | settled. Also the direct-neighbour rating (Listing 13.15) |
| `IPT_BEEADHOC_FORAGER_LIFETIME` | 2 | [PG460] §14.3.2 | settled for the value. Unit: §14.3.2 gives none; Listing 13.17 compares it with differences of `get_seconds()`, so seconds (the arm reads 2 s). The arm's 30 s is the hold-queue timeout, a different setting |
| `IPT_BEEADHOC_FORAGER_DANCETIME` | 2 | [PG460] §14.3.2 | settled; seconds, by the same reading of Listing 13.17 |
| `IPT_BEEADHOC_MAXHOPS` (longest route) | 10 | [PG460] §12.1.2 | settled for the value (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)). Note on the kernel module: §12.1.2's budget (3 bytes for the source-route option, 8 × 4 bytes of route, 5 bytes for the BeeAdHoc option) and the 40-byte check of Listing 13.5 do not obviously allow 10; it does not bind here (a trailer, see Mechanisms) |
| Info field | 24 bits, 3 bits per hop | [PG460] §12.1.2 | settled: room for 8 values |
| Rating divisor | 44 | [PG460] Listing 13.15 | settled |
| `SCOUTLISTLENGTH` (scout seen list) | 30 | [PG460] §14.3.2 | settled |
| `RXMTLISTLENGTH` (scout retransmit list) | 30 | [PG460] §14.3.2 | settled; the arm keeps one pending scout per destination and does not size a list |
| `SCOUTRESENDRATE` | 500 | [PG460] §14.3.2 | settled for the value; jiffies (Listing 13.25 adds it to `jiffies`); 500 ms at HZ = 1000, which the report does not state (the arm's reading) |
| `IPT_BEEADHOC_SWARMSIZE` | 20 | [PG460] §14.3.2 | settled |
| `IPT_BEEADHOC_SWARMSIZE_OWN` | 11 | [PG460] §14.3.2 | settled |
| `HASHSIZE` (destination hash table) | 256 | [PG460] §13.3 | settled; an implementation detail of the kernel table, no behaviour depends on it |
| `IPT_BEEADHOC_ARRAYSIZE` (foragers per destination and type) | 64 | repo choice (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): the arm's `ForagerArraySize` | Named in §13.3 (Listing 13.11), no value |
| `IPT_BEEADHOC_MAXAGE` (garbage collection) | 30 s | repo choice (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): new (the arm has no collector); well above the 2 s forager lifetime and the gaps of a steady flow, and equal to the hold-queue timeout | Named in §13.3, no value. A destination without traffic for `MAXAGE` is removed (Listing 13.14): a flow that sends less often than every 30 s loses its foragers between packets, and its next packet waits for a scout. §13.3 also says a timer "will free all foragers after `MAXAGE` seconds"; `core/` reads that as the same age-based collection run every `MAXAGE` (a repo reading, not the report's words). The collection removes idle entries by age; it does not cap the number of destinations held (each destination's arrays are capped by `ARRAYSIZE`). A cap on destinations, and what to evict at it (golden rule 7), is unresolved |
| `IPT_BEEADHOC_GARBAGE_PROBAB` | 1 (collect on every put) | repo choice (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): new (the arm has no collector); the probability spreads the kernel's CPU cost, which does not matter in simulation, and at 1 the collector draws no random number (golden rule 5) | Named in §13.3, no value |
| Battery bins for levels 2–6 | five equal bins of 10.2 % over (14, 65] % | repo choice (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): the arm's `BeeBatteryLevel` | Listing 13.7 elides them. (14, 24.2] → 2, (24.2, 34.4] → 3, (34.4, 44.6] → 4, (44.6, 54.8] → 5, (54.8, 65] → 6 |
| Initial scout TTL | 2 | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/BeeAdHoc/issues/5#issuecomment-6756882422)): the arm's `InitialScoutTtl`, the value `v270a` ran | §13.5: "a small TTL". AntHocNet #806 found it decides the arm's delay (52.8 ms with it, 24.3 ms with TTL 10) |
| Scout TTL growth and give-up | doubling to 10 (2 → 4 → 8 → 10), two rounds at 10, then the held packets are dropped | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/BeeAdHoc/issues/5#issuecomment-6756882422)): the arm's ring (`MaxRoundsAtMaxTtl` 2) | §13.5 gives neither |
| Scout relay jitter | U[0, 10 ms) | repo choice (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): the arm's `ScoutJitter` | ch. 3 (§3.4): "a small random delayer", no value; none in the Linux module |
| Hold queue for packets waiting on a scout | 64 packets, 30 s | repo choice (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): the arm's shared plumbing (ns-3 AODV's defaults) | §12.1.3: "for a short time" |
| Foreign foragers piggybacked per data packet | 1 | repo choice (maintainer decision 2026-11-10, [#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)): the arm's value | §13.7.1 says they are piggybacked, not how many |

## Anchor candidate

| Trend | Scenario | Metric | Comparators | Source |
|---|---|---|---|---|
| BeeAdHoc sends the fewest control packets, flat over pause time | ns-2, 100 nodes, 3000 × 1000 m, 900 s, 20 CBR flows of four 512-byte packets/s, random waypoint 0–10 m/s, pause 1, 30, 100, 500, 1000 s (AntHocNet's scalability setup); results in figures only | Control packets sent (§7.2.5 counts packets, not NRL) | AODV, DSR, DSDV ("left out in some cases due to errors", §7.1.4). The org has no DSR arm | [PG460] §7.1.4, §7.2.5, Fig. 7.3, Fig. 7.14 |

Status: **open: #5**. The citation of record is [W05], which we do not hold;
#5 re-reads this anchor against it once it is in the papers library. Two
cautions stand until then: ch. 7 measured an ns-2 BeeAdHoc, not the kernel
module this sheet follows (*Which version*), and its DSDV comparison may be
thin by the report's own account.

## Anchor history

- **Pre-registration (AntHocNet #800, generation `v270a`).** Source [PG460]
  §7.2.5 (Fig. 7.14). Cell: `routing-compare --scenario=paper`, 100 nodes,
  3000 × 1000 m, 900 s, 20 CBR flows at 4 packets/s, random waypoint
  U[0, 10] m/s, pause 1, 30, 100, 500, 1000 s, two-ray, 250 m range, UDP,
  image 3.42-opt, seeds 1–10. Deviations: 64-byte data packets instead of 512
  (same packet rate), no DSR arm (AODV and DSDV only), 10 seeds instead of 5.
  Gate: BeeAdHoc's NRL below AODV's and DSDV's (paired 95 % CI wholly below
  zero) at ≥ 4 of 5 pause times. Run in two settings, the arm's defaults and
  no expanding ring (`InitialScoutTtl=10`). Plan file: AntHocNet
  `tools/bench/campaigns/v2.7.0-beeadhoc-anchor.yaml`. Its 10 seeds predate
  the org's rule of ≥ 20 paired seeds (golden rule 6); a run on this
  repository uses ≥ 20.
- **Verdict (2026-11-01): MISS, 3 of 5 pause times**
  (swarm-routing/AntHocNet#841; AntHocNet
  `docs/benchmarks/v2.7.0-beeadhoc-anchor.md`). BeeAdHoc's NRL is flat over
  pause time (9.4–10.5) and below AODV's at every pause time, and below
  DSDV's at 1, 30 and 100 s; at 500 and 1000 s the two are level
  (−0.39 [−1.18, +0.40], +0.13 [−0.92, +1.18]). With the ring off, delay
  falls below AODV's but NRL rises by about 4, above DSDV's everywhere: no
  setting gives the report's smallest control load and best delay together.
  Under the comparator rule the arm shipped as an infeasibility verdict.
- **What comes next** (maintainer, #4, 2026-11-09): if settling a Listing
  13.15 reading changes the implementation, the anchor is re-registered under
  a new id; if both readings settle as implemented, `v270a` is re-run
  unchanged on this repository. That unchanged re-run (10 seeds) is a
  historical replication only: a comparison this repository counts needs
  its own pre-registration with paired per-seed deltas over ≥ 20 seeds.
- **Applied (2026-11-10).** The readings settled on [#4](https://github.com/swarm-routing/BeeAdHoc/issues/4#issuecomment-6756879325) (L per-hop values,
  no cap) and the three-type store ([#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)) change the implementation relative
  to the arm, so under that rule `v270a` is not this repository's anchor: a
  new anchor is pre-registered under a new id once `core/` exists, and a
  re-run of `v270a` stays a historical replication.

## Shared plumbing (from the arm)

AntHocNet's four adaptive arms share this ns-3 plumbing. None of it is routing
policy, and none of it comes from [PG460]: each row is a repository choice, kept
so held-packet loss and control traffic are counted alike for every arm.

| What | How | Why | Status |
|---|---|---|---|
| Control traffic | One UDP socket per interface (unicast plus subnet broadcast), TTL 1, ports 5541 / 5542 / 5543 / 5544 (one per arm) | The harnesses count routing control as UDP not addressed to the data port, the same rule as every other arm | repo choice (the kernel module uses UDP ports 1124 for scouts and 1125 for swarms, §13.5, Fig. 13.3) |
| No route yet | The packet is bounced through loopback and held with its forwarding callbacks: 64 packets, 30 s (ns-3 AODV's defaults); dropped through the error callback, which FlowMonitor books as a route drop | The AODV pattern, so held-packet loss lands in the same drop column for every arm | repo choice ([PG460] §12.1.3 gives no limits) |
| Previous hop | The sender stamps a `PrevHopTag` (its address and its upstream) on every data packet | A real node reads the sender from the link-layer source address, which ns-3's `RouteInput` does not expose. A tag adds no bytes, like the MAC address it stands in for | repo choice (ns-3 plumbing) |
| Link failure | WifiMac `DroppedMpdu` at the retry limit, resolved to the neighbour's IP through the ARP cache | The same detector as the AntHocNet adapter's ADR-0008 detector D | repo choice ([PG460] chs. 12–13 describe none) |
| The packet under a failed link | Recovered from the MPDU and re-routed by the arm, counted as `reinjected` so the harness's drop book nets it out of the MAC column | Written for ARA and Termite, which resend on a remaining link | repo choice; for BeeAdHoc the packet is dropped (Mechanisms, "A failed next link") |
| Randomness | One `UniformRandomVariable` per node, pinned by `AdaptiveHelper::AssignStreams` | Seed independence (AntHocNet #352); org golden rule 5 | repo choice |

## Differences from AntHocNet's `beeadhoc` arm

| Paper | Arm | Difference |
|---|---|---|
| §13.2.2: the kernel module measures only energy | The arm's sheet cites §13.3 for this | Locator corrected to §13.2.2; §13.3 is where the three-type store is defined |
| §13.3, Listing 13.28: a returned scout is stored once per type (three arrays) | One energy store | Each type array holds its own copies and dance counts, so the forager supply may differ; `core/` keeps three types ([#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)) |
| Listing 13.15 reads `route_length` (L) values, as Listing 13.27; §12.1.2's prose says intermediate hops only | Rates on the L − 1 intermediate hops' values | `core/` reads L values ([#4](https://github.com/swarm-routing/BeeAdHoc/issues/4#issuecomment-6756879325)); which node supplies the L-th stays open on #4 |
| Listing 13.15: no cap | `CapDances` on | `core/` does not cap ([#4](https://github.com/swarm-routing/BeeAdHoc/issues/4#issuecomment-6756879325)); inert in AntHocNet #806 |
| §13.5: initial TTL "small", growth and give-up not given | TTL 2 → 4 → 8 → 10, two rounds at 10 | Kept as repo choices ([#5](https://github.com/swarm-routing/BeeAdHoc/issues/5#issuecomment-6756882422)); they decide the delay (AntHocNet #806) |
| §3.4 (ns-2 version only): a random relay delay, no value | U[0, 10 ms) on every scout relay | The kernel module has none; the arm's value is kept as a repo choice ([#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)) |
| §13.7.3: the swarm sender reverses each forager's route | Routes reversed on arrival | Equivalent end to end; `core/` follows the report |
| §13.3: `MAXAGE` garbage collection | None | The arm's store is bounded by its 64-entry arrays only; `core/` collects at `MAXAGE` 30 s, `GARBAGE_PROBAB` 1 ([#13](https://github.com/swarm-routing/BeeAdHoc/issues/13#issuecomment-6756886273)) |
| §14.3.2: `RXMTLISTLENGTH` 30 | Not modelled | One pending scout per destination |
