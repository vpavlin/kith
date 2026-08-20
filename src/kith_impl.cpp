#include "kith_impl.h"

// Generated umbrella: modules() + typed dependency wrappers (loam_core).
#include "logos_sdk.h"

#include "contact_store.h"
#include "vcard.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <random>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <cctype>

using kith::json;

// -- small helpers ----------------------------------------------------------------
// RFC-4122-ish v4 UUID. MUST use a properly-seeded high-quality RNG: std::rand() is
// deterministic when unseeded (replays the same sequence from seed 1 every process
// launch), so unseeded it hands the FIRST book/contact after each restart an
// IDENTICAL id -> colliding books (shared log/key) and, worse, re-used contact ids
// that appendEvent's dedup-by-id silently DROPS (the exact scala_impl.cpp trap).
// Seed once from random_device; nonce hex nibbles from a persistent 64-bit Mersenne
// Twister.
static std::string generateUuid() {
    static std::mt19937_64 rng(std::random_device{}());
    static std::uniform_int_distribution<int> hex(0, 15);
    std::ostringstream oss;
    oss << std::hex;
    for (int i = 0; i < 32; i++) {
        int r = hex(rng);
        if (i == 8 || i == 12 || i == 16 || i == 20) oss << '-';
        if (i == 12) oss << '4';
        else if (i == 16) oss << (8 + (r & 3));
        else oss << r;
    }
    return oss.str();
}
static long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}
static std::string stableIdentity() {
    std::ifstream f("/etc/machine-id");
    std::string id; if (f) std::getline(f, id);
    if (id.empty()) id = "kith-" + std::to_string(nowMs());
    return "kith-" + id.substr(0, 16);
}

// -- construction -------------------------------------------------------------------
KithImpl::KithImpl() {
    m_store = new ContactStore();
}
KithImpl::~KithImpl() { delete m_store; }

// -- HLC + event helpers ------------------------------------------------------------
kith::HLC KithImpl::nextHlc() {
    long long t = nowMs();
    if (t > m_wall) { m_wall = t; m_ctr = 0; } else { m_ctr += 1; }
    return kith::HLC{ m_wall, m_ctr, m_identity };
}

// Author through loam_core's identity service (loam ADR 0004 / kith ADR 0003): the
// book's BOUND identity signs; keys never leave loam. This is why a book's owner is
// a real loam identity, not an ad hoc device key. Falls back to the local device key
// (kith_identity.hpp) so kith keeps working (single-writer, local-only) even without
// loam_core able to sign. 1:1 with scala_impl.cpp's mkEvent - see that file if this
// ever needs the keycard-aware async path scala has (kith ADR 0008, deliberately
// deferred here).
kith::Event KithImpl::mkEvent(const std::string& type, const json& payload, const std::string& bookId) {
    kith::Event e; e.v = 1; e.id = generateUuid(); e.type = type; e.payload = payload;
    if (!bookId.empty()) {
        std::string signer;
        try {
            std::string ir = modules().loam_core.identityForContainer(bookId); // sync caller -> JSON string
            if (!ir.empty()) {
                json meta = json::parse(ir, nullptr, false);
                if (meta.is_object()) signer = meta.value("address", std::string());
            }
        } catch (...) {}
        if (!signer.empty()) {
            e.dev = signer; e.hlc = nextHlc(); e.hlc.dev = signer;
            std::string digestHex = kith::toHexS(
                kith::sha256b(kith::strBytes(kith::canonicalMessage(e))).data(), 32);
            try {
                std::string sr = modules().loam_core.signDigest(bookId, digestHex); // sync caller -> JSON string
                if (!sr.empty()) {
                    json sres = json::parse(sr, nullptr, false);
                    std::string sg = sres.is_object() ? sres.value("sig", std::string()) : std::string();
                    std::string pk = sres.is_object() ? sres.value("pub", std::string()) : std::string();
                    if (!sg.empty() && !pk.empty()) {
                        e.pub = pk; e.sig = sg;
                        return e;   // signed by the loam identity
                    }
                }
            } catch (...) {}
        }
    }
    // Fallback: local device key. kith_engine::foldBook REQUIRES a valid signature,
    // so this path must actually sign, not just stamp an author.
    e.hlc = nextHlc(); e.dev = m_identity;
    if (m_signId.valid) kith::signEvent(m_signId, e);
    return e;
}

// Persist locally only. No sync/broadcast yet (see kith_impl.h's class doc) - a
// future ContactSync (modeled on scala's CalendarSync) plugs in here, exactly where
// scala_impl.cpp's publishAndApply calls m_sync->sendEvent after m_store->appendEvent.
void KithImpl::publishAndApply(const std::string& bookId, const kith::Event& e) {
    m_store->appendEvent(bookId, e);
}

