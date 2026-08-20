// vcard.ts — mobile port of the desktop core's minimal vCard 4.0 (RFC 6350)
// import/export (src/vcard.hpp, kith ADR 0005). Field mapping is direct: FN/N <->
// name, TEL <-> phones, EMAIL <-> emails, ADR <-> addresses, IMPP <-> handles,
// NOTE <-> notes, X-LOAM-KEY/X-LOAM-ADDR <-> loamIdentity (a Kith<->Kith vCard
// round-trips loamIdentity losslessly; other tools ignore the unknown X- fields).
//
// KNOWN LIMITATIONS (mirrors vcard.hpp's own TODO list — keep the two in sync):
//   - No RFC 6350 line UNFOLDING/FOLDING.
//   - No quoted-printable / charset= decoding.
//   - Only the first TYPE= parameter is used as the label.
//   - PHOTO is not imported or exported.
//   - ORG is read into name.org but not re-emitted as a standalone ORG line.

function esc(s: string): string {
  let o = "";
  for (const c of s) {
    if (c === "\\") o += "\\\\";
    else if (c === ",") o += "\\,";
    else if (c === ";") o += "\\;";
    else if (c === "\n") o += "\\n";
    else if (c === "\r") continue;
    else o += c;
  }
  return o;
}
function unesc(s: string): string {
  let o = "";
  for (let i = 0; i < s.length; i++) {
    if (s[i] === "\\" && i + 1 < s.length) {
      const n = s[i + 1];
      if (n === "n" || n === "N") { o += "\n"; i++; }
      else if (n === "," || n === ";" || n === "\\") { o += n; i++; }
      else o += s[i];
    } else o += s[i];
  }
  return o;
}
function trim(s: string): string { return s.replace(/^[\s\r\n]+|[\s\r\n]+$/g, ""); }

interface Prop { name: string; type: string; value: string }

function parseLine(line: string): Prop {
  const colon = line.indexOf(":");
  if (colon < 0) return { name: "", type: "", value: "" };
  const head = line.slice(0, colon);
  const value = line.slice(colon + 1);
  const semi = head.indexOf(";");
  let name = semi < 0 ? head : head.slice(0, semi);
  const dot = name.indexOf(".");
  if (dot >= 0) name = name.slice(dot + 1); // drop a "group." prefix (e.g. item1.TEL)
  name = trim(name).toUpperCase();
  let type = "";
  if (semi >= 0) {
    for (const param of head.slice(semi + 1).split(";")) {
      const up = param.toUpperCase();
      if (up.startsWith("TYPE=") && !type) {
        let t = param.slice(5);
        const comma = t.indexOf(",");
        if (comma >= 0) t = t.slice(0, comma); // first TYPE only
        type = t.toLowerCase();
      }
    }
  }
  return { name, type, value };
}

/** Parse a .vcf blob (possibly several BEGIN:VCARD…END:VCARD blocks). Returns
 *  Contact payloads (kith ADR 0002 shape, WITHOUT "id" — the caller assigns one).
 *  loamIdentity is set only if X-LOAM-KEY was present, always verified:false /
 *  addedVia:"import" (ADR 0005: "Import never fabricates identity"). */
