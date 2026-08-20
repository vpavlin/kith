#include "kith_impl.h"

// Generated umbrella: modules() + typed dependency wrappers (loam_core).
#include "logos_sdk.h"

#include "contact_store.h"
#include "contact_sync.h"
#include "vcard.hpp"

#include <nlohmann/json.hpp>
#include "logos_transport.hpp"
#include "logos_sync/catchup.hpp"   // delta catch-up (buildInitial / respond)
#include <QTimer>                    // catch-up retry timers (mesh forms ~10s after start)

#include <chrono>
#include <random>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <cctype>
#include <cstring>
#include <map>
#include <sstream>

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
// base64url (RFC 4648, no padding) - matches scala's/mobile's invite key encoding.
static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static std::string b64urlEncode(const std::string& in) {
    std::string out; int val = 0, bits = -6;
    for (unsigned char c : in) { val = (val << 8) + c; bits += 8;
        while (bits >= 0) { out.push_back(kB64[(val >> bits) & 0x3F]); bits -= 6; } }
    if (bits > -6) out.push_back(kB64[((val << 8) >> (bits + 8)) & 0x3F]);
    return out;
}
static std::string b64urlDecode(const std::string& in) {
    std::vector<int> T(256, -1); for (int i = 0; i < 64; i++) T[(unsigned char)kB64[i]] = i;
    std::string out; int val = 0, bits = -8;
    for (unsigned char c : in) { if (T[(unsigned char)c] == -1) continue; val = (val << 6) + T[(unsigned char)c]; bits += 6;
        if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; } }
    return out;
}
static std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF"; std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o.push_back(c);
        else { o.push_back('%'); o.push_back(hex[c >> 4]); o.push_back(hex[c & 15]); }
    }
    return o;
}
static std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) { o.push_back((char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
        else if (s[i] == '+') o.push_back(' ');
        else o.push_back(s[i]);
    }
    return o;
}
static std::map<std::string, std::string> parseQuery(const std::string& link) {
    std::map<std::string, std::string> q;
    auto pos = link.find('?'); if (pos == std::string::npos) return q;
    std::string qs = link.substr(pos + 1); std::stringstream ss(qs); std::string pair;
    while (std::getline(ss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq == std::string::npos) continue;
        q[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
    }
    return q;
}

// -- construction -------------------------------------------------------------------
KithImpl::KithImpl() {
    m_store = new ContactStore();
    m_sync = new ContactSync();
    // CRDT path: a received event's raw JSON is merged into that book's log.
    m_sync->setEventHandler([this](const std::string& bookId, const std::string& eventJson) {
        applyIncoming(bookId, eventJson);
    });
    m_sync->setStatusHandler([this](const std::string& bookId, const std::string& status) {
        syncStatusChanged(bookId, status);
    });
}
KithImpl::~KithImpl() { delete m_sync; delete m_store; }

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

// Persist locally, then broadcast (dedup-by-id on the wire makes a redelivered echo
// of our own write a no-op; a disk write that never reaches the network is still
// durable). Mirrors scala_impl.cpp's publishAndApply -> CalendarSync::sendEvent.
void KithImpl::publishAndApply(const std::string& bookId, const kith::Event& e) {
    m_store->appendEvent(bookId, e);
    m_sync->sendEvent(bookId, kith::eventToJson(e).dump());
}

// Merge a received event's raw JSON into that book's log. CATCH-UP: a peer
// publishing the ids it holds (SYNC_REQ) is routed to onSyncReq() and never
// stored - matches scala_impl.cpp's applyIncoming.
void KithImpl::applyIncoming(const std::string& bookId, const std::string& eventJson) {
    json j = json::parse(eventJson, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    kith::Event e = kith::eventFromJson(j);
    if (e.id.empty()) return;
    if (e.type == kith::ET::SYNC_REQ) { onSyncReq(bookId, e.payload); return; }
    m_store->appendEvent(bookId, e);   // idempotent (dedup by id)
    bookChanged(bookId);               // best-effort nudge (kith_impl.h note on delivery)
}

// Step the catch-up state machine for one incoming SYNC_REQ. `msg` is a single RBSR
// range statement (fp/ids/need); respond() self-ignores our own `from` and returns
// the exact events to serve plus the reply messages to publish. Recursive
// Range-Based Set Reconciliation: only the id-exact delta is transferred and every
// message is single-segment. 1:1 with scala_impl.cpp's onSyncReq.
void KithImpl::onSyncReq(const std::string& bookId, const json& msg) {
    auto step = logos_sync::catchup::respond(m_store->log(bookId), msg, m_identity);
    for (const auto& ev : step.serve)                              // the missing events, exactly
        m_sync->sendEvent(bookId, kith::eventToJson(ev).dump());
    for (const auto& r : step.replies)                            // fp/ids/need range replies
        m_sync->sendEvent(bookId, kith::eventToJson(mkEvent(kith::ET::SYNC_REQ, r)).dump());
}

// Kick off catch-up: publish the initial reconciliation message (a bounded set of
// range fingerprints over what we hold). Never touches the store - SYNC_REQ events
// are sent directly over the wire, mirroring scala_impl.cpp's sendSyncReq.
void KithImpl::sendSyncReq(const std::string& bookId) {
    json m = logos_sync::catchup::buildInitial(m_store->log(bookId), m_identity);
    m_sync->sendEvent(bookId, kith::eventToJson(mkEvent(kith::ET::SYNC_REQ, m)).dump());
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

    using LogosMap = nlohmann::json;
    using Tx = logos_transport::Transport<LogosMap>;

    // Route the Transport through the loam_core FACADE (kith ADR: "prefer loam_core
    // over re-embedding delivery_module" - loam_core.start()/join()/sendSealed()/
    // received() is a clean, already-proven surface; scala and kym_core both already
    // route through it). loam_core.start() does createNode+start together and
    // returns EARLY, so node readiness arrives via statusChanged("Connected") - latch
    // the createNode cb on the first Connected, exactly like scala_impl.cpp.
    Tx::Ops ops;
    ops.createNode = [this](const std::string& cfg, Tx::Cb cb) {
        modules().loam_core.setSenderIdAsync(m_identity.empty() ? std::string("kith-default") : m_identity,
                                             [](std::string) {});
        auto fired = std::make_shared<bool>(false);
        modules().loam_core.onStatusChanged([cb, fired](const std::string& s) {
            if (s == "Connected" && !*fired) { *fired = true; cb(true, ""); }
        });
        modules().loam_core.startAsync(cfg, [](std::string err) {
            if (!err.empty()) fprintf(stderr, "[kith] loam_core.start: %s\n", err.c_str());
        });
    };
    ops.start = [this](Tx::Cb cb) { cb(true, ""); };   // loam_core.start already did createNode+start
    ops.subscribe = [this](const std::string& t, Tx::Cb cb) {
        modules().loam_core.joinAsync(t, [cb](std::string) { cb(true, ""); });
    };
    ops.channelCreate = [this](const std::string&, const std::string&, const std::string&, Tx::Cb cb) {
        cb(true, "");   // loam_core.join() already subscribes + creates the SDS channel with our senderId
    };
    ops.channelSend = [this](const std::string& id, const LogosMap& payload, Tx::Cb cb) {
        std::string b64;
        if (payload.is_string()) b64 = payload.get<std::string>();
        else if (payload.is_array()) { for (const auto& c : payload) if (c.is_number_integer()) b64.push_back((char)c.get<int>()); }
        modules().loam_core.sendSealedAsync(id, b64, [cb](std::string) { cb(true, ""); });
    };
    ops.onMessage = [this](Tx::RecvCb handler) {
        modules().loam_core.onReceived(
            [handler](const std::string& topic, const std::string&, const std::string& payloadB64, int64_t) {
                handler(topic, LogosMap(payloadB64));
            });
    };
    ops.onChannelMessage = [this](Tx::RecvCb) { /* loam_core.onReceived covers both - avoid double-deliver */ };

    Tx tx(std::move(ops),
          Tx::Config{
              .logLevel = "INFO",
              .preset = "logos.test",   // logos.dev migrated to cluster 3; logos.test = cluster 2
              .entryNodes = {
                  "/dns4/node-01.do-ams3.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmQ9X2xDfPG3uL77V9piYDhjq14JhKCtcmNYsTMKNqrKCj",
                  "/dns4/node-02.do-ams3.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmB8NYprrfQrgWVzsJtYWkfjsXbmJEGNMG6othXsQ53BwG",
                  "/dns4/node-01.gc-us-central1-a.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmF8WtwGPmeGHgYAX2277jHgy5cW9F7zsB8EqUjBZQAZQ3",
                  "/dns4/node-02.gc-us-central1-a.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmUuXhUW9bdJpzN1kfDziFiUZo4bszTk66cvr7uuyCHXR7",
                  "/dns4/node-01.ac-cn-hongkong-c.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmL3oU95jh1BZHozn3uNhx8HEneirgr8M1jEAapzXGDqRF",
                  "/dns4/node-02.ac-cn-hongkong-c.logos.test.status.im/tcp/30303/p2p/16Uiu2HAm28CoBZjpyxsanC8tQpbvZ7bZJnVYuB1EgFzb571qpWsV",
              },
              .useChannels = true,
              .hubMode = false,
              .deviceId = m_identity.empty() ? std::string("kith-default") : m_identity,
          },
          // topics: every book in the registry has a channel.
          [this]{ std::vector<std::string> topics;
                  for (const auto& b : m_store->books())
                      topics.push_back(ContactSync::topicForBook(b.id));
                  return topics; },
          [this](const std::string& topic, const std::string& sealedOnceDecoded) {
              m_sync->handleReceive(topic, sealedOnceDecoded);
          },
          /*onReady*/ [this]{
              // Node connected: ask peers what we're missing (no blind whole-log seed).
              for (const auto& b : m_store->books()) sendSyncReq(b.id);
          },
          /*setStatus*/ [this](const std::string& s) { m_deliveryStatus = s; });

    m_sync->setTransport(std::move(tx));
    m_ctxReady = true;

    // Register each book's key so seal/open + channel joins work after a restart.
    for (const auto& b : m_store->books())
        if (!b.key.empty()) m_sync->startSync(b.id, b.key);

    m_sync->bootstrap();

    // Catch-up retry: start() finishes BEFORE the gossip mesh has peers (~10s to
    // form) and before the async subscribe/channel-join land, so a single SYNC_REQ
    // at onReady races into the void. Re-request at 3/10/25s, same as scala_impl.cpp.
    for (int ms : {3000, 10000, 25000})
        QTimer::singleShot(ms, [this]{
            if (m_sync && m_sync->ready())
                for (const auto& b : m_store->books()) sendSyncReq(b.id);
        });
}

void KithImpl::ensureDelivery() { if (m_sync) m_sync->bootstrap(); }

// -- identity -------------------------------------------------------------------------
// Keep in sync with metadata.json "version". The view compares this to the minimum
// it needs and warns on a stale core.
std::string KithImpl::coreVersion() const { return "0.2.0"; }
std::string KithImpl::getIdentity() const { return m_identity; }

// -- books ------------------------------------------------------------------------------
std::string KithImpl::createBook(const std::string& name, const std::string& identityId) {
    // Guard against ever reusing a book id (scala's exact guard - a duplicate id
    // would silently share a log/key with an existing book).
    std::string id;
    do { id = generateUuid(); } while (!m_store->book(id).id.empty());
    std::string key = generateUuid() + generateUuid();   // 72-char dashed, same shape scala/kym use
    m_store->upsertBook({ id, key, name });
    m_sync->startSync(id, key);
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
    ensureDelivery();   // self-drive: bring the node up even if onContextReady was flaky
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
                           {"contactCount", (int)f["contacts"].size()},
                           {"syncing", m_sync ? m_sync->isSyncing(b.id) : false}});
    }
    return arr.dump();
}
bool KithImpl::deleteBook(const std::string& id) {
    m_sync->stopSync(id);
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
    ensureDelivery();
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

// -- sync / share API (kith ADR 0004/0006, Phase 4) --------------------------------------
std::string KithImpl::getSyncStatus(const std::string& bookId) {
    if (m_store->book(bookId).id.empty()) return "not_shared";
    return m_sync->isSyncing(bookId) ? "syncing" : "offline";
}
std::string KithImpl::shareLink(const std::string& bookId) {
    kith::BookReg b = m_store->book(bookId);
    if (b.id.empty() || b.key.empty()) return "";
    json f = kith::foldBook(b.id, m_store->log(b.id));
    std::string nm = b.name;   // book name lives only in the registry (kith ADR 0002 has no book.meta event)
    return "kith://join?id=" + urlEncode(b.id) + "&key=" + b64urlEncode(b.key) + "&name=" + urlEncode(nm);
}
std::string KithImpl::parseShareLink(const std::string& link) {
    if (link.rfind("kith://join", 0) != 0) return "";
    auto q = parseQuery(link);
    std::string id = q["id"], key = b64urlDecode(q["key"]), name = q["name"];
    if (id.empty() || key.empty()) return "";
    return json{{"id", id}, {"key", key}, {"name", name}}.dump();
}
bool KithImpl::handleShareLink(const std::string& link, const std::string& identityId) {
    std::string parsed = parseShareLink(link);
    if (parsed.empty()) return false;
    json j = json::parse(parsed, nullptr, false);
    std::string id = j.value("id", std::string()), key = j.value("key", std::string()), name = j.value("name", std::string());
    if (id.empty() || key.empty()) return false;
    m_store->upsertBook({ id, key, name });
    m_sync->startSync(id, key);
    // Bind the chosen identity (kith ADR 0003: Loam owns WHO) so YOUR writes on this
    // book are authored by it. None passed -> the current default, resolved here so
    // it works even if a future view sends nothing (scala's exact resolution).
    std::string bindId = identityId;
    if (bindId.empty()) { try { json d = json::parse(modules().loam_core.getDefaultIdentityId(), nullptr, false);
        if (d.is_string()) bindId = d.get<std::string>(); } catch (...) {} }
    if (!bindId.empty()) { try { modules().loam_core.bindContainer(id, bindId); } catch (...) {} }
    sendSyncReq(id);   // just joined -> pull history
    return true;
}
std::string KithImpl::diagnostics() {
    ensureDelivery();
    json out;
    out["identity"] = m_identity;
    out["ctxReady"] = m_ctxReady;
    out["deliveryStatus"] = m_deliveryStatus;
    out["nodeReady"] = m_sync ? m_sync->ready() : false;
    out["dataDir"] = m_store ? m_store->dataDir() : std::string();
    json books = json::array();
    int totalContacts = 0;
    for (const auto& b : m_store->books()) {
        json f = kith::foldBook(b.id, m_store->log(b.id));
        int n = (int)f["contacts"].size(); totalContacts += n;
        books.push_back(json{{"id", b.id}, {"name", b.name},
                             {"syncing", m_sync ? m_sync->isSyncing(b.id) : false}, {"contacts", n}});
    }
    out["books"] = books;
    out["bookCount"] = (int)m_store->books().size();
    out["contactCount"] = totalContacts;
    return out.dump();
}
