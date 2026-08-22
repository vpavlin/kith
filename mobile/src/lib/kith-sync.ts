// Kith mobile sync — the CRDT event wire the desktop core (src/contact_sync.cpp
// sendEvent / handleReceive) speaks, carried over the shared loam-transport (SDS
// Reliable Channels). Drops onto the same per-book content topic the desktop
// listens on.
//
// Wire (must match the desktop byte-for-byte):
//   • topic     = "/kith/1/<bookId>/json"   (channelId == contentTopic)
//   • plaintext = ONE event's JSON (engine.eventToJson → JSON.stringify):
//                 {v,id,type,hlc:{wall,ctr,dev},dev,payload}
//   • sealed    = AES-256-GCM(plaintext) -> nonce||tag||ciphertext  (crypto.ts)
//   • transport double-base64s the sealed bytes for the channel (publishSealed);
//     on receive it hands candidate byte-arrays, we open() each with the book's
//     key and take the first that authenticates.
import * as Crypto from "expo-crypto";
import * as transport from "./loam-transport";
import { seal, open } from "./crypto";
import { utf8Bytes, utf8Decode } from "./utf8";

export function topicForBook(bookId: string): string {
  return `/kith/1/${bookId}/json`;
}

// ── debug snapshot (surface in the app's Debug panel) ────────────────────────
export function refreshDebug(): Promise<void> {
  return (transport as any).refreshDebug?.() ?? Promise.resolve();
}
export function getDebug() {
  const t = transport as any;
  const c = t.counters || {};
  const d = t.diag || {};
  return {
    mode: t.getNodeMode?.() ?? "?",
    backend: t.getCtx?.() ? "up" : "down",
    routes: routes.length,
    peers: c.peers, mesh: c.mesh,
    tx: { attempt: c.txAttempt ?? 0, sent: c.txTotal ?? 0, fail: c.txFail ?? 0, lastErr: d.txErr || "" },
    rx: { raw: c.rxRaw ?? 0, opened: c.rxOpened ?? 0, openFail: c.rxOpenFail ?? 0, new: c.rxNew ?? 0, dup: c.rxDup ?? 0 },
    sample: t.getRxSample?.() ?? "",
    store: t.getStoreInfo?.() ?? "",
  };
}

// One route per shared book: its topic + the symmetric key we seal/open with.
interface Route {
  bookId: string;
  encryptionKey: string;
  topic: string;
}
let routes: Route[] = [];
let deviceId = "kith-default";
// Handler the app registers to merge one inbound event (raw JSON string) into the
// book's log — mirrors the desktop OnEventReceived / applyIncoming path.
let onEvent: ((bookId: string, eventJson: string) => void) | null = null;

/** Register the callback the app uses to apply an inbound event. */
export function setEventHandler(cb: (bookId: string, eventJson: string) => void) {
  onEvent = cb;
}

export function deliveryAvailable(): boolean {
  return transport.deliveryAvailable();
}

// Transport hands candidate sealed-byte arrays for a topic; find the book that
// owns the topic and open with its key. Returns true iff a candidate
// authenticated (so the transport tallies opened vs failed).
function adapterReceive(topic: string, candidates: Uint8Array[]): boolean {
  const route = routes.find((r) => r.topic === topic);
  if (!route) return false;
  for (const cand of candidates) {
    const plain = open(route.encryptionKey, cand);
    if (!plain) continue; // wrong key / not ours / other candidate
    onEvent?.(route.bookId, utf8Decode(plain));
    return true;
  }
  return false;
}

/** Bring the node up on every shared book's topic. Idempotent. Call after the
 *  book list (with keys) is known; use joinBook() for later adds. */
export async function startSync(opts: {
  deviceId: string;
  books: { id: string; encryptionKey: string }[];
  onStatus?: (s: string) => void;
  shared?: boolean; // route through the device-wide Logos Delivery service
}): Promise<void> {
  if (!transport.deliveryAvailable()) throw new Error("Logos Delivery not present in this build");
  deviceId = opts.deviceId || "kith-default";
  routes = opts.books
    .filter((b) => b.encryptionKey)
    .map((b) => ({ bookId: b.id, encryptionKey: b.encryptionKey, topic: topicForBook(b.id) }));
  (transport as { preferServiceBackend?: (on: boolean, appId: string) => void })
    .preferServiceBackend?.(!!opts.shared, "kith");
  if (transport.getCtx()) {
    // already up — just join any new topics
    await transport.join(routes.map((r) => r.topic));
    return;
  }
  await transport.start({
    deviceId,
    topics: routes.map((r) => r.topic),
    onReceive: adapterReceive,
    onStatus: opts.onStatus,
  });
}

/** Reliable cold-start history pull: query the fleet store for every joined
 *  book's topic and open+fold each stored message (reuses adapterReceive). This
 *  is how a freshly-joined book gets events that predate its subscription —
 *  SYNC_REQ only re-serves from a LIVE peer, which a phone frequently can't
 *  reach. Returns {msgs, events, detail}. */
export async function storeSync(): Promise<{ msgs: number; events: number; detail: string }> {
  return transport.storeSync(adapterReceive);
}

/** Add a book's route to the live node (after creating/joining one). */
export async function joinBook(bookId: string, encryptionKey: string): Promise<void> {
  if (!routes.find((r) => r.bookId === bookId)) {
    routes.push({ bookId, encryptionKey, topic: topicForBook(bookId) });
  }
  if (transport.getCtx()) await transport.join([topicForBook(bookId)]);
}

/** Publish one CRDT event on a book's channel (sealed with its key). The
 *  argument is the event's JSON string (engine.eventToJson → JSON.stringify). */
export async function sendEvent(bookId: string, eventJson: string): Promise<void> {
  const route = routes.find((r) => r.bookId === bookId);
  if (!route) return; // not a shared book / no key on this device
  // Derive the seal nonce from the event's stable id (ADR 0011) so a re-sent
  // immutable event is byte-identical → the fleet store dedups it. Every event JSON
  // carries a UUID `id`; per-call SYNC_REQ frames each get a distinct id, so control
  // frames are never collapsed. Fallback to a random token if `id` is ever missing.
  let sealId = "";
  try {
    const parsed = JSON.parse(eventJson) as { id?: string };
    if (parsed && typeof parsed.id === "string") sealId = parsed.id;
  } catch {
    // not JSON — fall through to random token
  }
  if (!sealId) sealId = Array.from(Crypto.getRandomBytes(16), (b) => b.toString(16).padStart(2, "0")).join("");
  const sealed = seal(route.encryptionKey, utf8Bytes(eventJson), sealId);
  await transport.publishSealed(route.topic, sealed);
}

export function stopSync(): Promise<void> {
  routes = [];
  return transport.stop();
}
