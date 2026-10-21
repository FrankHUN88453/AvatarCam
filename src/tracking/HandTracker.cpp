// Kézkövetés: MediaPipe tenyér-detektor + 21 pontos kézmodell (OpenCV Model Zoo ONNX, Apache 2.0)
#include "tracking/HandTracker.h"
#include <windows.h>
#include <algorithm>
#include "core/I18n.h"

namespace {
// Bilineáris mintavétel BGRA képből -> RGB 0..1 (képen kívül fekete)
inline void sampleRGB01(const CameraFrame& f, float x, float y, float* out) {
    if (x < 0 || y < 0 || x > f.w - 1 || y > f.h - 1) {
        out[0] = out[1] = out[2] = 0;
        return;
    }
    int x0 = std::min((int)x, f.w - 2), y0 = std::min((int)y, f.h - 2);
    float fx = x - x0, fy = y - y0;
    const uint8_t* p00 = &f.bgra[((size_t)y0 * f.w + x0) * 4];
    const uint8_t* p10 = p00 + 4;
    const uint8_t* p01 = p00 + (size_t)f.w * 4;
    const uint8_t* p11 = p01 + 4;
    for (int c = 0; c < 3; c++) {
        int bc = 2 - c;
        float a = p00[bc] + (p10[bc] - p00[bc]) * fx;
        float b = p01[bc] + (p11[bc] - p01[bc]) * fx;
        out[c] = (a + (b - a) * fy) / 255.0f;
    }
}
inline float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }
inline float wrapAngle(float a) { return a - 2 * kPi * std::floor((a + kPi) / (2 * kPi)); }

float iou(const Vec4& a, const Vec4& b) {
    float x1 = std::max(a.x, b.x), y1 = std::max(a.y, b.y), x2 = std::min(a.z, b.z), y2 = std::min(a.w, b.w);
    float inter = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
    float u = (a.z - a.x) * (a.w - a.y) + (b.z - b.x) * (b.w - b.y) - inter;
    return u > 0 ? inter / u : 0;
}
}  // namespace

HandTracker::~HandTracker() { stop(); }

bool HandTracker::loadModels(std::string& err) {
    if (lm_.loaded()) return true;
    fs::path dir = exeDir() / L"models";
    if (!palm_.load(dir / L"palm_detection_mediapipe_2023feb.onnx", 1, err)) return false;
    if (!lm_.load(dir / L"handpose_estimation_mediapipe_2023feb.onnx", 1, err)) return false;
    // SSD horgonyok (192x192): 24x24 rács 2 horgonnyal, majd 12x12 rács 6 horgonnyal
    anchors_.clear();
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 24; x++)
            for (int k = 0; k < 2; k++) anchors_.push_back(Vec2((x + 0.5f) / 24, (y + 0.5f) / 24));
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 12; x++)
            for (int k = 0; k < 6; k++) anchors_.push_back(Vec2((x + 0.5f) / 12, (y + 0.5f) / 12));
    std::string names;
    for (auto& n : lm_.outputNames()) names += n + " ";
    logMsg("Hand model outputs: %s", names.c_str());
    return true;
}

bool HandTracker::start(Webcam* cam, std::string& err) {
    stop();
    if (!loadModels(err)) {
        status_ = T("Error: ") + err;
        return false;
    }
    cam_ = cam;
    for (auto& t : tracks_) t = Track{};
    stop_ = false;
    running_ = true;
    status_ = T("Searching for hands…");
    th_ = std::thread([this] { loop(); });
    return true;
}

void HandTracker::stop() {
    stop_ = true;
    if (th_.joinable()) th_.join();
    running_ = false;
    status_ = T("Not enabled");
    std::lock_guard<std::mutex> lk(mx_);
    result_ = HandResult{};
}

HandResult HandTracker::latest() {
    std::lock_guard<std::mutex> lk(mx_);
    return result_;
}

std::string HandTracker::status() {
    std::lock_guard<std::mutex> lk(mx_);
    if (!running_) return status_;
    return format("%s · %.1f ms", status_.c_str(), procMs_);
}

