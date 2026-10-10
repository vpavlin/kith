#include <set>
#include "kith_json.hpp"
#include "contact_store.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <cstdio>

namespace fs = std::filesystem;
using kith::json;

// Stable, writable data dir - the scala/qaku pattern ($HOME/.kith-core). Override
// with KITH_CORE_DATA (multi-instance / tests). NOT QStandardPaths (transient in the
// Basecamp AppImage sandbox).
static std::string computeDataDir() {
    if (const char* e = std::getenv("KITH_CORE_DATA")) if (*e) return e;
    if (const char* h = std::getenv("HOME")) if (*h) return std::string(h) + "/.kith-core";
    return "/tmp/.kith-core";
}
static bool ensureDir(const std::string& d) {
    if (d.empty()) return false;
    std::error_code ec; fs::create_directories(d, ec); return fs::exists(d, ec);
}
static json readJson(const std::string& path) {
    std::ifstream f(path);
    if (!f) return json();
    std::stringstream ss; ss << f.rdbuf();
    return json::parse(ss.str(), nullptr, false);   // no-throw; returns discarded on error
}
// True iff the file EXISTS but can't be read/parsed. A read-modify-write must then
// refuse to write: rewriting from the (empty) parse result would wipe real data.
static bool existsButUnreadable(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;
    return readJson(path).is_discarded();
}
// Atomic write: temp then rename. The rename only happens after the whole temp file
// was written and closed successfully (disk full / I/O error leaves the stream bad) -
// on any failure the temp is dropped and the OLD file is kept intact.
static bool writeJson(const std::string& path, const json& j) {
    const std::string tmp = path + ".tmp";
    std::error_code ec;
    {
        std::ofstream f(tmp, std::ios::out | std::ios::trunc);
        if (!f) return false;
        f << j.dump();
        f.flush();
        if (!f) { f.close(); fs::remove(tmp, ec); fprintf(stderr, "[kith] write failed: %s\n", path.c_str()); return false; }
        f.close();
        if (f.fail()) { fs::remove(tmp, ec); fprintf(stderr, "[kith] write failed: %s\n", path.c_str()); return false; }
    }
    fs::rename(tmp, path, ec);
    if (ec) { fs::remove(tmp, ec); fprintf(stderr, "[kith] rename failed: %s\n", path.c_str()); return false; }
    return true;
}

ContactStore::ContactStore() {
    m_dataDir = computeDataDir();
    ensureDir(m_dataDir);
    ensureDir(m_dataDir + "/logs");
}

std::string ContactStore::bookFile() const { return m_dataDir + "/books.json"; }
// Callers must pass a validated id (kith::isValidBookId) - never a raw string.
std::string ContactStore::logFile(const std::string& id) const { return m_dataDir + "/logs/" + id + ".json"; }
std::string ContactStore::kvFile() const { return m_dataDir + "/kv.json"; }

// -- registry --------------------------------------------------------------------
std::vector<kith::BookReg> ContactStore::books() const {
    std::vector<kith::BookReg> out;
    json arr = readJson(bookFile());
    if (!arr.is_array()) return out;
    // Dedup by id (first-wins). upsertBook updates every matching row but never
    // removes extras, so a books.json that ever picked up duplicate ids (older
    // builds / a write race) would render each book N times forever. Collapsing here
    // fixes the display immediately and, because upsert/remove rewrite the file from
    // this list, self-heals books.json on the next write. (scala 0.9.2 fix, same trap.)
    std::set<std::string> seen;
    for (auto& j : arr) {
        if (!j.is_object() || !j.contains("id")) continue;
        std::string id = kith::sv(j, "id");
        if (id.empty() || !seen.insert(id).second) continue;
        out.push_back({ id, kith::sv(j, "key"), kith::sv(j, "name") });
    }
    return out;
}
kith::BookReg ContactStore::book(const std::string& id) const {
    for (auto& r : books()) if (r.id == id) return r;
    return {};
}
void ContactStore::upsertBook(const kith::BookReg& r) {
    if (!kith::isValidBookId(r.id)) return;
    if (existsButUnreadable(bookFile())) { fprintf(stderr, "[kith] books.json unreadable - not rewriting\n"); return; }
    auto bks = books();
    bool found = false;
    for (auto& b : bks) if (b.id == r.id) {
        b.key = r.key.empty() ? b.key : r.key;
        if (!r.name.empty()) b.name = r.name;
        found = true;
    }
    if (!found) bks.push_back(r);
    json arr = json::array();
    for (auto& b : bks) arr.push_back({{"id", b.id}, {"key", b.key}, {"name", b.name}});
    writeJson(bookFile(), arr);
}
void ContactStore::removeBook(const std::string& id) {
    if (existsButUnreadable(bookFile())) { fprintf(stderr, "[kith] books.json unreadable - not rewriting\n"); return; }
    auto bks = books();
    json arr = json::array();
    for (auto& b : bks) if (b.id != id) arr.push_back({{"id", b.id}, {"key", b.key}, {"name", b.name}});
    if (!writeJson(bookFile(), arr)) return;
    if (!kith::isValidBookId(id)) return;   // never build a path from an unvalidated id
    std::error_code ec; fs::remove(logFile(id), ec);
}

// -- event log ---------------------------------------------------------------------
std::vector<kith::Event> ContactStore::log(const std::string& bookId) const {
    std::vector<kith::Event> out;
    if (!kith::isValidBookId(bookId)) return out;
    json arr = readJson(logFile(bookId));
    if (!arr.is_array()) return out;
    for (auto& j : arr) {
        try { kith::Event e = kith::eventFromJson(j); if (!e.id.empty()) out.push_back(e); }
        catch (...) { /* skip a bad entry */ }
    }
    return out;
}
bool ContactStore::writeLog(const std::string& bookId, const std::vector<kith::Event>& evs) const {
    if (!kith::isValidBookId(bookId)) return false;
    json arr = json::array();
    for (auto& e : evs) arr.push_back(kith::eventToJson(e));
    return writeJson(logFile(bookId), arr);
}
bool ContactStore::appendEvent(const std::string& bookId, const kith::Event& e) {
    if (e.id.empty() || !kith::isValidBookId(bookId)) return false;
    if (existsButUnreadable(logFile(bookId))) {
        fprintf(stderr, "[kith] log for %s unreadable - not rewriting\n", bookId.c_str());
        return false;
    }
    auto evs = log(bookId);
    for (auto& x : evs) if (x.id == e.id) return false;   // dedup by id - idempotent redelivery
    evs.push_back(e);
    return writeLog(bookId, kith::mergeEvents(evs));       // keep HLC-sorted + unique
}

// -- kv ------------------------------------------------------------------------------
std::string ContactStore::kvGet(const std::string& key) const {
    json o = readJson(kvFile());
    if (o.is_object() && o.contains(key) && o[key].is_string()) return o[key].get<std::string>();
    return {};
}
void ContactStore::kvSet(const std::string& key, const std::string& value) {
    if (existsButUnreadable(kvFile())) { fprintf(stderr, "[kith] kv.json unreadable - not rewriting\n"); return; }
    json o = readJson(kvFile());
    if (!o.is_object()) o = json::object();
    o[key] = value;
    writeJson(kvFile(), o);
}
