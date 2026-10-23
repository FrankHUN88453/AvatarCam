#include "app/Updater.h"
#include "core/I18n.h"
#include "core/Util.h"
#include "core/Version.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
const char* kLatestUrl = "https://api.github.com/repos/FrankHUN88453/AvatarCam/releases/latest";

struct HInternet {
    HINTERNET h = nullptr;
    HInternet(HINTERNET x = nullptr) : h(x) {}
    ~HInternet() {
        if (h) WinHttpCloseHandle(h);
    }
    operator HINTERNET() const { return h; }
};

// Egyszerű HTTPS GET. A kimenet vagy a memóriába (out), vagy fájlba (file) kerül; a progress 0..1.
bool httpGet(const std::string& url, std::string* out, FILE* file, const std::atomic<bool>* cancel, std::atomic<float>* progress, std::string& err) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc = {sizeof(uc)};
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = (DWORD)std::size(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = (DWORD)std::size(path);
    wchar_t extra[2048] = {};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = (DWORD)std::size(extra);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        err = "invalid URL";
        return false;
    }
    std::wstring ua = widen(std::string("AvatarCam/") + AVATARCAM_VERSION);
    HInternet ses(WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!ses) {
        err = "WinHttpOpen";
        return false;
    }
    WinHttpSetTimeouts(ses, 10000, 10000, 15000, 30000);
    HInternet con(WinHttpConnect(ses, host, uc.nPort, 0));
    if (!con) {
        err = format("connection failed (%lu)", GetLastError());
        return false;
    }
    std::wstring obj = std::wstring(path) + extra;
    HInternet req(WinHttpOpenRequest(con, L"GET", obj.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    if (!req) {
        err = "WinHttpOpenRequest";
        return false;
    }
    const wchar_t* hdr = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
    if (!WinHttpSendRequest(req, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
        err = format("no response (%lu)", GetLastError());
        return false;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        err = format("HTTP %lu", status);
        return false;
    }
    uint64_t total = 0;
    {
        wchar_t len[64] = {};
        DWORD ls = sizeof(len);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &ls, WINHTTP_NO_HEADER_INDEX)) total = _wcstoui64(len, nullptr, 10);
    }
    uint64_t got = 0;
    std::vector<char> buf(64 * 1024);
    for (;;) {
        if (cancel && *cancel) {
            err = "cancelled";
            return false;
        }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) {
            err = format("download interrupted (%lu)", GetLastError());
            return false;
        }
        if (avail == 0) break;
        DWORD rd = 0;
        if (!WinHttpReadData(req, buf.data(), std::min<DWORD>(avail, (DWORD)buf.size()), &rd)) {
            err = format("download interrupted (%lu)", GetLastError());
            return false;
        }
        if (out) out->append(buf.data(), rd);
        if (file && fwrite(buf.data(), 1, rd, file) != rd) {
            err = "write error";
            return false;
        }
        got += rd;
        if (progress && total) *progress = (float)((double)got / (double)total);
    }
    return true;
}

std::string sha256File(const std::wstring& path) {
    std::string hex;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return hex;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 && BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<unsigned char> buf(256 * 1024);
        size_t n;
        while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) BCryptHashData(h, buf.data(), (ULONG)n, 0);
        unsigned char dig[32];
        if (BCryptFinishHash(h, dig, sizeof(dig), 0) == 0)
            for (unsigned char c : dig) hex += format("%02x", c);
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    fclose(f);
    return hex;
}

std::vector<int> parseVersion(std::string v) {
    std::vector<int> r;
    size_t i = 0;
    while (i < v.size() && !isdigit((unsigned char)v[i])) i++;
    while (i < v.size()) {
        int n = 0;
        bool any = false;
        while (i < v.size() && isdigit((unsigned char)v[i])) n = n * 10 + (v[i++] - '0'), any = true;
        if (any) r.push_back(n);
        if (i < v.size() && v[i] == '.') i++;
        else break;
    }
    return r;
}
}  // namespace

void Updater::shutdown() {
    cancel_ = true;
    for (int i = 0; i < 30 && busy_; i++) Sleep(10);
    if (th_.joinable()) {
        if (busy_) th_.detach();
        else th_.join();
    }
}

void Updater::join() {
    if (th_.joinable()) th_.join();
}

std::string Updater::currentVersion() {
    // teszteléshez: AVATARCAM_FAKE_VERSION=0.1 esetén régebbi verziónak tetteti magát
    if (const char* f = getenv("AVATARCAM_FAKE_VERSION")) return f;
    return AVATARCAM_VERSION;
}

bool Updater::isNewer(const std::string& a, const std::string& b) {
    std::vector<int> x = parseVersion(a), y = parseVersion(b);
    size_t n = std::max(x.size(), y.size());
    x.resize(n, 0);
    y.resize(n, 0);
    return x > y;
}

UpdateInfo Updater::info() {
    std::lock_guard<std::mutex> lk(mx_);
    return info_;
}

std::string Updater::error() {
    std::lock_guard<std::mutex> lk(mx_);
    return error_;
}

