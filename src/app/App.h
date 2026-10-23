#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include "gfx/Gfx.h"
#include "render/Renderer.h"
#include "render/GpuTimer.h"
#include "app/Updater.h"
#include "app/Settings.h"
#include "import/Importer.h"
#include "avatar/Avatar.h"

class Tracking;
class SpoutSender;

class App {
public:
    App();
    ~App();
    int run(HINSTANCE hInst, const std::vector<std::wstring>& args);

    // --- belső állapot (a felület is használja) ---
    Gfx gfx;
    Renderer renderer;
    Settings settings;
    std::unique_ptr<Avatar> avatar;
    std::vector<AvatarListEntry> avatarList;
    std::unique_ptr<Tracking> tracking;
    std::unique_ptr<SpoutSender> spout;

    // avatár betöltés (háttérszál)
    std::thread loadThread;
    std::atomic<bool> loading{false};
    std::atomic<float> loadProgress{0};
    std::unique_ptr<Avatar> loadedAvatar;
    std::string loadError;
    std::mutex loadMx;
    std::string loadingId;
    void loadAvatar(const std::string& id);
    void pollAvatarLoad();

    // import (háttérszál)
    std::thread importThread;
    std::atomic<bool> importing{false};
    std::atomic<float> importProgress{0};
    std::vector<std::string> importLog;
    std::string importError;
    std::vector<std::string> importResult;
    std::atomic<bool> importDone{false};
    std::mutex importMx;
    void startImport(const std::wstring& file);
    void pollImport();

    // kamera
    CameraState camera;
    Vec3 camTargetSmooth{0, 1.35f, 0};
    bool camInit = false;
    void applyCameraPreset(int preset);
    void updateCamera(float dt);

    // pózolás
    PoseInput pose;
    int gestureLeft = 0, gestureRight = 0;
    float time = 0;

    // ablakok
    HWND hwnd = nullptr;
    HWND outHwnd = nullptr;
    ComPtr<IDXGISwapChain1> swap, outSwap;
    ComPtr<ID3D11RenderTargetView> mainRtv, outRtv;
    int outW = 0, outH = 0;
    bool outResize = false;
    UINT resizeW = 0, resizeH = 0;
    bool minimized = false;
    void createMainTargets();
    void createOutputWindow();
    void destroyOutputWindow();
    void presentOutput();

    // gyorsbillentyűk
    bool capturingHotkey = false;
    int captureIndex = -1;
    void registerHotkeys();
    void onHotkey(int id);
    void onKeyCapture(int vk);

    // presetek
    std::vector<std::string> presetList;
    void refreshPresets();
    void savePreset(const std::string& name);
    void loadPreset(const std::string& name);

    // mentés
    std::string lastSavedJson;
    double lastSaveCheck = 0;
    void autosave(bool force = false);

    AvatarOverrides& overrides();
    float uiScale = 1;
    bool showLog = false;
    bool showDemo = false;
    std::string statusText;
    double fpsTime = 0;
    int fpsFrames = 0;
    float fps = 0;

    // teljesítmény mérés
    GpuTimer gpuTimer;
    enum { kCpuTrack, kCpuAvatar, kCpuUpload, kCpuRender, kCpuOutput, kCpuUi, kCpuPresent, kCpuCount };
    enum { kGpuShadow = 1, kGpuScene, kGpuPost, kGpuOutput, kGpuUi };
    struct PerfStats {
        float cpu[kCpuCount] = {};  // fő szál szakaszai (ms, simított)
        float frameMs = 0;          // a fő szál munkája egy képkockán
        float processCores = 0;     // a teljes folyamat CPU használata (hány mag)
        int logicalCores = 1;
        double lastCpuT = 0;
        unsigned long long lastCyc = 0, lastTsc = 0;
        double logT = 0;
        std::string logPath;
    } perf;
    void perfUpdate(const float* cpuMs);
    double lastUiTime = 0;
    std::wstring shotPath_;  // teszt: --screenshot-ui alatt a felület mindig frissül

    // felület (App_ui.cpp)
    void drawUi();
    void uiAvatarTab();
    void uiPartsTab();
    void uiSceneTab();
    void uiMotionTab();
    void uiTrackingTab();
    void uiOutputTab();
    void uiBackground();  // háttér beállítások (a Jelenet és a Kimenet fülön is)
    void uiPreview();
    void uiImportPopup();
    void uiUpdatePopup();
    // frissítés: a hálózati szál a program végéig élhet, ezért az objektumot szándékosan nem töröljük
    Updater* updater = new Updater;
    bool updatePopupWanted = false, updateManualCheck = false, updateInstalling = false;
    void uiMenu(const json& menu, int depth);
    std::unordered_map<std::string, GpuTextureRef> iconCache;
    ID3D11ShaderResourceView* icon(const std::string& rel);
    std::string partFilter, blendFilter;
    std::string forceTab;  // teszteléshez: megnyitandó fül

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK outWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMsg(HWND, UINT, WPARAM, LPARAM);

private:
    void frame(float dt);
    void setupStyle(float scale);
};

extern App* gApp;