void HandTracker::loop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    uint64_t last = 0;
    CameraFrame f;
    while (!stop_) {
        if (!cam_ || !cam_->latest(f, last)) {
            if (cam_ && !cam_->isOpen()) break;
            if (cam_) cam_->waitFrame(last, 50);
            else Sleep(10);
            continue;
        }
        last = f.index;
        double t0 = nowSeconds();
        HandResult r;
        std::string err;
        process(f, r, err);
        r.time = f.time;
        float ms = (float)((nowSeconds() - t0) * 1000);
        int n = r.hands[0].valid + r.hands[1].valid;
        std::lock_guard<std::mutex> lk(mx_);
        procMs_ = procMs_ * 0.9f + ms * 0.1f;
        result_ = r;
        status_ = n == 0 ? T("Searching for hands…") : n == 1 ? T("Tracking 1 hand") : T("Tracking 2 hands");
    }
    running_ = false;
}

// ---------------------------------------------------------------------------
void HandTracker::detectPalms(const CameraFrame& f, std::vector<Roi>& out) {
    const int S = 192;
    float ratio = std::min((float)S / f.w, (float)S / f.h);
    float rw = f.w * ratio, rh = f.h * ratio;
    float padX = (S - rw) * 0.5f, padY = (S - rh) * 0.5f;
    std::vector<float> in((size_t)S * S * 3);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float sx = (x + 0.5f - padX) / ratio - 0.5f, sy = (y + 0.5f - padY) / ratio - 0.5f;
            sampleRGB01(f, sx, sy, &in[((size_t)y * S + x) * 3]);
        }
    std::vector<OnnxOutput> outs;
    if (!palm_.run(in, {1, S, S, 3}, outs) || outs.size() < 2) return;
    const OnnxOutput* reg = nullptr;
    const OnnxOutput* sc = nullptr;
    for (auto& o : outs) {
        if (!o.shape.empty() && o.shape.back() == 18) reg = &o;
        else if (!o.shape.empty() && o.shape.back() == 1) sc = &o;
    }
    if (!reg || !sc || reg->data.size() < anchors_.size() * 18) {
        static bool once = false;
        if (!once) {
            once = true;
            for (auto& o : outs) {
                std::string sh;
                for (auto d : o.shape) sh += std::to_string(d) + "x";
                logMsg("Palm detector output: %s (%zu values)", sh.c_str(), o.data.size());
            }
        }
        return;
    }
    if (getenv("AVATARCAM_HANDDEBUG")) {
        float mx = -1e9f;
        for (size_t i = 0; i < anchors_.size(); i++) mx = std::max(mx, sc->data[i]);
        logMsg("palm: max score %.3f (sigmoid %.3f)", mx, sigmoid(mx));
    }
    float scale = (float)std::max(f.w, f.h);
    float biasX = padX / ratio, biasY = padY / ratio;
    struct Det {
        float score;
        Vec4 box;
        Vec2 kp[7];
    };
    std::vector<Det> dets;
    for (size_t i = 0; i < anchors_.size(); i++) {
        float s = sigmoid(sc->data[i]);
        if (s < 0.5f) continue;
        const float* r = &reg->data[i * 18];
        Vec2 a = anchors_[i];
        float cx = r[0] / S + a.x, cy = r[1] / S + a.y, w = r[2] / S, h = r[3] / S;
        Det d;
        d.score = s;
        d.box = Vec4((cx - w / 2) * scale - biasX, (cy - h / 2) * scale - biasY, (cx + w / 2) * scale - biasX, (cy + h / 2) * scale - biasY);
        for (int k = 0; k < 7; k++) d.kp[k] = Vec2((r[4 + k * 2] / S + a.x) * scale - biasX, (r[5 + k * 2] / S + a.y) * scale - biasY);
        dets.push_back(d);
    }
    std::sort(dets.begin(), dets.end(), [](const Det& a, const Det& b) { return a.score > b.score; });
    std::vector<Det> keep;
    for (auto& d : dets) {
        bool ok = true;
        for (auto& k : keep)
            if (iou(d.box, k.box) > 0.3f) ok = false;
        if (ok) keep.push_back(d);
        if (keep.size() >= 2) break;
    }
    for (auto& d : keep) {
        // MediaPipe: forgatás a csuklótól (0) a középső ujj tövéig (2), eltolás -0.5*magasság, méret 2.6x
        Vec2 p1 = d.kp[0], p2 = d.kp[2];
        float angle = wrapAngle(kPi * 0.5f - std::atan2(-(p2.y - p1.y), p2.x - p1.x));
        float bw = d.box.z - d.box.x, bh = d.box.w - d.box.y;
        Vec2 c((d.box.x + d.box.z) * 0.5f, (d.box.y + d.box.w) * 0.5f);
        float ca = std::cos(angle), sa = std::sin(angle);
        float shiftY = -0.5f * bh;
        c = Vec2(c.x - sa * shiftY, c.y + ca * shiftY);
        Roi roi;
        roi.c = c;
        roi.size = std::max(bw, bh) * 2.6f;
        roi.angle = angle;
        out.push_back(roi);
    }
}

