# 11. The mobile engine is a separate package: kith-sdk

- **Status:** accepted
- **Date:** 2026-10-10

## Context

Other apps need Kith's people (Frequencies links artists from a Kith book to its nights,
[0007](0007-app-integration-and-add-author.md)). On desktop they call the `kith` core module. On a
phone there is no core module to call: each app runs the engine itself, so it needs the code, and a copy
would drift from the C++ fold.

## Decision

- The contents of `mobile/src/lib` move, with their history, to
  [`vpavlin/kith-sdk`](https://github.com/vpavlin/kith-sdk); loam-transport becomes its submodule.
- The Kith app mounts it as a submodule **at the same path**, so imports, tests and the Expo plugin path
  don't change. Clones need `--recursive`.
- `setAppId(id)` (new) lets an embedding app register with Loam as itself; the default stays `kith`.
- The parity tests stay here (they compile this repo's C++ fold). An engine change is an SDK commit
  first, then the submodule bump here with the tests passing.

## Consequences

- One engine for every app that reads Kith books; a fix lands by bumping the submodule.
- An app embedding several engines must resolve them to one loam-transport (see the SDK README).
