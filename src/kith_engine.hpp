#pragma once
// kith_engine.hpp - the pure, deterministic fold from a merged event log to book
// state. Modeled directly on scala_engine.hpp (event-log CRDT): every change is an
// immutable event; current state = fold over the merged log. Merge is union-by-id +
// HLC sort (logos_sync::mergeEvents), so it is idempotent (redelivery is a no-op),
// commutative and associative (arrival order is irrelevant) - offline devices
// converge with no lost writes.
//
// One log == one address book (kith ADR 0004: "one log per book"). A Contact (kith
// ADR 0002) is authored WHOLE-RECORD in v1 - contact.set carries the complete record,
// not a per-field diff ("start with whole-record contact.set" is ADR 0002's explicit
// v1 choice; field-level LWW is future work, same open question the ADR leaves).
#include <string>
#include <vector>
#include <map>
#include <set>
#include <nlohmann/json.hpp>

#include "logos_sync/event.hpp"
#include "logos_sync/merge.hpp"   // mergeEvents - union-by-id + HLC sort
#include "kith_identity.hpp"      // isSigned / verifyEvent - authenticity gate below

namespace kith {
using json = nlohmann::json;

// Adopt the shared spine into the kith:: namespace so the rest of the module
// (ContactStore, KithImpl) keeps compiling against kith::Event etc.
using logos_sync::HLC;
using logos_sync::compareHlc;
using logos_sync::Event;
using logos_sync::eventToJson;
using logos_sync::eventFromJson;
using logos_sync::mergeEvents;

// Event type constants (kith ADR 0002).
namespace ET {
    // {id, name:{display,given?,family?,org?}, phones:[{label,value}], emails:[{label,value}],
    //  handles:[{kind,value}], addresses:[{label,street?,city?,region?,postcode?,country?}],
    //  notes, avatar?, loamIdentity?:{address,pubHex,verified,addedVia}}
    // Whole-record LWW upsert by payload.id - the latest contact.set for an id wins.
    constexpr const char* CONTACT_SET = "contact.set";
    // {id} - tombstone. TERMINAL: matches scala's event.put/event.del rule, a
    // contact.set for a tombstoned id arriving (or already in the log) after its
    // contact.del is dropped, so a delete can never be silently resurrected by a
    // stale/racing edit.
    constexpr const char* CONTACT_DEL = "contact.del";
}

// fold: merged log -> book state {id, contacts:[...]}. contacts is sorted by id
// (std::map key order) purely for a stable, deterministic listContacts() output;
// the view/caller is expected to sort by name for display.
inline json foldBook(const std::string& bookId, const std::vector<Event>& log) {
    auto ordered = mergeEvents(log);
    std::map<std::string, json> contacts;   // contact id -> current record
    std::set<std::string> tombstones;

    for (const auto& e : ordered) {
        // Signatures are ALWAYS required (matches scala_engine's rule): an unsigned
        // or forged event is dropped entirely, so a tampered/replayed write can never
        // enter the folded book. This also means the fallback local device key
        // (kith_identity.hpp) must actually sign every event kith_impl authors -
        // see KithImpl::mkEvent.
        if (!isSigned(e) || !verifyEvent(e)) continue;
        if (e.type == ET::CONTACT_SET) {
            std::string id = e.payload.value("id", std::string());
            if (id.empty() || tombstones.count(id)) continue;   // tombstone terminal
            json c = e.payload;
            bool creating = !contacts.count(id);
            c["createdAt"] = creating ? e.hlc.wall : contacts[id].value("createdAt", e.hlc.wall);
            c["updatedAt"] = e.hlc.wall;
            c["authorAddr"] = !e.hlc.dev.empty() ? e.hlc.dev : e.dev;
            contacts[id] = std::move(c);
        } else if (e.type == ET::CONTACT_DEL) {
            std::string id = e.payload.value("id", std::string());
            if (id.empty()) continue;
            tombstones.insert(id);
            contacts.erase(id);
        }
    }

    json arr = json::array();
    for (auto& kv : contacts) arr.push_back(kv.second);
    return json{{"id", bookId}, {"contacts", arr}};
}

} // namespace kith
