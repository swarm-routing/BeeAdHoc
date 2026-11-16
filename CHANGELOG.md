## v0.2.1 (2026-11-16)

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
