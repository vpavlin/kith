// Mobile store (src/lib/store.ts) under node with an in-memory AsyncStorage stub:
// chunked logs + legacy migration, failed reads never clobber data, and the
// per-book / registry locks keep concurrent read-modify-writes from losing writes.
import assert from "node:assert/strict";
import AsyncStorage from "@react-native-async-storage/async-storage";
import { store, LOG_CHUNK_CHARS } from "../src/lib/store.ts";
import type { Event } from "../src/lib/engine.ts";

const S = AsyncStorage as any;
let n = 0;
function ev(i: number, pad = 0): Event {
  return { v: 1, id: `id-${String(i).padStart(6, "0")}`, type: "contact.set",
    hlc: { wall: 1000 + i, ctr: 0, dev: "0xabc" }, dev: "0xabc",
    payload: { id: `c${i}`, notes: "x".repeat(pad) } };
}
async function check(name: string, fn: () => Promise<void>) {
  S.__map.clear(); S.__rowLimit = Infinity;
  await fn();
  n++; console.log("  ok:", name);
}

await check("legacy single-key log is read, then migrated to chunks on write", async () => {
  S.__map.set("kith.log.b1", JSON.stringify([ev(1), ev(2)]));
  assert.equal((await store.getLog("b1")).length, 2);
  assert.equal(await store.appendEvent("b1", ev(3)), true);
  assert.deepEqual(JSON.parse(S.__map.get("kith.log.b1")), { v: 2, chunks: 1 });
  assert.deepEqual((await store.getLog("b1")).map((e) => e.id), ["id-000001", "id-000002", "id-000003"]);
});

await check("big log splits into chunks that each fit under the row limit", async () => {
  S.__rowLimit = 2 * LOG_CHUNK_CHARS;           // ~ the CursorWindow cap, relative
  for (let i = 0; i < 60; i++) await store.appendEvent("b2", ev(i, 20_000)); // ~1.2 MB total
  const idx = JSON.parse(S.__map.get("kith.log.b2"));
  assert.ok(idx.chunks >= 3, "expected several chunks, got " + idx.chunks);
  for (let i = 0; i < idx.chunks; i++) assert.ok(S.__map.get(`kith.log.b2.${i}`).length <= LOG_CHUNK_CHARS);
  assert.equal((await store.getLog("b2")).length, 60);
  assert.equal(await store.appendEvent("b2", ev(5, 20_000)), false); // dedup still works
});

await check("unreadable log: reads throw and appendEvent never overwrites it", async () => {
  const big = JSON.stringify([ev(1, 5000)]);
  S.__map.set("kith.log.b3", big);
  S.__rowLimit = 1000;
  await assert.rejects(store.getLog("b3"));
  await assert.rejects(store.appendEvent("b3", ev(2)));
  assert.equal(S.__map.get("kith.log.b3"), big);
});

await check("corrupt registry: upsertReg throws and leaves it untouched", async () => {
  S.__map.set("kith.books", "{not json");
  await assert.rejects(store.upsertReg({ id: "x", key: "k", name: "n" }));
  assert.equal(S.__map.get("kith.books"), "{not json");
});

await check("missing chunk is a read failure, not an empty log", async () => {
  for (let i = 0; i < 3; i++) await store.appendEvent("b4", ev(i));
  S.__map.delete("kith.log.b4.0");
  await assert.rejects(store.getLog("b4"));
});

await check("concurrent appends to one book all land (per-book lock)", async () => {
  await Promise.all(Array.from({ length: 50 }, (_, i) => store.appendEvent("b5", ev(i))));
  assert.equal((await store.getLog("b5")).length, 50);
});

await check("concurrent registry upserts all land (registry lock)", async () => {
  await Promise.all(Array.from({ length: 20 }, (_, i) => store.upsertReg({ id: "r" + i, key: "k", name: "n" })));
  assert.equal((await store.getRegistry()).length, 20);
});

await check("removeBook clears the index and every chunk", async () => {
  await store.upsertReg({ id: "b6", key: "k", name: "n" });
  for (let i = 0; i < 40; i++) await store.appendEvent("b6", ev(i, 20_000));
  assert.ok(JSON.parse(S.__map.get("kith.log.b6")).chunks > 1);
  await store.removeBook("b6");
  assert.deepEqual([...S.__map.keys()].filter((k) => k.startsWith("kith.log.b6")), []);
  assert.equal((await store.getRegistry()).length, 0);
});

await check("requireMember: an event for a removed book doesn't recreate its log", async () => {
  await store.upsertReg({ id: "b7", key: "k", name: "n" });
  assert.equal(await store.appendEvent("b7", ev(1), { requireMember: true }), true);
  await store.removeBook("b7");
  assert.equal(await store.appendEvent("b7", ev(2), { requireMember: true }), false);
  assert.equal(S.__map.has("kith.log.b7"), false);
});

console.log(`STORE OK — ${n} checks passed.`);
