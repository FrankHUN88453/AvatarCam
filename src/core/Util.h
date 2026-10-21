#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <functional>
#include <mutex>
#include <cstdint>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

// --- szöveg ---
std::wstring widen(std::string_view s);
std::string narrow(std::wstring_view s);
inline fs::path u8path(std::string_view s) { return fs::path(widen(s)); }
inline std::string pathToUtf8(const fs::path& p) { return narrow(p.wstring()); }
std::string toLower(std::string s);
std::string trim(std::string_view s);
bool startsWith(std::string_view s, std::string_view p);
bool endsWith(std::string_view s, std::string_view p);
bool iequals(std::string_view a, std::string_view b);
std::vector<std::string> split(std::string_view s, char sep);
std::string slugify(std::string_view s, size_t maxLen = 48);
std::string format(const char* fmt, ...);

// --- fájlok ---
bool readFile(const fs::path& p, std::string& out);
bool writeFile(const fs::path& p, std::string_view data);
bool readJson(const fs::path& p, json& out);
bool writeJson(const fs::path& p, const json& j, int indent = -1);

// --- alkalmazás könyvtárak ---
fs::path exeDir();
fs::path dataDir();     // beállítások, avatárok (az exe mellett)
fs::path avatarsDir();

// --- naplózás ---
void logMsg(const char* fmt, ...);
std::vector<std::string> logLines();

// --- idő ---
double nowSeconds();

// --- JSON segédek ---
// Hivatkozás egy al-objektumra/tömbre (üres, ha nincs) – biztonságos range-for ciklusokban
inline const json& jobj(const json& j, const char* key) {
    static const json empty = json::object();
    if (!j.is_object()) return empty;
    auto it = j.find(key);
    return it == j.end() || !it->is_object() ? empty : *it;
}
inline const json& jarr(const json& j, const char* key) {
    static const json empty = json::array();
    if (!j.is_object()) return empty;
    auto it = j.find(key);
    return it == j.end() || !it->is_array() ? empty : *it;
}
template <class T>
T jget(const json& j, const char* key, T def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    try {
        return it->get<T>();
    } catch (...) {
        return def;
    }
}
