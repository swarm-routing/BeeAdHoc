# CONTEXT.md — orientation and history

Background an agent or a new contributor needs that does not belong in
`AGENTS.md`: domain notes, current state, glossary, open questions. Read the
section a task needs, not the whole file.

## Current state

**Status: v0.1.0 released (ungated); `core/` complete in its four slices, the
ns-3 adapter behind it.** The status paragraph of [`AGENTS.md`](AGENTS.md)
("What this repo is") is the one kept current, part by part; this is the
short form:

- `core/` (#36) has route discovery, the data path (the source-route
  trailer), swarms and link failure, each with its tests.
- `ns3/` is the contrib module `beeadhoc` (#47): it carries out route
  discovery on swarm-core's `SwarmRoutingBase`. Its data packets carry the
  route as an ns-3 packet tag; it does not yet use the core's trailer, send
  swarms or react to a link failure.
- The comparator gates (org golden rule 6) are not cleared: the fidelity
  sheet exists ([`docs/fidelity.md`](docs/fidelity.md)), the delivery smoke
  and the anchor are #47's.
- Releases and what each one waits for: [`docs/roadmap.md`](docs/roadmap.md).

Where it comes from: before this repository, BeeAdHoc existed only as
AntHocNet's `beeadhoc` comparator arm (GPL-2.0-only). `core/` is written
fresh from the PG 460 report's Linux kernel module (chs. 12–13; the version
decided in #16) and the fidelity sheet, never from the arm's code (golden
rule 12). The arm's anchor on PG 460 ch. 7 missed (3 of 5 pause times;
recorded on #5).

## Glossary

Terms as [`docs/fidelity.md`](docs/fidelity.md) and `core/` use them.

- **Scout**: the route-discovery packet. A source with no forager for a
  destination broadcasts a forward scout; each relay appends itself; the
  destination sends it back along the reversed route as a backward scout
  (`core/beeadhoc-scout.*`).
- **Forager**: a source route to a destination, held at a node and copied
  into the data packets sent there (`core/beeadhoc-forager.*`).
- **Own / foreign**: an own forager comes from this node's returned scout (or
  comes home piggybacked or in a swarm); a foreign forager is the reversed
  route of a data packet delivered here, a way back to that packet's source.
  Own foragers are used first.
- **Dance**: one further use of a forager. Its rating (Listing 13.15) is the
  number of dances it gets; a forager with none left is removed when taken.
- **Info field**: 24 bits on a scout or data packet, 3 bits per hop: each
  forwarding host shifts it left and writes its battery level (0–7). The
  rating reads the minimum and the average of these values.
- **Forager type**: energy, delay or throughput; the store keeps one array
  per destination and type. Only energy is measured, and the adapter sends
  every packet as the energy type.
- **Piggybacking**: a data packet carries one foreign forager home to its
  owner, the packet's destination.
- **Swarm**: one source-routed packet that carries `SWARMSIZE` foreign
  foragers home when traffic is one-way (`core/beeadhoc-swarm.*`).
- **Trailer**: `RouteTrailer`, what the core puts on a data packet: the
  whole route, the info field and at most one piggybacked forager
  (`core/beeadhoc-trailer.*`).
- **Expanding ring**: a scout that is not answered is resent with a larger
  TTL (2, 4, 8, 10, twice at 10), then the search gives up.

## Open questions

- Does the published evaluation (GECCO 2005, SIS 2005) agree with the PG 460
  ch. 7 anchor, or does it need a second anchor? #5.
- Do the 2005 papers settle or contradict any row of the fidelity sheet
  (the rating's values, the dance cap, the scout TTL, the battery bins)? #27.
  The sheet has no `unknown` row left: every reading the report leaves open
  is a repo choice recorded on #4, #5, #13 or #36, and #27 reviews them.
- The adapter's adoption of the core's trailer, swarms and link failure, and
  the two remaining gates: #47.
- Should a returned scout with no search out call RouteReady? Today it does
  (`Router::onBackwardScout`), while `deliverData` and `onSwarm` do not: #104.
