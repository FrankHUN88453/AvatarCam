#include "app/App.h"
#include "tracking/Tracking.h"
#include "output/Spout.h"
#include "avatar/Physics.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <commdlg.h>
#include <shellapi.h>
#include "core/I18n.h"
#include "core/Version.h"

namespace {
const char* kGestures[] = {"Neutral", "Fist", "Open hand", "Pointing", "Victory (V)", "Rock", "Handgun", "Thumbs up"};
// a kézjelek nevei az aktuális nyelven
const char* const* gestureNames() {
    static const char* names[8];
    for (int i = 0; i < 8; i++) names[i] = T(kGestures[i]);
    return names;
}

ImTextureRef texRef(ID3D11ShaderResourceView* srv) { return ImTextureRef((ImTextureID)(intptr_t)srv); }

bool color3(const char* label, Vec3& c, ImGuiColorEditFlags f = 0) { return ImGui::ColorEdit3(label, &c.x, f | ImGuiColorEditFlags_Float); }

void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Dupla kattintás egy csúszkán: szöveges bevitel (mint a Ctrl+kattintás). A dupla kattintás első kattintása már
// a kattintás helyére állította az értéket, ezt visszavonjuk.
template <typename V>
bool typeInOnDoubleClick(V* v, V before) {
    static ImGuiID pressId = 0;
    static V pressValue{};
    ImGuiID id = ImGui::GetItemID();
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        bool restored = pressId == id;
        if (restored) *v = pressValue;
        pressId = 0;
        ImGui::ClearActiveID();
        ImGui::ActivateItemByID(id);
        GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
        return restored;
    }
    if (ImGui::IsItemActivated()) {
        pressId = id;
        pressValue = before;
    }
    return false;
}

void sliderHint(bool hasDefault) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay) && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", hasDefault ? T("Double-click or Ctrl+click to type a value. Right-click: default.") : T("Double-click or Ctrl+click to type a value."));
}

bool slider(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f", float def = NAN, ImGuiSliderFlags flags = 0) {
    float before = *v;
    bool ch = ImGui::SliderFloat(label, v, lo, hi, fmt, flags);
    ch |= typeInOnDoubleClick(v, before);
    if (!std::isnan(def) && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        *v = def;
        ch = true;
    }
    sliderHint(!std::isnan(def));
    return ch;
}

bool sliderInt(const char* label, int* v, int lo, int hi, const char* fmt = "%d", int def = INT_MIN) {
    int before = *v;
    bool ch = ImGui::SliderInt(label, v, lo, hi, fmt);
    ch |= typeInOnDoubleClick(v, before);
    if (def != INT_MIN && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        *v = def;
        ch = true;
    }
    sliderHint(def != INT_MIN);
    return ch;
}

