# BeeAdHoc

<!-- org-sync:begin badges -->
[![Release](https://img.shields.io/github/v/release/swarm-routing/BeeAdHoc?sort=semver&cacheSeconds=1800)](https://github.com/swarm-routing/BeeAdHoc/releases)
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.24124821.svg)](https://doi.org/10.5281/zenodo.24124821)
[![Cite](https://img.shields.io/badge/cite-CITATION.cff-blue)](CITATION.cff)
[![Licence: GPL-2.0-only, core/ tools/ MIT OR GPL-2.0-only](https://img.shields.io/badge/licence-GPL--2.0--only%2C%20core%2F%20tools%2F%20MIT%20OR%20GPL--2.0--only-blue)](LICENSING.md)
[![Conventional Commits](https://img.shields.io/badge/Conventional%20Commits-1.0.0-yellow)](https://www.conventionalcommits.org)
[![Colony Arena: BeeAdHoc](https://img.shields.io/badge/Colony%20Arena-project%20page-orange)](https://swarm-routing.github.io/projects/BeeAdHoc/)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/swarm-routing/BeeAdHoc/badge)](https://scorecard.dev/viewer/?uri=github.com/swarm-routing/BeeAdHoc)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue?logo=cplusplus)](AGENTS.md)
[![Runs on ns-3](https://img.shields.io/badge/runs%20on-ns--3-informational)](https://www.nsnam.org)
[![Built on: ns3-dev](https://img.shields.io/badge/built%20on-ns3--dev%20image-blue?logo=docker)](https://github.com/orgs/swarm-routing/packages/container/package/ns3-dev)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_BeeAdHoc&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=swarm-routing_BeeAdHoc)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_BeeAdHoc&metric=bugs)](https://sonarcloud.io/summary/new_code?id=swarm-routing_BeeAdHoc)
[![Security Rating](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_BeeAdHoc&metric=security_rating)](https://sonarcloud.io/summary/new_code?id=swarm-routing_BeeAdHoc)
[![Maintainability Rating](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_BeeAdHoc&metric=sqale_rating)](https://sonarcloud.io/summary/new_code?id=swarm-routing_BeeAdHoc)
[![codecov](https://codecov.io/gh/swarm-routing/BeeAdHoc/graph/badge.svg)](https://codecov.io/gh/swarm-routing/BeeAdHoc)
[![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/swarm-routing/BeeAdHoc?utm_source=oss&utm_medium=github&utm_campaign=swarm-routing%2FBeeAdHoc&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)](https://coderabbit.ai)
<!-- org-sync:end badges -->

BeeAdHoc, the honeybee-inspired routing protocol of
[Wedde & Farooq (2005)](https://doi.org/10.1145/1068009.1068034),
as specified by the PG 460 report's Linux kernel module (scouts, foragers
drawn from per-destination forager arrays, a battery (energy) metric only; #16),
as a simulator-agnostic `core/` plus the ns-3 contrib module `beeadhoc` on
[`swarm-core`](https://github.com/swarm-routing/ns3-swarm-core). **Status:**
the org's Phase 4. `core/` (#36) has route discovery, the data path (the
versioned source-route trailer, FORWARD-chain levels, the INPUT chain's
foreign foragers, piggybacking), swarms and link failure. The ns-3 module
`beeadhoc` (scout codec, adapter, helper, example, `test.py -s beeadhoc`, and
the `beeadhoc` arm of ns3-tools' `routing-compare`) carries all of it out:
every data packet carries the core's trailer, and the adapter sends swarms
and reacts to a link failure (#47). The gates come next (#41, #47). What the source specifies, row by
row, and what is still open: [docs/fidelity.md](docs/fidelity.md).

**Release status:** the first release, v0.1.0, comes before the comparator gates (maintainer decision 2026-11-10): it has **not** cleared them (org golden rule 6: fidelity sheet, per-PR delivery smoke, anchor). v0.1.0 shipped `core/` (route discovery only) and `docs/`, without an ns-3 module; `main` has since added the ns-3 module `beeadhoc` and the rest of `core/` (Status above), unreleased. The anchor is pending, so no number from v0.1.0 or from `main` counts as a benchmark result. The gated release is proposed as v0.2.0. The release ladder, with each release's exit criteria, is in [docs/roadmap.md](docs/roadmap.md). Releases are cut by [`release.yml`](https://github.com/swarm-routing/BeeAdHoc/blob/main/.github/workflows/release.yml) and listed under [Releases](https://github.com/swarm-routing/BeeAdHoc/releases).

How to work here, for people and AI agents alike: [AGENTS.md](AGENTS.md) has
the rules and how to build and verify; the org conventions are in
[CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md).

## Try it

`core/` alone, with no ns-3 tree (CMake and a C++20 compiler; it fetches
swarm-core's `core/` at the pinned commit):

```bash
make core-test    # cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core
```

The ns-3 module, in the org's `ns3-dev` image (docker), with
[`swarm-routing/ns3-swarm-core`](https://github.com/swarm-routing/ns3-swarm-core)
checked out beside this repository. The test suite (`test.py -s beeadhoc`):

```bash
docker run --rm -v "$PWD:/src/beeadhoc:ro" -v "$PWD/../ns3-swarm-core:/src/ns3-swarm-core:ro" \
    ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib --strict-warnings --quality-modules beeadhoc --test \
    swarm-core=/src/ns3-swarm-core beeadhoc=/src/beeadhoc/ns3
```

The example, a small random-waypoint MANET (replace `--test` with `--run`):

```bash
docker run --rm -v "$PWD:/src/beeadhoc:ro" -v "$PWD/../ns3-swarm-core:/src/ns3-swarm-core:ro" \
    ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib --run "beeadhoc-example --seed=1" \
    swarm-core=/src/ns3-swarm-core beeadhoc=/src/beeadhoc/ns3
```

The `beeadhoc` arm of the comparison harness, with
[`swarm-routing/ns3-tools`](https://github.com/swarm-routing/ns3-tools) also
checked out beside this repository:

```bash
docker run --rm -v "$PWD:/src/beeadhoc:ro" -v "$PWD/../ns3-swarm-core:/src/ns3-swarm-core:ro" \
    -v "$PWD/../ns3-tools:/src/ns3-tools:ro" ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib \
    --run "routing-compare --scenario=paper --nNodes=20 --time=30 --runs=1 --protocols=beeadhoc" \
    swarm-core=/src/ns3-swarm-core routing-bench=/src/ns3-tools/ns3/routing-bench beeadhoc=/src/beeadhoc/ns3
```

More (sanitizers, the pin, benchmark campaigns): [AGENTS.md](AGENTS.md) "Build & verify".

<!-- org-sync:begin citing -->
## Releases & citing

Versions follow [SemVer](https://semver.org) ([CHANGELOG.md](CHANGELOG.md)). Each release is listed under [Releases](https://github.com/swarm-routing/BeeAdHoc/releases), built by the org's release workflow and archived on [Zenodo](https://zenodo.org) with its own DOI.

To cite this repository, use the “Cite this repository” button (from [CITATION.cff](CITATION.cff)):

| DOI | resolves to |
|---|---|
| [`10.5281/zenodo.24124821`](https://doi.org/10.5281/zenodo.24124821) | **concept DOI**: always the latest release. This is the badge above, and the right citation when you mean this project. |

Each release also has its own **version DOI**, listed on [its Zenodo record](https://doi.org/10.5281/zenodo.24124821): cite that one when you reproduce a number measured at that release.

Please also cite the work it builds on:

- M. Farooq, H. F. Wedde, *BeeAdHoc: an efficient, secure and scalable routing framework for mobile adhoc networks*, Technische Universität Dortmund, 2006, [doi:10.17877/DE290R-14312](https://doi.org/10.17877/DE290R-14312). The version this repository implements (Project Group 460's final report, its Linux kernel module, chs. 12-13): the source of every row in docs/fidelity.md.
- D. H. Joppi, *ns3-swarm-core*, 2026, [doi:10.5281/zenodo.24138628](https://doi.org/10.5281/zenodo.24138628). The shared primitives core/ builds on (pinned by commit: SWARM_CORE_REF) and the ns-3 base the adapter derives from (SwarmRoutingBase).

What this implementation reproduces from its sources, and every deliberate deviation, is in [docs/fidelity.md](docs/fidelity.md).

Using it in your work? Contributions are welcome, and citing the DOI helps just as much.
<!-- org-sync:end citing -->

## Epics

<!-- epics:begin -->
<!-- Generated by swarm-routing/.github tools/epics/epics.py from the issues labelled `epic`; edit the issues, then re-run it. -->

What is planned and in progress here: the issues labelled [`epic`](https://github.com/swarm-routing/BeeAdHoc/issues?q=label%3Aepic). An epic's open sub-issues carry a `priority:*` and a `model:*` label, so a contributor or an AI agent can pick one up ([how issues work](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#issues)). The whole org's plan: the [roadmap](https://github.com/swarm-routing/.github/blob/main/ROADMAP.md#epics).

| Epic | Status | Sub-issues closed |
|------|--------|-------------------|
| [#3](https://github.com/swarm-routing/BeeAdHoc/issues/3) BeeAdHoc as a standalone hexagonal protocol (Phase 4) | In progress | 5 / 7 |

<!-- epics:end -->

## Licence

See [LICENSING.md](LICENSING.md).
