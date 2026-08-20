#include <set>
#include "contact_store.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

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
// Atomic write: temp then rename.
static void writeJson(const std::string& path, const json& j) {
    const std::string tmp = path + ".tmp";
    { std::ofstream f(tmp); if (!f) return; f << j.dump(); }
    std::error_code ec; fs::rename(tmp, path, ec);
    if (ec) { fs::remove(tmp, ec); }
}

ContactStore::ContactStore() {
    m_dataDir = computeDataDir();
    ensureDir(m_dataDir);
    ensureDir(m_dataDir + "/logs");
}

std::string ContactStore::bookFile() const { return m_dataDir + "/books.json"; }
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
        std::string id = j.value("id", std::string());
        if (id.empty() || !seen.insert(id).second) continue;
        out.push_back({ id, j.value("key", std::string()), j.value("name", std::string()) });
    }
    return out;
}
kith::BookReg ContactStore::book(const std::string& id) const {
    for (auto& r : books()) if (r.id == id) return r;
    return {};
}
void ContactStore::upsertBook(const kith::BookReg& r) {
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
    auto bks = books();
    json arr = json::array();
    for (auto& b : bks) if (b.id != id) arr.push_back({{"id", b.id}, {"key", b.key}, {"name", b.name}});
    writeJson(bookFile(), arr);
    std::error_code ec; fs::remove(logFile(id), ec);
}

// -- event log ---------------------------------------------------------------------
std::vector<kith::Event> ContactStore::log(const std::string& bookId) const {
    std::vector<kith::Event> out;
    json arr = readJson(logFile(bookId));
    if (!arr.is_array()) return out;
    for (auto& j : arr) {
        try { kith::Event e = kith::eventFromJson(j); if (!e.id.empty()) out.push_back(e); }
        catch (...) { /* skip a bad entry */ }
    }
    return out;
}
void ContactStore::writeLog(const std::string& bookId, const std::vector<kith::Event>& evs) const {
    json arr = json::array();
    for (auto& e : evs) arr.push_back(kith::eventToJson(e));
    writeJson(logFile(bookId), arr);
}
bool ContactStore::appendEvent(const std::string& bookId, const kith::Event& e) {
    if (e.id.empty()) return false;
    auto evs = log(bookId);
    for (auto& x : evs) if (x.id == e.id) return false;   // dedup by id - idempotent redelivery
    evs.push_back(e);
    writeLog(bookId, kith::mergeEvents(evs));              // keep HLC-sorted + unique
    return true;
}

// -- kv ------------------------------------------------------------------------------
std::string ContactStore::kvGet(const std::string& key) const {
    json o = readJson(kvFile());
    if (o.is_object() && o.contains(key) && o[key].is_string()) return o[key].get<std::string>();
    return {};
}
void ContactStore::kvSet(const std::string& key, const std::string& value) {
    json o = readJson(kvFile());
    if (!o.is_object()) o = json::object();
    o[key] = value;
    writeJson(kvFile(), o);
}
