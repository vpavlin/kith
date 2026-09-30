// Local address-book store (AsyncStorage), CRDT log model — mirrors the desktop
// core (src/contact_store.h): a book is a membership entry in the registry; its
// state lives in an append-only EVENT LOG (one log per book — kith ADR 0004).
// Reads FOLD the log (engine.ts). The mobile app is a full peer: it holds its own
// logs, merges inbound events idempotently, and publishes its own.
//
//   kith.books            – registry: [{id,key,name}] (membership)
//   kith.log.<bookId>     – log INDEX {v:2,chunks:N} (legacy: the whole Event[] inline)
//   kith.log.<bookId>.<n> – log chunk n (Event[] JSON, ≤ LOG_CHUNK_CHARS each)
//
// Why chunks: Android AsyncStorage (SQLite) cannot READ a single row > ~2 MB
// (CursorWindow), so a big book stored under one key becomes unreadable. Reads
// distinguish "missing" (→ default) from "failed" (→ throw), and a failed read is
// never followed by a write, so an unreadable value is never overwritten with [].
import AsyncStorage from "@react-native-async-storage/async-storage";
import { type Event, mergeEvents, foldBook, eventFromJson, eventToJson } from "./engine";

export interface Contact {
  id: string;
  name: { display: string; given?: string; family?: string; org?: string };
  phones: { label: string; value: string }[];
  emails: { label: string; value: string }[];
  handles: { kind: string; value: string }[];
  addresses: { label: string; street?: string; city?: string; region?: string; postcode?: string; country?: string }[];
  notes: string;
  avatar?: string;
  loamIdentity?: { address: string; pubHex: string; verified: boolean; addedVia: string };
  createdAt?: number;
  updatedAt?: number;
  authorAddr?: string;
}

export interface Book {
  id: string;
  name: string;
  encryptionKey?: string; // present iff this device holds the book's key
  contactCount?: number;
}

// Registry entry = membership. `key` is the per-book encryptionKey.
export interface BookReg {
  id: string;
  key: string;
  name: string;
}

const K_BOOKS = "kith.books";
const logKey = (bookId: string) => `kith.log.${bookId}`;
const chunkKey = (bookId: string, n: number) => `kith.log.${bookId}.${n}`;
// Per-chunk budget in UTF-16 chars: even all-3-byte UTF-8 stays ~1.35 MB, well under
// the ~2 MB CursorWindow row limit. A single event larger than this gets its own chunk.
export const LOG_CHUNK_CHARS = 450_000;

// Thrown when stored data exists but can't be read/parsed. Callers must NOT write
// over the key after this — surface it instead (the data may still be recoverable).
export class StorageReadError extends Error {
  constructor(key: string, cause: unknown) {
    super(`could not read ${key}: ${cause instanceof Error ? cause.message : String(cause)}`);
    this.name = "StorageReadError";
  }
}

// Missing key → fallback. Read or parse failure → StorageReadError (never a silent default).
async function readJson<T>(key: string, fallback: T): Promise<T> {
  let s: string | null;
  try {
    s = await AsyncStorage.getItem(key);
  } catch (e) {
    throw new StorageReadError(key, e);
  }
  if (s === null || s === undefined) return fallback;
  try {
    return JSON.parse(s) as T;
  } catch (e) {
    throw new StorageReadError(key, e);
  }
}
async function writeJson(key: string, value: unknown): Promise<void> {
  await AsyncStorage.setItem(key, JSON.stringify(value));
}

// ── per-key async mutex (promise chain) ──────────────────────────────────────
// Every read-modify-write of the registry or a book's log runs under its lock, so
// concurrent inbound events (one async task per message) can't lose each other's
// appends by interleaving read → write.
const lockTails = new Map<string, Promise<unknown>>();
function withLock<T>(name: string, fn: () => Promise<T>): Promise<T> {
  const prev = lockTails.get(name) ?? Promise.resolve();
  const run = prev.then(fn, fn);
  const tail = run.catch(() => {});
  lockTails.set(name, tail);
  void tail.then(() => { if (lockTails.get(name) === tail) lockTails.delete(name); });
  return run;
}
const REG_LOCK = "reg";
const logLock = (bookId: string) => `log:${bookId}`;

// ── registry (membership) ─────────────────────────────────────────────────────
async function getRegistry(): Promise<BookReg[]> {
  const r = await readJson<BookReg[]>(K_BOOKS, []);
  if (!Array.isArray(r)) throw new StorageReadError(K_BOOKS, new Error("not an array"));
  return r;
}
async function getReg(id: string): Promise<BookReg | undefined> {
  return (await getRegistry()).find((b) => b.id === id);
}
function upsertReg(r: BookReg): Promise<void> {
  return withLock(REG_LOCK, async () => {
    const books = await getRegistry(); // throws on a failed read → nothing is written
    const i = books.findIndex((b) => b.id === r.id);
    if (i >= 0) {
      books[i] = { ...books[i], key: r.key || books[i].key, name: r.name || books[i].name };
    } else {
      books.push(r);
    }
    await writeJson(K_BOOKS, books);
  });
}
function removeBook(id: string): Promise<void> {
  return withLock(REG_LOCK, async () => {
    const books = (await getRegistry()).filter((b) => b.id !== id);
    await writeJson(K_BOOKS, books);
    await withLock(logLock(id), async () => {
      // Index + every chunk (by prefix, so an unreadable index can't strand chunks).
      const prefix = logKey(id) + ".";
      const keys = (await AsyncStorage.getAllKeys().catch(() => [] as readonly string[]))
        .filter((k) => k.startsWith(prefix));
      await AsyncStorage.multiRemove([logKey(id), ...keys]).catch(() => {});
    });
  });
}