bool HandTracker::landmarks(const CameraFrame& f, const Roi& roi, HandObs& obs) {
    const int S = 224;
    float ca = std::cos(roi.angle), sa = std::sin(roi.angle);
    std::vector<float> in((size_t)S * S * 3);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float lx = ((x + 0.5f) / S - 0.5f) * roi.size, ly = ((y + 0.5f) / S - 0.5f) * roi.size;
            float px = roi.c.x + ca * lx - sa * ly, py = roi.c.y + sa * lx + ca * ly;
            sampleRGB01(f, px - 0.5f, py - 0.5f, &in[((size_t)y * S + x) * 3]);
        }
    std::vector<OnnxOutput> outs;
    if (!lm_.run(in, {1, S, S, 3}, outs) || outs.size() < 4) return false;
    // kimenetek azonosítása: két 63 elemű (kép és világ pontok), két 1 eleműl (megbízhatóság, kéz oldal)
    const OnnxOutput* screen = nullptr;
    const OnnxOutput* world = nullptr;
    std::vector<const OnnxOutput*> singles;
    for (auto& o : outs) {
        if (o.data.size() == 63) {
            float mx = 0;
            for (float v : o.data) mx = std::max(mx, std::fabs(v));
            (mx > 2.0f ? screen : world) = &o;
        } else if (o.data.size() == 1)
            singles.push_back(&o);
    }
    if (!screen || !world || singles.size() < 2) return false;
    // sorrend (OpenCV Model Zoo): pontok, megbízhatóság, kéz oldal, világ pontok
    float conf = singles[0]->data[0], hand = singles[1]->data[0];
    if (getenv("AVATARCAM_HANDDEBUG")) logMsg("hand model: conf=%.3f side=%.3f", conf, hand);
    if (conf > 1.0f || conf < 0.0f) conf = sigmoid(conf);
    obs.score = conf;
    obs.handedness = hand;
    if (conf < 0.5f) return false;
    for (int i = 0; i < 21; i++) {
        float x = screen->data[i * 3], y = screen->data[i * 3 + 1];
        float lx = (x / S - 0.5f) * roi.size, ly = (y / S - 0.5f) * roi.size;
        obs.lms[i] = Vec2(roi.c.x + ca * lx - sa * ly, roi.c.y + sa * lx + ca * ly);
        // a világ pontok a kivágás tengelyeiben vannak -> a kép tengelyeibe forgatjuk
        float wx = world->data[i * 3], wy = world->data[i * 3 + 1], wz = world->data[i * 3 + 2];
        obs.world[i] = Vec3(ca * wx - sa * wy, sa * wx + ca * wy, wz);
    }
    // mélység: a tenyér méretéből (a világ pontok kép-síkbeli hossza / a képpont hossz)
    static const int pairs[][2] = {{0, 5}, {0, 9}, {0, 13}, {0, 17}, {5, 17}, {5, 9}, {9, 13}, {13, 17}};
    float wsum = 0, psum = 0;
    for (auto& p : pairs) {
        Vec3 dw = obs.world[p[1]] - obs.world[p[0]];
        Vec2 dp = obs.lms[p[1]] - obs.lms[p[0]];
        wsum += std::sqrt(dw.x * dw.x + dw.y * dw.y);
        psum += std::sqrt(dp.x * dp.x + dp.y * dp.y);
    }
    float fpx = (float)f.w;
    float z = psum > 1 ? fpx * wsum / psum : 0.6f;
    z = clampf(z, 0.15f, 3.0f);
    obs.wristCam = Vec3((obs.lms[0].x - f.w * 0.5f) * z / fpx, (obs.lms[0].y - f.h * 0.5f) * z / fpx, z);
    obs.valid = true;
    return true;
}

