// Kith contact-book engine — the pure, deterministic fold from a merged event log
// to book state. This is a BYTE-FOR-BYTE mirror of the desktop core's
// src/kith_engine.hpp (event-log CRDT): every change is an immutable event;
// current state = fold over the merged log. Merge = union-by-id + HLC sort, so it
// is idempotent (redelivery is a no-op), commutative and associative (arrival
// order is irrelevant) — offline devices converge with no lost writes.
//
// One book == one log (kith ADR 0004: "one log per book"). A Contact (kith ADR
// 0002) is authored WHOLE-RECORD in v1 — contact.set carries the complete record,
// not a per-field diff (kith_engine.hpp's explicit v1 choice; field-level LWW is
// future work). Keep this in lockstep with kith_engine.hpp; the golden-vector
// parity test (test/parity.sh) guards the two against drift.
import { verifyEvent, isSigned } from "./identity";

// ── HLC (hybrid logical clock): total order wall → ctr → dev ─────────────────
export interface HLC {
  wall: number; // ms epoch (int64 on desktop; safe integer here)
  ctr: number;
  dev: string;
}
export function compareHlc(a: HLC, b: HLC): number {
  if (a.wall !== b.wall) return a.wall < b.wall ? -1 : 1;
  if (a.ctr !== b.ctr) return a.ctr < b.ctr ? -1 : 1;
  if (a.dev !== b.dev) return a.dev < b.dev ? -1 : 1;
  return 0;
}

// ── Event: the immutable unit. id (UUIDv4) is the idempotency/dedup key ──────
export interface Event {
  v: number;
  id: string;
  type: string;
  hlc: HLC;
  dev: string;
  payload: any;
  pub?: string; // author's 33B secp256k1 public key (hex) — authenticity layer
  sig?: string; // 64B ECDSA over the canonical event (hex); absent on legacy events
}

// Event type constants — keep in lockstep with kith_engine.hpp ET::.
export const ET = {
  // {id, name:{display,given?,family?,org?}, phones:[{label,value}], emails:[{label,value}],
  //  handles:[{kind,value}], addresses:[{label,street?,city?,region?,postcode?,country?}],
  //  notes, avatar?, loamIdentity?:{address,pubHex,verified,addedVia}}
  // Whole-record LWW upsert by payload.id — the latest contact.set for an id wins.
  CONTACT_SET: "contact.set",
  // {id} — tombstone. TERMINAL: a contact.set for a tombstoned id arriving (or
  // already in the log) after its contact.del is dropped, so a delete can never
  // be silently resurrected by a stale/racing edit.
  CONTACT_DEL: "contact.del",
  // {have:[id…], from} — CATCH-UP: a joining/reconnecting peer publishes an RBSR
  // range summary of the ids it already holds; peers serve only the delta
  // (catchup.ts). NOT stored, NOT folded.
  SYNC_REQ: "sync.req",
} as const;

export function eventToJson(e: Event): any {
  const j: any = {
    v: e.v,
    id: e.id,
    type: e.type,
    hlc: { wall: e.hlc.wall, ctr: e.hlc.ctr, dev: e.hlc.dev },
    dev: e.dev,
    payload: e.payload,
  };
  if (e.pub) j.pub = e.pub;
  if (e.sig) j.sig = e.sig;
  return j;
}
export function eventFromJson(j: any): Event {
  const hlc = j && j.hlc && typeof j.hlc === "object" ? j.hlc : {};
  return {
    v: typeof j?.v === "number" ? j.v : 1,
    id: typeof j?.id === "string" ? j.id : "",
    type: typeof j?.type === "string" ? j.type : "",
    hlc: {
      wall: typeof hlc.wall === "number" ? hlc.wall : 0,
      ctr: typeof hlc.ctr === "number" ? hlc.ctr : 0,
      dev: typeof hlc.dev === "string" ? hlc.dev : "",
    },
    dev: typeof j?.dev === "string" ? j.dev : "",
    payload: j && typeof j.payload === "object" && j.payload !== null ? j.payload : {},
    ...(typeof j?.pub === "string" ? { pub: j.pub } : {}),
    ...(typeof j?.sig === "string" ? { sig: j.sig } : {}),
  };
}

