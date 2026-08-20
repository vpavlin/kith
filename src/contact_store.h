#pragma once
// ContactStore - CRDT log storage, modeled directly on scala's CalendarStore. A book
// is a membership entry (id + key + name) in the registry; its state lives in an
// append-only EVENT LOG (one log per book - kith ADR 0004). Reads fold the log
// (kith_engine::foldBook). Plain files under a stable $HOME data dir, robust no-throw:
//     <dataDir>/books.json         - registry: [{id,key,name}] (membership)
//     <dataDir>/logs/<bookId>.json - that book's append-only event log
//     <dataDir>/kv.json            - identity / device id / settings
#include "kith_engine.hpp"
#include <string>
#include <vector>

namespace kith {
struct BookReg { std::string id, key, name; };
}

class ContactStore {
public:
    ContactStore();

    std::string dataDir() const { return m_dataDir; }

    // -- registry (membership) --
    std::vector<kith::BookReg> books() const;
    kith::BookReg book(const std::string& id) const;   // empty id if unknown
    void upsertBook(const kith::BookReg& r);
    void removeBook(const std::string& id);            // + delete its log

    // -- per-book event log --
    std::vector<kith::Event> log(const std::string& bookId) const;
    // Merge one event into the log (dedup by id), persist. Returns true if NEW.
    bool appendEvent(const std::string& bookId, const kith::Event& e);

    // -- kv (identity, device id, settings) --
    std::string kvGet(const std::string& key) const;
    void kvSet(const std::string& key, const std::string& value);

private:
    std::string m_dataDir;
    std::string bookFile() const;
    std::string logFile(const std::string& id) const;
    std::string kvFile() const;
    void writeLog(const std::string& bookId, const std::vector<kith::Event>& evs) const;
};
