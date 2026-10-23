#include "app/App.h"
#include "core/I18n.h"
#include "core/Version.h"
#include "tracking/Tracking.h"
#include "output/Spout.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <shellapi.h>
#include <dwmapi.h>
#include <timeapi.h>
#include "stb_image_write.h"
#include <intrin.h>
#include "core/I18n.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

App* gApp = nullptr;

App::App() { gApp = this; }
App::~App() {
    if (loadThread.joinable()) loadThread.join();
    if (importThread.joinable()) importThread.join();
}

AvatarOverrides& App::overrides() { return settings.avatars[avatar ? avatar->id : std::string("_")]; }

// ---------------------------------------------------------------------------
void App::loadAvatar(const std::string& id) {
    if (loading) return;
    if (loadThread.joinable()) loadThread.join();
    loading = true;
    loadProgress = 0;
    loadingId = id;
    logMsg("Loading avatar: %s", id.c_str());
    loadThread = std::thread([this, id] {
        auto av = std::make_unique<Avatar>();
        std::string err;
        bool ok = av->load(avatarsDir() / u8path(id), gfx, err, &loadProgress);
        std::lock_guard<std::mutex> lk(loadMx);
        if (ok) {
            av->id = id;
            loadedAvatar = std::move(av);
        } else
            loadError = err.empty() ? "Ismeretlen hiba" : err;
        loading = false;
    });
}

void App::pollAvatarLoad() {
    std::lock_guard<std::mutex> lk(loadMx);
    if (loadedAvatar) {
        avatar = std::move(loadedAvatar);
        settings.avatarId = avatar->id;
        statusText = T("Loaded: ") + avatar->name;
        // új avatárnál a kamera nézet újraszámolása; ugyanannál megtartjuk a mentett nézetet
        if (settings.camera.forAvatar != avatar->id) {
            applyCameraPreset(settings.camera.preset >= 0 ? settings.camera.preset : 1);
            settings.camera.forAvatar = avatar->id;
        }
        camInit = true;
        camTargetSmooth = settings.camera.target;
        iconCache.clear();
        autosave(true);
    }
    if (!loadError.empty()) {
        statusText = T("Load error: ") + loadError;
        logMsg("%s", statusText.c_str());
        loadError.clear();
    }
}

void App::startImport(const std::wstring& file) {
    if (importing) return;
    if (importThread.joinable()) importThread.join();
    importing = true;
    importDone = false;
    importProgress = 0;
    {
        std::lock_guard<std::mutex> lk(importMx);
        importLog.clear();
        importError.clear();
        importResult.clear();
    }
    importThread = std::thread([this, file] {
        std::string err;
        auto ids = importAvatarFile(
            file,
            [this](const std::string& m, float p) {
                std::lock_guard<std::mutex> lk(importMx);
                importLog.push_back(m);
                if (importLog.size() > 400) importLog.erase(importLog.begin());
                importProgress = p;
                logMsg("%s", m.c_str());
            },
            &err);
        std::lock_guard<std::mutex> lk(importMx);
        importResult = ids;
        importError = err;
        importing = false;
        importDone = true;
    });
}

void App::pollImport() {
    if (!importDone) return;
    importDone = false;
    avatarList = listAvatars();
    std::lock_guard<std::mutex> lk(importMx);
    if (!importResult.empty()) loadAvatar(importResult[0]);
}

