#pragma once
// vcard.hpp - MINIMAL vCard 4.0 (RFC 6350) import/export (kith ADR 0005). Field
// mapping is direct: FN/N <-> name, TEL <-> phones, EMAIL <-> emails, ADR <->
// addresses, IMPP <-> handles, NOTE <-> notes, X-LOAM-KEY/X-LOAM-ADDR <-> loamIdentity
// (ADR 0005's private extension - a Kith->Kith vCard round-trips loamIdentity
// losslessly; a Kith->other-app vCard degrades gracefully since other tools ignore
// unknown X- fields).
//
// KNOWN LIMITATIONS (TODO, not yet implemented - fine for a v1 core per the scaffold
// brief; flag before calling this "vCard support" in any release notes):
//   - No RFC 6350 line UNFOLDING (a property split across continuation lines
//     starting with a space/tab) on import, and no line FOLDING (>75 octets) on
//     export. Long NOTE/ADR values will be technically non-conformant.
//   - No quoted-printable / charset= decoding (vCard 2.1-era encoding).
//   - Only the FIRST TYPE= parameter is used as the label; multi-TYPE
//     ("TYPE=work,voice") is not split out.
//   - PHOTO is not imported or exported at all (kith ADR 0006's avatar size-cap
//     story is still open) - photos are silently dropped on import, never emitted
//     on export.
//   - ORG (organisation) is read into name.org but not re-emitted as its own ORG
//     line on export (folded back into N/FN only).
//   - vCard 3.0 tolerance (ADR 0005 open question) is untested; only 4.0-shaped
//     input has been exercised.
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace kith {
namespace vcard {
using json = nlohmann::json;

// -- escaping (RFC 6350 SS 3.4): backslash, comma, semicolon, newline --------------
inline std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case ',':  o += "\\,";  break;
            case ';':  o += "\\;";  break;
            case '\n': o += "\\n";  break;
            case '\r': break;   // dropped; \n alone is the folded line break kith emits
            default:   o += c;
        }
    }
    return o;
}
inline std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n' || n == 'N') { o += '\n'; i++; }
            else if (n == ',' || n == ';' || n == '\\') { o += n; i++; }
            else o += s[i];
        } else o += s[i];
    }
    return o;
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::string upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }

// One parsed property line: NAME;PARAM=val;...:VALUE
struct Prop {
    std::string name;                                  // upper-cased, no group prefix
    std::string type;                                   // first TYPE= param, lower-cased ("" if none)
    std::string value;                                   // raw (still escaped)
};

// Split "NAME;P1=V1;P2=V2:VALUE" (no line-unfolding - see file header TODO).
static Prop parseLine(const std::string& line) {
    Prop p;
    size_t colon = line.find(':');
    if (colon == std::string::npos) return p;
    std::string head = line.substr(0, colon);
    p.value = line.substr(colon + 1);
    size_t semi = head.find(';');
    std::string name = semi == std::string::npos ? head : head.substr(0, semi);
    // Drop a "group." prefix (e.g. "item1.TEL") if present.
    size_t dot = name.find('.');
    if (dot != std::string::npos) name = name.substr(dot + 1);
    p.name = upper(trim(name));
    if (semi != std::string::npos) {
        std::stringstream ss(head.substr(semi + 1));
        std::string param;
        while (std::getline(ss, param, ';')) {
            std::string up = upper(param);
            if (up.rfind("TYPE=", 0) == 0 && p.type.empty()) {
                std::string t = param.substr(5);
                size_t comma = t.find(',');
                if (comma != std::string::npos) t = t.substr(0, comma);   // first TYPE only (see TODO)
                for (auto& c : t) c = (char)tolower((unsigned char)c);
                p.type = t;
            }
        }
    }
    return p;
}

