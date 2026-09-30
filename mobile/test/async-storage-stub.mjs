// In-memory node stub for @react-native-async-storage/async-storage (store.test.ts).
// `__rowLimit` simulates Android's CursorWindow: reading a value longer than it throws,
// exactly like a > ~2 MB row does on a device.
const m = new Map();
const S = {
  __map: m,
  __rowLimit: Infinity,
  async getItem(k) {
    if (!m.has(k)) return null;
    const v = m.get(k);
    if (v.length > S.__rowLimit) throw new Error("Row too big to fit into CursorWindow");
    return v;
  },
  async setItem(k, v) { m.set(k, String(v)); },
  async removeItem(k) { m.delete(k); },
  async getAllKeys() { return [...m.keys()]; },
  async multiSet(pairs) { for (const [k, v] of pairs) m.set(k, String(v)); },
  async multiRemove(keys) { for (const k of keys) m.delete(k); },
};
export default S;
