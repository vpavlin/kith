# Kith — road to MVP

**MVP = an installable Basecamp app where you create a book, add / edit / delete contacts, see them
in a UI, and import/export vCards.** Multi-device sync and cross-app integration come right after.

Design lives in [`docs/adr/`](docs/adr) (ADRs 0001–0010). Implementation is driven in phases; each is a
self-contained task (often handed to a Sonnet subagent), verified and published before the next starts.

| Phase | What | Owner | Status |
|-------|------|-------|--------|
| 1 | **Core `kith`** — contact CRDT fold, book/contact store, core API (createBook, add/edit/deleteContact, listContacts, findByAddress, addAuthorToContacts), vCard import/export, loam_core-signed authoring | Sonnet | ✅ builds (`0.1.0`) |
| 2 | **View `kith_ui`** — pure-QML Basecamp view over the core: books list, contacts list + detail/edit, identity picker (bind book→identity), vCard import/export, add-author. Logos design system. **No blocking IPC in QML.** | Sonnet | ✅ builds (`0.1.0`) |
| 3 | **Package + publish** — build `kith` + `kith_ui` `.lgx`, publish to LAN + public Basecamp repos, verify installable | Opus | ✅ published (`0.1.0`) |
| 4 | **Sync** — loam-transport, one content topic per book, share/join a book (invite link), catch-up; the CalendarSync pattern from scala | Sonnet | ✅ published (`0.2.0`) |
| 5 | **Android app** — React-Native/Expo app on loam-transport + logos-sync, with the contact fold reimplemented in TS in lockstep with `kith_engine.hpp` (ADR 0006), published to F-Droid; the scala/qaku/kym mobile pattern (skills: logos-mobile-app, loam-integrate-app) | Sonnet | ▶ building |
| 6 | **Polish** — keycard-signed writes via a unified `loam_core.signAsync` (one async signing path for device/soft/keycard, so apps stop reimplementing the tap flow and kith stops silently device-signing a keycard book) (ADR 0008), `pickContact()` for cross-app (Scala editors / Qaku admins, ADR 0007), avatar bounding, full vCard (line folding, PHOTO), device pairing (ADR 0009) | mixed | ⏳ |

**MVP = phases 1–3** (usable, local, installable on desktop). Phase 4 makes it multi-device, phase 5
brings the phone (and needs a **shared-fold conformance test** so the TS and C++ folds can't drift —
ADR 0006's open question), phase 6 is the ecosystem payoff. Rules carried into every phase: match
scala's idioms, seeded RNG + dedup-by-id, no per-item blocking `logos.callModule` in QML,
`manifestVersion 0.3.0`.
