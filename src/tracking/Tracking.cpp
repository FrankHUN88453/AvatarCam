#include "tracking/Tracking.h"
#include "tracking/FaceTracker.h"
#include "tracking/HandTracker.h"
#include "tracking/Webcam.h"
#include "tracking/Mic.h"
#include "tracking/Vmc.h"
#include "app/Settings.h"
#include "gfx/Gfx.h"
#include <thread>
#include "core/I18n.h"

namespace {
struct Starter {
    std::thread th;
    std::atomic<bool> busy{false};
    std::string key;
    double lastTry = -100;
    void join() {
        if (th.joinable()) th.join();
    }
    ~Starter() { join(); }
};
Starter gCamStart, gMicStart;
std::string gCamErr, gFaceErr, gHandErr, gMicErr, gVmcErr;
double gFaceTry = -100, gHandTry = -100;
std::string gVmcKey;
// kamera előnézet
ComPtr<ID3D11Texture2D> gPrevTex;
ComPtr<ID3D11ShaderResourceView> gPrevSrv;
int gPrevW = 0, gPrevH = 0;
}  // namespace

Tracking::Tracking()
    : cam_(std::make_unique<Webcam>()), hand_(std::make_unique<HandTracker>()), face_(std::make_unique<FaceTracker>()), mic_(std::make_unique<MicLipSync>()),
      vmc_(std::make_unique<VmcReceiver>()) {}

Tracking::~Tracking() {
    gCamStart.join();
    gMicStart.join();
    face_->stop();
    hand_->stop();
    cam_->close();
    mic_->stop();
    vmc_->stop();
}

std::vector<std::string> Tracking::cameras() { return enumerateCameras(); }
std::vector<std::string> Tracking::microphones() { return enumerateMicrophones(); }

std::string Tracking::faceStatus() const {
    if (gCamStart.busy) return T("Starting camera…");
    if (!gCamErr.empty() && !cam_->isOpen()) return T("Error: ") + gCamErr;
    if (!gFaceErr.empty() && !face_->running()) return T("Error: ") + gFaceErr;
    return face_->status();
}
float Tracking::faceMs() const { return face_ ? face_->procMs() : 0.0f; }
float Tracking::handMs() const { return hand_ ? hand_->procMs() : 0.0f; }
std::string Tracking::handStatus() const {
    if (gCamStart.busy) return T("Starting camera…");
    if (!gCamErr.empty() && !cam_->isOpen()) return T("Error: ") + gCamErr;
    if (!gHandErr.empty() && !hand_->running()) return T("Error: ") + gHandErr;
    return hand_->status();
}
std::string Tracking::micStatus() const {
    if (gMicStart.busy) return T("Starting…");
    if (!gMicErr.empty() && !mic_->running()) return T("Error: ") + gMicErr;
    return mic_->running() ? mic_->status : T("Not enabled");
}
std::string Tracking::vmcStatus() const {
    if (!gVmcErr.empty() && !vmc_->running()) return T("Error: ") + gVmcErr;
    if (!vmc_->running()) return T("Not enabled");
    return vmc_->active() ? vmc_->status : format(T("Waiting for data…"));
}
float Tracking::micLevel() const { return mic_->rawLevel() * 8.0f; }

void Tracking::recenter() { wantRecenter_ = true; }