// -- import: one .vcf blob (possibly several BEGIN:VCARD...END:VCARD blocks) -----
// Returns Contact payloads (kith ADR 0002 shape, WITHOUT "id" - the caller assigns
// one, same as any other addContact()). loamIdentity is set only if X-LOAM-KEY was
// present, and always with verified:false / addedVia:"import" (ADR 0005: "Import
// never fabricates identity").
inline std::vector<json> import(const std::string& text) {
    std::vector<json> out;
    std::stringstream all(text);
    std::string raw;
    bool inCard = false;
    json name; std::vector<json> phones, emails, handles, addresses;
    std::string notes, loamKey, loamAddr;

    auto flush = [&]() {
        if (!inCard) return;
        json c;
        if (!name.empty()) c["name"] = name;
        else c["name"] = json{{"display", ""}};
        c["phones"] = phones; c["emails"] = emails; c["handles"] = handles; c["addresses"] = addresses;
        c["notes"] = notes;
        if (!loamKey.empty()) c["loamIdentity"] = json{{"address", loamAddr}, {"pubHex", loamKey},
                                                        {"verified", false}, {"addedVia", "import"}};
        out.push_back(c);
        name = json(); phones.clear(); emails.clear(); handles.clear(); addresses.clear();
        notes.clear(); loamKey.clear(); loamAddr.clear();
        inCard = false;
    };

    while (std::getline(all, raw)) {
        std::string line = raw;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::string up = upper(trim(line));
        if (up == "BEGIN:VCARD") { flush(); inCard = true; continue; }
        if (up == "END:VCARD") { flush(); continue; }
        if (!inCard) continue;
        Prop p = parseLine(line);
        std::string v = unescape(p.value);
        if (p.name == "FN") {
            if (!name.is_object()) name = json::object();
            name["display"] = v;
        } else if (p.name == "N") {
            // N: Family;Given;Middle;Prefix;Suffix
            std::vector<std::string> parts; std::stringstream ss(p.value); std::string part;
            while (std::getline(ss, part, ';')) parts.push_back(unescape(part));
            if (!name.is_object()) name = json::object();
            if (parts.size() > 0 && !parts[0].empty()) name["family"] = parts[0];
            if (parts.size() > 1 && !parts[1].empty()) name["given"] = parts[1];
            if (!name.contains("display")) {
                std::string given = parts.size() > 1 ? parts[1] : "";
                std::string family = parts.size() > 0 ? parts[0] : "";
                std::string disp = trim(given + " " + family);
                if (!disp.empty()) name["display"] = disp;
            }
        } else if (p.name == "ORG") {
            if (!name.is_object()) name = json::object();
            name["org"] = v;
        } else if (p.name == "TEL") {
            phones.push_back(json{{"label", p.type.empty() ? "other" : p.type}, {"value", v}});
        } else if (p.name == "EMAIL") {
            emails.push_back(json{{"label", p.type.empty() ? "other" : p.type}, {"value", v}});
        } else if (p.name == "IMPP") {
            // IMPP:telegram:@alice  ->  kind=telegram, value=@alice (scheme is the "kind").
            std::string kind = "other", val = v;
            size_t c1 = v.find(':');
            if (c1 != std::string::npos) { kind = v.substr(0, c1); val = v.substr(c1 + 1); }
            handles.push_back(json{{"kind", kind}, {"value", val}});
        } else if (p.name == "ADR") {
            // ADR: POBox;Ext;Street;City;Region;Postcode;Country
            std::vector<std::string> parts; std::stringstream ss(p.value); std::string part;
            while (std::getline(ss, part, ';')) parts.push_back(unescape(part));
            json a = json::object();
            a["label"] = p.type.empty() ? "other" : p.type;
            if (parts.size() > 2 && !parts[2].empty()) a["street"] = parts[2];
            if (parts.size() > 3 && !parts[3].empty()) a["city"] = parts[3];
            if (parts.size() > 4 && !parts[4].empty()) a["region"] = parts[4];
            if (parts.size() > 5 && !parts[5].empty()) a["postcode"] = parts[5];
            if (parts.size() > 6 && !parts[6].empty()) a["country"] = parts[6];
            addresses.push_back(a);
        } else if (p.name == "NOTE") {
            notes = notes.empty() ? v : (notes + "\n" + v);
        } else if (p.name == "X-LOAM-KEY") {
            loamKey = v;
        } else if (p.name == "X-LOAM-ADDR") {
            loamAddr = v;
        }
        // PHOTO and anything else: silently ignored (see file header TODO).
    }
    flush();   // a file with no trailing END:VCARD still yields what we parsed
    return out;
}

// -- export: one folded Contact (kith ADR 0002 shape, as returned by foldBook) ---
inline std::string exportOne(const json& c) {
    std::ostringstream o;
    o << "BEGIN:VCARD\r\n" << "VERSION:4.0\r\n";
    json name = c.value("name", json::object());
    std::string display = name.is_object() ? name.value("display", std::string()) : std::string();
    if (display.empty()) display = c.value("id", std::string());
    o << "FN:" << escape(display) << "\r\n";
    std::string family = name.is_object() ? name.value("family", std::string()) : std::string();
    std::string given  = name.is_object() ? name.value("given", std::string())  : std::string();
    if (!family.empty() || !given.empty())
        o << "N:" << escape(family) << ";" << escape(given) << ";;;\r\n";
    if (name.is_object() && name.contains("org") && name["org"].is_string())
        o << "ORG:" << escape(name["org"].get<std::string>()) << "\r\n";
    if (c.contains("phones") && c["phones"].is_array())
        for (auto& p : c["phones"])
            o << "TEL;TYPE=" << p.value("label", std::string("other")) << ":" << escape(p.value("value", std::string())) << "\r\n";
    if (c.contains("emails") && c["emails"].is_array())
        for (auto& e : c["emails"])
            o << "EMAIL;TYPE=" << e.value("label", std::string("other")) << ":" << escape(e.value("value", std::string())) << "\r\n";
    if (c.contains("handles") && c["handles"].is_array())
        for (auto& h : c["handles"])
            o << "IMPP:" << escape(h.value("kind", std::string("other"))) << ":" << escape(h.value("value", std::string())) << "\r\n";
    if (c.contains("addresses") && c["addresses"].is_array())
        for (auto& a : c["addresses"])
            o << "ADR;TYPE=" << a.value("label", std::string("other")) << ":;;"
              << escape(a.value("street", std::string())) << ";" << escape(a.value("city", std::string())) << ";"
              << escape(a.value("region", std::string())) << ";" << escape(a.value("postcode", std::string())) << ";"
              << escape(a.value("country", std::string())) << "\r\n";
    if (c.contains("notes") && c["notes"].is_string() && !c["notes"].get<std::string>().empty())
        o << "NOTE:" << escape(c["notes"].get<std::string>()) << "\r\n";
    if (c.contains("loamIdentity") && c["loamIdentity"].is_object()) {
        const auto& li = c["loamIdentity"];
        if (li.contains("pubHex") && li["pubHex"].is_string())
            o << "X-LOAM-KEY:" << li["pubHex"].get<std::string>() << "\r\n";
        if (li.contains("address") && li["address"].is_string())
            o << "X-LOAM-ADDR:" << li["address"].get<std::string>() << "\r\n";
    }
    o << "END:VCARD\r\n";
    return o.str();
}

inline std::string exportMany(const json& contactsArr) {
    std::ostringstream o;
    for (const auto& c : contactsArr) o << exportOne(c);
    return o.str();
}

} // namespace vcard
} // namespace kith
