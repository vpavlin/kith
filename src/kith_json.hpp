// kith_json.hpp - typed reads for JSON that other people wrote (contacts, payloads).
// nlohmann's value() throws on a wrong type, and an exception in a Basecamp core kills the module.
#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace kith {
// "" (or the default) unless `o` is an object whose `k` is a string.
inline std::string sv(const nlohmann::json& o, const char* k, const std::string& d = std::string()) {
    if (!o.is_object()) return d;
    auto it = o.find(k);
    return (it != o.end() && it->is_string()) ? it->get<std::string>() : d;
}
} // namespace kith