// ---------------------------------------------------------------------------
// Kamera
void App::applyCameraPreset(int preset) {
    CameraSettings& c = settings.camera;
    c.preset = preset;
    float scale = settings.pose.scale;
    Vec3 head(0, 1.55f, 0);
    float h = 1.7f;
    if (avatar) {
        h = avatar->height * scale;
        if (avatar->humanoid[HB_Head] >= 0) {
            avatar->computeWorld(0);
            head = avatar->nodes[avatar->humanoid[HB_Head]].world.getTranslation();
        } else
            head = Vec3(0, h * 0.9f, 0);
    }
    float tanh = std::tan(c.fov * kDeg2Rad * 0.5f);
    auto fit = [&](float visible) { return visible / (2 * tanh); };
    switch (preset) {
        case 0: c.target = head + Vec3(0, 0.03f, 0), c.distance = fit(0.42f * scale); break;
        case 1: c.target = head - Vec3(0, 0.16f * scale, 0), c.distance = fit(0.78f * scale); break;
        case 2: c.target = head - Vec3(0, 0.36f * scale, 0), c.distance = fit(1.15f * scale); break;
        case 3: c.target = Vec3(0, h * 0.52f, 0), c.distance = fit(h * 1.12f); break;
        default: break;
    }
    c.target.x = head.x;
    c.yaw = 0;
    c.pitch = preset == 3 ? 2 : 4;
    c.roll = 0;
    camTargetSmooth = c.target;
}

void App::updateCamera(float dt) {
    CameraSettings& c = settings.camera;
    Vec3 target = c.target;
    if (c.followHead && avatar && avatar->humanoid[HB_Head] >= 0) {
        // csak a fej elmozdulását követjük a pihenő helyzethez képest
        Vec3 head = avatar->nodes[avatar->humanoid[HB_Head]].world.getTranslation();
        static Vec3 restHead = head;
        static const Avatar* owner = nullptr;
        if (owner != avatar.get()) {
            owner = avatar.get();
            restHead = head;
        }
        target = target + (head - restHead) * c.followAmount;
    }
    camTargetSmooth = lerp(camTargetSmooth, target, 1.0f - std::exp(-c.smoothing * 3 * dt));
    if (!c.followHead) camTargetSmooth = target;
    float yaw = c.yaw * kDeg2Rad, pitch = c.pitch * kDeg2Rad;
    Vec3 dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    camera.target = camTargetSmooth;
    camera.eye = camTargetSmooth + dir * c.distance;
    Vec3 fwd = normalize(camera.target - camera.eye);
    camera.up = Quat::axisAngle(fwd, c.roll * kDeg2Rad).rotate(Vec3(0, 1, 0));
    camera.fovY = c.fov * kDeg2Rad;
    camera.aspect = (float)settings.output.width / std::max(1, settings.output.height);
}