export function importVcard(text: string): any[] {
  const out: any[] = [];
  let inCard = false;
  let name: any = null;
  let phones: any[] = [], emails: any[] = [], handles: any[] = [], addresses: any[] = [];
  let notes = "", loamKey = "", loamAddr = "";

  const flush = () => {
    if (!inCard) return;
    const c: any = {};
    c.name = name && Object.keys(name).length ? name : { display: "" };
    c.phones = phones; c.emails = emails; c.handles = handles; c.addresses = addresses;
    c.notes = notes;
    if (loamKey) c.loamIdentity = { address: loamAddr, pubHex: loamKey, verified: false, addedVia: "import" };
    out.push(c);
    name = null; phones = []; emails = []; handles = []; addresses = [];
    notes = ""; loamKey = ""; loamAddr = "";
    inCard = false;
  };

  const lines = text.split(/\r\n|\n|\r/);
  for (const raw of lines) {
    const line = raw;
    if (!line) continue;
    const up = trim(line).toUpperCase();
    if (up === "BEGIN:VCARD") { flush(); inCard = true; continue; }
    if (up === "END:VCARD") { flush(); continue; }
    if (!inCard) continue;
    const p = parseLine(line);
    const v = unesc(p.value);
    if (p.name === "FN") {
      name = name || {};
      name.display = v;
    } else if (p.name === "N") {
      const parts = p.value.split(";").map(unesc);
      name = name || {};
      if (parts[0]) name.family = parts[0];
      if (parts[1]) name.given = parts[1];
      if (!name.display) {
        const disp = trim((parts[1] || "") + " " + (parts[0] || ""));
        if (disp) name.display = disp;
      }
    } else if (p.name === "ORG") {
      name = name || {};
      name.org = v;
    } else if (p.name === "TEL") {
      phones.push({ label: p.type || "other", value: v });
    } else if (p.name === "EMAIL") {
      emails.push({ label: p.type || "other", value: v });
    } else if (p.name === "IMPP") {
      let kind = "other", val = v;
      const c1 = v.indexOf(":");
      if (c1 >= 0) { kind = v.slice(0, c1); val = v.slice(c1 + 1); }
      handles.push({ kind, value: val });
    } else if (p.name === "ADR") {
      const parts = p.value.split(";").map(unesc);
      const a: any = { label: p.type || "other" };
      if (parts[2]) a.street = parts[2];
      if (parts[3]) a.city = parts[3];
      if (parts[4]) a.region = parts[4];
      if (parts[5]) a.postcode = parts[5];
      if (parts[6]) a.country = parts[6];
      addresses.push(a);
    } else if (p.name === "NOTE") {
      notes = notes ? notes + "\n" + v : v;
    } else if (p.name === "X-LOAM-KEY") {
      loamKey = v;
    } else if (p.name === "X-LOAM-ADDR") {
      loamAddr = v;
    }
    // PHOTO and anything else: silently ignored.
  }
  flush(); // a file with no trailing END:VCARD still yields what was parsed
  return out;
}

/** Export one folded Contact (kith ADR 0002 shape, as returned by foldBook). */
export function exportOneVcard(c: any): string {
  const lines: string[] = ["BEGIN:VCARD", "VERSION:4.0"];
  const name = c.name || {};
  let display = name.display || "";
  if (!display) display = c.id || "";
  lines.push("FN:" + esc(display));
  const family = name.family || "", given = name.given || "";
  if (family || given) lines.push("N:" + esc(family) + ";" + esc(given) + ";;;");
  if (typeof name.org === "string") lines.push("ORG:" + esc(name.org));
  for (const p of c.phones || []) lines.push(`TEL;TYPE=${p.label || "other"}:${esc(p.value || "")}`);
  for (const e of c.emails || []) lines.push(`EMAIL;TYPE=${e.label || "other"}:${esc(e.value || "")}`);
  for (const h of c.handles || []) lines.push(`IMPP:${esc(h.kind || "other")}:${esc(h.value || "")}`);
  for (const a of c.addresses || [])
    lines.push(
      `ADR;TYPE=${a.label || "other"}:;;${esc(a.street || "")};${esc(a.city || "")};${esc(a.region || "")};${esc(a.postcode || "")};${esc(a.country || "")}`,
    );
  if (typeof c.notes === "string" && c.notes) lines.push("NOTE:" + esc(c.notes));
  if (c.loamIdentity && typeof c.loamIdentity === "object") {
    if (typeof c.loamIdentity.pubHex === "string") lines.push("X-LOAM-KEY:" + c.loamIdentity.pubHex);
    if (typeof c.loamIdentity.address === "string") lines.push("X-LOAM-ADDR:" + c.loamIdentity.address);
  }
  lines.push("END:VCARD");
  return lines.join("\r\n") + "\r\n";
}

export function exportManyVcard(contacts: any[]): string {
  return contacts.map(exportOneVcard).join("");
}
