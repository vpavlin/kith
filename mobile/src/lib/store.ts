// Local address-book store (AsyncStorage), CRDT log model — mirrors the desktop
// core (src/contact_store.h): a book is a membership entry in the registry; its
// state lives in an append-only EVENT LOG (one log per book — kith ADR 0004).
// Reads FOLD the log (engine.ts). The mobile app is a full peer: it holds its own
// logs, merges inbound events idempotently, and publishes its own.
//
//   kith.books        – registry: [{id,key,name}] (membership)
//   kith.log.<bookId> – that book's append-only event log (Event[])
import AsyncStorage from "@react-native-async-storage/async-storage";
import { Event, mergeEvents, foldBook, eventFromJson } from "./engine";

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

async function readJson<T>(key: string, fallback: T): Promise<T> {
  try {
    const s = await AsyncStorage.getItem(key);
    return s ? (JSON.parse(s) as T) : fallback;
  } catch {
    return fallback;
  }
}
async function writeJson(key: string, value: unknown): Promise<void> {
  await AsyncStorage.setItem(key, JSON.stringify(value));
}

// ── registry (membership) ─────────────────────────────────────────────────────
async function getRegistry(): Promise<BookReg[]> {
  return readJson<BookReg[]>(K_BOOKS, []);
}
async function getReg(id: string): Promise<BookReg | undefined> {
  return (await getRegistry()).find((b) => b.id === id);
}
async function upsertReg(r: BookReg): Promise<void> {
  const books = await getRegistry();
  const i = books.findIndex((b) => b.id === r.id);
  if (i >= 0) {
    books[i] = { ...books[i], key: r.key || books[i].key, name: r.name || books[i].name };
  } else {
    books.push(r);
  }
  await writeJson(K_BOOKS, books);
}
async function removeBook(id: string): Promise<void> {
  const books = (await getRegistry()).filter((b) => b.id !== id);
  await writeJson(K_BOOKS, books);
  await AsyncStorage.removeItem(logKey(id)).catch(() => {});
}

// ── per-book event log ────────────────────────────────────────────────────────
async function getLog(bookId: string): Promise<Event[]> {
  const raw = await readJson<any[]>(logKey(bookId), []);
  const out: Event[] = [];
  for (const j of raw) {
    const e = eventFromJson(j);
    if (e.id) out.push(e);
  }
  return out;
}
// Merge one event into the log (dedup by id), persist. Returns true if NEW.
async function appendEvent(bookId: string, ev: Event): Promise<boolean> {
  if (!ev.id) return false;
  const log = await getLog(bookId);
  if (log.some((x) => x.id === ev.id)) return false; // dedup — idempotent redelivery
  const merged = mergeEvents([...log, ev]); // keep HLC-sorted + unique
  await writeJson(logKey(bookId), merged);
  return true;
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
