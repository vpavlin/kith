// App logic: bridges the local CRDT log store and the event wire. Authors local
// edits as immutable events (contact.set / contact.del), merges inbound events
// into the log, and folds for the UI — the exact model the desktop core
// (kith_impl.cpp publishAndApply / applyIncoming) uses. Also parses/builds the
// `kith://join` links the desktop uses to share a book's key (kith_impl.cpp
// shareLink/parseShareLink).
import { AppState } from "react-native";
import * as Crypto from "expo-crypto";
import * as SecureStore from "expo-secure-store";
import { fromByteArray, toByteArray } from "base64-js";
import { store, Book, Contact, StorageReadError } from "./store";
import { Event, ET, Clock, eventToJson, eventFromJson, foldBook } from "./engine";
import { utf8Bytes, utf8Decode } from "./utf8";
import { getIdentity, getAddress, signEvent } from "./identity";
import * as sync from "./kith-sync";
import { buildInitial, respond } from "./catchup";

// ── device identity (SDS senderId + event author) ───────────────────────────
// The identity is a secp256k1 keypair (identity.ts); its ADDRESS ("0x…") is the
// verifiable author id used for event.dev / hlc.dev / the SDS senderId. Every
// authored event is signed with the device's private key and verified on merge —
// this is the mobile mirror of kith_impl.cpp's LOCAL DEVICE KEY fallback signer
// (mobile has no loam_core to bind a per-book identity to, so every book here is
// single-writer-per-device, same as the desktop's fallback path).
export async function getDeviceId(): Promise<string> {
  return await getAddress();
}

// ── clock + event construction (mirrors kith_impl.cpp nextHlc / mkEvent) ─────
let clock: Clock | null = null;
let deviceId = "kith-default";
export function myDeviceId(): string { return deviceId; }
async function ensureClock(): Promise<Clock> {
  if (clock) return clock;
  deviceId = await getDeviceId();
  const c = new Clock(deviceId);
  for (const r of await store.getRegistry()) c.primeFrom(await store.getLog(r.id));
  clock = c;
  return c;
}
async function mkEvent(type: string, payload: any): Promise<Event> {
  const c = await ensureClock();
  // JSON round-trip the payload: strips undefined at EVERY depth (and NaN → null, etc.)
  // so the signed bytes are exactly what storage/the wire/the desktop will see.
  const clean = JSON.parse(JSON.stringify(payload ?? {}));
  const e: Event = { v: 1, id: Crypto.randomUUID(), type, hlc: c.send(Date.now()), dev: deviceId, payload: clean };
  // Transient sync msgs (SYNC_REQ) fire constantly and are never folded, but
  // kith_engine.hpp still requires EVERY event to carry a valid signature (the
  // fold drops unsigned events outright) — sign everything, always.
  const id = await getIdentity();
  return signEvent(id, e) as Event;
}
// Local-first: persist FIRST (the authored event has no other copy until it's on
// disk), render immediately, and send in the BACKGROUND — never await the wire. A
// send that fails or never lands is covered by RBSR catch-up / store sync.
async function publishAndApply(bookId: string, e: Event): Promise<void> {
  await store.appendEvent(bookId, e);
  notifyChange();
  void sync.sendEvent(bookId, JSON.stringify(eventToJson(e))).catch(() => {});
}

// ── catch-up (logos-sync v2 RBSR) ────────────────────────────────────────────
// Legacy (pre-v2) peers' bare SYNC_REQ re-serves the WHOLE log — at most once per
// 30 s per book, however many such requests arrive.
const SERVE_LOG_EVERY_MS = 30_000;
const lastServe: Record<string, number> = {};
async function serveLog(bookId: string): Promise<void> {
  const now = Date.now();
  if (lastServe[bookId] && now - lastServe[bookId] < SERVE_LOG_EVERY_MS) return;
  lastServe[bookId] = now;
  for (const e of await store.getLog(bookId)) {
    await sync.sendEvent(bookId, JSON.stringify(eventToJson(e))).catch(() => {});
  }
}
// A round-OPENING fp (no lo/hi: a peer's buildInitial) is answered at most once per
// (peer, book) per 10 s — every peer re-asks on start and at 9/24 s, and several
// devices answering each of those in full floods the channel. Recursive sub-range
// fp/ids/need steps of a round in progress are never throttled.
const OPEN_FP_EVERY_MS = 10_000;
const lastOpenFp = new Map<string, number>();
function throttleOpeningFp(bookId: string, msg: any): boolean {
  if (msg.t !== "fp" || msg.lo !== undefined || msg.hi !== undefined) return false;
  const k = bookId + "|" + String(msg.from ?? "");
  const now = Date.now();
  const last = lastOpenFp.get(k);
  if (last !== undefined && now - last < OPEN_FP_EVERY_MS) return true;
  lastOpenFp.set(k, now);
  return false;
}