// Union by id, sort by HLC. Idempotent — redelivery is a no-op. Pure.
export function mergeEvents(...logs: Event[][]): Event[] {
  const byId = new Map<string, Event>();
  for (const log of logs) for (const e of log) if (e.id && !byId.has(e.id)) byId.set(e.id, e);
  const out = [...byId.values()];
  out.sort((x, y) => compareHlc(x.hlc, y.hlc));
  return out;
}

export interface FoldedBook {
  id: string;
  contacts: any[];
}

// ── fold: merged log → book state ────────────────────────────────────────────
// Returns {id, contacts:[…]}. contact.set is a whole-record LWW upsert by
// payload.id; contact.del is a TERMINAL tombstone. contacts is sorted by id to
// match C++ std::map iteration order (a stable, deterministic listContacts()
// output) — the view/caller sorts by name for display. Mirrors kith_engine.hpp
// foldBook exactly.
export function foldBook(bookId: string, log: Event[]): FoldedBook {
  const ordered = mergeEvents(log);
  const contacts = new Map<string, any>(); // contact id -> current record
  const tombstones = new Set<string>();

  for (const e of ordered) {
    // Signatures are ALWAYS required (matches kith_engine.hpp): an unsigned or
    // forged event is dropped entirely, so a tampered/replayed write can never
    // enter the folded book.
    const signed = isSigned(e);
    if (!signed || !verifyEvent(e)) continue;
    if (e.type === ET.CONTACT_SET) {
      const id: string = e.payload?.id ?? "";
      if (!id || tombstones.has(id)) continue; // tombstone terminal
      const c: any = { ...e.payload };
      const creating = !contacts.has(id);
      c.createdAt = creating ? e.hlc.wall : (contacts.get(id)?.createdAt ?? e.hlc.wall);
      c.updatedAt = e.hlc.wall;
      c.authorAddr = e.hlc.dev || e.dev;
      contacts.set(id, c);
    } else if (e.type === ET.CONTACT_DEL) {
      const id: string = e.payload?.id ?? "";
      if (!id) continue;
      tombstones.add(id);
      contacts.delete(id);
    }
  }

  // Match C++ std::map iteration: contacts ordered by id string.
  const ids = [...contacts.keys()].sort();
  return { id: bookId, contacts: ids.map((id) => contacts.get(id)) };
}

// ── Clock: stamps local events, advances past ingested causes ────────────────
// Mirrors the desktop nextHlc (wall→ctr bump) and additionally primes from the
// whole log on load + advances on every ingest (receive), so mobile-authored
// events causally sort after everything it has already seen. This is strictly a
// correctness improvement over the desktop clock and does NOT affect the wire or
// the fold (convergence is over the merged set, independent of either clock).
export class Clock {
  wall = 0;
  ctr = 0;
  dev: string;
  constructor(dev: string) {
    this.dev = dev;
  }
  // Prime from an existing log so we never author an event that sorts before a
  // cause we already hold.
  primeFrom(log: Event[]) {
    for (const e of log) this.observe(e.hlc);
  }
  private observe(remote: HLC) {
    if (remote.wall > this.wall) {
      this.wall = remote.wall;
      this.ctr = remote.ctr;
    } else if (remote.wall === this.wall && remote.ctr > this.ctr) {
      this.ctr = remote.ctr;
    }
  }
  receive(remote: HLC) {
    this.observe(remote);
  }
  send(now: number): HLC {
    if (now > this.wall) {
      this.wall = now;
      this.ctr = 0;
    } else {
      this.ctr += 1;
    }
    return { wall: this.wall, ctr: this.ctr, dev: this.dev };
  }
}
