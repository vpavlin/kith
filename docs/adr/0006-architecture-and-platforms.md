# 6. Architecture & platforms — Basecamp module + mobile app

- **Status:** Proposed
- **Date:** 2026-08-20

## Context

Kith should run where the other ecosystem apps run — on the desktop inside Logos Basecamp and on a
phone — and be built and shipped the same way, so it inherits the existing tooling rather than
inventing new.

## Decision

Ship Kith in the established two-surface shape:

- **Desktop — a Basecamp core + view module pair**, exactly like Scala:
  - `kith` (core): universal-authoring engine — the CRDT fold over the event log, contact CRUD,
    vCard import/export, and the query surface apps call (`listContacts`, `pickContact`,
    `findByAddress`). Depends on `loam_core` (identity/signing) and the delivery module (sync).
  - `kith_ui` (view): a pure-QML view (Logos design system) — list, detail/edit, import/export, and
    the identity badge on contacts that carry a `loamIdentity`.
  - Built with logos-module-builder into `.lgx`, published to the Basecamp package repo(s).
- **Mobile — a React-Native / Expo app** on `loam-transport` (shared node) + `logos-sync`, published
  to F-Droid (`xyz.vpavlin.kith`), same pipeline as qaku/kym/scala.

**Shared engine.** Put the contact model, fold, and vCard logic in a portable core reused by both
surfaces (the desktop `kith` core and the mobile app), so there is one source of truth for "what a
contact is and how it folds" — mirroring how the ecosystem shares sync logic via logos-sync.

**Assets.** Avatars are small, bounded blobs stored in the log (or a side-store) with a hard size cap
(e.g. ≤64 KB, downscaled on import); Kith must stay light.

**Naming/ids.** Package/app id `xyz.vpavlin.kith`; core module `kith`, view `kith_ui`. Content-topic
namespace `/kith/1/<bookId>/…` for sync.

## Consequences

- Reuses the entire build/publish toolchain (logos-module-builder, `.lgx`, F-Droid, the LAN + public
  repos) — no new infrastructure.
- Core/view split means the same version-skew discipline as Scala (a view method needs the core
  updated too) — call it out in build docs.
- A portable shared engine avoids two diverging implementations of the fold/vCard.

## Open questions

- Whether the desktop core and mobile share a literal C++/TS core or parallel implementations kept in
  lockstep by a conformance test — decide at scaffold time, following whatever logos-sync makes
  cheapest.
- A headless hub (like the other apps) for always-on multi-device sync — likely yes, later.
