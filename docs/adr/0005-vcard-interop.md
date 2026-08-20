# 5. vCard import/export + the `X-LOAM-KEY` extension

- **Status:** Proposed
- **Date:** 2026-08-20

## Context

An address book that can't talk to the rest of the world is a dead end. People already have contacts
on their phones, in Google/Apple/Nextcloud, in `.vcf` files. Kith should let them bring those in and
take them out — and, uniquely, carry a Loam identity along for the ride.

## Decision

Support **vCard 4.0** (RFC 6350) import and export as a first-class feature, not an afterthought.

- **Field mapping** is direct: `FN`/`N` ↔ name, `TEL` ↔ phones, `EMAIL` ↔ emails, `ADR` ↔ addresses,
  `IMPP` ↔ handles (Telegram/Signal/Matrix as `IMPP` URIs where possible), `NOTE` ↔ notes, `PHOTO` ↔
  avatar. Labels map to vCard `TYPE` parameters.
- **The Loam identity travels as a private extension:** `X-LOAM-KEY` (and `X-LOAM-ADDR`). A Kith→Kith
  vCard round-trips the `loamIdentity` losslessly; a Kith→other-app vCard degrades gracefully (other
  tools ignore the `X-` fields and keep the normal contact data).

```
BEGIN:VCARD
VERSION:4.0
FN:Alice Example
EMAIL;TYPE=work:alice@example.org
IMPP:telegram:@alice
X-LOAM-KEY:0289abcd…            (compressed pubHex)
X-LOAM-ADDR:0x1234…            (derived address, informational)
END:VCARD
```

- **Import never fabricates identity.** A vCard without `X-LOAM-KEY` yields a contact with no
  `loamIdentity` (human info only). `verified` is set false and `addedVia:"import"`; a pubkey from an
  imported card is *claimed*, not *verified*, until confirmed out-of-band.
- **Export honours scope.** Exporting is a local action; sharing a single contact *to* someone over the
  transport ([0007](0007-app-integration-and-add-author.md)) reuses the same vCard payload.

## Consequences

- Instant adoption path: import your phone's contacts, keep using them, and enrich the few that have a
  Loam identity.
- Interop with the whole vCard world; the Loam bits are additive and non-breaking.
- We must be careful that an imported, unverified `X-LOAM-KEY` cannot silently become a trusted
  principal — hence `verified:false` on import and the app-side trust bar in
  [0003](0003-identity-seam.md).

## Open questions

- vCard 3.0 tolerance on import (older exporters) — accept and up-convert.
- Photo size/format normalisation on import ([0006](0006-architecture-and-platforms.md)).
