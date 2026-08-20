# 9. Device pairing & membership

- **Status:** Proposed
- **Date:** 2026-08-20

## Context

Two things must "pair" in Kith: your **own devices** (a personal book that syncs across phone +
desktop) and, later, **other people** (a shared / household book). The rest of the ecosystem
(KYM, Scala) does this with a **bearer capability** — a QR/link carrying `{bookId, key}` (Scala's
`scala://join?id=…&key=…`). Whoever holds it joins the topic and decrypts.

That is simple and proven, but for a long-lived, personal address book its weaknesses bite:

- it's a **bearer secret** — anyone who ever sees the link has **permanent** access;
- there is **no per-device / per-person identity** — you can't see who is in;
- you cannot **revoke one member** — you rotate the key and re-share to everyone.

Kith is the identity/contacts app, so it can afford to do this properly — and, crucially, the
"smarter" path is often also the *simpler* one, because *who to share with is already a keyed contact*.

## Decision

**Default to identity-rooted, key-wrapped membership; keep a bearer code only as a bootstrap fallback.**

A book has a **content key** (seals its events/snapshots). Membership grants that key by **wrapping it
to a principal's public key**, recorded as a signed event in the log — never by putting the raw key on
the wire.

1. **Share with a person → often no code at all.** You (usually) already have them as a contact with a
   `loamIdentity` ([0003](0003-identity-seam.md), [0007](0007-app-integration-and-add-author.md)). Pick
   them → **wrap the content key to their pubkey** → record a signed `member.add{pubHex, wrappedKey}`.
   They unwrap with their key. **Revoke = drop their wrap + rotate the content key** (individually, not
   everyone). The "add author" flywheel means the people you'd share with are already keyed.

2. **Link your own devices → identity-rooted, no shared secret.** Each device holds its own key (the
   loam device identity already exists). The new device shows a QR of its **device pubkey**; the
   existing device verifies a short **SAS** (out-of-band, to defeat MITM), wraps the content key to the
   new device's pubkey, and signs `device.authorize`. The content key never appears in a QR — only a
   public key does — and each device is individually revocable.

3. **Keycard as root of trust (when present).** The card signs the device-authorization certs — the
   **delegation-cert** primitive from the keycard work ([0008](0008-keycard-support.md)): a scoped,
   **expiring** `{delegatePub, notAfter, scope}` authorizing a device key. Add a device with one tap;
   revoke by not renewing. Root never leaves the card.

4. **Bearer-code fallback (bootstrap only).** For sharing with someone who has **no** Kith identity yet,
   fall back to the KYM-style `{bookId, key}` link — explicitly the weaker path, flagged as such in the
   UI, and upgradeable to a wrapped membership once they have an identity.

## Consequences

- **Per-principal / per-device revocation** and visible membership — impossible with a pure bearer code.
- No long-lived secret on the wire (except the explicit fallback); the content key is wrapped, not
  shared.
- Reuses what exists: loam device identities + the keycard delegation cert / custody slider — no new
  crypto primitive.
- Slightly more machinery than a bare link, but it lands the properties an address book actually needs,
  and degrades gracefully to the simple case.

## Open questions

- **Rotation on revoke** — re-wrap the (new) content key to every *remaining* member; forward-secret
  for future events, but a revoked member can still read data they already had (expected; see
  [0010](0010-storage-backed-seeding.md) for the same caveat on old snapshots).
- Where wrapped keys live — inline in `member.add` events (simple, replayable) vs a separate keyring
  object.
- SAS UX for device linking (how the short code is shown/compared).
- Whether this membership model should be lifted into a shared logos-sync capability so Scala/Qaku/KYM
  can adopt it too.