HandTracker::Roi HandTracker::roiFromLandmarks(const HandObs& h) const {
    // MediaPipe: forgatás a csuklótól az ujjtövek felé, forgatott befoglaló doboz, eltolás -0.1, méret 2.0x
    Vec2 mid = (h.lms[5] + h.lms[9] + h.lms[13]) * (1.0f / 3);
    float angle = wrapAngle(kPi * 0.5f - std::atan2(-(mid.y - h.lms[0].y), mid.x - h.lms[0].x));
    float ca = std::cos(angle), sa = std::sin(angle);
    // pontok a forgatott keretben
    float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
    for (auto& p : h.lms) {
        float lx = ca * p.x + sa * p.y, ly = -sa * p.x + ca * p.y;
        mnx = std::min(mnx, lx), mxx = std::max(mxx, lx), mny = std::min(mny, ly), mxy = std::max(mxy, ly);
    }
    float cx = (mnx + mxx) * 0.5f, cy = (mny + mxy) * 0.5f;
    float w = mxx - mnx, hgt = mxy - mny;
    cy += -0.1f * hgt;
    Roi r;
    r.c = Vec2(ca * cx - sa * cy, sa * cx + ca * cy);
    r.size = std::max(w, hgt) * 2.0f;
    r.angle = angle;
    return r;
}

bool HandTracker::process(const CameraFrame& f, HandResult& out, std::string& err) {
    if (!loadModels(err)) return false;
    out = HandResult{};
    out.imgW = f.w;
    out.imgH = f.h;
    frame_++;
    // új kezek keresése, ha nem követünk kettőt: kéz nélkül minden 2., egy kéznél minden 4. képkockán
    // (a tenyérkereső a drágább modell; így ha a kezed nincs a képen, alig fogyaszt)
    int active = tracks_[0].active + tracks_[1].active;
    if (active < 2 && frame_ % (active == 0 ? 2 : 4) == 0) {
        std::vector<Roi> rois;
        detectPalms(f, rois);
        for (auto& r : rois) {
            bool dup = false;
            for (auto& t : tracks_)
                if (t.active && length(Vec3(t.roi.c.x - r.c.x, t.roi.c.y - r.c.y, 0)) < t.roi.size * 0.35f) dup = true;
            if (dup) continue;
            for (auto& t : tracks_)
                if (!t.active) {
                    t = Track{};
                    t.active = true;
                    t.roi = r;
                    break;
                }
        }
    }
    HandObs obs[2];
    for (int i = 0; i < 2; i++) {
        Track& t = tracks_[i];
        if (!t.active) continue;
        if (!landmarks(f, t.roi, obs[i]) || t.roi.size < 16) {
            t.active = false;
            continue;
        }
        t.roi = roiFromLandmarks(obs[i]);
        t.age++;
        // a modell oldal-becslése tükrözött képre vonatkozik: nem tükrözött kameraképen fordítva
        float right = 1.0f - obs[i].handedness;
        t.sideVote = t.age == 1 ? right : t.sideVote * 0.85f + right * 0.15f;
        obs[i].userRight = t.sideVote > 0.5f;
    }
    // két kéz ugyanazon az oldalon: a képen balra lévő a felhasználó jobb keze
    if (obs[0].valid && obs[1].valid && obs[0].userRight == obs[1].userRight) {
        bool zeroLeft = obs[0].lms[0].x < obs[1].lms[0].x;
        obs[0].userRight = zeroLeft;
        obs[1].userRight = !zeroLeft;
    }
    // két track ugyanazt a kezet követi
    if (obs[0].valid && obs[1].valid && length(Vec3(obs[0].lms[0].x - obs[1].lms[0].x, obs[0].lms[0].y - obs[1].lms[0].y, 0)) < tracks_[0].roi.size * 0.15f) {
        tracks_[1].active = false;
        obs[1].valid = false;
    }
    // kimenet: [0] = a felhasználó bal keze, [1] = jobb keze
    for (int i = 0; i < 2; i++)
        if (obs[i].valid) out.hands[obs[i].userRight ? 1 : 0] = obs[i];
    return true;
}