// ---------------------------------------------------------------------------
// Ablakok
LRESULT CALLBACK App::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    return gApp ? gApp->handleMsg(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT App::handleMsg(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_SIZE:
            minimized = w == SIZE_MINIMIZED;
            if (w != SIZE_MINIMIZED) {
                resizeW = LOWORD(l);
                resizeH = HIWORD(l);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((w & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DROPFILES: {
            HDROP drop = (HDROP)w;
            wchar_t path[MAX_PATH * 2];
            if (DragQueryFileW(drop, 0, path, (UINT)std::size(path))) {
                std::wstring p = path;
                std::string ext = toLower(narrow(fs::path(p).extension().wstring()));
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
                    settings.background.image = narrow(p);
                    settings.background.mode = 3;
                } else
                    startImport(p);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_HOTKEY:
            onHotkey((int)w);
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (capturingHotkey && w != VK_CONTROL && w != VK_SHIFT && w != VK_MENU && w != VK_LWIN && w != VK_RWIN) {
                onKeyCapture((int)w);
                return 0;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK App::outWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_SETCURSOR: {
            // a kép felett nincs egérmutató, így az OBS felvételén sem látszik
            WORD ht = LOWORD(l);
            bool borderless = gApp && gApp->settings.output.outputBorderless;
            if (ht == HTCLIENT || (borderless && ht == HTCAPTION)) {
                SetCursor(nullptr);
                return TRUE;
            }
            break;
        }
        case WM_SYSCOMMAND:
            // kis méretre téve nem lenne mit rögzíteni, ezért nem engedjük
            if ((w & 0xfff0) == SC_MINIMIZE) return 0;
            break;
        case WM_NCHITTEST: {
            // keret nélküli ablak húzható bárhol
            LRESULT r = DefWindowProcW(h, m, w, l);
            if (r == HTCLIENT && gApp && gApp->settings.output.outputBorderless) return HTCAPTION;
            return r;
        }
        case WM_SIZE:
            // ha mégis kis méretre került (pl. Win+D), aktiválás nélkül visszaállítjuk
            if (w == SIZE_MINIMIZED) {
                ShowWindowAsync(h, SW_SHOWNOACTIVATE);
                return 0;
            }
            if (gApp) gApp->outResize = true;
            return 0;
        case WM_CLOSE:
            if (gApp) gApp->settings.output.outputWindow = false;
            return 0;
        case WM_NCRBUTTONUP:
        case WM_RBUTTONUP:
            if (gApp) gApp->settings.output.outputWindow = false;
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void App::createMainTargets() {
    mainRtv.Reset();
    ComPtr<ID3D11Texture2D> bb;
    swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    gfx.dev()->CreateRenderTargetView(bb.Get(), nullptr, &mainRtv);
}

void App::createOutputWindow() {
    if (outHwnd) return;
    const OutputSettings& o = settings.output;
    DWORD style = o.outputBorderless ? WS_POPUP : (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX);
    RECT r = {0, 0, o.width, o.height};
    // nagy felbontásnál a képernyőre kicsinyítve, de a render felbontás változatlan
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    float k = std::min(1.0f, std::min(sw * 0.9f / o.width, sh * 0.9f / o.height));
    r.right = (LONG)(o.width * k);
    r.bottom = (LONG)(o.height * k);
    AdjustWindowRect(&r, style, FALSE);
    outHwnd = CreateWindowExW(0, L"AvatarCamOutput", L"AvatarCam Output", style | WS_VISIBLE, 60, 60, r.right - r.left, r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    RECT cr;
    GetClientRect(outHwnd, &cr);
    sd.Width = std::max<LONG>(1, cr.right);
    sd.Height = std::max<LONG>(1, cr.bottom);
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (FAILED(gfx.factory()->CreateSwapChainForHwnd(gfx.dev(), outHwnd, &sd, nullptr, nullptr, &outSwap))) {
        DestroyWindow(outHwnd);
        outHwnd = nullptr;
        return;
    }
    gfx.factory()->MakeWindowAssociation(outHwnd, DXGI_MWA_NO_ALT_ENTER);
    outResize = true;
}

void App::destroyOutputWindow() {
    outRtv.Reset();
    outSwap.Reset();
    if (outHwnd) DestroyWindow(outHwnd);
    outHwnd = nullptr;
}

void App::presentOutput() {
    if (!outHwnd || !outSwap) return;
    if (outResize) {
        outRtv.Reset();
        RECT cr;
        GetClientRect(outHwnd, &cr);
        outW = std::max<LONG>(1, cr.right);
        outH = std::max<LONG>(1, cr.bottom);
        outSwap->ResizeBuffers(0, outW, outH, DXGI_FORMAT_UNKNOWN, 0);
        ComPtr<ID3D11Texture2D> bb;
        outSwap->GetBuffer(0, IID_PPV_ARGS(&bb));
        gfx.dev()->CreateRenderTargetView(bb.Get(), nullptr, &outRtv);
        outResize = false;
    }
    renderer.blit(outRtv.Get(), outW, outH, true);
    outSwap->Present(0, 0);
}

// ---------------------------------------------------------------------------
// Gyorsbillentyűk (globálisak: játék közben is működnek)
void App::registerHotkeys() {
    for (int i = 0; i < 200; i++) UnregisterHotKey(hwnd, 100 + i);
    for (size_t i = 0; i < settings.hotkeys.size(); i++) {
        const Hotkey& k = settings.hotkeys[i];
        if (!k.vk) continue;
        UINT mods = MOD_NOREPEAT | (k.ctrl ? MOD_CONTROL : 0) | (k.alt ? MOD_ALT : 0) | (k.shift ? MOD_SHIFT : 0);
        if (!RegisterHotKey(hwnd, 100 + (int)i, mods, k.vk)) logMsg("Hotkey already in use: %d", k.vk);
    }
}

void App::onKeyCapture(int vk) {
    capturingHotkey = false;
    if (captureIndex < 0 || captureIndex >= (int)settings.hotkeys.size()) return;
    if (vk == VK_ESCAPE) return;
    Hotkey& k = settings.hotkeys[captureIndex];
    k.vk = vk;
    k.ctrl = GetKeyState(VK_CONTROL) < 0;
    k.alt = GetKeyState(VK_MENU) < 0;
    k.shift = GetKeyState(VK_SHIFT) < 0;
    registerHotkeys();
}

void App::onHotkey(int id) {
    int i = id - 100;
    if (i < 0 || i >= (int)settings.hotkeys.size()) return;
    const Hotkey& k = settings.hotkeys[i];
    if (k.action == "param" && avatar) {
        auto& params = overrides().params;
        float cur = params.count(k.target) ? params[k.target] : avatar->paramValues[k.target];
        params[k.target] = (k.toggle && std::fabs(cur - k.value) < 0.001f) ? 0.0f : k.value;
    } else if (k.action == "gestureL") {
        gestureLeft = (k.toggle && gestureLeft == (int)k.value) ? 0 : (int)k.value;
    } else if (k.action == "gestureR") {
        gestureRight = (k.toggle && gestureRight == (int)k.value) ? 0 : (int)k.value;
    } else if (k.action == "camera") {
        applyCameraPreset((int)k.value);
    } else if (k.action == "preset") {
        loadPreset(k.target);
    } else if (k.action == "recenter") {
        if (tracking) tracking->recenter();
    } else if (k.action == "expression" && avatar) {
        auto& ex = overrides().expressions;
        float cur = ex.count(k.target) ? ex[k.target] : 0.0f;
        ex[k.target] = (k.toggle && cur > 0.5f) ? 0.0f : 1.0f;
    }
}

// ---------------------------------------------------------------------------
void App::refreshPresets() {
    presetList.clear();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dataDir() / L"presets", ec))
        if (e.path().extension() == L".json") presetList.push_back(narrow(e.path().stem().wstring()));
    std::sort(presetList.begin(), presetList.end());
}

void App::savePreset(const std::string& name) {
    if (name.empty()) return;
    writeJson(dataDir() / L"presets" / u8path(name + ".json"), settings.sceneToJson(), 2);
    refreshPresets();
}

void App::loadPreset(const std::string& name) {
    json j;
    if (!readJson(dataDir() / L"presets" / u8path(name + ".json"), j)) return;
    settings.sceneFromJson(j);
    camTargetSmooth = settings.camera.target;
    statusText = T("Preset loaded: ") + name;
}

void App::autosave(bool force) {
    double now = nowSeconds();
    if (!force && now - lastSaveCheck < 2.0) return;
    lastSaveCheck = now;
    json j;
    to_json(j, settings);
    std::string s = j.dump();
    if (s == lastSavedJson) return;
    if (saveSettings(settings)) lastSavedJson = s;
}

// ---------------------------------------------------------------------------
void App::setupStyle(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 6;
    st.ChildRounding = 6;
    st.FrameRounding = 5;
    st.PopupRounding = 6;
    st.GrabRounding = 5;
    st.TabRounding = 5;
    st.ScrollbarRounding = 8;
    st.FramePadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(8, 6);
    st.WindowPadding = ImVec2(10, 10);
    st.WindowBorderSize = 0;
    st.FrameBorderSize = 0;
    st.SeparatorTextBorderSize = 2;
    ImVec4* c = st.Colors;
    ImVec4 bg(0.075f, 0.08f, 0.10f, 1), panel(0.11f, 0.12f, 0.15f, 1), frame(0.16f, 0.17f, 0.21f, 1);
    ImVec4 accent(0.22f, 0.78f, 0.95f, 1), accentDim(0.16f, 0.50f, 0.66f, 1), accent2(0.62f, 0.38f, 0.98f, 1);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 0.98f);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.27f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.26f, 0.32f, 1);
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = panel;
    c[ImGuiCol_Header] = ImVec4(0.17f, 0.20f, 0.26f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.30f, 0.38f, 1);
    c[ImGuiCol_HeaderActive] = accentDim;
    c[ImGuiCol_Button] = ImVec4(0.17f, 0.20f, 0.26f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.36f, 0.46f, 1);
    c[ImGuiCol_ButtonActive] = accentDim;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accentDim;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Tab] = panel;
    c[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.36f, 0.46f, 1);
    c[ImGuiCol_TabSelected] = ImVec4(0.16f, 0.30f, 0.40f, 1);
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.24f, 0.30f, 1);
    c[ImGuiCol_PlotHistogram] = accent2;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.22f, 0.5f, 0.7f, 0.5f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.95f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.54f, 0.60f, 1);
    st.ScaleAllSizes(scale);
    st.FontScaleDpi = scale;
}

int App::run(HINSTANCE hInst, const std::vector<std::wstring>& args) {
    timeBeginPeriod(1);
    // a telepítő (AppMutex) ebből látja, hogy a program fut
    CreateMutexW(nullptr, FALSE, L"AvatarCamRunning");
    ImGui_ImplWin32_EnableDpiAwareness();
    float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    // teszteléshez külön beállításfájl: --settings <fájl>
    extern fs::path gSettingsPath;
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == L"--settings") gSettingsPath = args[i + 1];
    loadSettings(settings);
    setLanguage(settings.language);
    {
        json j;
        to_json(j, settings);
        lastSavedJson = j.dump();
    }
    std::error_code ec;
    fs::create_directories(avatarsDir(), ec);
    fs::create_directories(dataDir() / L"presets", ec);

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, wndProc, 0, 0, hInst, LoadIconW(hInst, MAKEINTRESOURCEW(2)), LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"AvatarCamMain", nullptr};
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    WNDCLASSEXW wo = {sizeof(wo), CS_HREDRAW | CS_VREDRAW, outWndProc, 0, 0, hInst, wc.hIcon, LoadCursorW(nullptr, IDC_ARROW), (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, L"AvatarCamOutput", nullptr};
    RegisterClassExW(&wo);
    hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, widen(std::string(T("AvatarCam – VRChat avatar virtual camera")) + " " AVATARCAM_VERSION).c_str(), WS_OVERLAPPEDWINDOW, 80, 60, (int)(1500 * dpi), (int)(900 * dpi), nullptr, nullptr, hInst, nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

    std::string err;
    if (!gfx.init(err)) {
        MessageBoxW(hwnd, widen(err).c_str(), L"AvatarCam", MB_ICONERROR);
        return 1;
    }
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(gfx.factory()->CreateSwapChainForHwnd(gfx.dev(), hwnd, &sd, nullptr, nullptr, &swap))) {
        MessageBoxW(hwnd, widen(T("Could not create the swap chain.")).c_str(), L"AvatarCam", MB_ICONERROR);
        return 1;
    }
    gfx.factory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    createMainTargets();
    gpuTimer.init(gfx.dev());
    if (!renderer.init(gfx, err)) {
        logMsg("Shader error: %s", err.c_str());
        MessageBoxW(hwnd, widen(T("Shader error:\n") + err).c_str(), L"AvatarCam", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    static std::string iniPath = narrow((dataDir() / L"imgui.ini").wstring());
    io.IniFilename = iniPath.c_str();
    uiScale = dpi * settings.uiScale;
    setupStyle(uiScale);
    io.ConfigDpiScaleFonts = false;
    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::string fontPath = narrow(std::wstring(winDir) + L"\\Fonts\\segoeui.ttf");
    ImFontConfig fc;
    fc.OversampleH = 2;
    if (fs::exists(u8path(fontPath)))
        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f, &fc);
    else
        io.Fonts->AddFontDefault();
    std::string boldPath = narrow(std::wstring(winDir) + L"\\Fonts\\segoeuib.ttf");
    if (fs::exists(u8path(boldPath))) io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 16.0f, &fc);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(gfx.dev(), gfx.ctx());

    tracking = std::make_unique<Tracking>();
    spout = std::make_unique<SpoutSender>();
    avatarList = listAvatars();
    refreshPresets();
    registerHotkeys();
    if (args.size() >= 1 && fs::exists(args[0])) startImport(args[0]);
    else if (!settings.avatarId.empty() && fs::exists(avatarsDir() / u8path(settings.avatarId) / L"avatar.json")) loadAvatar(settings.avatarId);
    else if (!avatarList.empty()) loadAvatar(avatarList[0].id);

    // új verzió keresése a háttérben, kicsit később, hogy az indulást ne lassítsa
    if (settings.checkUpdates) updater->checkAsync(3.0);

    // tesztelés: --screenshot <fájl> <másodperc>
    std::wstring shotPath;
    double shotAt = 0;
    bool shotUi = false;
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == L"--ui-tab") forceTab = narrow(args[i + 1]);
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == L"--screenshot" || args[i] == L"--screenshot-ui") {
            shotUi = args[i] == L"--screenshot-ui";
            shotPath = args[i + 1];
            shotAt = i + 2 < args.size() ? _wtof(args[i + 2].c_str()) : 6.0;
            if (shotUi) shotPath_ = shotPath;
        }

    // képkocka időzítő: nagy felbontású várakoztatható időzítő. A Sleep(1) Windows 11-en ~16 ms is lehet,
    // ha az ablak takarásban van (pl. játék közben) – ez 32 fps-re fogná vissza a kimenetet.
    HANDLE frameTimer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    if (!frameTimer) frameTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    double last = nowSeconds(), nextFrame = last;
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (resizeW && resizeH) {
            mainRtv.Reset();
            swap->ResizeBuffers(0, resizeW, resizeH, DXGI_FORMAT_UNKNOWN, 0);
            createMainTargets();
            resizeW = resizeH = 0;
        }
        double now = nowSeconds();
        // képkocka korlát (a kimenet számára)
        double period = 1.0 / std::max(10, settings.output.fpsLimit);
        if (now < nextFrame) {
            double wait = nextFrame - now;
            if (frameTimer && wait > 0.0003) {
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG)(wait * 1e7);
                SetWaitableTimer(frameTimer, &due, 0, nullptr, nullptr, FALSE);
                MsgWaitForMultipleObjectsEx(1, &frameTimer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
            continue;
        }
        nextFrame += period;
        if (nextFrame < now) nextFrame = now + period;  // lemaradás után nem pótolunk
        float dt = (float)std::min(0.1, now - last);
        last = now;
        frame(dt);
        if (!shotPath.empty() && nowSeconds() > shotAt && (!loading || nowSeconds() > shotAt * 4)) {
            // a kimeneti kép mentése PNG-be (automatikus teszteléshez)
            ComPtr<ID3D11Texture2D> src;
            if (shotUi) swap->GetBuffer(0, IID_PPV_ARGS(&src));
            else src = renderer.output().tex;
            D3D11_TEXTURE2D_DESC d;
            src->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> st;
            gfx.dev()->CreateTexture2D(&d, nullptr, &st);
            gfx.ctx()->CopyResource(st.Get(), src.Get());
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(gfx.ctx()->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) {
                if (d.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
                    for (UINT y = 0; y < d.Height; y++) {
                        uint8_t* row = (uint8_t*)m.pData + (size_t)y * m.RowPitch;
                        for (UINT x = 0; x < d.Width; x++) std::swap(row[x * 4], row[x * 4 + 2]);
                    }
                stbi_write_png(narrow(shotPath).c_str(), d.Width, d.Height, 4, m.pData, m.RowPitch);
                gfx.ctx()->Unmap(st.Get(), 0);
            }
            shotPath.clear();
            done = true;
        }
    }
    if (frameTimer) CloseHandle(frameTimer);
    updater->shutdown();
    autosave(true);
    destroyOutputWindow();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    timeEndPeriod(1);
    return 0;
}