void Updater::checkAsync(double delaySeconds) {
    State s = state_;
    if (s == Checking || s == Downloading) return;
    join();
    cancel_ = false;
    state_ = Checking;
    busy_ = true;
    th_ = std::thread([this, delaySeconds] {
        struct Done {
            std::atomic<bool>& b;
            ~Done() { b = false; }
        } done{busy_};
        for (double t = 0; t < delaySeconds && !cancel_; t += 0.1) Sleep(100);
        std::string body, err;
        if (!httpGet(kLatestUrl, &body, nullptr, &cancel_, nullptr, err)) {
            std::lock_guard<std::mutex> lk(mx_);
            error_ = err;
            state_ = Failed;
            logMsg("Update check failed: %s", err.c_str());
            return;
        }
        UpdateInfo ui;
        try {
            json j = json::parse(body);
            ui.version = j.value("tag_name", "");
            if (!ui.version.empty() && (ui.version[0] == 'v' || ui.version[0] == 'V')) ui.version.erase(0, 1);
            ui.notes = j.value("body", "");
            ui.pageUrl = j.value("html_url", "https://github.com/FrankHUN88453/AvatarCam/releases");
            if (j.contains("assets") && j["assets"].is_array())
                for (auto& a : j["assets"]) {
                    std::string n = a.value("name", "");
                    if (endsWith(toLower(n), "-setup.exe")) {
                        ui.assetName = n;
                        ui.assetUrl = a.value("browser_download_url", "");
                        ui.assetSize = a.value("size", (uint64_t)0);
                        std::string d = a.contains("digest") && a["digest"].is_string() ? a["digest"].get<std::string>() : "";
                        if (startsWith(d, "sha256:")) ui.sha256 = toLower(d.substr(7));
                    }
                }
        } catch (...) {
            std::lock_guard<std::mutex> lk(mx_);
            error_ = "invalid response";
            state_ = Failed;
            return;
        }
        bool newer = !ui.version.empty() && isNewer(ui.version, currentVersion());
        logMsg("Update check: latest %s, running %s%s", ui.version.c_str(), currentVersion().c_str(), newer ? " -> update available" : "");
        std::lock_guard<std::mutex> lk(mx_);
        info_ = ui;
        error_.clear();
        state_ = newer ? Available : UpToDate;
    });
}

void Updater::downloadAsync() {
    if (state_ != Available && state_ != Failed) return;
    join();
    UpdateInfo ui = info();
    if (ui.assetUrl.empty()) return;
    cancel_ = false;
    progress_ = 0;
    state_ = Downloading;
    busy_ = true;
    th_ = std::thread([this, ui] {
        struct Done {
            std::atomic<bool>& b;
            ~Done() { b = false; }
        } done{busy_};
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        fs::path dir = fs::path(tmp) / L"AvatarCam-update";
        std::error_code ec;
        fs::create_directories(dir, ec);
        // csak fájlnevet fogadunk el (a kiadás eszközének neve nem tartalmazhat útvonalat)
        std::string name = ui.assetName.find_first_of("/\\:") == std::string::npos ? ui.assetName : "AvatarCam-setup.exe";
        fs::path file = dir / u8path(name);
        std::string err;
        bool ok = false;
        if (FILE* f = _wfopen(file.c_str(), L"wb")) {
            ok = httpGet(ui.assetUrl, nullptr, f, &cancel_, &progress_, err);
            fclose(f);
        } else
            err = "cannot write to the temp folder";
        if (ok && !ui.sha256.empty()) {
            std::string h = sha256File(file.wstring());
            if (h != ui.sha256) {
                ok = false;
                err = T("the downloaded file is damaged (checksum mismatch)");
            }
        }
        std::lock_guard<std::mutex> lk(mx_);
        if (!ok) {
            fs::remove(file, ec);
            error_ = err;
            state_ = cancel_ ? Available : Failed;
            logMsg("Update download failed: %s", err.c_str());
            return;
        }
        downloaded_ = pathToUtf8(file);
        progress_ = 1;
        state_ = Downloaded;
        logMsg("Update downloaded: %s", downloaded_.c_str());
    });
}

bool Updater::launchInstaller(bool hungarian) {
    std::string file;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (state_ != Downloaded) return false;
        file = downloaded_;
    }
    // a jelenlegi exe mappájába telepít (a beállítások, avatárok ott maradnak), majd újraindítja a programot
    std::wstring params = L"/SP- /SILENT /NOCANCEL /update=1 /LANG=" + std::wstring(hungarian ? L"hungarian" : L"english") + L" \"/DIR=" + exeDir().wstring() + L"\"";
    HINSTANCE r = ShellExecuteW(nullptr, L"open", widen(file).c_str(), params.c_str(), nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        std::lock_guard<std::mutex> lk(mx_);
        error_ = format("could not start the installer (%d)", (int)(INT_PTR)r);
        state_ = Failed;
        return false;
    }
    logMsg("Installer started: %s", file.c_str());
    return true;
}

std::string releaseNotesForLanguage(const std::string& body, bool hungarian) {
    std::string b = body;
    b.erase(std::remove(b.begin(), b.end(), '\r'), b.end());
    // a kiadási megjegyzés felépítése: angol rész, "---", majd "**Magyarul:**" után a magyar rész
    size_t sep = b.find("\n---");
    size_t hu = b.find("Magyarul");
    if (hungarian && hu != std::string::npos) {
        size_t start = hu + 8;  // "Magyarul" után a ":" és "**" jelek átugrása
        while (start < b.size() && (b[start] == ':' || b[start] == '*' || b[start] == ' ')) start++;
        return trim(b.substr(start));
    }
    return trim(sep == std::string::npos ? b : b.substr(0, sep));
}
