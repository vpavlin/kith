# 4. Sync, storage, and address-book scopes

- **Status:** Proposed
- **Date:** 2026-08-20

## Context

Kith must sync a person's address book across *their* devices (local-first, offline-tolerant), and
later support address books shared between several people (a household, a team). This is the same
multi-writer-convergence problem the other ecosystem apps already solved.

## Decision

**Reuse the ecosystem sync stack unchanged:**

- **[logos-sync](https://github.com/vpavlin/logos-sync)** — event-log CRDT + HLC. Every change is an
  append-only event (`contact.field.set`, `contact.delete`, …); the current book is the fold. Ordering
  by HLC, field-level last-write-wins ([0002](0002-contact-data-model.md)). Cold-start / catch-up via
  the same RBSR + store-pull path the other apps use.
- **[loam-transport](https://github.com/vpavlin/loam-transport)** — carries the event log over the
  shared Loam delivery node (SDS Reliable Channels), one content topic per address book.
- **loam_core identity** — signs each edit event, so a shared book can verify who changed what.

**An address book has a scope:**

- **Personal (default)** — single-writer, but multi-*device*: the book syncs across your own devices
  only. One content topic derived from a book id you hold; no one else has the key.
- **Shared (later phase)** — multi-writer: several people converge on one book (household/team). Same
  CRDT, membership = who holds the book's key (mirrors how Scala/KYM share a calendar/budget). Per-book
  membership, not per-contact ACLs.

**Storage:** the event log + folded snapshot persist locally on each device (the durable copy), same
as the other apps. Avatars are bounded blobs ([0006](0006-architecture-and-platforms.md)).

## Consequences

- Almost no new sync code — Kith is another logos-sync + loam-transport consumer.
- Personal-first means privacy by default: your book is yours, on your devices, encrypted on the wire.
- Shared books fall out of the same machinery when we want them; the model doesn't change, only who
  holds the key.
- Signing edits (via loam_core) means a shared book is tamper-evident: every change is attributable.

## Open questions

- One log for the whole book vs. per-contact topics — start with **one log per book** (simpler,
  matches Scala's per-calendar log); revisit only if books get very large.
- Conflict UX when the same field is edited on two offline devices — LWW resolves it silently; surface
  a "recently changed" hint rather than a merge dialog.