void App::frame(float dt) {
    if (settings.language != language()) {
        setLanguage(settings.language);
        SetWindowTextW(hwnd, widen(std::string(T("AvatarCam – VRChat avatar virtual camera")) + " " AVATARCAM_VERSION).c_str());
    }
    float cpuMs[kCpuCount] = {};
    double lapT = nowSeconds();
    auto lap = [&](int i) {
        double n = nowSeconds();
        cpuMs[i] += (float)((n - lapT) * 1000);
        lapT = n;
    };
    time += dt;
    pollAvatarLoad();
    pollImport();
    fpsFrames++;
    if (nowSeconds() - fpsTime > 0.5) {
        fps = fpsFrames / (float)(nowSeconds() - fpsTime);
        fpsTime = nowSeconds();
        fpsFrames = 0;
    }

    // követés
    TrackingFrame tf;
    if (tracking) {
        tracking->update(settings.tracking, gfx, dt);
        tf = tracking->frame();
    }
    lap(kCpuTrack);
    pose = PoseInput{};
    pose.time = time;
    pose.bodyFollow = settings.tracking.bodyFollow;
    pose.gestureLeft = gestureLeft;
    pose.gestureRight = gestureRight;
    if (tf.face) {
        pose.head = scaleRotation(tf.head, settings.tracking.headScale);
        pose.headOffset = tf.headPos;
        if (settings.tracking.eyeTracking) {
            pose.eyeYaw = tf.eyeYaw;
            pose.eyePitch = tf.eyePitch;
            pose.eyesOverride = true;
        }
        if (settings.tracking.blinkTracking) {
            pose.blinkL = tf.blinkL;
            pose.blinkR = tf.blinkR;
        }
        if (settings.tracking.mouthTracking) pose.arkit = tf.arkit;
    }
    for (int k = 0; k < 15; k++) pose.visemes[k] = tf.visemes[k];
    for (int h = 0; h < 2; h++) {
        pose.hands[h].valid = tf.hands[h].valid;
        pose.hands[h].rel = tf.hands[h].rel;
        pose.hands[h].dir = tf.hands[h].dir;
        pose.hands[h].side = tf.hands[h].side;
        memcpy(pose.hands[h].finger, tf.hands[h].finger, sizeof(pose.hands[h].finger));
    }
    pose.mouthOpen = tf.mouthOpen;
    PoseSettings ps = settings.pose;
    if (tf.face && settings.tracking.blinkTracking) ps.autoBlink = false;
    if (tf.face) ps.headIdle *= 0.2f;

    if (avatar) {
        avatar->update(dt, pose, ps, &overrides());
        lap(kCpuAvatar);
        avatar->uploadGpu(gfx);
    }
    updateCamera(dt);
    lap(kCpuUpload);

    // háttérkép
    renderer.setBackgroundImage(settings.background.mode == 3 ? settings.background.image : std::string());
    renderer.timer = &gpuTimer;
    gpuTimer.begin(gfx.ctx());
    renderer.render(avatar.get(), settings, camera, time);
    lap(kCpuRender);

    // kimeneti ablak és Spout
    if (settings.output.outputWindow && !outHwnd) createOutputWindow();
    if (!settings.output.outputWindow && outHwnd) destroyOutputWindow();
    presentOutput();
    if (spout) {
        if (settings.output.spout) spout->send(gfx, renderer.output().tex.Get(), renderer.width(), renderer.height(), settings.output.spoutName);
        else if (spout->active()) spout->release();
    }
    gpuTimer.mark(gfx.ctx(), kGpuOutput);
    lap(kCpuOutput);

    // felület: ha az ablak nincs előtérben (pl. játék közben), ritkábban frissítjük – a kimenet ettől
    // független, az mindig teljes sebességgel megy
    bool drawUiNow = !minimized;
    bool separateOutput = settings.output.outputWindow || settings.output.spout;
    if (drawUiNow && separateOutput && settings.output.uiIdleFps > 0 && GetForegroundWindow() != hwnd && shotPath_.empty())
        drawUiNow = nowSeconds() - lastUiTime >= 1.0 / std::max(1, settings.output.uiIdleFps) - 0.002;
    if (drawUiNow) {
        lastUiTime = nowSeconds();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        drawUi();
        ImGui::Render();
        float clear[4] = {0.075f, 0.08f, 0.10f, 1};
        ID3D11RenderTargetView* rtv = mainRtv.Get();
        gfx.ctx()->OMSetRenderTargets(1, &rtv, nullptr);
        gfx.ctx()->ClearRenderTargetView(rtv, clear);
        D3D11_VIEWPORT vp = {0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 0, 1};
        gfx.ctx()->RSSetViewports(1, &vp);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        gpuTimer.mark(gfx.ctx(), kGpuUi);
        gpuTimer.end(gfx.ctx());
        lap(kCpuUi);
        swap->Present(0, 0);
        lap(kCpuPresent);
    } else {
        // nincs Present ezen a képkockán: a parancsokat azonnal elküldjük, hogy a kimenet (Spout, OBS) ne késsen
        gpuTimer.end(gfx.ctx());
        gfx.ctx()->Flush();
    }
    autosave();
    perfUpdate(cpuMs);
}

