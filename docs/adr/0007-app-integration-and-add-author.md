# 7. App integration (contact picker) + "add author to contacts"

- **Status:** Proposed
- **Date:** 2026-08-20

## Context

Kith only earns its keep if other ecosystem apps actually use it — both to *read* it (pick a person to
grant a role) and to *feed* it (capture people they already know about). We need a small, stable query
API for the read direction, and a concrete mechanism for the write direction, without either side
leaking into the other's concern ([0003](0003-identity-seam.md): Kith owns WHO, apps own WHAT).

## Decision

**App-facing query API**, exposed by the `kith` core ([0006](0006-architecture-and-platforms.md)):

- `listContacts()` — the full folded address book (or a scoped book, [0004](0004-sync-and-address-book-scopes.md)).
- `pickContact()` — a shared contact-picker UI/intent: Kith presents its own list/search UI and returns
  the contact the user chose. This is the primary integration point — an app never renders its own
  contact list.
- `findByAddress(address)` — look up a contact by its `loamIdentity.address`, e.g. to resolve "who wrote
  this" back to a display name.
- A **principals-only** query (`pickContact({ requireIdentity: true })` or an equivalent filter) that
  restricts the picker to contacts carrying a `loamIdentity` — the grantable principals from
  [0003](0003-identity-seam.md)'s two-tier consumption. An "add editor" flow uses this; a "share via SMS"
  flow does not.

`pickContact()` returns a **reference/snapshot, not a live binding** — the chosen contact's fields at
the moment of picking (at minimum `address`/`pubHex` for a principal, or name/email/phone for a human
reference). The consuming app copies what it needs into its own state (e.g. an `address` into a
calendar's `editors` set); it does not hold a pointer back into Kith. If the contact's name changes
later, that's Kith's data, not the app's.

**The "add author to contacts" flywheel.** Every event in a shared app (a Scala calendar entry, a Qaku
post) already carries its author's `pub`/`dev`. A one-tap **"Add to Kith"** action, offered by the
consuming app on any event, hands Kith a minimal contact payload:

```jsonc
{ name?, address, pubHex, addedVia: "event-author", verified: false }
```

This is exactly the shape from [0002](0002-contact-data-model.md)'s `loamIdentity` fields. It starts
`verified: false` — receiving a well-formed event proves the address signed it, not that "Alice" is the
right name for it ([0003](0003-identity-seam.md): trust is out-of-band). The user (or a later scanned
QR/pairing exchange) can mark it verified. This is how a book of verifiable people accumulates
organically, just from normal collaboration, instead of manual entry.

**Integration is two directions, both go through the same seam:**

1. **Read** — an app calls `pickContact()` (optionally principals-only) to grant a role; it stores the
   `address` in its own ACL and is done with Kith until the next pick.
2. **Write** — an app offers "Add author to Kith," handing over a minimal payload on an event's author.
   Kith owns storing and folding it; the app never touches Kith's data model beyond this one call.

Kith never learns about roles; apps never grow contact storage.

## Consequences

- One picker implementation, reused by every consumer, instead of N apps each rendering ad hoc contact
  lists — matches the motivation in [0001](0001-kith-is-a-loam-ecosystem-app.md).
- The "add author" flow makes `verified: false` the common case for organically-added contacts; apps
  that gate sensitive roles on `verified` ([0003](0003-identity-seam.md)) will see fewer eligible
  principals than the raw address book — expected, not a bug.
- Because the picker returns a snapshot, apps must not assume freshness; a `findByAddress` re-lookup is
  the way to get current data later.

## Open questions

- Is `pickContact()` a Basecamp cross-module call (`logos.callModule("kith", "pickContact", …)`) or a
  shared QML component the host embeds directly? Cross-module call keeps Kith as the single owner of the
  picker UI/UX; a shared component may render faster but duplicates the UI per app.
- How does a mobile app invoke the picker — a deep link/intent to the Kith app, or an in-process shared
  library? Needs to work even if Kith isn't already running.
- Should `pickContact()` support multi-select (e.g. inviting several editors at once)?