// ── per-book event log ────────────────────────────────────────────────────────
// Raw JSON events of a book. Handles both the chunked format and the legacy
// single-key array (migrated to chunks on the next write). Throws on any failure.
async function readLogRaw(bookId: string): Promise<any[]> {
  const idx = await readJson<any>(logKey(bookId), null);
  if (idx === null) return [];
  if (Array.isArray(idx)) return idx; // legacy single-key format
  const n = idx && typeof idx === "object" && Number.isInteger(idx.chunks) ? idx.chunks : -1;
  if (n < 0) throw new StorageReadError(logKey(bookId), new Error("bad log index"));
  const out: any[] = [];
  for (let i = 0; i < n; i++) {
    const k = chunkKey(bookId, i);
    const part = await readJson<any>(k, null);
    if (!Array.isArray(part)) throw new StorageReadError(k, new Error("missing or bad chunk"));
    for (const j of part) out.push(j);
  }
  return out;
}
// Split events into JSON-array chunks of ≤ LOG_CHUNK_CHARS (≥ 1 event per chunk).
export function chunkLog(events: unknown[], budget = LOG_CHUNK_CHARS): string[] {
  const chunks: string[] = [];
  let cur: string[] = [];
  let size = 2;
  for (const e of events) {
    const s = JSON.stringify(e);
    if (cur.length && size + s.length + 1 > budget) {
      chunks.push("[" + cur.join(",") + "]");
      cur = [];
      size = 2;
    }
    cur.push(s);
    size += s.length + 1;
  }
  if (cur.length) chunks.push("[" + cur.join(",") + "]");
  return chunks;
}
// Write the whole log as chunks + index in ONE multiSet (a single SQLite transaction
// on Android, so a crash never leaves the index pointing at half-written chunks),
// then drop any now-unused tail chunks from a previous, longer write.
async function writeLog(bookId: string, events: Event[], prevChunks: number): Promise<void> {
  const chunks = chunkLog(events.map(eventToJson));
  const pairs: [string, string][] = chunks.map((c, i) => [chunkKey(bookId, i), c]);
  pairs.push([logKey(bookId), JSON.stringify({ v: 2, chunks: chunks.length })]);
  await AsyncStorage.multiSet(pairs);
  if (prevChunks > chunks.length) {
    const stale: string[] = [];
    for (let i = chunks.length; i < prevChunks; i++) stale.push(chunkKey(bookId, i));
    await AsyncStorage.multiRemove(stale).catch(() => {});
  }
}
async function chunkCount(bookId: string): Promise<number> {
  const idx = await readJson<any>(logKey(bookId), null);
  return idx && !Array.isArray(idx) && Number.isInteger(idx.chunks) ? idx.chunks : 0;
}

async function getLog(bookId: string): Promise<Event[]> {
  const raw = await readLogRaw(bookId);
  const out: Event[] = [];
  for (const j of raw) {
    const e = eventFromJson(j);
    if (e.id) out.push(e);
  }
  return out;
}
// Merge one event into the log (dedup by id), persist. Returns true if NEW.
// Serialized per book; a failed read throws BEFORE any write (never clobbers data).
function appendEvent(bookId: string, ev: Event): Promise<boolean> {
  if (!ev.id) return Promise.resolve(false);
  return withLock(logLock(bookId), async () => {
    const log = await getLog(bookId);
    if (log.some((x) => x.id === ev.id)) return false; // dedup — idempotent redelivery
    const merged = mergeEvents([...log, ev]); // keep HLC-sorted + unique
    await writeLog(bookId, merged, await chunkCount(bookId));
    return true;
  });
}

// ── folded reads (what the UI consumes) ───────────────────────────────────────
export const store = {
  getRegistry,
  getReg,
  upsertReg,
  removeBook,
  getLog,
  appendEvent,

  async listBooks(): Promise<Book[]> {
    const regs = await getRegistry();
    const out: Book[] = [];
    for (const r of regs) {
      const f = foldBook(r.id, await getLog(r.id));
      out.push({ id: r.id, name: r.name, encryptionKey: r.key, contactCount: f.contacts.length });
    }
    return out;
  },

  async contactsFor(bookId: string): Promise<Contact[]> {
    const f = foldBook(bookId, await getLog(bookId));
    return f.contacts as Contact[];
  },

  async allContacts(): Promise<{ bookId: string; contact: Contact }[]> {
    const regs = await getRegistry();
    const out: { bookId: string; contact: Contact }[] = [];
    for (const r of regs) {
      const f = foldBook(r.id, await getLog(r.id));
      for (const c of f.contacts) out.push({ bookId: r.id, contact: c as Contact });
    }
    return out;
  },
};