void App::perfUpdate(const float* cpuMs) {
    float sum = 0;
    for (int i = 0; i < kCpuCount; i++) {
        perf.cpu[i] = perf.cpu[i] * 0.95f + cpuMs[i] * 0.05f;
        sum += cpuMs[i];
    }
    perf.frameMs = perf.frameMs * 0.95f + sum * 0.05f;
    double now = nowSeconds();
    if (now - perf.lastCpuT >= 1.0) {
        // a GetProcessTimes óraütés alapú, és alulméri a rövid munkákat; a ciklusszámláló pontos
        ULONG64 cyc = 0;
        unsigned long long tsc = __rdtsc();
        if (QueryProcessCycleTime(GetCurrentProcess(), &cyc)) {
            if (perf.lastCpuT > 0 && tsc > perf.lastTsc) perf.processCores = (float)((double)(cyc - perf.lastCyc) / (double)(tsc - perf.lastTsc));
            perf.lastCyc = cyc;
            perf.lastTsc = tsc;
        }
        perf.lastCpuT = now;
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        perf.logicalCores = std::max(1, (int)si.dwNumberOfProcessors);
    }
    // teszteléshez: AVATARCAM_PERFLOG=<fájl> esetén 2 mp-enként egy sor
    if (perf.logPath.empty()) {
        const char* p = getenv("AVATARCAM_PERFLOG");
        perf.logPath = p ? p : "-";
    }
    if (perf.logPath != "-" && now - perf.logT > 2.0) {
        perf.logT = now;
        const float* g = gpuTimer.ms;
        std::string line = format("fps %.1f | cpu %.2f cores | main thread %.2f ms (tracking %.2f, avatar %.2f, upload %.2f, render %.2f, output %.2f, ui %.2f, present %.2f) | gpu %.2f ms (shadow %.2f, scene %.2f, post %.2f, output %.2f, ui %.2f) | face %.1f ms, hands %.1f ms\n",
                                  fps, perf.processCores, perf.frameMs, perf.cpu[0], perf.cpu[1], perf.cpu[2], perf.cpu[3], perf.cpu[4], perf.cpu[5], perf.cpu[6],
                                  gpuTimer.totalMs, g[1], g[2], g[3], g[4], g[5], tracking ? tracking->faceMs() : 0.f, tracking ? tracking->handMs() : 0.f);
        if (FILE* fp = _wfopen(widen(perf.logPath).c_str(), L"ab")) {
            fwrite(line.data(), 1, line.size(), fp);
            fclose(fp);
        }
    }
}
