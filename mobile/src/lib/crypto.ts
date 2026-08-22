// Kith contact-book crypto — AES-256-GCM seal/open, byte-for-byte compatible with
// the desktop core (src/contact_sync.cpp seal()/open()).
//
// Wire format of the SEALED bytes (what loam-transport carries, per book):
//   nonce(12) || tag(16) || ciphertext        (AES-256-GCM, no AAD)
//
// KEY DERIVATION — must match the desktop EXACTLY (quirks included). Lives in
// ./crypto-derive (pure, dep-free, unit-tested against the C ground truth in
// test/crypto-parity.sh). The load-bearing quirk: C sscanf("%2hhx") parses a
// dash-started window like "-a" as a SIGNED -10 → 0xf6, NOT 0. See crypto-derive.ts.
import { gcm } from "@noble/ciphers/aes.js";
import { hmac } from "@noble/hashes/hmac.js";
import { sha256 } from "@noble/hashes/sha256.js";
import { deriveKey } from "./crypto-derive";
import { utf8Bytes } from "./utf8";

/** Deterministic 12-byte AEAD nonce derived from a seal id (logos-sync ADR 0011).
 *  Byte-identical to the desktop core (src/contact_sync.cpp nonceFor) and to
 *  kym/qaku's scheme — the derivation is cipher-agnostic (kith stays AES-256-GCM).
 *  The HMAC key is the same 32-byte AES key the cipher uses; domain "kith/nonce/v1|". */
export function nonceFor(key: Uint8Array, sealId: string): Uint8Array {
  return hmac(sha256, key, utf8Bytes(`kith/nonce/v1|${sealId}`)).slice(0, 12);
}

/** Seal plaintext bytes for a book: AES-256-GCM → nonce||tag||ciphertext,
 *  exactly the layout the desktop's open() expects. The nonce is DERIVED from
 *  `sealId` (ADR 0011): pass the event's stable UUID id so a re-sent immutable
 *  event seals byte-identically and the fleet store dedups it instead of piling up
 *  a fresh random-nonce copy each send. open() is unchanged (nonce rides the wire),
 *  so old random-nonce messages still decrypt. */
export function seal(encryptionKey: string, plaintext: Uint8Array, sealId: string): Uint8Array {
  const key = deriveKey(encryptionKey);
  const nonce = nonceFor(key, sealId);
  // @noble gcm returns ciphertext||tag (tag appended). The desktop packs
  // nonce||tag||ciphertext, so we split noble's output and re-pack.
  const ctAndTag = gcm(key, nonce).encrypt(plaintext);
  const tag = ctAndTag.slice(ctAndTag.length - 16);
  const ct = ctAndTag.slice(0, ctAndTag.length - 16);
  const out = new Uint8Array(12 + 16 + ct.length);
  out.set(nonce, 0);
  out.set(tag, 12);
  out.set(ct, 28);
  return out;
}

/** Open sealed bytes (nonce||tag||ciphertext). Returns null on auth failure or
 *  wrong key (so a receiver can try each key/candidate and take the first that
 *  authenticates). */
export function open(encryptionKey: string, sealed: Uint8Array): Uint8Array | null {
  if (sealed.length < 28 + 16) return null; // nonce+tag+min ciphertext
  const key = deriveKey(encryptionKey);
  const nonce = sealed.slice(0, 12);
  const tag = sealed.slice(12, 28);
  const ct = sealed.slice(28);
  // Re-assemble noble's expected ciphertext||tag ordering.
  const ctAndTag = new Uint8Array(ct.length + 16);
  ctAndTag.set(ct, 0);
  ctAndTag.set(tag, ct.length);
  try {
    return gcm(key, nonce).decrypt(ctAndTag);
  } catch {
    return null; // GCM tag mismatch → wrong key / not ours / tampered
  }
}