std::wstring openFileDialog(HWND owner, const wchar_t* filter, const wchar_t* title) {
    wchar_t buf[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = (DWORD)std::size(buf);
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return buf;
    return L"";
}

const char* keyName(int vk) {
    static char buf[64];
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = (LONG)(sc << 16);
    if (vk >= VK_PRIOR && vk <= VK_HELP) lp |= 1 << 24;
    wchar_t w[64];
    if (GetKeyNameTextW(lp, w, 64) > 0) {
        std::string s = narrow(w);
        strncpy(buf, s.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        return buf;
    }
    snprintf(buf, sizeof(buf), "VK %d", vk);
    return buf;
}

void lightPresets(Settings& s, int which) {
    s.lights.clear();
    auto add = [&](const char* name, int type, Vec3 col, float inten, float yaw, float pitch, bool shadow, Vec3 pos = {0.8f, 1.8f, 1.2f}) {
        LightSettings l;
        l.name = name;
        l.type = type;
        l.color = col;
        l.intensity = inten;
        l.yaw = yaw;
        l.pitch = pitch;
        l.shadow = shadow;
        l.position = pos;
        s.lights.push_back(l);
    };
    switch (which) {
        case 0:  // stúdió
            s.resetLights();
            break;
        case 1:  // neon
            add("Purple", 0, {0.75f, 0.35f, 1.0f}, 1.2f, 55, 20, true);
            add("Cyan", 0, {0.2f, 0.85f, 1.0f}, 1.1f, -60, 15, false);
            add("Pink rim", 0, {1.0f, 0.3f, 0.7f}, 1.4f, 170, 30, false);
            s.effects.ambientSky = {0.35f, 0.3f, 0.55f};
            s.effects.ambientGround = {0.15f, 0.1f, 0.25f};
            break;
        case 2:  // naplemente
            add("Sun", 0, {1.0f, 0.62f, 0.35f}, 1.6f, 70, 12, true);
            add("Sky", 0, {0.45f, 0.55f, 0.9f}, 0.5f, -50, 40, false);
            add("Rim", 0, {1.0f, 0.5f, 0.3f}, 1.0f, -160, 20, false);
            s.effects.ambientSky = {0.6f, 0.55f, 0.7f};
            s.effects.ambientGround = {0.4f, 0.28f, 0.22f};
            break;
        case 3:  // drámai
            add("Key", 0, {1.0f, 0.95f, 0.9f}, 2.0f, 75, 45, true);
            add("Rim", 0, {0.6f, 0.75f, 1.0f}, 1.5f, -150, 25, false);
            s.effects.ambientSky = {0.25f, 0.27f, 0.32f};
            s.effects.ambientGround = {0.1f, 0.1f, 0.12f};
            break;
        case 4:  // lapos (VRChat-szerű egyenletes)
            add("Front", 0, {1, 1, 1}, 0.9f, 0, 20, false);
            s.effects.ambientSky = {0.9f, 0.9f, 0.95f};
            s.effects.ambientGround = {0.75f, 0.75f, 0.8f};
            s.effects.ambientIntensity = 0.75f;
            break;
    }
}
}  // namespace

ID3D11ShaderResourceView* App::icon(const std::string& rel) {
    if (!avatar || rel.empty()) return nullptr;
    auto it = iconCache.find(rel);
    if (it == iconCache.end()) it = iconCache.emplace(rel, gfx.loadTextureFile((avatarsDir() / u8path(avatar->id) / u8path(rel)).wstring(), true)).first;
    return it->second ? it->second->srv.Get() : nullptr;
}

// ===========================================================================
void App::drawUi() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * uiScale, 8 * uiScale));
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    static float leftW = 0;
    if (leftW <= 0) leftW = 460 * uiScale;
    ImGui::BeginChild("left", ImVec2(leftW, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    {
        // fejléc
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.85f, 1.0f, 1));
        ImGui::TextUnformatted("AvatarCam");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("%s %s", T("virtual camera"), AVATARCAM_VERSION);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90 * uiScale + ImGui::GetCursorPosX());
        ImGui::TextDisabled("%.0f FPS", fps);
        if (avatar) ImGui::Text(T("Avatar: %s"), avatar->name.c_str());
        else if (loading) ImGui::Text(T("Loading…"));
        else ImGui::TextDisabled(T("No avatar loaded"));
        if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
            auto tab = [&](const char* id, void (App::*fn)()) {
                ImGuiTabItemFlags tf = 0;
                const char* name = T(id);
                // teszteléshez (--ui-tab): az angol vagy a magyar név is jó
                if (!forceTab.empty() && (forceTab == id || forceTab == name || forceTab == T(id))) {
                    tf = ImGuiTabItemFlags_SetSelected;
                    if (ImGui::GetFrameCount() > 3) forceTab.clear();
                }
                if (ImGui::BeginTabItem((std::string(name) + "###" + id).c_str(), nullptr, tf)) {
                    ImGui::BeginChild("tabbody", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_None);
                    (this->*fn)();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            };
            tab("Avatar", &App::uiAvatarTab);
            tab("Body parts", &App::uiPartsTab);
            tab("Scene", &App::uiSceneTab);
            tab("Motion", &App::uiMotionTab);
            tab("Tracking", &App::uiTrackingTab);
            tab("Output", &App::uiOutputTab);
            ImGui::EndTabBar();
        }
        ImGui::TextDisabled("%s", statusText.c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    // elválasztó (húzható)
    ImGui::InvisibleButton("splitter", ImVec2(6 * uiScale, ImGui::GetContentRegionAvail().y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) leftW = std::clamp(leftW + ImGui::GetIO().MouseDelta.x, 320 * uiScale, vp->WorkSize.x - 300 * uiScale);
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("preview", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    uiPreview();
    ImGui::EndChild();
    ImGui::End();
    uiImportPopup();
    uiUpdatePopup();
    if (showLog) {
        ImGui::SetNextWindowSize(ImVec2(700 * uiScale, 400 * uiScale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(T("Log"), &showLog)) {
            for (auto& l : logLines()) ImGui::TextUnformatted(l.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::End();
    }
    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
}

// ===========================================================================
void App::uiPreview() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // eszköztár
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6 * uiScale);
    const char* presets[] = {T("Face"), T("Chest"), T("Waist"), T("Full body")};
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine();
        bool sel = settings.camera.preset == i;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.6f, 1));
        if (ImGui::Button(presets[i])) applyCameraPreset(i);
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|  %dx%d", renderer.width(), renderer.height());
    ImGui::SameLine();
    ImGui::Checkbox(T("Output window (OBS)"), &settings.output.outputWindow);
    float top = ImGui::GetCursorPosY();
    avail = ImGui::GetContentRegionAvail();
    float aspect = (float)renderer.width() / std::max(1, renderer.height());
    ImVec2 size = avail;
    if (size.x / size.y > aspect) size.x = size.y * aspect;
    else size.y = size.x / aspect;
    ImVec2 cur = ImGui::GetCursorScreenPos();
    ImVec2 p0(cur.x + (avail.x - size.x) * 0.5f, cur.y + (avail.y - size.y) * 0.5f);
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // ha a kimeneti ablak nyitva van, a kép csak ott látszik (itt nem duplikáljuk); az egérrel itt is lehet forgatni
    bool inOutputWindow = settings.output.outputWindow && outHwnd;
    if (inOutputWindow) {
        dl->AddRectFilled(p0, p1, IM_COL32(24, 26, 32, 255));
    } else if (settings.background.mode == 0) {
        // sakktábla az átlátszósághoz
        float cs = 16 * uiScale;
        dl->PushClipRect(p0, p1, true);
        for (float y = p0.y; y < p1.y; y += cs)
            for (float x = p0.x; x < p1.x; x += cs) {
                bool odd = (((int)((x - p0.x) / cs) + (int)((y - p0.y) / cs)) & 1) != 0;
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cs, y + cs), odd ? IM_COL32(52, 55, 62, 255) : IM_COL32(38, 40, 46, 255));
            }
        dl->PopClipRect();
    }
    if (renderer.output().srv && !inOutputWindow) dl->AddImage(texRef(renderer.output().srv.Get()), p0, p1);
    dl->AddRect(p0, p1, IM_COL32(60, 70, 90, 255));
    (void)top;

    // egér: forgatás / mozgatás / nagyítás
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("view", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    ImGuiIO& io = ImGui::GetIO();
    CameraSettings& c = settings.camera;
    if (ImGui::IsItemActive()) {
        ImVec2 d = io.MouseDelta;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            c.yaw -= d.x * 0.3f;
            c.pitch = std::clamp(c.pitch + d.y * 0.3f, -80.0f, 80.0f);
            c.preset = -1;
        } else {
            Vec3 fwd = normalize(camera.target - camera.eye);
            Vec3 right = normalize(cross(fwd, Vec3(0, 1, 0)));
            Vec3 up = cross(right, fwd);
            float k = c.distance * 2 * std::tan(c.fov * kDeg2Rad * 0.5f) / size.y;
            c.target = c.target - right * (d.x * k) + up * (d.y * k);
            c.preset = -1;
        }
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0) {
        c.distance = std::clamp(c.distance * std::pow(0.9f, io.MouseWheel), 0.1f, 20.0f);
        c.preset = -1;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) applyCameraPreset(1);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(T("Left mouse: orbit · Right/middle: pan · Wheel: zoom · Double-click: default view"));

    // fények jelölése
    Mat4 vpm = camera.proj() * camera.view();
    auto project = [&](const Vec3& w, ImVec2& out) {
        Vec4 cl = vpm * Vec4(w, 1);
        if (cl.w <= 0.01f) return false;
        out = ImVec2(p0.x + (cl.x / cl.w * 0.5f + 0.5f) * size.x, p0.y + (0.5f - cl.y / cl.w * 0.5f) * size.y);
        return true;
    };
    int li = 0;
    for (size_t i = 0; i < settings.lights.size() && !inOutputWindow; i++) {
        auto& L = settings.lights[i];
        if (!L.enabled) continue;
        Vec3 center = camera.target;
        Vec3 pos = L.type == 0 ? center + renderer.lightDirection(li) * 0.9f : L.position;
        li++;
        ImVec2 sp;
        if (!project(pos, sp)) continue;
        if (sp.x < p0.x || sp.y < p0.y || sp.x > p1.x || sp.y > p1.y) continue;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(L.color.x, L.color.y, L.color.z, 0.9f));
        dl->AddCircleFilled(sp, 7 * uiScale, col);
        dl->AddCircle(sp, 8 * uiScale, IM_COL32(0, 0, 0, 160), 0, 2);
        char t[8];
        snprintf(t, sizeof(t), "%d", (int)i + 1);
        dl->AddText(ImVec2(sp.x + 10 * uiScale, sp.y - 8 * uiScale), IM_COL32(230, 235, 245, 200), t);
    }
    if (loading) {
        ImVec2 mid((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        ImGui::SetCursorScreenPos(ImVec2(mid.x - 150 * uiScale, mid.y - 12 * uiScale));
        ImGui::ProgressBar(loadProgress, ImVec2(300 * uiScale, 0), T("Loading avatar…"));
    } else if (inOutputWindow) {
        ImVec2 mid((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        const char* l1 = T("The image is shown in the output window (OBS).");
        const char* l2 = T("You can still orbit, pan and zoom the camera here with the mouse.");
        ImVec2 s1 = ImGui::CalcTextSize(l1), s2 = ImGui::CalcTextSize(l2);
        float gap = 6 * uiScale;
        dl->AddText(ImVec2(mid.x - s1.x * 0.5f, mid.y - s1.y - gap * 0.5f), IM_COL32(200, 210, 225, 255), l1);
        dl->AddText(ImVec2(mid.x - s2.x * 0.5f, mid.y + gap * 0.5f), IM_COL32(140, 150, 170, 255), l2);
    } else if (!avatar) {
        ImVec2 mid((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        const char* t = T("Drop a .unitypackage / .vrm / .fbx / .glb file here to import it");
        ImVec2 ts = ImGui::CalcTextSize(t);
        dl->AddText(ImVec2(mid.x - ts.x * 0.5f, mid.y - ts.y * 0.5f), IM_COL32(200, 210, 225, 255), t);
    }
}

// ===========================================================================
void App::uiImportPopup() {
    if (importing && !ImGui::IsPopupOpen(T("Import"))) ImGui::OpenPopup(T("Import"));
    ImGui::SetNextWindowSize(ImVec2(620 * uiScale, 380 * uiScale), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(T("Import"), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::ProgressBar(importProgress, ImVec2(-1, 0));
        ImGui::BeginChild("log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), ImGuiChildFlags_Borders);
        {
            std::lock_guard<std::mutex> lk(importMx);
            for (auto& l : importLog) ImGui::TextUnformatted(l.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        if (!importing) {
            std::string err;
            {
                std::lock_guard<std::mutex> lk(importMx);
                err = importError;
            }
            if (!err.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), T("Error: %s"), err.c_str());
            else ImGui::TextColored(ImVec4(0.4f, 1, 0.6f, 1), T("Done!"));
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80 * uiScale + ImGui::GetCursorPosX());
            if (ImGui::Button(T("Close"), ImVec2(80 * uiScale, 0))) ImGui::CloseCurrentPopup();
        } else
            ImGui::TextDisabled(T("Working… (converting large textures can take a while)"));
        ImGui::EndPopup();
    }
}

// ===========================================================================
void App::uiMenu(const json& menu, int depth) {
    if (!menu.is_object() || !avatar) return;
    auto& params = overrides().params;
    auto get = [&](const std::string& n) {
        auto it = params.find(n);
        if (it != params.end()) return it->second;
        auto jt = avatar->paramValues.find(n);
        return jt == avatar->paramValues.end() ? 0.0f : jt->second;
    };
    int idx = 0;
    for (auto& c : menu["controls"]) {
        ImGui::PushID(idx++);
        std::string type = c.value("type", "button");
        std::string name = c.value("name", "");
        if (name.empty()) name = T("(unnamed)");
        std::string param = c.value("parameter", "");
        float value = c.value("value", 1.0f);
        ID3D11ShaderResourceView* ic = c["icon"].is_string() ? icon(c["icon"].get<std::string>()) : nullptr;
        float isz = ImGui::GetFrameHeight();
        if (ic) {
            ImGui::Image(texRef(ic), ImVec2(isz, isz));
            ImGui::SameLine();
        }
        if (type == "submenu") {
            if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                if (c["subMenu"].is_object()) uiMenu(c["subMenu"], depth + 1);
                ImGui::TreePop();
            }
        } else if (type == "toggle") {
            bool on = !param.empty() && std::fabs(get(param) - value) < 0.001f;
            if (ImGui::Checkbox(name.c_str(), &on) && !param.empty()) params[param] = on ? value : 0.0f;
        } else if (type == "button") {
            ImGui::Button(name.c_str());
            if (!param.empty()) {
                if (ImGui::IsItemActive()) params[param] = value;
                else if (ImGui::IsItemDeactivated()) params[param] = 0;
            }
        } else if (type == "radial") {
            std::string sp = c["subParameters"].size() ? c["subParameters"][0].get<std::string>() : "";
            if (!sp.empty()) {
                float v = get(sp);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
                if (slider(("##r" + name).c_str(), &v, 0, 1, "%.2f")) {
                    params[sp] = v;
                    if (!param.empty()) params[param] = value;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(name.c_str());
            } else
                ImGui::TextDisabled("%s", name.c_str());
        } else if (type == "twoaxis" || type == "fouraxis") {
            ImGui::TextUnformatted(name.c_str());
            ImGui::Indent();
            for (size_t k = 0; k < c["subParameters"].size(); k++) {
                std::string sp = c["subParameters"][k].get<std::string>();
                if (sp.empty()) continue;
                float v = get(sp);
                if (slider((sp + "##" + std::to_string(k)).c_str(), &v, type == "twoaxis" ? -1.0f : 0.0f, 1.0f)) params[sp] = v;
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
}

void App::uiAvatarTab() {
    ImGui::SeparatorText(T("Avatars"));
    if (ImGui::Button(T("Import…"))) {
        std::wstring flt = widen(T("Avatar files")) + L" (*.unitypackage, *.vrm, *.glb, *.fbx, *.zip)" + L'\0' + L"*.unitypackage;*.vrm;*.glb;*.gltf;*.fbx;*.zip" + L'\0' +
                           widen(T("All files")) + L'\0' + L"*.*" + L'\0';
        std::wstring f = openFileDialog(hwnd, flt.c_str(), widen(T("Import avatar")).c_str());
        if (!f.empty()) startImport(f);
    }
    help(T("Supported: VRChat .unitypackage (including menu, toggles, PhysBones, blinking, lip sync), VRM 0.x/1.0, GLB/glTF and FBX. You can also drag the file into the window."));
    ImGui::SameLine();
    if (ImGui::Button(T("Refresh"))) avatarList = listAvatars();
    ImGui::SameLine();
    if (ImGui::Button(T("Folder"))) ShellExecuteW(nullptr, L"open", avatarsDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    static std::string toDelete;
    if (ImGui::BeginListBox("##avatars", ImVec2(-1, 5.5f * ImGui::GetTextLineHeightWithSpacing()))) {
        for (auto& a : avatarList) {
            bool sel = avatar && avatar->id == a.id;
            std::string label = a.name + "##" + a.id;
            if (ImGui::Selectable(label.c_str(), sel) && !sel && !loading) loadAvatar(a.id);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60 * uiScale + ImGui::GetCursorPosX());
            ImGui::TextDisabled("%s", a.format.c_str());
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(T("Delete…"))) toDelete = a.id;
                ImGui::EndPopup();
            }
        }
        ImGui::EndListBox();
    }
    if (!toDelete.empty()) ImGui::OpenPopup(T("Delete avatar"));
    if (ImGui::BeginPopupModal(T("Delete avatar"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(T("Delete the avatar \"%s\" from the AvatarCam folder?\n(Your original file is not affected.)"), toDelete.c_str());
        if (ImGui::Button(T("Delete"))) {
            if (avatar && avatar->id == toDelete) avatar.reset();
            std::error_code ec;
            fs::remove_all(avatarsDir() / u8path(toDelete), ec);
            avatarList = listAvatars();
            toDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(T("Cancel"))) {
            toDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!avatar) return;

    ImGui::SeparatorText(T("Hand gestures"));
    help(T("VRChat hand gestures (GestureLeft/GestureRight). The avatar's fingers follow them, and if your avatar has gesture-driven facial expressions, those appear too."));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::Combo(T("Left hand"), &gestureLeft, gestureNames(), 8);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::Combo(T("Right hand"), &gestureRight, gestureNames(), 8);

    if (!avatar->vrmExpressions.empty()) {
        ImGui::SeparatorText(T("Expressions"));
        help(T("The VRM model's built-in expressions. They can also be bound to hotkeys (Output tab)."));
        auto& ex = overrides().expressions;
        static const char* skip[] = {"aa", "ih", "ou", "ee", "oh", "blink", "blinkleft", "blinkright", "lookup", "lookdown", "lookleft", "lookright", "neutral"};
        for (auto& e : avatar->vrmExpressions) {
            std::string ln = toLower(e.name);
            bool hide = false;
            for (auto* k : skip) hide |= ln == k;
            if (hide || e.binds.empty()) continue;
            float v = ex.count(e.name) ? ex[e.name] : 0.0f;
            if (slider(e.name.c_str(), &v, 0, 1, "%.2f")) ex[e.name] = v;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ex.erase(e.name);
        }
        if (ImGui::SmallButton(T("Reset expressions"))) ex.clear();
    }
    if (avatar->hasExpressions() && avatar->manifest["expressions"]["menu"].is_object()) {
        ImGui::SeparatorText(T("VRChat menu"));
        help(T("Your avatar's own VRChat Expression menu: outfits, accessories, colors. It works the same as in the game."));
        if (ImGui::SmallButton(T("Defaults"))) overrides().params.clear();
        uiMenu(avatar->manifest["expressions"]["menu"], 0);
    }
    if (ImGui::CollapsingHeader(T("All parameters (advanced)"))) {
        auto& params = overrides().params;
        for (auto& p : avatar->params) {
            if (!p.menu) continue;
            float v = params.count(p.name) ? params[p.name] : avatar->paramValues[p.name];
            ImGui::PushID(p.name.c_str());
            if (p.type == "bool") {
                bool b = v > 0.5f;
                if (ImGui::Checkbox(p.name.c_str(), &b)) params[p.name] = b ? 1.0f : 0.0f;
            } else if (p.type == "int") {
                int i = (int)v;
                if (ImGui::InputInt(p.name.c_str(), &i)) params[p.name] = (float)std::clamp(i, 0, 255);
            } else {
                if (slider(p.name.c_str(), &v, -1, 1)) params[p.name] = v;
            }
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader(T("Information"))) {
        int hb = 0;
        for (int i = 0; i < HB_Count; i++) hb += avatar->humanoid[i] >= 0;
        ImGui::Text(T("Format: %s"), avatar->format.c_str());
        ImGui::Text(T("Nodes: %zu   Meshes: %zu   Materials: %zu"), avatar->nodes.size(), avatar->meshes.size(), avatar->materials.size());
        ImGui::Text(T("Humanoid bones: %d/%d"), hb, (int)HB_Count);
        ImGui::Text(T("Physics chains: %zu"), avatar->physics ? avatar->physics->chainCount() : 0);
        ImGui::Text(T("Blink: %s   Visemes: %s   ARKit: %zu"), avatar->blink.mesh >= 0 || avatar->blinkL.mesh >= 0 ? T("yes") : T("no"), avatar->visemes[10].mesh >= 0 ? T("yes") : T("no"), avatar->arkitShapes.size());
        if (ImGui::Button(T("Open log"))) showLog = true;
    }
}

// ===========================================================================
void App::uiPartsTab() {
    if (!avatar) {
        ImGui::TextDisabled(T("Load an avatar first."));
        return;
    }
    AvatarOverrides& ov = overrides();
    ImGui::SeparatorText(T("Colors"));
    slider(T("Global hue shift"), &ov.globalHue, -0.5f, 0.5f, "%.3f", 0.0f);
    help(T("Shifts the hue of the whole avatar. Right-click: default."));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", T("Filter by name…"), &partFilter);
    if (ImGui::SmallButton(T("Show all"))) ov.partVisible.clear();
    ImGui::SameLine();
    if (ImGui::SmallButton(T("Reset colors"))) ov.materials.clear();
    ImGui::SeparatorText(T("Body parts"));
    std::string flt = toLower(partFilter);
    for (size_t pi = 0; pi < avatar->parts.size(); pi++) {
        auto& part = avatar->parts[pi];
        if (!flt.empty() && toLower(part.label).find(flt) == std::string::npos) continue;
        ImGui::PushID((int)pi);
        bool visNow = false;
        for (int m : part.meshes) visNow |= avatar->meshes[m].visible;
        bool overridden = ov.partVisible.count(part.path) > 0;
        bool v = visNow;
        if (ImGui::Checkbox("##vis", &v)) ov.partVisible[part.path] = v;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ov.partVisible.erase(part.path);
        ImGui::SetItemTooltip(overridden ? T("Set manually (right-click: back to the avatar/menu state)") : T("Visible (per the avatar's menu)"));
        ImGui::SameLine();
        bool colored = false;
        for (int mi : part.materials) {
            std::string key = avatar->materials[mi].key;
            auto it = ov.materials.find(key);
            if (it != ov.materials.end() && !it->second.isDefault()) colored = true;
        }
        if (!visNow) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        bool open = ImGui::TreeNodeEx((part.label + (colored ? "  •" : "") + (overridden ? "  ⚑" : "")).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        if (!visNow) ImGui::PopStyleColor();
        if (open) {
            for (int mi : part.materials) {
                AvMaterial& m = avatar->materials[mi];
                ImGui::PushID(mi);
                MaterialOverride& o = ov.materials[m.key];
                ImGui::TextDisabled(T("Material: %s"), m.sourceName.c_str());
                color3(T("Tint"), o.tint);
                slider(T("Hue"), &o.hue, -0.5f, 0.5f, "%.3f", 0.0f);
                slider(T("Saturation"), &o.saturation, -1, 1, "%.2f", 0.0f);
                slider(T("Brightness"), &o.brightness, -1, 1, "%.2f", 0.0f);
                ImGui::Checkbox(T("Custom emission color"), &o.emissiveOverride);
                if (o.emissiveOverride) {
                    ImGui::SameLine();
                    color3("##em", o.emissive, ImGuiColorEditFlags_NoInputs);
                }
                slider(T("Emission strength"), &o.emissiveBoost, 0, 6, "%.2fx", 1.0f);
                slider(T("Opacity"), &o.opacity, 0, 1, "%.2f", 1.0f);
                if (ImGui::SmallButton(T("Reset"))) o = MaterialOverride{};
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    // a változatlan felülírások törlése
    for (auto it = ov.materials.begin(); it != ov.materials.end();) {
        if (it->second.isDefault() && !ImGui::IsAnyItemActive()) it = ov.materials.erase(it);
        else ++it;
    }

    if (ImGui::CollapsingHeader(T("Blendshapes"))) {
        help(T("Blendshapes of the avatar's meshes (expressions, body shape, clothing fit). Right-click a slider: back to default."));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##bf", T("Filter…"), &blendFilter);
        std::string bf = toLower(blendFilter);
        for (size_t mi = 0; mi < avatar->meshes.size(); mi++) {
            AvMesh& m = avatar->meshes[mi];
            if (m.src->morphs.empty()) continue;
            ImGui::PushID((int)mi);
            if (ImGui::TreeNode(m.name.c_str())) {
                for (size_t k = 0; k < m.src->morphs.size(); k++) {
                    const std::string& n = m.src->morphs[k].name;
                    if (!bf.empty() && toLower(n).find(bf) == std::string::npos) continue;
                    std::string key = m.path + "|" + n;
                    float v = m.weights[k];
                    bool has = ov.blendShapes.count(key) > 0;
                    if (has) ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.9f, 0.6f, 0.2f, 1));
                    if (slider((n + "##" + std::to_string(k)).c_str(), &v, 0, 1, "%.2f")) ov.blendShapes[key] = v;
                    if (has) ImGui::PopStyleColor();
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ov.blendShapes.erase(key);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton(T("Reset all blendshapes"))) ov.blendShapes.clear();
    }
}

// ===========================================================================
void App::uiBackground() {
    BackgroundSettings& bg = settings.background;
    {
        const char* modes[] = {T("Transparent (OBS)"), T("Solid color"), T("Gradient"), T("Image"), "Chroma key"};
        ImGui::Combo(T("Type"), &bg.mode, modes, 5);
        if (bg.mode == 0) ImGui::TextDisabled(T("Transparent in OBS – the game or scene shows behind it."));
        if (bg.mode == 1) color3(T("Color"), bg.color);
        if (bg.mode == 2) {
            color3(T("Color 1"), bg.color);
            color3(T("Color 2"), bg.color2);
            slider(T("Angle"), &bg.gradientAngle, 0, 360, "%.0f°", 90);
        }
        if (bg.mode == 3) {
            ImGui::InputText(T("Image"), &bg.image);
            if (ImGui::Button(T("Browse…"))) {
                std::wstring flt = widen(T("Images")) + L'\0' + L"*.png;*.jpg;*.jpeg;*.bmp;*.tga" + L'\0';
                std::wstring f = openFileDialog(hwnd, flt.c_str(), widen(T("Background image")).c_str());
                if (!f.empty()) bg.image = narrow(f);
            }
            ImGui::SameLine();
            ImGui::TextDisabled(renderer.backgroundImageOk() ? T("loaded") : T("no image"));
            if (auto* srv = renderer.backgroundSrv()) {
                float tw = std::min(ImGui::GetContentRegionAvail().x, 220.0f * uiScale);
                ImGui::Image(texRef(srv), ImVec2(tw, tw / std::max(0.2f, renderer.backgroundAspect())));
            }
            const char* fits[] = {T("Fill"), T("Fit"), T("Stretch")};
            ImGui::Combo(T("Fit mode"), &bg.imageFit, fits, 3);
            slider(T("Darken"), &bg.imageDim, 0, 0.9f, "%.2f", 0);
            color3(T("Border color"), bg.color);
        }
        if (bg.mode == 4) {
            color3(T("Key color"), bg.chroma);
            if (ImGui::SmallButton(T("Green"))) bg.chroma = {0, 1, 0};
            ImGui::SameLine();
            if (ImGui::SmallButton(T("Blue"))) bg.chroma = {0, 0, 1};
            ImGui::SameLine();
            if (ImGui::SmallButton("Magenta")) bg.chroma = {1, 0, 1};
            ImGui::TextDisabled(T("Remove it in OBS with a “Chroma Key” filter."));
        }
    }
}

void App::uiSceneTab() {
    if (ImGui::CollapsingHeader(T("Background"), ImGuiTreeNodeFlags_DefaultOpen)) uiBackground();
    if (ImGui::CollapsingHeader(T("Lights"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted(T("Presets:"));
        const char* lp[] = {T("Studio"), "Neon", T("Sunset"), T("Dramatic"), T("Flat")};
        for (int i = 0; i < 5; i++) {
            ImGui::SameLine();
            if (ImGui::SmallButton(lp[i])) lightPresets(settings, i);
        }
        int removeAt = -1;
        for (size_t i = 0; i < settings.lights.size(); i++) {
            LightSettings& L = settings.lights[i];
            ImGui::PushID((int)i);
            ImGui::Checkbox("##en", &L.enabled);
            ImGui::SameLine();
            ImVec4 lc(L.color.x, L.color.y, L.color.z, 1);
            ImGui::ColorButton("##c", lc, ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
            ImGui::SameLine();
            if (ImGui::TreeNodeEx(format("%d. %s###l", (int)i + 1, T(L.name.c_str())).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                // az alapértelmezett nevek angol kulcsok: a mezőben is az aktuális nyelven látszanak
                std::string shown = T(L.name.c_str());
                if (ImGui::InputText(T("Name"), &shown)) L.name = shown;
                const char* types[] = {T("Directional (sun)"), T("Point"), "Spot"};
                ImGui::Combo(T("Type"), &L.type, types, 3);
                color3(T("Color"), L.color);
                slider(T("Intensity"), &L.intensity, 0, 6, "%.2f", 1.0f);
                if (L.type == 0) {
                    slider(T("Direction (horizontal)"), &L.yaw, -180, 180, "%.0f°", 0);
                    slider(T("Elevation"), &L.pitch, -89, 89, "%.0f°", 30);
                    ImGui::Checkbox(T("Follow camera"), &L.followCamera);
                    ImGui::Checkbox(T("Casts shadows"), &L.shadow);
                } else {
                    ImGui::DragFloat3(T("Position"), &L.position.x, 0.01f, -10, 10, "%.2f");
                    slider(T("Range"), &L.range, 0.2f, 20, "%.1f m", 6);
                    if (L.type == 2) {
                        slider(T("Cone angle"), &L.spotAngle, 5, 80, "%.0f°", 35);
                        slider(T("Soft edge"), &L.spotSoftness, 0, 1, "%.2f", 0.3f);
                    }
                }
                if (ImGui::SmallButton(T("Delete light"))) removeAt = (int)i;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeAt >= 0) settings.lights.erase(settings.lights.begin() + removeAt);
        if (settings.lights.size() < 8) {
            if (ImGui::Button(T("+ Directional"))) {
                LightSettings l;
                l.name = "New light";
                settings.lights.push_back(l);
            }
            ImGui::SameLine();
            if (ImGui::Button(T("+ Point"))) {
                LightSettings l;
                l.name = "Point light";
                l.type = 1;
                l.intensity = 2.5f;
                l.position = {0.6f, 1.6f, 0.8f};
                l.range = 3;
                settings.lights.push_back(l);
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Spot")) {
                LightSettings l;
                l.name = "Spot light";
                l.type = 2;
                l.intensity = 3;
                l.position = {0, 2.5f, 1.2f};
                l.range = 5;
                settings.lights.push_back(l);
            }
        }
        ImGui::SeparatorText(T("Ambient light"));
        color3(T("Sky"), settings.effects.ambientSky);
        color3(T("Ground"), settings.effects.ambientGround);
        slider(T("Ambient intensity"), &settings.effects.ambientIntensity, 0, 2, "%.2f", 0.55f);
    }
    if (ImGui::CollapsingHeader(T("Camera"))) {
        CameraSettings& c = settings.camera;
        const char* pr[] = {T("Face"), T("Chest"), T("Waist"), T("Full body")};
        for (int i = 0; i < 4; i++) {
            if (i) ImGui::SameLine();
            if (ImGui::Button(pr[i])) applyCameraPreset(i);
        }
        slider(T("Field of view"), &c.fov, 10, 90, "%.0f°", 30);
        slider(T("Distance"), &c.distance, 0.1f, 8, "%.2f m");
        slider(T("Orbit"), &c.yaw, -180, 180, "%.0f°", 0);
        slider(T("Pitch"), &c.pitch, -80, 80, "%.0f°", 4);
        slider(T("Roll"), &c.roll, -45, 45, "%.0f°", 0);
        ImGui::DragFloat3(T("Target"), &c.target.x, 0.005f, -5, 5, "%.2f");
        ImGui::Checkbox(T("Follow head"), &c.followHead);
        if (c.followHead) {
            slider(T("Follow amount"), &c.followAmount, 0, 1, "%.2f", 0.6f);
            slider(T("Smoothing"), &c.smoothing, 0.5f, 10, "%.1f", 5);
        }
    }
    if (ImGui::CollapsingHeader(T("Look and effects"))) {
        EffectSettings& e = settings.effects;
        const char* sh[] = {"Toon (anime)", T("Realistic (PBR)"), T("Flat (unlit)")};
        ImGui::Combo(T("Shading"), &e.shading, sh, 3);
        if (e.shading == 0) {
            slider(T("Shadow edge"), &e.toonThreshold, -0.6f, 0.6f, "%.2f", 0);
            slider(T("Shadow softness"), &e.toonSoftness, 0.001f, 0.5f, "%.3f", 0.08f);
            color3(T("Shadow color"), e.shadeTint);
        }
        ImGui::SeparatorText(T("Rim light"));
        ImGui::Checkbox(T("Enabled##rim"), &e.rim);
        if (e.rim) {
            color3(T("Color##rim"), e.rimColor);
            slider(T("Intensity##rim"), &e.rimIntensity, 0, 2, "%.2f", 0.25f);
            slider(T("Sharpness##rim"), &e.rimPower, 1, 10, "%.1f", 4);
        }
        ImGui::SeparatorText(T("Outline"));
        ImGui::Checkbox(T("Enabled##ol"), &e.outline);
        if (e.outline) {
            color3(T("Color##ol"), e.outlineColor);
            slider(T("Width"), &e.outlineWidth, 0.3f, 6, "%.1f px", 1.5f);
        }
        ImGui::SeparatorText(T("Glow (bloom)"));
        ImGui::Checkbox(T("Enabled##bl"), &e.bloom);
        if (e.bloom) {
            ImGui::Checkbox(T("Only emissive parts glow"), &e.bloomEmissiveOnly);
            slider(T("Intensity##bl"), &e.bloomStrength, 0, 3, "%.2f", 0.6f);
            if (!e.bloomEmissiveOnly) slider(T("Threshold"), &e.bloomThreshold, 0, 5, "%.2f", 1.4f);
            slider(T("Radius"), &e.bloomRadius, 0, 1, "%.2f", 0.75f);
        }
        slider(T("Emissive parts strength"), &e.emissionScale, 0, 4, "%.2fx", 1.0f);
        const char* mf[] = {T("Chrome"), T("Dark metal"), T("Skip")};
        ImGui::Combo(T("Missing Poiyomi matcap fallback"), &e.matcapFallback, mf, 3);
        help(T("Poiyomi's built-in matcap (reflection) textures are not included in avatar packages. The program uses a generic metallic shine instead: this gives piercings and metal parts their gleam."));
        ImGui::SeparatorText(T("Color"));
        const char* tm[] = {T("None"), T("ACES (filmic)"), "AgX", "Neutral"};
        ImGui::Combo(T("Tone mapping"), &e.toneMap, tm, 4);
        slider(T("Exposure"), &e.exposure, 0.2f, 3, "%.2f", 1);
        slider(T("Contrast"), &e.contrast, 0.5f, 1.6f, "%.2f", 1);
        slider(T("Saturation##g"), &e.saturation, 0, 2, "%.2f", 1);
        ImGui::SeparatorText(T("Shadows"));
        ImGui::Checkbox(T("Shadows"), &e.shadows);
        if (e.shadows) slider(T("Shadow strength"), &e.shadowStrength, 0, 1, "%.2f", 0.65f);
        ImGui::Checkbox(T("Ground shadow"), &e.groundShadow);
        if (e.groundShadow) slider(T("Ground shadow strength"), &e.groundShadowOpacity, 0, 1, "%.2f", 0.35f);
        const char* aa[] = {T("Off"), "2x", "4x", "8x"};
        int ai = e.msaa >= 8 ? 3 : e.msaa >= 4 ? 2 : e.msaa >= 2 ? 1 : 0;
        if (ImGui::Combo(T("Anti-aliasing (MSAA)"), &ai, aa, 4)) e.msaa = ai == 0 ? 1 : 1 << ai;
    }
}

// ===========================================================================
void App::uiMotionTab() {
    PoseSettings& p = settings.pose;
    if (ImGui::CollapsingHeader(T("Posture"), ImGuiTreeNodeFlags_DefaultOpen)) {
        slider(T("Arms down"), &p.armDown, 0, 90, "%.0f°", 72);
        slider(T("Arms forward"), &p.armForward, -30, 40, "%.0f°", 8);
        slider(T("Elbow bend"), &p.elbowBend, 0, 90, "%.0f°", 12);
        slider(T("Wrist"), &p.wristBend, -30, 30, "%.0f°", 5);
        slider(T("Finger curl"), &p.fingerCurl, 0, 1, "%.2f", 0.25f);
        slider(T("Avatar rotation"), &p.bodyYaw, -180, 180, "%.0f°", 0);
        ImGui::DragFloat3(T("Offset"), &p.bodyOffset.x, 0.005f, -3, 3, "%.2f");
        slider(T("Scale"), &p.scale, 0.2f, 3, "%.2fx", 1);
    }
    if (ImGui::CollapsingHeader(T("Idle motion"), ImGuiTreeNodeFlags_DefaultOpen)) {
        slider(T("Breathing"), &p.breathing, 0, 3, "%.2f", 1);
        slider(T("Sway"), &p.sway, 0, 3, "%.2f", 0.5f);
        slider(T("Head motion"), &p.headIdle, 0, 3, "%.2f", 0.5f);
        ImGui::Checkbox(T("Auto blink"), &p.autoBlink);
        if (p.autoBlink) {
            if (slider(T("Earliest"), &p.blinkMin, 0.5f, 20, T("%.1f s"), 3)) p.blinkMax = std::max(p.blinkMax, p.blinkMin);
            if (slider(T("Latest"), &p.blinkMax, 0.5f, 20, T("%.1f s"), 6)) p.blinkMin = std::min(p.blinkMin, p.blinkMax);
            help(T("The time between two blinks is random within this range, e.g. between 3 and 6 s.\nIf both are equal, it blinks exactly that often."));
            ImGui::Checkbox(T("Occasional double blink"), &p.doubleBlink);
            help(T("Now and then two quick blinks in a row, like people do. When off, blinks always follow each other within the range above."));
        }
        ImGui::Checkbox(T("Eye movement (look around)"), &p.lookAround);
        if (p.lookAround) slider(T("Eye movement amount"), &p.lookAmount, 0, 1, "%.2f", 0.4f);
    }
    if (ImGui::CollapsingHeader(T("Physics (hair, tail, ears, clothes)"), ImGuiTreeNodeFlags_DefaultOpen)) {
        AvatarOverrides& ov = overrides();
        ImGui::Checkbox(T("Physics enabled"), &ov.physics);
        slider(T("Physics strength"), &ov.physicsStrength, 0.2f, 3, "%.2f", 1);
        if (ImGui::Button(T("Reset physics")) && avatar) avatar->resetPhysics();
        if (avatar && avatar->physics) {
            ImGui::SameLine();
            ImGui::TextDisabled(T("%zu chains"), avatar->physics->chainCount());
        }
    }
}

// ===========================================================================
void App::uiTrackingTab() {
    TrackingSettings& t = settings.tracking;
    if (ImGui::CollapsingHeader(T("Face tracking (webcam)"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox(T("Enabled##face"), &t.faceEnabled);
        static std::vector<std::string> cams;
        static double camsTime = -10;
        if (nowSeconds() - camsTime > 5 && tracking) {
            cams = tracking->cameras();
            camsTime = nowSeconds();
        }
        if (ImGui::BeginCombo(T("Camera"), t.cameraName.empty() ? (cams.empty() ? T("(none)") : cams[0].c_str()) : t.cameraName.c_str())) {
            for (size_t i = 0; i < cams.size(); i++)
                if (ImGui::Selectable(cams[i].c_str(), t.cameraName == cams[i])) {
                    t.cameraName = cams[i];
                    t.cameraIndex = (int)i;
                }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled(T("Status: %s"), tracking ? tracking->faceStatus().c_str() : "-");
        ImGui::Checkbox(T("Mirror"), &t.mirror);
        slider(T("Smoothing"), &t.smoothing, 0, 0.95f, "%.2f", 0.5f);
        slider(T("Head motion amount"), &t.headScale, 0, 2, "%.2f", 1);
        slider(T("Body follow"), &t.bodyFollow, 0, 1, "%.2f", 0.3f);
        ImGui::Checkbox(T("Eyes"), &t.eyeTracking);
        ImGui::SameLine();
        ImGui::Checkbox(T("Blink"), &t.blinkTracking);
        ImGui::SameLine();
        ImGui::Checkbox(T("Mouth / face"), &t.mouthTracking);
        if (ImGui::Button(T("Recenter")) && tracking) tracking->recenter();
        help(T("Look straight into the camera and press this: it becomes the neutral head pose."));
        ImGui::Checkbox(T("Show camera image"), &t.showCamera);
        if (t.showCamera && tracking) {
            if (auto* srv = tracking->cameraPreview(gfx)) {
                float w = ImGui::GetContentRegionAvail().x;
                ImGui::Image(texRef(srv), ImVec2(w, w * 0.75f));
            }
        }
    }
    if (ImGui::CollapsingHeader(T("Hand tracking (webcam)"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox(T("Enabled##hand"), &t.handEnabled);
        help(T("Tracks your hands from the webcam image (arms, wrists, fingers). Uses the same camera as face tracking. Keep your hands clearly visible and well lit."));
        ImGui::TextDisabled(T("Status: %s"), tracking ? tracking->handStatus().c_str() : "-");
        ImGui::Checkbox(T("Mirror##hand"), &t.handMirror);
        help(T("On: moves like a mirror image – your right hand moves the avatar's left hand.\nOff: the other way round – your right hand moves the avatar's right hand.\nIf your camera already delivers a mirrored image and the hands move the wrong way, toggle this."));
        slider(T("Hand smoothing"), &t.handSmoothing, 0, 0.95f, "%.2f", 0.5f);
    }
    if (ImGui::CollapsingHeader(T("Lip sync from microphone"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox(T("Enabled##mic"), &t.micEnabled);
        static std::vector<std::string> mics;
        static double micsTime = -10;
        if (nowSeconds() - micsTime > 5 && tracking) {
            mics = tracking->microphones();
            micsTime = nowSeconds();
        }
        if (ImGui::BeginCombo(T("Microphone"), t.micName.empty() ? T("Default") : t.micName.c_str())) {
            if (ImGui::Selectable(T("Default"), t.micName.empty())) t.micName.clear();
            for (auto& m : mics)
                if (ImGui::Selectable(m.c_str(), t.micName == m)) t.micName = m;
            ImGui::EndCombo();
        }
        ImGui::Checkbox(T("Automatic gain"), &t.micAutoGain);
        help(T("Adapts to your voice automatically, so the mouth moves similarly whether you speak quietly or loudly. Background noise is not amplified. The gain below fine-tunes it."));
        if (slider(T("Gain"), &t.micGain, 0.5f, 50, "%.1fx", 1, ImGuiSliderFlags_Logarithmic)) t.micGain = std::clamp(t.micGain, 0.1f, 100.0f);
        help(T("Amplifies the microphone signal for lip sync. Raise it if the mouth barely moves while you talk."));
        if (t.micAutoGain && tracking && t.micEnabled) {
            ImGui::SameLine();
            float g = tracking->micGain();
            ImGui::TextDisabled(T("now %.1fx (%+.0f dB)"), g, 20.0f * std::log10(std::max(g, 1e-3f)));
        }
        slider(T("Mouth opening"), &t.micMouth, 0.2f, 2, "%.2fx", 1);
        help(T("How wide the mouth opens for the same loudness."));
        slider(T("Noise gate"), &t.micThreshold, 0, 0.2f, "%.3f", 0.015f);
        help(T("Sounds below this level don't move the mouth. The white line on the meter marks the gate: background noise should stay left of it, your voice should go well past it."));
        {
            // szintjelző: a mikrofon szintje erősítés után, a fehér vonal a zajküszöb
            float lvl = tracking ? tracking->micLevel() : 0;
            const float scale = 0.25f;
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x, h = 8 * uiScale;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(40, 44, 54, 255), 2);
            float fill = std::min(1.0f, lvl / scale);
            float gate = tracking && tracking->micGate() > 0 ? tracking->micGate() : t.micThreshold;
            bool open = lvl > gate;
            dl->AddRectFilled(p, ImVec2(p.x + w * fill, p.y + h), open ? IM_COL32(80, 210, 120, 255) : IM_COL32(110, 120, 140, 255), 2);
            float gx = p.x + w * std::min(1.0f, gate / scale);
            dl->AddLine(ImVec2(gx, p.y - 2), ImVec2(gx, p.y + h + 2), IM_COL32(255, 255, 255, 230), 2);
            ImGui::Dummy(ImVec2(w, h + 2));
        }
        ImGui::TextDisabled(T("Status: %s"), tracking ? tracking->micStatus().c_str() : "-");
    }
    if (ImGui::CollapsingHeader(T("External tracker (VMC protocol)"))) {
        ImGui::Checkbox(T("Receive VMC"), &t.vmcEnabled);
        ImGui::InputInt("Port", &t.vmcPort);
        ImGui::TextDisabled(T("Status: %s"), tracking ? tracking->vmcStatus().c_str() : "-");
        ImGui::TextWrapped(T("For VMC data from phone trackers (e.g. Waidayo) or other VTuber apps (VSeeFace, XR Animator, VMC4B, etc.). Enter this computer and port in the sending app."));
    }
}

// ===========================================================================
void App::uiOutputTab() {
    OutputSettings& o = settings.output;
    if (ImGui::CollapsingHeader(T("Image##out"), ImGuiTreeNodeFlags_DefaultOpen)) {
        struct Res {
            const char* name;
            int w, h;
        } res[] = {{"1920×1080", 1920, 1080}, {"1280×720", 1280, 720}, {"2560×1440", 2560, 1440}, {T("1080×1920 (portrait)"), 1080, 1920}, {"1080×1080", 1080, 1080}, {"3840×2160", 3840, 2160}};
        std::string cur = format("%d×%d", o.width, o.height);
        if (ImGui::BeginCombo(T("Resolution"), cur.c_str())) {
            for (auto& r : res)
                if (ImGui::Selectable(r.name, o.width == r.w && o.height == r.h)) o.width = r.w, o.height = r.h;
            ImGui::EndCombo();
        }
        int wh[2] = {o.width, o.height};
        if (ImGui::InputInt2(T("Custom"), wh, ImGuiInputTextFlags_EnterReturnsTrue)) {
            o.width = std::clamp(wh[0], 64, 7680);
            o.height = std::clamp(wh[1], 64, 4320);
        }
        sliderInt(T("FPS limit"), &o.fpsLimit, 15, 144, "%d", 60);
        ImGui::Checkbox(T("Mirror##out"), &o.mirror);
        ImGui::Checkbox(T("Straight alpha"), &o.straightAlpha);
        help(T("For OBS's “Allow Transparency” mode, straight (non-premultiplied) alpha gives clean edges. If the edges look dark or bright, try turning it off."));
    }
    if (ImGui::CollapsingHeader(T("Background##out"), ImGuiTreeNodeFlags_DefaultOpen)) uiBackground();
    if (ImGui::CollapsingHeader(T("OBS connection"), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox(T("Output window"), &o.outputWindow)) {}
        ImGui::SameLine();
        if (ImGui::Checkbox(T("Borderless"), &o.outputBorderless) && outHwnd) destroyOutputWindow();
        if (!o.outputWindow && !o.spout)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1));
            ImGui::TextWrapped("%s", T("No separate output is on: if OBS captures the main window, it shows nothing while minimized and the mouse can appear on it. Turn on the output window!"));
            ImGui::PopStyleColor();
        }
        ImGui::TextWrapped(T("1) Turn on the output window.\n2) OBS: Sources → + → Game Capture.\n3) Mode: “Capture specific window”, Window: “[AvatarCam.exe]: AvatarCam Output”.\n4) Check “Allow Transparency” and uncheck “Capture Cursor”.\nThe output window keeps delivering frames even when you alt+tab away or the game is in front of it. It can't be minimized and has no mouse cursor. Drag it with the mouse; right-click closes it."));
        ImGui::Separator();
        ImGui::Checkbox(T("Send via Spout2"), &o.spout);
        ImGui::InputText(T("Spout name"), &o.spoutName);
        ImGui::TextDisabled("Spout: %s", spout ? (spout->active() ? T("sending") : spout->status.c_str()) : "-");
        help(T("Spout is the fastest connection with full transparency, but OBS needs the “Spout2 Plugin for OBS”."));
        ImGui::Separator();
        ImGui::TextWrapped(T("If you start OBS's Virtual Camera, the avatar becomes a camera image in Discord, Zoom and elsewhere."));
    }
    if (ImGui::CollapsingHeader(T("Performance (while gaming)"), ImGuiTreeNodeFlags_DefaultOpen)) {
        float cpuPct = 100.0f * perf.processCores / std::max(1, perf.logicalCores);
        float gpuPct = gpuTimer.totalMs * fps / 10.0f;
        ImGui::Text(T("%.0f FPS  ·  CPU %.1f%%  ·  GPU %.2f ms/frame (~%.0f%%)"), fps, cpuPct, gpuTimer.totalMs, gpuPct);
        help(T("CPU: the whole program relative to all processor cores.\nGPU: graphics card time per frame. Under light load the card lowers its clock, so the value looks higher then; while gaming (at full clock) it is lower."));
        if (tracking && (settings.tracking.faceEnabled || settings.tracking.handEnabled))
            ImGui::TextDisabled(T("Tracking per frame: face %.1f ms, hands %.1f ms"), tracking->faceMs(), tracking->handMs());
        ImGui::TextDisabled(T("Main thread: %.2f ms (avatar %.2f, render %.2f, UI %.2f)"), perf.frameMs, perf.cpu[kCpuAvatar] + perf.cpu[kCpuUpload], perf.cpu[kCpuRender], perf.cpu[kCpuUi] + perf.cpu[kCpuPresent]);
        sliderInt(T("UI FPS in background"), &o.uiIdleFps, 0, 60, o.uiIdleFps == 0 ? T("always full") : "%d", 15);
        help(T("When the AvatarCam window is not in the foreground (e.g. while gaming), the UI and preview refresh only this many times per second.\nOnly applies when you use the output window or Spout: the image going to OBS stays at full speed.\nWithout a separate output (OBS captures the main window), the main window always refreshes at full speed."));
        if (ImGui::Button(T("Power saving"))) {
            o.fpsLimit = 30;
            o.uiIdleFps = 5;
            settings.effects.msaa = 2;
        }
        help(T("For streaming while gaming: 30 FPS output, 2x anti-aliasing, UI refreshing rarely in the background.\nIf OBS streams at 30 fps, the output needs no more; for a 60 fps stream keep the FPS limit at 60."));
        ImGui::SameLine();
        if (ImGui::Button(T("Full quality"))) {
            o.fpsLimit = 60;
            o.uiIdleFps = 15;
            settings.effects.msaa = 4;
        }
        ImGui::TextWrapped(T("Tip: if the avatar only fills a corner of the stream, 1280×720 is plenty. Capping the game's FPS leaves GPU headroom for OBS and the avatar."));
    }
    if (ImGui::CollapsingHeader(T("Presets (scene)"))) {
        static std::string newName;
        ImGui::InputTextWithHint("##pn", T("New preset name"), &newName);
        ImGui::SameLine();
        if (ImGui::Button(T("Save")) && !newName.empty()) {
            savePreset(newName);
            newName.clear();
        }
        for (auto& p : presetList) {
            ImGui::PushID(p.c_str());
            if (ImGui::Button(T("Load"))) loadPreset(p);
            ImGui::SameLine();
            if (ImGui::Button(T("Delete"))) {
                std::error_code ec;
                fs::remove(dataDir() / L"presets" / u8path(p + ".json"), ec);
                refreshPresets();
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(p.c_str());
            ImGui::PopID();
        }
        ImGui::TextDisabled(T("Includes background, lights, camera, effects and posture."));
    }
    if (ImGui::CollapsingHeader(T("Hotkeys"))) {
        ImGui::TextWrapped(T("Global hotkeys: they work while gaming too (e.g. outfit toggle, hand gesture, camera view)."));
        const char* actions[] = {T("VRChat parameter"), T("Left hand gesture"), T("Right hand gesture"), T("Camera view"), T("Preset"), T("Recenter tracking"), T("Expression (VRM)")};
        const char* actionIds[] = {"param", "gestureL", "gestureR", "camera", "preset", "recenter", "expression"};
        int removeAt = -1;
        bool changed = false;
        for (size_t i = 0; i < settings.hotkeys.size(); i++) {
            Hotkey& k = settings.hotkeys[i];
            ImGui::PushID((int)i);
            std::string kn = k.vk ? std::string(k.ctrl ? "Ctrl+" : "") + (k.alt ? "Alt+" : "") + (k.shift ? "Shift+" : "") + keyName(k.vk) : T("(none)");
            if (capturingHotkey && captureIndex == (int)i) kn = T("Press a key…");
            if (ImGui::Button((kn + "##key").c_str(), ImVec2(150 * uiScale, 0))) {
                capturingHotkey = true;
                captureIndex = (int)i;
            }
            ImGui::SameLine();
            int ai = 0;
            for (int a = 0; a < 7; a++)
                if (k.action == actionIds[a]) ai = a;
            ImGui::SetNextItemWidth(140 * uiScale);
            if (ImGui::Combo("##act", &ai, actions, 7)) k.action = actionIds[ai];
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeAt = (int)i;
            ImGui::Indent();
            if (k.action == "param") {
                if (ImGui::BeginCombo(T("Parameter"), k.target.empty() ? T("(choose)") : k.target.c_str())) {
                    if (avatar)
                        for (auto& p : avatar->params)
                            if (p.menu && ImGui::Selectable(p.name.c_str(), k.target == p.name)) k.target = p.name;
                    ImGui::EndCombo();
                }
                ImGui::InputFloat(T("Value"), &k.value, 1, 1, "%.2f");
                ImGui::Checkbox(T("Toggle (on/off)"), &k.toggle);
            } else if (k.action == "gestureL" || k.action == "gestureR") {
                int g = (int)k.value;
                if (ImGui::Combo(T("Gesture"), &g, gestureNames(), 8)) k.value = (float)g;
                ImGui::Checkbox(T("Toggle (on/off)"), &k.toggle);
            } else if (k.action == "camera") {
                int c = (int)k.value;
                const char* pr[] = {T("Face"), T("Chest"), T("Waist"), T("Full body")};
                if (ImGui::Combo(T("View"), &c, pr, 4)) k.value = (float)c;
            } else if (k.action == "expression") {
                if (ImGui::BeginCombo(T("Expression"), k.target.empty() ? T("(choose)") : k.target.c_str())) {
                    if (avatar)
                        for (auto& e : avatar->vrmExpressions)
                            if (ImGui::Selectable(e.name.c_str(), k.target == e.name)) k.target = e.name;
                    ImGui::EndCombo();
                }
                ImGui::Checkbox(T("Toggle (on/off)"), &k.toggle);
            } else if (k.action == "preset") {
                if (ImGui::BeginCombo(T("Preset"), k.target.c_str())) {
                    for (auto& p : presetList)
                        if (ImGui::Selectable(p.c_str(), k.target == p)) k.target = p;
                    ImGui::EndCombo();
                }
            }
            ImGui::Unindent();
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeAt >= 0) {
            settings.hotkeys.erase(settings.hotkeys.begin() + removeAt);
            changed = true;
        }
        if (ImGui::Button(T("+ New hotkey"))) {
            settings.hotkeys.push_back(Hotkey{0, false, false, false, "param", "", 1, true});
            capturingHotkey = true;
            captureIndex = (int)settings.hotkeys.size() - 1;
        }
        if (changed) registerHotkeys();
    }
    if (ImGui::CollapsingHeader("Program", ImGuiTreeNodeFlags_DefaultOpen)) {
        // a címke mindkét nyelven olvasható, hogy rossz nyelv mellett is megtalálható legyen
        if (ImGui::BeginCombo("Language / Nyelv", languageName(settings.language))) {
            for (int l = 0; l < kLangCount; l++)
                if (ImGui::Selectable(languageName(l), settings.language == l)) settings.language = l;
            ImGui::EndCombo();
        }
        if (slider(T("UI scale"), &settings.uiScale, 0.75f, 2.0f, "%.2f", 1.0f)) {}
        ImGui::TextDisabled(T("(applies after restart)"));
        if (ImGui::Button(T("Log"))) showLog = true;
        ImGui::SameLine();
        if (ImGui::Button(T("Data folder"))) ShellExecuteW(nullptr, L"open", dataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::TextDisabled("AvatarCam %s – Dear ImGui %s", AVATARCAM_VERSION, IMGUI_VERSION);
        ImGui::Checkbox(T("Check for updates at startup"), &settings.checkUpdates);
        ImGui::SameLine();
        if (ImGui::SmallButton(T("Check now"))) {
            updateManualCheck = true;
            updater->checkAsync(0);
        }
        switch (updater->state()) {
            case Updater::Checking: ImGui::TextDisabled("%s", T("Checking for updates…")); break;
            case Updater::UpToDate: ImGui::TextDisabled("%s", T("You have the latest version.")); break;
            case Updater::Available:
            case Updater::Downloading:
            case Updater::Downloaded:
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.6f, 1), T("Version %s is available."), updater->info().version.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(T("Details"))) updatePopupWanted = true;
                break;
            case Updater::Failed: ImGui::TextDisabled(T("Update check failed: %s"), updater->error().c_str()); break;
            default: break;
        }
    }
}

// ===========================================================================
namespace {
// A kiadási megjegyzés (markdown) egyszerű megjelenítése: címsorok, felsorolás, félkövér/kód/hivatkozás jelölés nélkül
void renderNotes(const std::string& md) {
    size_t pos = 0;
    while (pos <= md.size()) {
        size_t nl = md.find('\n', pos);
        std::string line = md.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? md.size() + 1 : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        // inline jelölések
        std::string t;
        for (size_t i = 0; i < line.size(); i++) {
            if (line.compare(i, 2, "**") == 0) {
                i++;
                continue;
            }
            if (line[i] == '`') continue;
            if (line[i] == '[') {
                size_t close = line.find("](", i);
                size_t end = close == std::string::npos ? std::string::npos : line.find(')', close);
                if (end != std::string::npos) {
                    t += line.substr(i + 1, close - i - 1);
                    i = end;
                    continue;
                }
            }
            t += line[i];
        }
        std::string s = trim(t);
        if (s.empty()) {
            ImGui::Spacing();
        } else if (s == "---") {
            ImGui::Separator();
        } else if (s[0] == '#') {
            size_t k = s.find_first_not_of("# ");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.85f, 1.0f, 1));
            ImGui::TextWrapped("%s", k == std::string::npos ? "" : s.c_str() + k);
            ImGui::PopStyleColor();
        } else if (s.size() > 1 && (s[0] == '-' || s[0] == '*') && s[1] == ' ') {
            ImGui::Bullet();
            ImGui::TextWrapped("%s", s.c_str() + 2);
        } else {
            ImGui::TextWrapped("%s", s.c_str());
        }
    }
}
}  // namespace

void App::uiUpdatePopup() {
    Updater::State st = updater->state();
    const char* popupId = "###update";
    if (st == Updater::Available && !updateInstalling) {
        static bool shownOnce = false;
        std::string v = updater->info().version;
        // automatikusan egyszer jelenik meg (kihagyott verziónál csak kézi keresésre)
        if (!shownOnce && (v != settings.skippedVersion || updateManualCheck)) updatePopupWanted = true;
        shownOnce = true;
    }
    if (st == Updater::UpToDate || st == Updater::Failed) updateManualCheck = false;
    if (updatePopupWanted) {
        ImGui::OpenPopup(popupId);
        updatePopupWanted = false;
    }
    ImGui::SetNextWindowSize(ImVec2(600 * uiScale, 0), ImGuiCond_Appearing);
    std::string title = std::string(T("Update available")) + popupId;
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    UpdateInfo ui = updater->info();
    ImGui::Text(T("AvatarCam %s is available. You have version %s."), ui.version.c_str(), Updater::currentVersion().c_str());
    ImGui::SeparatorText(T("What's new"));
    ImGui::BeginChild("notes", ImVec2(0, 240 * uiScale), ImGuiChildFlags_Borders);
    std::string notes = releaseNotesForLanguage(ui.notes, language() == kLangHungarian);
    if (notes.empty()) ImGui::TextDisabled("%s", T("(no description)"));
    else renderNotes(notes);
    ImGui::EndChild();
    ImGui::Spacing();
    if (updateInstalling) {
        ImGui::TextWrapped("%s", T("Installing the update… AvatarCam closes now and restarts when the installation is done."));
    } else if (st == Updater::Downloading) {
        float p = updater->progress();
        ImGui::ProgressBar(p, ImVec2(-1, 0), format(T("Downloading… %.0f%%"), p * 100).c_str());
        if (ImGui::Button(T("Cancel"))) updater->cancelDownload();
    } else if (st == Updater::Downloaded) {
        if (updater->launchInstaller(language() == kLangHungarian)) {
            updateInstalling = true;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    } else {
        if (st == Updater::Failed && !updater->error().empty())
            ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), T("Update failed: %s"), updater->error().c_str());
        bool canInstall = !ui.assetUrl.empty();
        if (canInstall) {
            if (ImGui::Button(T("Update now"))) updater->downloadAsync();
            ImGui::SetItemTooltip("%s", T("Downloads and installs the update, then restarts AvatarCam. Your settings and avatars are kept."));
        } else {
            if (ImGui::Button(T("Open download page"))) ShellExecuteW(nullptr, L"open", widen(ui.pageUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        if (ImGui::Button(T("Later"))) ImGui::CloseCurrentPopup();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize(T("Skip this version")).x - ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::SmallButton(T("Skip this version"))) {
            settings.skippedVersion = ui.version;
            ImGui::CloseCurrentPopup();
        }
        if (canInstall && st == Updater::Failed) {
            if (ImGui::SmallButton(T("Open download page"))) ShellExecuteW(nullptr, L"open", widen(ui.pageUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
    ImGui::EndPopup();
}

// ===========================================================================
// Önteszt (AvatarCam.exe --ui-selftest): fej nélküli ImGui környezetben szimulált egérrel ellenőrzi, hogy a
// csúszkára dupla kattintva szöveges bevitel nyílik, és az érték nem ugrik el.
int uiSelfTest() {
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60;
    io.IniFilename = nullptr;
    float v = 72;
    ImVec2 rmin, rmax;
    ImGuiID sid = 0;
    auto frame = [&](ImVec2 pos, int button) {
        io.AddMousePosEvent(pos.x, pos.y);
        if (button >= 0) io.AddMouseButtonEvent(0, button == 1);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 200));
        ImGui::Begin("selftest");
        slider("Arms down", &v, 0, 90, "%.0f", 72);
        sid = ImGui::GetItemID();
        rmin = ImGui::GetItemRectMin();
        rmax = ImGui::GetItemRectMax();
        ImGui::End();
        ImGui::Render();
    };
    frame(ImVec2(-100, -100), -1);
    ImVec2 at(rmin.x + 25, (rmin.y + rmax.y) * 0.5f);
    frame(at, -1);
    frame(at, 1);  // első kattintás
    frame(at, 0);
    frame(at, 1);  // második kattintás (dupla)
    frame(at, 0);
    for (int i = 0; i < 4; i++) frame(at, -1);
    ImGuiContext& g = *GImGui;
    bool typing = g.ActiveId == sid && g.TempInputId == sid;
    float after = v;
    // gépelés: "45" + Enter
    io.AddInputCharactersUTF8("45");
    frame(at, -1);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(at, -1);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(at, -1);
    frame(at, -1);
    float typed = v;
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(prev);
    printf("double-click -> text input: %s, value after double-click: %.1f (expected 72), after typing 45+Enter: %.1f\n", typing ? "YES" : "NO", after, typed);
    return typing && std::fabs(after - 72) < 0.01f && std::fabs(typed - 45) < 0.01f ? 0 : 1;
}