async function sendSyncReq(bookId: string): Promise<void> {
  const msg = buildInitial(await store.getLog(bookId), deviceId);
  const e = await mkEvent(ET.SYNC_REQ, msg);
  await sync.sendEvent(bookId, JSON.stringify(eventToJson(e))).catch(() => {});
}

// ── kith://join links — MUST match the desktop core byte-for-byte ────────────
// kith://join?id=<bookId>&key=<b64url(encryptionKey)>&name=<name>
// (kith_impl.cpp shareLink/parseShareLink; b64url is the same RFC-4648 no-pad
// alphabet the desktop's b64urlEncode/Decode use.)
function b64urlEncode(s: string): string {
  return fromByteArray(utf8Bytes(s)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
function b64urlDecode(s: string): string {
  let b = s.replace(/-/g, "+").replace(/_/g, "/");
  while (b.length % 4) b += "=";
  return utf8Decode(toByteArray(b));
}
// Robust query parse (RN Hermes has spotty URLSearchParams).
function parseQuery(q: string): Record<string, string> {
  const out: Record<string, string> = {};
  for (const pair of q.split("&")) {
    const i = pair.indexOf("=");
    if (i < 0) continue;
    try {
      out[decodeURIComponent(pair.slice(0, i))] = decodeURIComponent(pair.slice(i + 1).replace(/\+/g, "%20"));
    } catch { /* skip malformed */ }
  }
  return out;
}
export function parseInvite(link: string): { bookId: string; key: string; name?: string } | null {
  try {
    if (!link.startsWith("kith://join")) return null;
    const q = link.split("?")[1] || "";
    const p = parseQuery(q);
    const id = p["id"] || "";
    const keyB64 = p["key"] || "";
    if (!id || !keyB64) return null;
    return { bookId: id, key: b64urlDecode(keyB64), name: p["name"] || undefined };
  } catch {
    return null;
  }
}
export function buildInvite(book: Book): string {
  const key = b64urlEncode(book.encryptionKey || "");
  return `kith://join?id=${encodeURIComponent(book.id)}&key=${key}&name=${encodeURIComponent(book.name)}`;
}

// ── inbound: merge one received event into the book's log ────────────────────
sync.setEventHandler((bookId, eventJson) => {
  void (async () => {
    try {
      const j = JSON.parse(eventJson);
      const e = eventFromJson(j);
      if (!e.id) return;
      // CATCH-UP (logos-sync v2, recursive RBSR): step the reconciliation for one
      // incoming range statement — serve the id-exact missing events + publish
      // the fp/ids/need replies (all single-segment). A payload with no `t` is
      // an OLD peer's bare SYNC_REQ → fall back to a whole-log serve.
      if (e.type === ET.SYNC_REQ) {
        const msg: any = e.payload;
        if (!msg || !msg.t) { await serveLog(bookId); return; }
        if (throttleOpeningFp(bookId, msg)) return;
        const step = respond(await store.getLog(bookId), msg, deviceId);
        for (const ev of step.serve)
          await sync.sendEvent(bookId, JSON.stringify(eventToJson(ev))).catch(() => {});
        for (const r of step.replies)
          await sync.sendEvent(bookId, JSON.stringify(eventToJson(await mkEvent(ET.SYNC_REQ, r)))).catch(() => {});
        return;
      }
      (await ensureClock()).receive(e.hlc); // advance past the ingested cause
      // requireMember: a book removed from this device must not be recreated by a
      // late event still in flight. Idempotent (dedup by id).
      const isNew = await store.appendEvent(bookId, e, { requireMember: true });
      if (isNew) notifyChange();
    } catch (err) {
      // Malformed event → ignore. A storage failure is NOT swallowed silently: the
      // store refused to write (nothing clobbered) — log it; the UI surfaces it on refresh.
      if (err instanceof StorageReadError) console.warn("kith: inbound event not stored:", err.message);
    }
  })();
});

// ── outbound: local edits → append event + publish ──────────────────────────
export async function createBook(name: string): Promise<Book> {
  const id = Crypto.randomUUID();
  const encryptionKey = Crypto.randomUUID() + Crypto.randomUUID(); // 72-char dashed, same shape the desktop uses
  const nm = name.trim() || "Contacts";
  await store.upsertReg({ id, key: encryptionKey, name: nm });
  await sync.joinBook(id, encryptionKey);
  notifyChange();
  return { id, name: nm, encryptionKey, contactCount: 0 };
}

export async function deleteBook(bookId: string): Promise<void> {
  sync.leaveBook(bookId); // stop routing its topic first, so no event recreates the log
  await store.removeBook(bookId);
  notifyChange();
}

export async function addContact(bookId: string, fields: Omit<Contact, "id">): Promise<Contact> {
  const id = Crypto.randomUUID();
  const payload = { ...fields, id };
  await publishAndApply(bookId, await mkEvent(ET.CONTACT_SET, payload));
  return { ...fields, id } as Contact;
}

export async function editContact(bookId: string, contact: Contact): Promise<void> {
  await publishAndApply(bookId, await mkEvent(ET.CONTACT_SET, contact));
}

export async function deleteContact(bookId: string, contactId: string): Promise<void> {
  await publishAndApply(bookId, await mkEvent(ET.CONTACT_DEL, { id: contactId }));
}

export async function searchContacts(query: string): Promise<{ bookId: string; contact: Contact }[]> {
  const q = query.trim().toLowerCase();
  if (!q) return [];
  const all = await store.allContacts();
  return all.filter(({ contact: c }) => {
    let hay = (c.name?.display || "") + " " + (c.notes || "");
    for (const e of c.emails || []) hay += " " + e.value;
    for (const p of c.phones || []) hay += " " + p.value;
    for (const h of c.handles || []) hay += " " + h.value;
    return hay.toLowerCase().includes(q);
  });
}

export async function findByAddress(address: string): Promise<{ bookId: string; contact: Contact } | null> {
  if (!address) return null;
  const all = await store.allContacts();
  return all.find(({ contact: c }) => c.loamIdentity?.address === address) || null;
}

export async function joinFromInvite(link: string): Promise<Book | null> {
  const inv = parseInvite(link);
  if (!inv) return null;
  await store.upsertReg({ id: inv.bookId, key: inv.key, name: inv.name || "Shared book" });
  await sync.joinBook(inv.bookId, inv.key);
  await sendSyncReq(inv.bookId).catch(() => {}); // just joined — pull history
  notifyChange();
  const books = await store.listBooks();
  return books.find((b) => b.id === inv.bookId) || null;
}

// ── shared-node preference ──────────────────────────────────────────────────
// Default ON (the Loam shared node): only an explicit "0" opts out.
export async function getSharedNode(): Promise<boolean> {
  return (await SecureStore.getItemAsync("kith-shared-node")) !== "0";
}
export async function setSharedNode(on: boolean): Promise<void> {
  await SecureStore.setItemAsync("kith-shared-node", on ? "1" : "0");
}

/** Bring sync up on every shared book we hold a key for. */
export async function startSyncing(shared?: boolean, onStatus?: (s: string) => void): Promise<void> {
  const useShared = shared ?? (await getSharedNode());
  const regs = await store.getRegistry();
  await sync.startSync({
    deviceId: await getDeviceId(),
    books: regs.filter((b) => b.key).map((b) => ({ id: b.id, encryptionKey: b.key })),
    shared: useShared,
    onStatus,
  });
  // Catch-up: ask peers to re-serve each book's log. Retried a few times to beat
  // a still-forming mesh (a dropped first SYNC_REQ otherwise = no history).
  const askAll = () => { for (const b of regs.filter((r) => r.key)) sendSyncReq(b.id).catch(() => {}); };
  askAll();
  setTimeout(askAll, 9000);
  setTimeout(askAll, 24000);
  // Reliable history: pull every joined book's log straight from the fleet
  // store. SYNC_REQ only re-serves from a LIVE peer (which a phone often can't
  // reach), whereas the store always has it.
  const pull = () => { sync.storeSync().catch(() => {}); };
  setTimeout(pull, 2000);
  setTimeout(pull, 12000);
  startReconcile();
}

// Periodic catch-up while the app is in the foreground, and one round on every return to it. The
// startup rounds alone left anything missed afterwards (sent while the app wasn't approved in Loam,
// while offline, while Loam was down) unrecovered until kith restarted. Each round is a small
// fingerprint per book; only missing events move. Same cadence as scala.
const RECONCILE_MS = 45_000;
let reconcileTimer: ReturnType<typeof setInterval> | null = null;
let appStateSub: { remove: () => void } | null = null;
async function reconcileAll(): Promise<void> {
  if (AppState.currentState !== "active") return;   // idle in the background
  for (const b of await store.getRegistry()) if (b.key) sendSyncReq(b.id).catch(() => {});
}
function startReconcile(): void {
  if (reconcileTimer) return;   // idempotent — startSyncing can run again after a retry
  reconcileTimer = setInterval(() => { reconcileAll().catch(() => {}); }, RECONCILE_MS);
  appStateSub = AppState.addEventListener("change", (st) => { if (st === "active") reconcileAll().catch(() => {}); });
}

// ── tiny change bus so the UI can refresh after inbound/outbound edits ───────
type Listener = () => void;
const listeners = new Set<Listener>();
export function onChange(cb: Listener): () => void {
  listeners.add(cb);
  return () => listeners.delete(cb);
}
// Coalesced: a catch-up burst of N events → ONE UI refresh, not N.
let notifyTimer: ReturnType<typeof setTimeout> | null = null;
function notifyChange() {
  if (notifyTimer) return;
  notifyTimer = setTimeout(() => { notifyTimer = null; listeners.forEach((l) => l()); }, 200);
}
export function foldBookNow(bookId: string, log: Event[]) {
  return foldBook(bookId, log);
}
