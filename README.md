# Kith

**Your people, in the Loam soil.**

Kith is the contacts / address-book app of the Loam ecosystem — a local-first, p2p address book
where each contact is a rich record (name, phones, emails, messaging handles, notes) that can
optionally carry a **Loam cryptographic identity**. That one optional field is what turns "a name and
a phone number" into a **grantable principal**: someone [Scala](https://github.com/vpavlin/scala) can
make a calendar *editor*, [Qaku](https://github.com/vpavlin/qaku) an *admin*, KYM a household *member*.

> The name is Old English — *kith* (as in "kith and kin"): the people you know, your acquaintances and
> neighbours, as distinct from *kin*, your blood family. Loam is the soil; Kith is your people in it.

## The idea in one picture

```
        ┌──────────────────────────────────────────────┐
        │  Kith  (the directory — WHO)                   │
        │  Contact = { name, phones[], emails[],         │
        │              handles[], notes, avatar,         │
        │              loamIdentity?{address,pubHex} }   │
        └───────────────┬───────────────┬────────────────┘
      human reference   │               │  cryptographic principal
     (name/email/phone) │               │  (only if loamIdentity present)
                        ▼               ▼
                 ┌───────────┐    ┌───────────┐
                 │  Scala    │    │  Qaku     │   each app owns WHAT (its ACL/roles)
                 │  editors  │    │  admins   │
                 └───────────┘    └───────────┘
                        └──────── built on ────────┘
                 Loam: identity (signs your edits) · transport (p2p sync) · logos-sync (CRDT)
```

**Loam owns WHO. Each app owns WHAT.** Kith is the shared directory; the role a person plays
(editor / admin / member) is each consuming app's own concern.

## Why a separate app (not a `loam_core` feature)

A real address book is rich PIM data — phones, emails, Telegram/Signal/Matrix handles, addresses,
notes, avatars — of which the Loam pubkey is *one* field. That's an application, not an identity
primitive. `loam_core` stays minimal and security-critical (your signing identities + authorship
verification); Kith is the human/CRM layer on top. **Dependency points Kith → Loam, never back.**

## Built on what already exists

Kith reuses the ecosystem substrate almost wholesale:

- **[logos-sync](https://github.com/vpavlin/logos-sync)** — event-log CRDT + HLC. A contact is a record
  folded from edit events, last-write-wins per field. Personal book = single-writer; a shared/household
  book = multi-writer, for free.
- **[loam-transport](https://github.com/vpavlin/loam-transport)** — syncs your book across *your*
  devices, local-first p2p, over the shared Loam delivery node.
- **loam_core identity** — signs your contact edits; provides the `loamIdentity` field's semantics
  (address = `sha256(compressed-pub)[24:64]`, the same address that signs a person's events).

Shipped like the siblings: a **Basecamp core + view module** (desktop) and a **React-Native mobile
app**, both over logos-sync + loam-transport.

## Two things baked in early

- **vCard import/export** — interop with phone contacts and the wider world; `loamIdentity` maps to an
  `X-LOAM-KEY` extension field. Big adoption lever.
- **"Add author to contacts"** — receive an event in a shared calendar → one tap captures that author
  (name + pubkey) into Kith. This is the flywheel: the more you collaborate, the richer your book of
  *verifiable* people becomes.

## Status

**Design phase.** This repo currently holds the design doc (this file) and a starter set of ADR
proposals under [`docs/adr/`](docs/adr). Nothing is built yet. See
[`docs/adr/0001`](docs/adr/0001-kith-is-a-loam-ecosystem-app.md) for the framing decision.

## The ADRs

| # | Decision | Status |
|---|----------|--------|
| [0001](docs/adr/0001-kith-is-a-loam-ecosystem-app.md) | Kith is a separate Loam-ecosystem app, not a `loam_core` feature | Proposed |
| [0002](docs/adr/0002-contact-data-model.md) | Contact data model — rich PIM fields + optional `loamIdentity` | Proposed |
| [0003](docs/adr/0003-identity-seam.md) | The identity seam — Loam owns WHO, apps own WHAT | Proposed |
| [0004](docs/adr/0004-sync-and-address-book-scopes.md) | Sync, storage, and address-book scopes (personal vs shared) | Proposed |
| [0005](docs/adr/0005-vcard-interop.md) | vCard import/export + the `X-LOAM-KEY` extension | Proposed |
| [0006](docs/adr/0006-architecture-and-platforms.md) | Architecture & platforms — Basecamp module + mobile | Proposed |
| [0007](docs/adr/0007-app-integration-and-add-author.md) | App integration (contact picker) + "add author to contacts" | Proposed |
