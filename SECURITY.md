# Security Policy

`swarm-routing/BeeAdHoc` is research code for network simulation. This document records its
security posture honestly; it promises nothing the code does not do. The
org-wide policy is [swarm-routing/.github SECURITY.md](https://github.com/swarm-routing/.github/blob/main/SECURITY.md).

## Trust boundary

<!-- Which input is untrusted, the only code that parses it, and what that
code guarantees. A protocol: its wire-format decoder (org invariant 3). A
tool: the parsers of files it reads. If nothing parses untrusted input, say
so in one line. -->
The decoder of the scout header (`core/beeadhoc-scout.cc`, read in ns-3
through `ns3/model/beeadhoc-header.cc`), built on `swarm-core`'s
bounds-checked little-endian readers with roundtrip and seeded fuzz tests
(org invariant 3). It refuses a wrong version, an unknown mode, info bits
above 24, a route longer than `MaxHops` + 1 nodes, a short buffer or trailing
bytes. The data-path trailer and the swarm packet will be decoded the same
way when they are written (#36).

## Known, by-design limitations (out of scope)

- **BeeAdHoc has no authentication** (Wedde & Farooq, 2005). Any node can forge
  scouts or foragers, and the battery and delay metrics they carry are
  self-reported, so a node can lie about its energy to attract or repel
  traffic. Out of scope; AntHocNet ADR-0020's default-off profile applies if a
  security profile is ever added.
- A simulation protocol for ns-3, not a network stack for real radios.

<!-- org-sync:begin reporting -->
## Reporting a vulnerability

Report privately through GitHub's private vulnerability reporting: the
repository's **Security** tab, then **Report a vulnerability**. If that is
not available, open an issue that only asks for a private contact, and put
no vulnerability details in it. This is a research project maintained on a
best-effort basis: expect an acknowledgement within a few days, not an SLA.
<!-- org-sync:end reporting -->

## Verifying what it ships

Nothing is released yet, so there is nothing to verify.
