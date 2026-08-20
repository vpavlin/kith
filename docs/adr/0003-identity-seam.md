# 3. The identity seam — Loam owns WHO, apps own WHAT

- **Status:** Proposed
- **Date:** 2026-08-20

## Context

The point of contacts-with-identities is to grant people roles in *other* apps: Scala editors, Qaku
admins, KYM members. We must decide where the line sits between "a person" and "what that person is
allowed to do," so responsibilities don't smear across apps.

## Decision

**Loam owns WHO; each consuming app owns WHAT.**

- **WHO (Kith + loam_core):** the directory of people and their identities. Kith holds the rich record;
  the `loamIdentity{address, pubHex}` is the verifiable handle. `loam_core` remains the authority on
  *your own* signing identities and on verifying that an event was authored by a given address.
- **WHAT (each app):** roles and access control stay in the consuming app's own state. Scala writes a
  contact's `address` into a calendar's `editors` set; Qaku into a room's `admins`; the ACL and its
  fold rules live in that app, exactly as they do today. Kith never models "editor" or "admin."

Consumption is **two-tier**:

1. **Human reference** — any app can show a contact's name / email / handle (a display concern). No
   identity required.
2. **Cryptographic principal** — only a contact that carries a `loamIdentity` can be granted a
   verifiable role. The app takes the `address`/`pubHex` and writes it into its ACL; because that is
   the same key that signs the person's events, the app can later *verify* their writes with the same
   value it used to *authorize* them. Contacts without a `loamIdentity` simply do not appear in an
   "add editor / add admin" list — you cannot grant edit rights to a phone number.

**Trust is out-of-band.** A `loamIdentity` is public data; that a given pubkey really belongs to
"Alice" is established by how it was added (a scanned QR/pairing code, or captured from an event she
authored — [0007](0007-app-integration-and-add-author.md)). Kith records this as `verified` +
`addedVia`; apps may choose to require `verified` before granting sensitive roles.

## Consequences

- Clean separation: Kith and loam_core never grow app-specific role logic; apps never grow a contacts
  database.
- One key does double duty (authorize + verify), so "who I granted" and "who actually wrote" can never
  drift apart.
- Apps decide their own trust bar (e.g. require `verified` for admins) without Kith dictating policy.
- Revocation is an app concern (remove the address from its ACL); deleting a Kith contact does not
  retroactively revoke roles already written into other apps' state.
