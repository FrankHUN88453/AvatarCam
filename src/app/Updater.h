#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

// Frissítés keresése a GitHub kiadások között, a telepítő letöltése és indítása.
struct UpdateInfo {
    std::string version;     // pl. "0.3"
    std::string notes;       // a kiadás leírása (markdown)
    std::string pageUrl;     // a kiadás oldala
    std::string assetUrl;    // a telepítő letöltési címe (üres, ha nincs telepítő a kiadásban)
    std::string assetName;
    std::string sha256;      // a GitHub által közölt ellenőrzőösszeg (ha van)
    uint64_t assetSize = 0;
};

class Updater {
public:
    enum State { Idle, Checking, UpToDate, Available, Downloading, Downloaded, Failed };
    // kilépéskor: ha egy hálózati kérés még fut, nem várjuk meg (a szál leválik; az objektumot ezért nem töröljük)
    void shutdown();
    void cancelDownload() { cancel_ = true; }
    void checkAsync(double delaySeconds);  // a legújabb kiadás lekérdezése háttérszálon
    void downloadAsync();                  // a telepítő letöltése az ideiglenes mappába
    // a letöltött telepítő indítása az exe mappájába (a beállítások megmaradnak); true esetén a programnak ki kell lépnie
    bool launchInstaller(bool hungarian);
    State state() const { return state_; }
    float progress() const { return progress_; }
    UpdateInfo info();
    std::string error();
    static std::string currentVersion();                            // a futó program verziója
    static bool isNewer(const std::string& a, const std::string& b);  // a > b?

private:
    void join();
    std::thread th_;
    std::mutex mx_;
    std::atomic<State> state_{Idle};
    std::atomic<float> progress_{0};
    std::atomic<bool> cancel_{false}, busy_{false};
    UpdateInfo info_;
    std::string error_, downloaded_;
};

// A kiadás leírásából a felület nyelvének megfelelő rész (a magyar rész a "Magyarul" jelölés után következik)
std::string releaseNotesForLanguage(const std::string& body, bool hungarian);
