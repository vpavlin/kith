# 2. Contact data model — rich PIM fields + optional `loamIdentity`

- **Status:** Proposed
- **Date:** 2026-08-20

## Context

A contact must hold everyday address-book data AND, for some people, a verifiable Loam identity that
lets them be granted roles in other apps. The model must fold cleanly under an event-log CRDT
(multiple devices / writers, offline, last-write-wins) and round-trip through vCard
([0005](0005-vcard-interop.md)).

## Decision

A **Contact** is a record with multi-valued PIM fields and an **optional** cryptographic identity:

```jsonc
Contact = {
  id: "uuid",                     // stable, app-generated
  name: { display, given?, family?, org? },
  phones:   [{ label: "mobile|home|work|…", value }],
  emails:   [{ label, value }],
  handles:  [{ kind: "telegram|signal|matrix|xmpp|…", value }],
  addresses:[{ label, street?, city?, region?, postcode?, country? }],
  notes:    "free text",
  avatar?:  "small image ref (see 0006)",
  loamIdentity?: { address, pubHex, verified: bool, addedVia: "manual|qr|event-author" },
  createdAt, updatedAt            // HLC-derived
}
```

- **`loamIdentity` is optional and central.** A contact without it is human info you can read/call/DM.
  A contact *with* it is a **grantable principal** ([0003](0003-identity-seam.md)). `address` =
  `sha256(compressed-pubHex)[24:64]` — the same address that signs that person's events, so granting a
  role and verifying authorship use one key.
- **Field-level LWW.** Each field (or list entry, keyed by a stable sub-id) is folded last-write-wins
  by HLC, so two devices editing different fields of the same contact both win. See
  [0004](0004-sync-and-address-book-scopes.md).
- **Multi-valued by design.** Phones/emails/handles/addresses are lists of labelled entries, matching
  vCard and real life (two phones, work + personal email).

## Consequences

- Maps directly onto logos-sync events: `contact.set` / `contact.field.set` / `contact.delete`
  (tombstone), folded to the current record.
- The `loamIdentity` field is the single bridge to the rest of the ecosystem; everything else is
  ordinary PIM data.
- `verified` + `addedVia` record *how much to trust* a pubkey (see [0003](0003-identity-seam.md) and
  [0007](0007-app-integration-and-add-author.md)); they never gate reading the contact, only its use as
  a principal.
- vCard interop constrains field shapes toward vCard 4.0 equivalents ([0005](0005-vcard-interop.md)).

## Open questions

- Groups / tags / favourites (a `labels: []` field?) — likely yes, deferred to a later ADR.
- Avatar storage & size limits — see [0006](0006-architecture-and-platforms.md).
