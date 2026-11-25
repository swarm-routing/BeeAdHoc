## v0.3.0 (2026-11-25)

The "reviewed" release. The fidelity sheet is reviewed against the citation of record, GECCO 2005, and against SIS 2005 (#27). The one deviation that review found is fixed: the destination now answers every copy of a scout, as [PG460] §13.5 and GECCO 2005 §2.2 specify (#242, #250). That fix is a protocol behaviour change: its paired smoke A/B (20 seeds, in #250) shows higher control overhead (NRL +1.64 and +3.83 in the two smoke cells), and under mobility a lower 99th-percentile delay. The anchor `bee-anchor-2` is re-read against the papers (#5). Its MISS verdict stands, qualified: the papers measure pause times up to 60 s only. A second anchor in GECCO 2005's own scenario, `bee-anchor-w05`, is pre-registered and runs toward v1.0.0 (#251). The forager draw, the trailer forwarding, the header path and the scout-resend timer no longer allocate or run needlessly; each change keeps same-seed output byte-identical (#55, #97). The comparator gates have not been passed, so no number from this release counts as a benchmark result (org golden rule 6).

### Feat

- **core**: take swarm-core from its vcpkg port (manifest mode) (#179)

### Fix

- **core**: the destination answers every copy of a scout (#250)
- **make**: the vcpkg branch of core-test clears a cached SWARM_CORE_DIR (#181)

### Perf

- **ns3**: serialize headers without a vector (#247)
- **ns3**: forward the decoded trailer without copying its routes (#246)
- **core**: cancel a scout's resend timer once its search ends (#245)
- **core**: reuse the forager draw's candidate buffer (#237)

## v0.2.1 (2026-11-16)

A patch release built on the frozen swarm-core v1.0.0 (swarm-routing/ns3-swarm-core#83): `swarm-core.lock` now locks swarm-core v1.0.0 (#161), whose API is frozen at `kSwarmCoreApiVersion` 2. BeeAdHoc itself is unchanged since v0.2.0: no protocol, wire-format or API change, and the comparator gates are v0.2.0's (the anchor's written infeasibility verdict, `docs/benchmarks/v0.2.0-anchor.md`). The release workflow gained an `increment` input (#162), used here to force the patch.

## v0.2.0 (2026-11-16)

### Feat

- **release**: each release opens the beeadhoc-core port pr in swarm-routing/vcpkg-registry (#146)
- **bench**: the anchor plan, generation bee-anchor-2 (#145)
- **core**: core/ installs as the cmake package beeadhoc-core, for its vcpkg port (#143)
- **core**: build core/ at C++23, the standard of the ns-3 release the org supports (#135)
- **core**: consume swarm-core by release: swarm-core.lock, URL + URL_HASH, ns-3 dependencies on tags with a canary on main (#134)
- **ns3**: the adapter carries the core's trailer, swarms and link failure (#108)
- **core**: swarms and link failure (#102)
- **core**: the data path, a versioned source-route trailer, forward and input chains, piggybacking (#96)
- **bench**: benchmark pipeline: a caller of the reusable cell and the change-test smoke plan (#95)
- **observer**: report through swarm-core's router observer (#58)
- **ns3**: the beeadhoc contrib module on swarm-core: codec, adapter, helper, arm, tests (#48)

### Fix

- audit follow-ups (a returned scout's route repeats, make lint, stale status text) (#110)

## v0.1.0 (2026-11-11)
### Release status

v0.1.0 is cut before the comparator gates (maintainer decision 2026-11-10). It has **not** cleared the three gates of org golden rule 6 (a fidelity sheet from its source, the per-PR delivery smoke, and an anchor reproducing the source's own trend). The anchor is pending, and so is the ns-3 contrib module `beeadhoc`: it is still being written, and an `ns3/` directory in this release's archive, if there is one, is that work in progress as it stood on `main`. No result from this release counts as a benchmark number, and it does not enter the benchmarks suite until a release clears the gates; that gated release is proposed as v0.2.0 (swarm-routing/.github ROADMAP "Releases"). It ships `core/` (MIT OR GPL-2.0-only, with `core/LICENSE`) and `docs/` (plus `ns3/` if it exists), as they stand on `main` at the release commit.


### Feat

- **core**: scouts and forager arrays, the first slice of beeadhoc's core (#37)
