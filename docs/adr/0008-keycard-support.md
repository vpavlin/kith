# 8. Keycard support

- **Status:** Proposed
- **Date:** 2026-08-20

## Context

A contact's value as a *principal* rests on a key ([0002](0002-contact-data-model.md),
[0003](0003-identity-seam.md)). The strongest form of that key is a **Status Keycard** — the private
key lives on the card, events are signed on-card with a tap, the key never leaves the chip. The
ecosystem already has the whole Keycard stack: `loam_core` enrols a card and signs digests with it
(phone over NFC, desktop over PC/SC), and `keycard-ui` handles the PIN/tap with implicit-unlock
(unlock once, then just tap). Kith should ride all of it, not reinvent any of it.

There are three distinct places a card shows up in a contacts app, and they must not be conflated.

## Decision

**Kith consumes the existing Keycard stack via `loam_core`; it implements no card crypto itself.**

1. **Authoring with a Keycard.** Kith authors its edit events through `loam_core` identities exactly as
   Scala does ([0006](0006-architecture-and-platforms.md), [0003](0003-identity-seam.md)) — device /
   soft / **keycard**, one bound per book. Binding a book to a keycard identity means every edit is
   **card-signed**: for a **shared / household** book ([0004](0004-sync-and-address-book-scopes.md))
   that makes the log tamper-evident and every change attributable to a person, not a device. It
   inherits the platform UX unchanged — desktop implicit-unlock (the keycard-ui PIN cache), phone
   tap-per-sign. Keycard is **opt-in per book** (bound at create), never a silent default.

2. **Your published identity can BE your Keycard.** The `loamIdentity` you attach to your *own* contact
   card — the one you export in a vCard (`X-LOAM-KEY`, [0005](0005-vcard-interop.md)) or share to
   someone — can be your keycard address/pubkey. Others then add you as a **card-backed** verifiable
   principal; when you later act as an editor in their Scala calendar or an admin in their Qaku room,
   your writes are card-signed. **One card = one identity** across phone (NFC), desktop (PC/SC), and
   Kith — the address is `sha256(compressed-pub)[24:64]` regardless of where the key sits.

3. **Card-backed contacts are transparent.** A contact's `loamIdentity.pubHex` is just a public key;
   whether the *owner* keeps it on a card is their concern, invisible to the wire. "Add author to
   contacts" ([0007](0007-app-integration-and-add-author.md)) and authorship verification work
   **identically** for card-signed events — a card sig verifies like any other. Kith MAY surface a
   "card-backed" badge when that signal is available, but never *depends* on it and never treats a
   non-card identity as lesser.

**Scope boundary.** Kith does not talk to the card, hold a PIN, or derive paths — that is `loam_core` +
the `keycard` / `keycard-ui` modules. **Encryption *to* a contact** (sealing a shared card or a private
note to their key) is explicitly **out of scope here**: the on-card signing key uses the
non-exportable `1582'` path, so encryption would need the separate `1581'`/auth path and a real
key-agreement design — a future ADR, not this one.

## Consequences

- Zero new security surface in Kith — it reuses the audited Keycard path (`loam_core` enrol/sign,
  `keycard-ui` unlock). All the enrol/sign correctness already solved upstream applies unchanged.
- Shared address books gain tamper-evidence for free: bind the book to a card, and every edit is
  attributable to a person.
- A person's hardware identity is portable *into* their contact card and back out via vCard, so
  card-backed trust travels with the person across the ecosystem.
- Kith stays a pure `loam_core` consumer — the dependency direction from
  [0001](0001-kith-is-a-loam-ecosystem-app.md) holds; no card code leaks into the contacts app.

## Open questions

- Per-book identity-binding UX — the "which identity signs this book?" picker, shared with the pattern
  Scala/Qaku use; probably a common component.
- Whether to display a "card-backed" badge on a contact, and where that signal comes from (the enrolling
  side knows; a received event does not inherently say "this was a card").
- A person with *both* a soft key and a card — is that one contact with two identities, or the card
  supersedes? Likely: one contact, a primary `loamIdentity` plus optional alternates — defer to a
  data-model follow-up if it proves needed.
