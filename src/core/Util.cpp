#include "core/Util.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <chrono>
#include <deque>

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}

std::string toLower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return std::string(s.substr(a, b - a));
}

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(s.size() - p.size()) == p; }
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == sep) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::string slugify(std::string_view s, size_t maxLen) {
    std::string out;
    bool dash = false;
    for (unsigned char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (c >= 'A' && c <= 'Z') {
            c = char(c - 'A' + 'a');
            ok = true;
        }
        if (ok) {
            if (dash && !out.empty()) out += '-';
            out += char(c);
            dash = false;
        } else
            dash = true;
        if (out.size() >= maxLen) break;
    }
    return out.empty() ? "avatar" : out;
}

std::string format(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    f.seekg(0);
    out.resize(size_t(sz));
    if (sz > 0) f.read(out.data(), sz);
    return true;
}

bool writeFile(const fs::path& p, std::string_view data) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), data.size());
        if (!f) return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
    return !ec;
}

bool readJson(const fs::path& p, json& out) {
    std::string s;
    if (!readFile(p, s)) return false;
    try {
        out = json::parse(s);
        return true;
    } catch (...) {
        return false;
    }
}

bool writeJson(const fs::path& p, const json& j, int indent) {
    return writeFile(p, j.dump(indent, ' ', false, json::error_handler_t::replace));
}

fs::path exeDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return fs::path(std::wstring(buf, n)).parent_path();
}

fs::path dataDir() {
    static fs::path dir = [] {
        fs::path d = exeDir();
        // fejlesztés közben (bin mappa) a projekt gyökerét használjuk
        if (d.filename() == L"bin" && fs::exists(d.parent_path() / L"CMakeLists.txt")) d = d.parent_path();
        return d;
    }();
    return dir;
}

fs::path avatarsDir() { return dataDir() / L"avatars"; }

static std::mutex gLogMutex;
static std::deque<std::string> gLog;

void logMsg(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    {
        std::lock_guard<std::mutex> lk(gLogMutex);
        gLog.emplace_back(buf);
        while (gLog.size() > 500) gLog.pop_front();
    }
    OutputDebugStringW(widen(std::string(buf) + "\n").c_str());
    static FILE* f = [] {
        FILE* ff = nullptr;
        _wfopen_s(&ff, (dataDir() / L"avatarcam.log").c_str(), L"w");
        return ff;
    }();
    if (f) {
        fprintf(f, "%s\n", buf);
        fflush(f);
    }
}

std::vector<std::string> logLines() {
    std::lock_guard<std::mutex> lk(gLogMutex);
    return {gLog.begin(), gLog.end()};
}

double nowSeconds() {
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}