// -- context lifecycle --------------------------------------------------------------
void KithImpl::onContextReady() {
    // Local fallback signing identity: load the persisted secp256k1 private key, or
    // generate one on first run. m_identity is the derived address ("0x...") used
    // only when a book has no loam_core-bound identity to sign with yet.
    std::string privHex = m_store->kvGet("sign_key");
    if (!privHex.empty()) m_signId = kith::identityFromPriv(kith::fromHexB(privHex));
    if (!m_signId.valid) {
        m_signId = kith::generateIdentity();
        if (m_signId.valid) m_store->kvSet("sign_key", kith::toHexS(m_signId.priv.data(), 32));
    }
    m_identity = m_signId.valid ? m_signId.address : m_store->kvGet("identity");
    if (m_identity.empty()) m_identity = stableIdentity();
    m_store->kvSet("identity", m_identity);
    m_ctxReady = true;

    // NOTE: no delivery/transport bootstrap here yet - see kith_impl.h's class doc
    // "OUT OF SCOPE" list. When sync lands, mirror scala_impl.cpp's onContextReady:
    // route the Transport through modules().loam_core (ADR 0015-style facade), one
    // content topic per book (`/kith/1/<bookId>/...`, kith ADR 0006).
}

// -- identity -------------------------------------------------------------------------
// Keep in sync with metadata.json "version".
std::string KithImpl::coreVersion() const { return "0.1.0"; }
std::string KithImpl::getIdentity() const { return m_identity; }

// -- books ------------------------------------------------------------------------------
std::string KithImpl::createBook(const std::string& name, const std::string& identityId) {
    // Guard against ever reusing a book id (scala's exact guard - a duplicate id
    // would silently share a log/key with an existing book).
    std::string id;
    do { id = generateUuid(); } while (!m_store->book(id).id.empty());
    std::string key = generateUuid() + generateUuid();   // 72-char dashed, unused until sync exists
    m_store->upsertBook({ id, key, name });
    // Bind the chosen identity in loam_core so every contact.set/contact.del this
    // book authors is signed by it (kith ADR 0003: Loam owns WHO). Empty -> the
    // current default identity, resolved HERE so a book reliably gets the identity
    // the user has picked even if a future view sends nothing.
    std::string bindId = identityId;
    if (bindId.empty()) {
        try {
            json d = json::parse(modules().loam_core.getDefaultIdentityId(), nullptr, false);
            if (d.is_string()) bindId = d.get<std::string>();   // getDefaultIdentityId returns a JSON-quoted string
        } catch (...) {}
    }
    if (!bindId.empty()) { try { modules().loam_core.bindContainer(id, bindId); } catch (...) {} }
    return id;
}
std::string KithImpl::listBooks() {
    json arr = json::array();
    for (const auto& b : m_store->books()) {
        json f = kith::foldBook(b.id, m_store->log(b.id));
        // Resolve the book's authoring identity ADDRESS here (once per book), so a
        // future view gets it in this single listBooks() call instead of making one
        // blocking loam IPC per book on refresh (same reasoning as scala's
        // listCalendars()).
        std::string authorAddr;
        try {
            json im = json::parse(modules().loam_core.identityForContainer(b.id), nullptr, false);
            if (im.is_object()) authorAddr = im.value("address", std::string());
        } catch (...) {}
        arr.push_back(json{{"id", b.id}, {"name", b.name}, {"authorAddr", authorAddr},
                           {"contactCount", (int)f["contacts"].size()}});
    }
    return arr.dump();
}
bool KithImpl::deleteBook(const std::string& id) {
    m_store->removeBook(id);
    return true;
}

// -- contacts ---------------------------------------------------------------------------
std::string KithImpl::addContact(const std::string& bookId, const std::string& contactJson) {
    json p = json::parse(contactJson, nullptr, false);
    if (p.is_discarded() || !p.is_object()) return "";
    p["id"] = generateUuid();
    publishAndApply(bookId, mkEvent(kith::ET::CONTACT_SET, p, bookId));
    bookChanged(bookId);
    return p["id"].get<std::string>();
}
std::string KithImpl::editContact(const std::string& bookId, const std::string& contactJson) {
    json p = json::parse(contactJson, nullptr, false);
    if (p.is_discarded() || !p.is_object() || !p.contains("id") || !p["id"].is_string()) return "";
    std::string id = p["id"].get<std::string>();
    publishAndApply(bookId, mkEvent(kith::ET::CONTACT_SET, p, bookId));
    bookChanged(bookId);
    return id;
}
bool KithImpl::deleteContact(const std::string& bookId, const std::string& contactId) {
    if (contactId.empty()) return false;
    publishAndApply(bookId, mkEvent(kith::ET::CONTACT_DEL, json{{"id", contactId}}, bookId));
    bookChanged(bookId);
    return true;
}
std::string KithImpl::listContacts(const std::string& bookId) {
    json f = kith::foldBook(bookId, m_store->log(bookId));
    return f["contacts"].dump();
}
std::string KithImpl::bookOwning(const std::string& contactId) const {
    for (const auto& b : m_store->books()) {
        json f = kith::foldBook(b.id, m_store->log(b.id));
        for (const auto& c : f["contacts"])
            if (c.value("id", std::string()) == contactId) return b.id;
    }
    return "";
}
std::string KithImpl::getContact(const std::string& contactId) {
    std::string bid = bookOwning(contactId);
    if (bid.empty()) return "{}";
    json f = kith::foldBook(bid, m_store->log(bid));
    for (const auto& c : f["contacts"])
        if (c.value("id", std::string()) == contactId) return c.dump();
    return "{}";
}