void Tracking::update(const TrackingSettings& s, Gfx& gfx, float dt) {
    (void)gfx;
    double now = nowSeconds();
    // --- kamera, arc- és kézkövetés (a kamera megnyitása háttérszálon, hogy a felület ne akadjon) ---
    std::string ckey = s.cameraName + "|" + std::to_string(s.cameraIndex) + "|" + std::to_string(s.cameraWidth) + "x" + std::to_string(s.cameraHeight);
    bool wantCam = s.faceEnabled || s.handEnabled;
    if (wantCam) {
        bool needOpen = !gCamStart.busy && (!cam_->isOpen() || gCamStart.key != ckey) && (gCamStart.key != ckey || now - gCamStart.lastTry > 5);
        if (needOpen) {
            gCamStart.join();
            gCamStart.key = ckey;
            gCamStart.lastTry = now;
            gCamStart.busy = true;
            gCamStart.th = std::thread([this, s] {
                face_->stop();
                hand_->stop();
                cam_->close();
                std::string err;
                if (!cam_->open(s.cameraName, s.cameraIndex, s.cameraWidth, s.cameraHeight, err)) gCamErr = err;
                else gCamErr.clear();
                gFaceTry = gHandTry = -100;
                gCamStart.busy = false;
            });
        }
    } else if (cam_->isOpen() && !gCamStart.busy) {
        face_->stop();
        hand_->stop();
        cam_->close();
        gCamStart.key.clear();
    }
    if (!gCamStart.busy && cam_->isOpen()) {
        if (s.faceEnabled && !face_->running() && now - gFaceTry > 5) {
            gFaceTry = now;
            std::string err;
            if (!face_->start(cam_.get(), err)) gFaceErr = err;
            else gFaceErr.clear();
        } else if (!s.faceEnabled && face_->running())
            face_->stop();
        if (s.handEnabled && !hand_->running() && now - gHandTry > 5) {
            gHandTry = now;
            std::string err;
            if (!hand_->start(cam_.get(), err)) gHandErr = err;
            else gHandErr.clear();
        } else if (!s.handEnabled && hand_->running())
            hand_->stop();
    }
    // --- mikrofon ---
    std::string mkey = s.micName;
    if (s.micEnabled) {
        bool needStart = !gMicStart.busy && (!mic_->running() || gMicStart.key != mkey) && (gMicStart.key != mkey || now - gMicStart.lastTry > 5);
        if (needStart) {
            gMicStart.join();
            gMicStart.key = mkey;
            gMicStart.lastTry = now;
            gMicStart.busy = true;
            gMicStart.th = std::thread([this, mkey] {
                std::string err;
                if (!mic_->start(mkey, err)) gMicErr = err;
                else gMicErr.clear();
                gMicStart.busy = false;
            });
        }
        mic_->setParams(s.micGain, s.micThreshold);
    } else if (mic_->running() && !gMicStart.busy) {
        mic_->stop();
        gMicStart.key.clear();
    }
    // --- VMC ---
    std::string vkey = std::to_string(s.vmcPort);
    if (s.vmcEnabled && (!vmc_->running() || gVmcKey != vkey)) {
        if (gVmcKey != vkey || gVmcErr.empty()) {
            std::string err;
            gVmcKey = vkey;
            if (!vmc_->start(s.vmcPort, err)) gVmcErr = err;
            else gVmcErr.clear();
        }
    } else if (!s.vmcEnabled && vmc_->running()) {
        vmc_->stop();
        gVmcKey.clear();
    }

    // --- képkocka összeállítása ---
    TrackingFrame f;
    f.time = now;
    FaceResult fr = face_->running() ? face_->latest() : FaceResult{};
    bool faceOk = fr.valid && now - fr.time < 0.6;
    float k = 1.0f - std::exp(-lerpf(40.0f, 3.0f, clampf(s.smoothing, 0, 0.98f)) * dt);
    if (faceOk) {
        Quat q = fr.head;
        if (s.mirror) q = Quat(q.x, -q.y, -q.z, q.w);
        if (wantRecenter_) {
            calib_ = q;
            wantRecenter_ = false;
        }
        Quat rel = (q * calib_.conj()).normalized();
        f.face = true;
        f.head = rel;
        float openL = s.mirror ? fr.eyeOpenR : fr.eyeOpenL, openR = s.mirror ? fr.eyeOpenL : fr.eyeOpenR;
        if (std::fabs(openL - openR) < 0.3f) openL = openR = (openL + openR) * 0.5f;  // összekapcsolt pislogás
        auto blinkCurve = [&](float open) { return smoothstepf(s.blinkThreshold - 0.25f, s.blinkThreshold + 0.25f, 1.0f - open); };
        f.blinkL = blinkCurve(openL);
        f.blinkR = blinkCurve(openR);
        if (fr.gazeValid) {
            f.eyeYaw = (s.mirror ? -fr.gazeX : fr.gazeX) * 0.35f;
            f.eyePitch = -fr.gazeY * 0.25f;
        }
        float open = fr.mouthOpen, round = fr.mouthRound, wide = std::max(0.0f, fr.mouthWide);
        f.mouthOpen = open;
        f.visemes[10] = open * (1 - round * 0.7f);
        f.visemes[13] = open * round * 0.8f;
        f.visemes[14] = round * open * 0.5f;
        f.visemes[11] = open * wide * 0.5f;
        float smL = s.mirror ? fr.smileR : fr.smileL, smR = s.mirror ? fr.smileL : fr.smileR;
        float brL = s.mirror ? fr.browR : fr.browL, brR = s.mirror ? fr.browL : fr.browR;
        f.arkit["jawopen"] = open;
        f.arkit["mouthfunnel"] = round * open;
        f.arkit["mouthpucker"] = round * (1 - open);
        f.arkit["mouthsmileleft"] = smL;
        f.arkit["mouthsmileright"] = smR;
        f.arkit["mouthstretchleft"] = wide * 0.5f;
        f.arkit["mouthstretchright"] = wide * 0.5f;
        f.arkit["browinnerup"] = saturate((brL + brR) * 0.5f);
        f.arkit["browouterupleft"] = saturate(brL);
        f.arkit["browouterupright"] = saturate(brR);
        f.arkit["browdownleft"] = saturate(-brL);
        f.arkit["browdownright"] = saturate(-brR);
        f.arkit["eyeblinkleft"] = f.blinkL;
        f.arkit["eyeblinkright"] = f.blinkR;
        f.arkit["eyelookupleft"] = f.arkit["eyelookupright"] = saturate(f.eyePitch / 0.25f);
        f.arkit["eyelookdownleft"] = f.arkit["eyelookdownright"] = saturate(-f.eyePitch / 0.25f);
        f.arkit["eyelookoutleft"] = f.arkit["eyelookinright"] = saturate(f.eyeYaw / 0.35f);
        f.arkit["eyelookinleft"] = f.arkit["eyelookoutright"] = saturate(-f.eyeYaw / 0.35f);
    }
    // kezek
    if (hand_->running()) {
        HandResult hr = hand_->latest();
        // a fej (orr) helye kamera térben: az arcmodell egysége ~0.15 m; ha nincs arc, a kép közepe 0.6 m-re
        Vec3 headCam = faceOk ? fr.trans * 0.15f : Vec3(0, -0.05f, 0.6f);
        // a tükrözés a kezeknél külön állítható; a kéz térbeli képét és az avatár oldalát együtt fordítjuk,
        // különben a csukló kifordulna
        bool hm = s.handMirror;
        auto toView = [&](const Vec3& v) { return Vec3(hm ? -v.x : v.x, -v.y, -v.z); };
        for (int u = 0; u < 2; u++) {
            const HandObs& ho = hr.hands[u];
            if (!ho.valid || now - hr.time > 0.5) continue;
            // u: a felhasználó bal (0) / jobb (1) keze; tükrözéskor az avatár másik keze
            int av = hm ? 1 - u : u;
            TrackingFrame::Hand& th = f.hands[av];
            th.valid = true;
            th.lastSeen = now;
            th.rel = toView(ho.wristCam - headCam);
            th.dir = normalize(toView(ho.world[9] - ho.world[0]));
            th.side = normalize(toView(ho.world[5] - ho.world[17]));
            // ízületi szögek a világ pontokból
            static const int chains[5][5] = {{0, 1, 2, 3, 4}, {0, 5, 6, 7, 8}, {0, 9, 10, 11, 12}, {0, 13, 14, 15, 16}, {0, 17, 18, 19, 20}};
            static const float neutral[5][3] = {{0.55f, 0.15f, 0.1f}, {0.2f, 0.05f, 0.05f}, {0.2f, 0.05f, 0.05f}, {0.2f, 0.05f, 0.05f}, {0.25f, 0.05f, 0.05f}};
            for (int fi = 0; fi < 5; fi++)
                for (int k = 0; k < 3; k++) {
                    Vec3 va = ho.world[chains[fi][k + 1]] - ho.world[chains[fi][k]];
                    Vec3 vb = ho.world[chains[fi][k + 2]] - ho.world[chains[fi][k + 1]];
                    float c = dot(normalize(va), normalize(vb));
                    float ang = std::acos(clampf(c, -1, 1));
                    th.finger[fi][k] = clampf(ang - neutral[fi][k], 0, 1.75f);
                }
        }
    }
    // VMC (telefonos/külső követő)
    std::unordered_map<std::string, float> blends;
    Quat vmcHead;
    bool vmcHasHead = false;
    if (vmc_->running() && vmc_->get(blends, vmcHead, vmcHasHead)) {
        if (!faceOk && vmcHasHead) {
            f.face = true;
            f.head = vmcHead;  // a VMC adat már avatár térben van, nem tükrözzük
        }
        for (auto& [n, v] : blends) {
            if (n == "a") f.visemes[10] = std::max(f.visemes[10], v);
            else if (n == "i") f.visemes[12] = std::max(f.visemes[12], v);
            else if (n == "u") f.visemes[14] = std::max(f.visemes[14], v);
            else if (n == "e") f.visemes[11] = std::max(f.visemes[11], v);
            else if (n == "o") f.visemes[13] = std::max(f.visemes[13], v);
            else if (n == "blink") f.blinkL = std::max(f.blinkL, v), f.blinkR = std::max(f.blinkR, v), f.face = true;
            else if (n == "blink_l") f.blinkL = std::max(f.blinkL, v), f.face = true;
            else if (n == "blink_r") f.blinkR = std::max(f.blinkR, v), f.face = true;
            else {
                // ARKit (perfect sync) nevek kisbetűvel, aláhúzás nélkül
                std::string key;
                for (char c : n)
                    if (c != '_') key += c;
                f.arkit[key] = v;
            }
        }
    }
    // mikrofon
    if (mic_->running()) {
        float vis[15], lvl;
        mic_->get(vis, lvl);
        for (int i = 0; i < 15; i++) f.visemes[i] = std::max(f.visemes[i], vis[i]);
        f.voice = lvl;
        f.mouthOpen = std::max(f.mouthOpen, lvl);
        if (lvl > 0.05f) f.arkit["jawopen"] = std::max(f.arkit.count("jawopen") ? f.arkit["jawopen"] : 0.0f, lvl * 0.7f);
    }

    // simítás
    std::lock_guard<std::mutex> lk(mx_);
    TrackingFrame& o = smooth_;
    if (f.face) {
        o.head = o.face ? slerp(o.head, f.head, k) : f.head;
        o.eyeYaw = damp(o.eyeYaw, f.eyeYaw, 25, dt);
        o.eyePitch = damp(o.eyePitch, f.eyePitch, 25, dt);
    } else {
        // elveszett arc: lassan vissza alaphelyzetbe
        o.head = slerp(o.head, Quat(), 1.0f - std::exp(-2.0f * dt));
        o.eyeYaw = damp(o.eyeYaw, 0, 5, dt);
        o.eyePitch = damp(o.eyePitch, 0, 5, dt);
    }
    o.face = f.face;
    o.blinkL = f.blinkL;  // a pislogást nem simítjuk (gyors mozgás)
    o.blinkR = f.blinkR;
    o.mouthOpen = damp(o.mouthOpen, f.mouthOpen, 30, dt);
    for (int i = 0; i < 15; i++) o.visemes[i] = damp(o.visemes[i], f.visemes[i], 30, dt);
    for (auto& [n, v] : f.arkit) {
        float& cur = o.arkit[n];
        cur = damp(cur, v, 30, dt);
    }
    if (!f.face && !mic_->running()) o.arkit.clear();
    o.voice = f.voice;
    o.time = now;
    float hk = 1.0f - std::exp(-lerpf(35.0f, 4.0f, clampf(s.handSmoothing, 0, 0.98f)) * dt);
    for (int i = 0; i < 2; i++) {
        TrackingFrame::Hand& oh = o.hands[i];
        const TrackingFrame::Hand& nh = f.hands[i];
        if (nh.valid) {
            if (!oh.valid) {
                oh = nh;
            } else {
                oh.rel = lerp(oh.rel, nh.rel, hk);
                oh.dir = normalize(lerp(oh.dir, nh.dir, hk));
                oh.side = normalize(lerp(oh.side, nh.side, hk));
                for (int a = 0; a < 5; a++)
                    for (int b = 0; b < 3; b++) oh.finger[a][b] = lerpf(oh.finger[a][b], nh.finger[a][b], std::min(1.0f, hk * 1.5f));
            }
            oh.valid = true;
            oh.lastSeen = now;
        } else if (oh.valid && now - oh.lastSeen > 0.35)
            oh.valid = false;  // rövid kimaradásokat áthidalunk
    }
}

