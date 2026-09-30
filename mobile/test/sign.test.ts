// Signing edge cases (identity.ts cjson) + the fold's verify memo (engine.ts).
import assert from "node:assert/strict";
import { signEvent, verifyEvent, identityFromPriv, fromHex } from "../src/lib/identity.ts";
import { verifyEventCached, eventFromJson } from "../src/lib/engine.ts";

const id = identityFromPriv(fromHex("00".repeat(31) + "07"));
const mk = (payload: any) => signEvent(id, { v: 1, id: "e-" + Math.random(), type: "contact.set",
  hlc: { wall: 5, ctr: 0, dev: "" }, dev: "", payload });

// 1) undefined-valued keys (any depth) sign as ABSENT, so the event still verifies
//    after a JSON round-trip (storage / wire / desktop).
const e1 = mk({ id: "c1", avatar: undefined, name: { display: "A", org: undefined }, phones: [] });
const rt = eventFromJson(JSON.parse(JSON.stringify(e1)));
assert.equal(verifyEvent(e1), true);
assert.equal(verifyEvent(rt), true, "round-tripped event must still verify");
console.log("  ok: undefined payload keys survive the JSON round-trip");

// 2) memo agrees with the uncached verifier, rejects forgeries and malformed hex.
const e2 = eventFromJson(JSON.parse(JSON.stringify(mk({ id: "c2" }))));
for (let i = 0; i < 3; i++) assert.equal(verifyEventCached(e2), true);
const forged = { ...e2, payload: { id: "c2", notes: "evil" } };
assert.equal(verifyEventCached(forged), false);
assert.equal(verifyEventCached(forged), false);
const other = identityFromPriv(fromHex("00".repeat(31) + "08"));
assert.equal(verifyEventCached({ ...e2, pub: other.pubHex }), false); // author ≠ address(pub)
assert.equal(verifyEventCached({ ...e2, sig: e2.sig + "00" }), false); // wrong sig length
assert.equal(verifyEventCached({ ...e2, pub: "zz" + e2.pub!.slice(2) }), false); // non-hex
assert.equal(verifyEventCached(e2), true);
console.log("  ok: verify memo matches the verifier and rejects forgeries");
console.log("SIGN OK");
