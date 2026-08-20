# 1. Kith is a separate Loam-ecosystem app, not a `loam_core` feature

- **Status:** Proposed
- **Date:** 2026-08-20

## Context

A feature request for Scala ("add contacts, so I can pick a person to make an editor") raised a
question of *where* contacts belong. Two readings:

1. **Narrow** — a directory of *cryptographic identities* (address/pubkey + label), a thin extension
   of `loam_core`'s existing `IdentityStore` (which holds *your own* signing identities).
2. **Broad** — a real **address book**: name, phones, emails, messaging handles, postal addresses,
   notes, avatar — where a Loam pubkey is *one optional field* among many.

The request is the broad one. That is an application with its own data model, UI, import/export, and
lifecycle — not an identity primitive. Several ecosystem apps (Scala editors, Qaku admins, KYM
members) independently need "pick a person," and duplicating a contact list in each is three
inconsistent copies of the same people.

## Decision

Build **Kith**, a standalone Loam-ecosystem app (a sibling to Scala / KYM / Qaku / Perun) that owns the
address book. `loam_core` stays minimal and security-critical — *your* signing identities plus
authorship verification. Kith is the human/CRM layer on top.

**The dependency points Kith → Loam, and never back.** `loam_core` must not learn about Kith; Kith
consumes loam_core (identity), loam-transport (sync), and logos-sync (CRDT).

Name: **Kith** (Old English "kith and kin" — the people you know). App id follows the ecosystem
convention: `xyz.vpavlin.kith`.

## Consequences

- One shared address book, consumed by every app — no per-app contact silos.
- `loam_core` keeps a tight security surface; the rich, fast-moving PIM code lives in Kith.
- Kith is "just another Logos app," so it reuses the whole build/sync/publish pipeline (see
  [0006](0006-architecture-and-platforms.md)).
- A new app to brand, build, and maintain — justified by cross-app reuse and the standalone value of a
  local-first, p2p address book.

## Alternatives considered

- **A `loam_core` contacts store** — rejected: pulls rich, churny PIM data and UI into a
  security-critical identity module, and still needs a UI somewhere.
- **Per-app contacts (in Scala, in Qaku…)** — rejected: duplicated, inconsistent, and each app would
  reinvent import/export and identity capture.