TrackingFrame Tracking::frame() const {
    std::lock_guard<std::mutex> lk(mx_);
    return smooth_;
}

ID3D11ShaderResourceView* Tracking::cameraPreview(Gfx& gfx) {
    CameraFrame fr;
    FaceResult res;
    if (!cam_->isOpen()) return nullptr;
    if (!face_->running() || !face_->previewFrame(fr, res)) {
        res = FaceResult{};
        if (!cam_->latest(fr, 0)) return nullptr;
    }
    // arcpontok rajzolása
    auto dot = [&](float x, float y, uint8_t r, uint8_t g, uint8_t b) {
        int cx = (int)x, cy = (int)y;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
                int px = cx + dx, py = cy + dy;
                if (px < 0 || py < 0 || px >= fr.w || py >= fr.h) continue;
                uint8_t* p = &fr.bgra[((size_t)py * fr.w + px) * 4];
                p[0] = b, p[1] = g, p[2] = r, p[3] = 255;
            }
    };
    if (res.valid) {
        for (auto& p : res.lms) dot(p.x, p.y, 40, 220, 255);
        if (res.gazeValid)
            for (auto& p : res.pupils) dot(p.x, p.y, 255, 60, 60);
    }
    if (hand_->running()) {
        HandResult hr = hand_->latest();
        static const int bones[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {0, 5}, {5, 6}, {6, 7}, {7, 8}, {5, 9}, {9, 10}, {10, 11}, {11, 12},
                                       {9, 13}, {13, 14}, {14, 15}, {15, 16}, {13, 17}, {0, 17}, {17, 18}, {18, 19}, {19, 20}};
        for (int u = 0; u < 2; u++) {
            const HandObs& h = hr.hands[u];
            if (!h.valid) continue;
            uint8_t r = u == 1 ? 255 : 120, g = u == 1 ? 170 : 255, b = u == 1 ? 60 : 120;
            for (auto& bn : bones)
                for (int k = 0; k <= 8; k++) {
                    Vec2 pt = h.lms[bn[0]] + (h.lms[bn[1]] - h.lms[bn[0]]) * (k / 8.0f);
                    dot(pt.x, pt.y, r, g, b);
                }
        }
    }
    if (!gPrevTex || gPrevW != fr.w || gPrevH != fr.h) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = fr.w;
        d.Height = fr.h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        gPrevTex.Reset();
        gPrevSrv.Reset();
        if (FAILED(gfx.dev()->CreateTexture2D(&d, nullptr, &gPrevTex))) return nullptr;
        gfx.dev()->CreateShaderResourceView(gPrevTex.Get(), nullptr, &gPrevSrv);
        gPrevW = fr.w, gPrevH = fr.h;
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(gfx.ctx()->Map(gPrevTex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        for (int y = 0; y < fr.h; y++) {
            uint8_t* dst = (uint8_t*)m.pData + (size_t)y * m.RowPitch;
            memcpy(dst, &fr.bgra[(size_t)y * fr.w * 4], (size_t)fr.w * 4);
            for (int x = 0; x < fr.w; x++) dst[x * 4 + 3] = 255;
        }
        gfx.ctx()->Unmap(gPrevTex.Get(), 0);
    }
    return gPrevSrv.Get();
}
