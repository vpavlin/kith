# 10. Durable snapshots & storage-backed seeding (Logos Storage / Codex)

- **Status:** Exploratory — depends on Logos Storage (Codex) maturity; prototype and verify before
  depending on it.
- **Date:** 2026-08-20

## Context

The recurring pain across the whole ecosystem is **cold-start seeding**: a fresh device replays the
full event log from a *live* peer, bounded by the messaging fleet store's retention window. Every
"joined but no history" issue (Waku store-pull, RBSR, the shared-node storeSync proxy) is a variant of
this. **Logos Storage (Codex)** — decentralized, content-addressed durable storage — offers a cleaner
path: publish state once, seed a new device from a single **CID**.

## Decision (proposed, to prototype)

Split responsibilities: **messaging carries pointers, storage carries payloads.**

```
Waku / loam-transport  →  live tail + small coordination events + CID pointers
Logos Storage (Codex)  →  encrypted bulk: snapshots, avatars, durable history
```

**Seeding a new device becomes:**
1. A device folds the book to a **snapshot** (state up to an HLC watermark), **seals it** with the book
   content key ([0009](0009-device-pairing-and-membership.md)), stores it in Codex → gets a **CID**.
2. It publishes a tiny `snapshot.published{cid, uptoHlc}` event over Waku.
3. A new device fetches that **one CID**, decrypts it with the membership-wrapped content key, then
   replays only the **tail** (events after `uptoHlc`) over Waku.

No live peer re-serves, no dependence on the store's retention window, and seeding is one fetch instead
of a fragile full replay. Two bonuses fall out: **avatars** (ADR [0006](0006-architecture-and-platforms.md)'s
bounded blobs) move to Codex (content-addressed, fetched on demand, keeping the log light), and the
encrypted book gains a **durable backup** that survives even if all your devices are offline.

**Guardrails:**
- **Encryption is mandatory.** Codex is public storage — store **ciphertext only**. The CID is public;
  the decryption key travels through the membership model ([0009](0009-device-pairing-and-membership.md)),
  never into Codex. (This is why 0009 lands first — it already produces the wrapped key.)
- **Pluggable, not hard-coupled.** Implement a `DurableSnapshotStore` provider behind the sync seam
  (logos-sync already abstracts catch-up). Codex is one provider; a plain blob store or the existing
  Waku store-pull are fallbacks. **Ship v1 on the proven Waku store-pull; slot Codex in as an
  enhancement** when it's viable.
- **Verify before building.** Codex is early/testnet — confirm its real API, persistence/pinning
  guarantees, and availability against the Logos docs before committing (do not design to assumptions).

## Consequences

- Instant, reliable seeding decoupled from messaging retention; a lighter event log; durable backup.
- The pattern is **not Kith-specific** — Scala/KYM/Qaku share the identical cold-start pain, so proving
  it behind the sync seam generalizes into a shared logos-sync capability. Kith is a good greenfield
  **pilot**.
- Adds an external dependency — kept optional and pluggable precisely because Codex is not yet proven.

## Open questions

- Codex's actual API, durability/pinning guarantees, and maturity (**verify in the Logos docs first**).
- Snapshot cadence and log **compaction** (when to snapshot; prune events below the watermark).
- **Garbage-collecting** superseded snapshots; who pins/keeps them alive.
- Key rotation vs old snapshots — a revoked member can still read snapshots they already had the key
  for (same forward-only caveat as [0009](0009-device-pairing-and-membership.md)); rotation protects
  future snapshots, not past ones.