// -- app-facing query API (kith ADR 0007) ------------------------------------------------
std::string KithImpl::findByAddress(const std::string& address) {
    if (address.empty()) return "{}";
    for (const auto& b : m_store->books()) {
        json f = kith::foldBook(b.id, m_store->log(b.id));
        for (const auto& c : f["contacts"]) {
            if (c.contains("loamIdentity") && c["loamIdentity"].is_object() &&
                c["loamIdentity"].value("address", std::string()) == address)
                return c.dump();
        }
    }
    return "{}";
}
std::string KithImpl::searchContacts(const std::string& query) {
    json out = json::array();
    std::string q = query; for (auto& ch : q) ch = (char)tolower((unsigned char)ch);
    if (q.empty()) return out.dump();
    for (const auto& b : m_store->books()) {
        json f = kith::foldBook(b.id, m_store->log(b.id));
        for (const auto& c : f["contacts"]) {
            std::string hay = c.value("name", json::object()).value("display", std::string());
            hay += " " + c.value("notes", std::string());
            if (c.contains("emails") && c["emails"].is_array())
                for (auto& e : c["emails"]) hay += " " + e.value("value", std::string());
            if (c.contains("phones") && c["phones"].is_array())
                for (auto& p : c["phones"]) hay += " " + p.value("value", std::string());
            if (c.contains("handles") && c["handles"].is_array())
                for (auto& h : c["handles"]) hay += " " + h.value("value", std::string());
            for (auto& ch : hay) ch = (char)tolower((unsigned char)ch);
            if (hay.find(q) != std::string::npos) out.push_back(c);
        }
    }
    return out.dump();
}
std::string KithImpl::addAuthorToContacts(const std::string& bookId, const std::string& name,
                                          const std::string& address, const std::string& pubHex) {
    if (address.empty()) return "";
    std::string bid = bookId;
    if (bid.empty()) {
        for (const auto& b : m_store->books()) if (b.name == "Contacts") { bid = b.id; break; }
        if (bid.empty()) bid = createBook("Contacts");
    }
    // Dedup: an address already present in this book is not re-added.
    {
        json f = kith::foldBook(bid, m_store->log(bid));
        for (const auto& c : f["contacts"])
            if (c.contains("loamIdentity") && c["loamIdentity"].is_object() &&
                c["loamIdentity"].value("address", std::string()) == address)
                return c.value("id", std::string());
    }
    json p;
    p["name"] = json{{"display", name.empty() ? address : name}};
    p["phones"] = json::array(); p["emails"] = json::array();
    p["handles"] = json::array(); p["addresses"] = json::array();
    p["notes"] = "";
    p["loamIdentity"] = json{{"address", address}, {"pubHex", pubHex},
                             {"verified", false}, {"addedVia", "event-author"}};
    return addContact(bid, p.dump());
}

// -- vCard interop (kith ADR 0005; see src/vcard.hpp for the exact TODO list) -----------
std::string KithImpl::importVcard(const std::string& bookId, const std::string& vcardText) {
    auto contacts = kith::vcard::import(vcardText);
    json ids = json::array();
    for (auto& c : contacts) {
        std::string id = addContact(bookId, c.dump());
        if (!id.empty()) ids.push_back(id);
    }
    return json{{"imported", (int)ids.size()}, {"ids", ids}}.dump();
}
std::string KithImpl::exportVcard(const std::string& bookId, const std::string& contactId) {
    if (!contactId.empty()) {
        json c = json::parse(getContact(contactId), nullptr, false);
        if (!c.is_object() || c.empty()) return "";
        return kith::vcard::exportOne(c);
    }
    json f = kith::foldBook(bookId, m_store->log(bookId));
    return kith::vcard::exportMany(f["contacts"]);
}
