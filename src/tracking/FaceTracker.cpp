// Webkamerás arckövetés az OpenSeeFace modelljeivel (BSD 2-Clause, (c) 2019 Emiliana)
// A detektálás és az arcpont dekódolás az OpenSeeFace tracker.py logikáját követi.
#include "tracking/FaceTracker.h"
#include <windows.h>
#include <algorithm>
#include "core/I18n.h"

namespace {
// 66 pontos 3D arcmodell (OpenSeeFace)
const float kFace3d[66][3] = {
    {0.4551769692672f, 0.300895790030204f, -0.764429433974752f},   {0.448998827123556f, 0.166995837790733f, -0.765143004071253f},
    {0.437431554952677f, 0.022655479179981f, -0.739267175112735f},  {0.415033422928434f, -0.088941454648772f, -0.747947437846473f},
    {0.389123587370091f, -0.232380029794684f, -0.704788385327458f}, {0.334630113904382f, -0.361265387599081f, -0.615587579236862f},
    {0.263725112132858f, -0.460009725616771f, -0.491479221041573f}, {0.16241621322721f, -0.558037146073869f, -0.339445180872282f},
    {0.0f, -0.621079019321682f, -0.287294770748887f},               {-0.16241621322721f, -0.558037146073869f, -0.339445180872282f},
    {-0.263725112132858f, -0.460009725616771f, -0.491479221041573f},{-0.334630113904382f, -0.361265387599081f, -0.615587579236862f},
    {-0.389123587370091f, -0.232380029794684f, -0.704788385327458f},{-0.415033422928434f, -0.088941454648772f, -0.747947437846473f},
    {-0.437431554952677f, 0.022655479179981f, -0.739267175112735f}, {-0.448998827123556f, 0.166995837790733f, -0.765143004071253f},
    {-0.4551769692672f, 0.300895790030204f, -0.764429433974752f},   {0.385529968662985f, 0.402800553948697f, -0.310031082540741f},
    {0.322196658344302f, 0.464439136821772f, -0.250558059367669f},  {0.25409760441282f, 0.46420381416882f, -0.208177722146526f},
    {0.186875436782135f, 0.44706071961879f, -0.145299823706503f},   {0.120880983543622f, 0.423566314072968f, -0.110757158774771f},
    {-0.120880983543622f, 0.423566314072968f, -0.110757158774771f}, {-0.186875436782135f, 0.44706071961879f, -0.145299823706503f},
    {-0.25409760441282f, 0.46420381416882f, -0.208177722146526f},   {-0.322196658344302f, 0.464439136821772f, -0.250558059367669f},
    {-0.385529968662985f, 0.402800553948697f, -0.310031082540741f}, {0.0f, 0.293332603215811f, -0.137582088779393f},
    {0.0f, 0.194828701837823f, -0.069158109325951f},                {0.0f, 0.103844017393155f, -0.009151819844964f},
    {0.0f, 0.0f, 0.0f},                                              {0.080626352317973f, -0.041276068128093f, -0.134161035564826f},
    {0.046439347377934f, -0.057675223874769f, -0.102990627164664f}, {0.0f, -0.068753126205604f, -0.090545348482397f},
    {-0.046439347377934f, -0.057675223874769f, -0.102990627164664f},{-0.080626352317973f, -0.041276068128093f, -0.134161035564826f},
    {0.315905195966084f, 0.298337502555443f, -0.285107407636464f},  {0.275252345439353f, 0.312721904921771f, -0.244558251170671f},
    {0.176394511553111f, 0.311907184376107f, -0.219205360345231f},  {0.131229723798772f, 0.284447361805627f, -0.234239149487417f},
    {0.184124948330084f, 0.260179585304867f, -0.226590776513707f},  {0.279433549294448f, 0.267363071770222f, -0.248441437111633f},
    {-0.131229723798772f, 0.284447361805627f, -0.234239149487417f}, {-0.176394511553111f, 0.311907184376107f, -0.219205360345231f},
    {-0.275252345439353f, 0.312721904921771f, -0.244558251170671f}, {-0.315905195966084f, 0.298337502555443f, -0.285107407636464f},
    {-0.279433549294448f, 0.267363071770222f, -0.248441437111633f}, {-0.184124948330084f, 0.260179585304867f, -0.226590776513707f},
    {0.121155252430729f, -0.208988660580347f, -0.160606287940521f}, {0.041356305910044f, -0.194484199722098f, -0.096159882202821f},
    {0.0f, -0.205180167345702f, -0.083299217789729f},               {-0.041356305910044f, -0.194484199722098f, -0.096159882202821f},
    {-0.121155252430729f, -0.208988660580347f, -0.160606287940521f},{-0.132325402795928f, -0.290857984604968f, -0.187067868218105f},
    {-0.064137791831655f, -0.325377847425684f, -0.158924039726607f},{0.0f, -0.343742581679188f, -0.113925986025684f},
    {0.064137791831655f, -0.325377847425684f, -0.158924039726607f}, {0.132325402795928f, -0.290857984604968f, -0.187067868218105f},
    {0.181481567104525f, -0.243239316141725f, -0.231284988892766f}, {0.083999507750469f, -0.239717753728704f, -0.155256465640701f},
    {0.0f, -0.256058040176369f, -0.0950619498899f},                 {-0.083999507750469f, -0.239717753728704f, -0.155256465640701f},
    {-0.181481567104525f, -0.243239316141725f, -0.231284988892766f},{-0.074036069749345f, -0.250689938345682f, -0.177346470406188f},
    {0.0f, -0.264945854681568f, -0.112349967428413f},               {0.074036069749345f, -0.250689938345682f, -0.177346470406188f}};

const int kContour[] = {0, 1, 8, 15, 16, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 39, 42, 45};

// ImageNet normalizálás (RGB)
const float kMean[3] = {0.485f, 0.456f, 0.406f};
const float kStd[3] = {0.229f, 0.224f, 0.225f};

inline float logitf(float p, float factor) {
    p = clampf(p, 1e-7f, 0.9999999f);
    return std::log(p / (1 - p)) / factor;
}

// Bilineáris mintavétel BGRA képből -> RGB (0..255)
inline void sampleRGB(const CameraFrame& f, float x, float y, float rgb[3]) {
    x = clampf(x, 0, (float)f.w - 1.001f);
    y = clampf(y, 0, (float)f.h - 1.001f);
    int x0 = (int)x, y0 = (int)y;
    float fx = x - x0, fy = y - y0;
    const uint8_t* p00 = &f.bgra[((size_t)y0 * f.w + x0) * 4];
    const uint8_t* p10 = p00 + 4;
    const uint8_t* p01 = p00 + (size_t)f.w * 4;
    const uint8_t* p11 = p01 + 4;
    for (int c = 0; c < 3; c++) {
        int bc = 2 - c;  // BGR -> RGB
        float a = p00[bc] + (p10[bc] - p00[bc]) * fx;
        float b = p01[bc] + (p11[bc] - p01[bc]) * fx;
        rgb[c] = a + (b - a) * fy;
    }
}

// Rodrigues: forgatásvektor -> 3x3 mátrix (sorfolytonos)
void rodrigues(const float r[3], float R[9]) {
    float th = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (th < 1e-9f) {
        R[0] = R[4] = R[8] = 1;
        R[1] = R[2] = R[3] = R[5] = R[6] = R[7] = 0;
        return;
    }
    float x = r[0] / th, y = r[1] / th, z = r[2] / th;
    float c = std::cos(th), s = std::sin(th), C = 1 - c;
    R[0] = c + x * x * C, R[1] = x * y * C - z * s, R[2] = x * z * C + y * s;
    R[3] = y * x * C + z * s, R[4] = c + y * y * C, R[5] = y * z * C - x * s;
    R[6] = z * x * C - y * s, R[7] = z * y * C + x * s, R[8] = c + z * z * C;
}

Quat matToQuat(const float m[9]) {
    // m sorfolytonos: m[r*3+c]
    float tr = m[0] + m[4] + m[8];
    Quat q;
    if (tr > 0) {
        float S = std::sqrt(tr + 1) * 2;
        q.w = 0.25f * S, q.x = (m[7] - m[5]) / S, q.y = (m[2] - m[6]) / S, q.z = (m[3] - m[1]) / S;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float S = std::sqrt(1 + m[0] - m[4] - m[8]) * 2;
        q.w = (m[7] - m[5]) / S, q.x = 0.25f * S, q.y = (m[1] + m[3]) / S, q.z = (m[2] + m[6]) / S;
    } else if (m[4] > m[8]) {
        float S = std::sqrt(1 + m[4] - m[0] - m[8]) * 2;
        q.w = (m[2] - m[6]) / S, q.x = (m[1] + m[3]) / S, q.y = 0.25f * S, q.z = (m[5] + m[7]) / S;
    } else {
        float S = std::sqrt(1 + m[8] - m[0] - m[4]) * 2;
        q.w = (m[3] - m[1]) / S, q.x = (m[2] + m[6]) / S, q.y = (m[5] + m[7]) / S, q.z = 0.25f * S;
    }
    return q.normalized();
}

// kis lineáris egyenletrendszer (Gauss elimináció)
bool solve6(double A[6][6], double b[6], double x[6]) {
    for (int i = 0; i < 6; i++) {
        int piv = i;
        for (int r = i + 1; r < 6; r++)
            if (std::fabs(A[r][i]) > std::fabs(A[piv][i])) piv = r;
        if (std::fabs(A[piv][i]) < 1e-14) return false;
        if (piv != i) {
            for (int c = 0; c < 6; c++) std::swap(A[i][c], A[piv][c]);
            std::swap(b[i], b[piv]);
        }
        for (int r = i + 1; r < 6; r++) {
            double f = A[r][i] / A[i][i];
            for (int c = i; c < 6; c++) A[r][c] -= f * A[i][c];
            b[r] -= f * b[i];
        }
    }
    for (int i = 5; i >= 0; i--) {
        double s = b[i];
        for (int c = i + 1; c < 6; c++) s -= A[i][c] * x[c];
        x[i] = s / A[i][i];
    }
    return true;
}
}  // namespace

// ---------------------------------------------------------------------------
void AdaptiveRange::add(float v) {
    if (!std::isfinite(v)) return;
    values_.push_back(v);
    if (values_.size() > 900) values_.pop_front();
    dirty_++;
}

float AdaptiveRange::percentile(float p) {
    if (values_.empty()) return 0;
    if (dirty_ > 15 || sorted_.empty()) {
        sorted_.assign(values_.begin(), values_.end());
        std::sort(sorted_.begin(), sorted_.end());
        dirty_ = 0;
    }
    size_t i = (size_t)clampf(p * (sorted_.size() - 1), 0, (float)sorted_.size() - 1);
    return sorted_[i];
}

// ---------------------------------------------------------------------------
FaceTracker::~FaceTracker() { stop(); }

bool FaceTracker::loadModels(std::string& err) {
    if (lm_.loaded()) return true;
    fs::path dir = exeDir() / L"models";
    if (!fs::exists(dir / L"lm_model3_opt.onnx")) dir = dataDir() / L"bin" / L"models";
    // egy szál modellenként: így a legkisebb a teljes processzorhasználat, és 30 fps-hez bőven elég
    if (!det_.load(dir / L"mnv3_detection_opt.onnx", 1, err)) return false;
    if (!lm_.load(dir / L"lm_model3_opt.onnx", 1, err)) return false;
    std::string gerr;
    if (!gaze_.load(dir / L"mnv3_gaze32_split_opt.onnx", 1, gerr)) logMsg("Eye tracking model could not be loaded: %s", gerr.c_str());
    return true;
}

bool FaceTracker::start(Webcam* cam, std::string& err) {
    stop();
    if (!loadModels(err)) {
        status_ = T("Error: ") + err;
        return false;
    }
    cam_ = cam;
    stop_ = false;
    running_ = true;
    haveRect_ = false;
    poseInit_ = false;
    status_ = T("Searching for face…");
    th_ = std::thread([this] { loop(); });
    return true;
}

void FaceTracker::stop() {
    stop_ = true;
    if (th_.joinable()) th_.join();
    running_ = false;
    status_ = T("Not enabled");
    std::lock_guard<std::mutex> lk(mx_);
    result_.valid = false;
}

FaceResult FaceTracker::latest() {
    std::lock_guard<std::mutex> lk(mx_);
    return result_;
}

std::string FaceTracker::status() {
    std::lock_guard<std::mutex> lk(mx_);
    if (!running_) return status_;
    return format(T("%s · camera %s %.0f fps · %.1f ms"), status_.c_str(), cam_ ? cam_->status.c_str() : "-", cam_ ? cam_->fps() : 0.0f, procMs_);
}

bool FaceTracker::previewFrame(CameraFrame& frame, FaceResult& res) {
    std::lock_guard<std::mutex> lk(mx_);
    if (lastFrame_.bgra.empty()) return false;
    frame = lastFrame_;
    res = result_;
    return true;
}

void FaceTracker::loop() {
    // normál alatti prioritás: játék mellett a játék szálai élveznek elsőbbséget
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    uint64_t last = 0;
    CameraFrame f;
    while (!stop_) {
        if (!cam_->latest(f, last)) {
            if (!cam_->isOpen()) {
                std::lock_guard<std::mutex> lk(mx_);
                status_ = T("The camera stopped");
                break;
            }
            cam_->waitFrame(last, 50);
            continue;
        }
        last = f.index;
        double t0 = nowSeconds();
        FaceResult r;
        std::string err;
        processImage(f, r, err);
        r.time = f.time;
        float ms = (float)((nowSeconds() - t0) * 1000);
        std::lock_guard<std::mutex> lk(mx_);
        procMs_ = procMs_ * 0.9f + ms * 0.1f;
        result_ = r;
        status_ = r.valid ? T("Tracking face") : T("Searching for face…");
        lastFrame_ = std::move(f);
        f = CameraFrame{};
    }
    running_ = false;
}

bool FaceTracker::processImage(const CameraFrame& f, FaceResult& out, std::string& err) {
    if (!loadModels(err)) return false;
    out = FaceResult{};
    out.imgW = f.w;
    out.imgH = f.h;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!haveRect_) {
            Rect r;
            if (!detect(f, r)) return false;
            rect_ = r;
            haveRect_ = true;
        }
        if (landmarks(f, rect_, out)) {
            // a következő képkockához az arcpontok befoglaló téglalapja
            Vec2 mn = out.lms[0], mx = out.lms[0];
            for (auto& p : out.lms) {
                mn.x = std::min(mn.x, p.x), mn.y = std::min(mn.y, p.y);
                mx.x = std::max(mx.x, p.x), mx.y = std::max(mx.y, p.y);
            }
            rect_ = {mn.x, mn.y, mx.x - mn.x, mx.y - mn.y};
            eyes(f, out);
            solvePose(out);
            features(out);
            out.valid = true;
            return true;
        }
        haveRect_ = false;
    }
    return false;
}

bool FaceTracker::detect(const CameraFrame& f, Rect& out) {
    const int S = 224;
    std::vector<float> in(3 * S * S);
    float sx = (float)f.w / S, sy = (float)f.h / S;
    float rgb[3];
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            sampleRGB(f, (x + 0.5f) * sx - 0.5f, (y + 0.5f) * sy - 0.5f, rgb);
            for (int c = 0; c < 3; c++) in[(size_t)c * S * S + y * S + x] = (rgb[c] / 255.0f - kMean[c]) / kStd[c];
        }
    std::vector<OnnxOutput> outs;
    if (!det_.run(in, {1, 3, S, S}, outs) || outs.size() < 2) return false;
    const OnnxOutput* o = &outs[0];
    const OnnxOutput* mp = &outs[1];
    if (o->shape.size() >= 2 && o->shape[1] != 2) std::swap(o, mp);
    const int G = 56;
    if (o->data.size() < 2 * G * G || mp->data.size() < G * G) return false;
    int best = -1;
    float bestC = 0.6f;
    for (int i = 0; i < G * G; i++) {
        float c = o->data[i];
        if (c != mp->data[i]) continue;  // nem lokális maximum
        if (c > bestC) bestC = c, best = i;
    }
    if (best < 0) return false;
    int gy = best / G, gx = best % G;
    float r = o->data[G * G + best] * 112.0f;
    float x = gx * 4.0f, y = gy * 4.0f;
    out = {(x - r) * sx, (y - r) * sy, 2 * r * sx, 2 * r * sy};
    return out.w > 8 && out.h > 8;
}

bool FaceTracker::landmarks(const CameraFrame& f, const Rect& r, FaceResult& res) {
    const int S = 224;
    float cx1 = r.x - (int)(r.w * 0.1f), cy1 = r.y - (int)(r.h * 0.125f);
    float cx2 = r.x + r.w + (int)(r.w * 0.1f), cy2 = r.y + r.h + (int)(r.h * 0.125f);
    cx1 = clampf(cx1, 0, (float)f.w - 1), cx2 = clampf(cx2, 0, (float)f.w - 1);
    cy1 = clampf(cy1, 0, (float)f.h - 1), cy2 = clampf(cy2, 0, (float)f.h - 1);
    if (cx2 - cx1 < 8 || cy2 - cy1 < 8) return false;
    float scx = (cx2 - cx1) / S, scy = (cy2 - cy1) / S;
    std::vector<float> in(3 * S * S);
    float rgb[3];
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            sampleRGB(f, cx1 + (x + 0.5f) * scx - 0.5f, cy1 + (y + 0.5f) * scy - 0.5f, rgb);
            for (int c = 0; c < 3; c++) in[(size_t)c * S * S + y * S + x] = (rgb[c] / 255.0f - kMean[c]) / kStd[c];
        }
    std::vector<OnnxOutput> outs;
    if (!lm_.run(in, {1, 3, S, S}, outs) || outs.empty()) return false;
    const auto& t = outs[0].data;
    const int R = 28, RR = R * R;
    if (t.size() < (size_t)198 * RR) return false;
    float res1 = S - 1.0f, outRes = 27.0f;
    float confSum = 0;
    for (int i = 0; i < 66; i++) {
        const float* hm = &t[(size_t)i * RR];
        int m = (int)(std::max_element(hm, hm + RR) - hm);
        float conf = hm[m];
        float offRow = res1 * logitf(t[(size_t)(66 + i) * RR + m], 16.0f);
        float offCol = res1 * logitf(t[(size_t)(132 + i) * RR + m], 16.0f);
        float row = res1 * std::floor((float)m / R) / outRes + offRow;
        float col = res1 * (float)(m % R) / outRes + offCol;
        res.lms[i] = Vec2(cx1 + scx * col, cy1 + scy * row);
        confSum += conf;
    }
    res.conf = confSum / 66;
    return res.conf > 0.6f;
}

void FaceTracker::eyes(const CameraFrame& f, FaceResult& res) {
    if (!gaze_.loaded()) return;
    struct EyeCrop {
        Vec2 center;
        float angle, rx, ry;
        bool flip;
    } crops[2];
    std::vector<float> in(2 * 3 * 32 * 32);
    int corners[2][2] = {{36, 39}, {42, 45}};
    float rgb[3];
    for (int e = 0; e < 2; e++) {
        Vec2 a = res.lms[corners[e][0]], b = res.lms[corners[e][1]];
        EyeCrop& c = crops[e];
        c.center = (a + b) * 0.5f;
        c.angle = std::atan2(b.y - a.y, b.x - a.x);
        float radius = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)) * 0.5f;
        c.rx = radius * 1.4f, c.ry = radius * 1.2f;
        c.flip = e == 1;
        float ca = std::cos(c.angle), sa = std::sin(c.angle);
        for (int u = 0; u < 32; u++)
            for (int v = 0; v < 32; v++) {
                float lx = -c.rx + (u + 0.5f) * (2 * c.rx / 32), ly = -c.ry + (v + 0.5f) * (2 * c.ry / 32);
                if (c.flip) lx = -lx;
                float px = c.center.x + ca * lx - sa * ly, py = c.center.y + sa * lx + ca * ly;
                sampleRGB(f, px, py, rgb);
                // a modell bemenete [n][c][x][y] (OpenSeeFace transzponált elrendezés)
                for (int ch = 0; ch < 3; ch++) in[(size_t)e * 3072 + (size_t)ch * 1024 + u * 32 + v] = (rgb[ch] / 255.0f - kMean[ch]) / kStd[ch];
            }
    }
    std::vector<OnnxOutput> outs;
    if (!gaze_.run(in, {2, 3, 32, 32}, outs) || outs.empty()) return;
    const auto& o = outs[0].data;
    if (o.size() < 2 * 3 * 64) return;
    float gx = 0, gy = 0;
    for (int e = 0; e < 2; e++) {
        const float* hm = &o[(size_t)e * 192];
        int m = (int)(std::max_element(hm, hm + 64) - hm);
        int x = m / 8, y = m % 8;
        float ex = 32.0f * x / 8.0f + 32.0f * logitf(hm[64 + m], 8.0f);
        float ey = 32.0f * y / 8.0f + 32.0f * logitf(hm[128 + m], 8.0f);
        const EyeCrop& c = crops[e];
        float lx = -c.rx + ex * (2 * c.rx / 32), ly = -c.ry + ey * (2 * c.ry / 32);
        if (c.flip) lx = -lx;
        float ca = std::cos(c.angle), sa = std::sin(c.angle);
        res.pupils[e] = Vec2(c.center.x + ca * lx - sa * ly, c.center.y + sa * lx + ca * ly);
        float half = c.rx / 1.4f;
        gx += lx / half;
        gy += ly / half;
    }
    res.gazeX = clampf(gx * 0.5f * 2.2f, -1, 1);
    res.gazeY = clampf(gy * 0.5f * 3.0f, -1, 1);
    res.gazeValid = true;
}

// Fejpozíció: a 3D arcmodell illesztése az arcpontokra (Levenberg–Marquardt)
void FaceTracker::solvePose(FaceResult& res) {
    const float fpx = (float)res.imgW, cx = res.imgW * 0.5f, cy = res.imgH * 0.5f;
    const int N = (int)std::size(kContour);
    // modell pont -> "néző" keret (x jobbra a képen, y fel, z a kamera felé)
    auto Q = [](int i) { return Vec3(-kFace3d[i][0], kFace3d[i][1], kFace3d[i][2]); };
    if (!poseInit_) {
        // kezdeti becslés: szemből néző arc a kép középpontján
        float pxDist = length(Vec3(res.lms[0].x - res.lms[16].x, res.lms[0].y - res.lms[16].y, 0));
        float z = fpx * 0.91f / std::max(pxDist, 1.0f);
        rvec_[0] = kPi, rvec_[1] = 0, rvec_[2] = 0;
        tvec_[0] = (res.lms[30].x - cx) * z / fpx;
        tvec_[1] = (res.lms[30].y - cy) * z / fpx;
        tvec_[2] = z;
        poseInit_ = true;
    }
    auto residuals = [&](const float* r, const float* t, double* out) {
        float R[9];
        rodrigues(r, R);
        for (int k = 0; k < N; k++) {
            Vec3 q = Q(kContour[k]);
            float X = R[0] * q.x + R[1] * q.y + R[2] * q.z + t[0];
            float Y = R[3] * q.x + R[4] * q.y + R[5] * q.z + t[1];
            float Z = R[6] * q.x + R[7] * q.y + R[8] * q.z + t[2];
            if (Z < 1e-3f) Z = 1e-3f;
            out[k * 2] = fpx * X / Z + cx - res.lms[kContour[k]].x;
            out[k * 2 + 1] = fpx * Y / Z + cy - res.lms[kContour[k]].y;
        }
    };
    std::vector<double> r0(N * 2), r1(N * 2), J((size_t)N * 2 * 6);
    double lambda = 1e-3;
    float p[6] = {rvec_[0], rvec_[1], rvec_[2], tvec_[0], tvec_[1], tvec_[2]};
    residuals(p, p + 3, r0.data());
    double err0 = 0;
    for (double v : r0) err0 += v * v;
    for (int it = 0; it < 12; it++) {
        for (int k = 0; k < 6; k++) {
            float q[6];
            std::copy(p, p + 6, q);
            float eps = k < 3 ? 1e-4f : std::max(1e-4f, std::fabs(p[k]) * 1e-4f);
            q[k] += eps;
            residuals(q, q + 3, r1.data());
            for (int i = 0; i < N * 2; i++) J[(size_t)i * 6 + k] = (r1[i] - r0[i]) / eps;
        }
        double A[6][6] = {}, b[6] = {}, dx[6];
        for (int i = 0; i < N * 2; i++)
            for (int a = 0; a < 6; a++) {
                b[a] -= J[(size_t)i * 6 + a] * r0[i];
                for (int c = 0; c < 6; c++) A[a][c] += J[(size_t)i * 6 + a] * J[(size_t)i * 6 + c];
            }
        for (int a = 0; a < 6; a++) A[a][a] *= (1 + lambda);
        if (!solve6(A, b, dx)) break;
        float q[6];
        for (int k = 0; k < 6; k++) q[k] = p[k] + (float)dx[k];
        residuals(q, q + 3, r1.data());
        double err1 = 0;
        for (double v : r1) err1 += v * v;
        if (err1 < err0) {
            std::copy(q, q + 6, p);
            r0 = r1;
            lambda *= 0.3;
            if (err0 - err1 < 1e-6 * err0) {
                err0 = err1;
                break;
            }
            err0 = err1;
        } else
            lambda *= 10;
    }
    if (!std::isfinite(err0) || p[5] <= 0) {
        poseInit_ = false;
        return;
    }
    std::copy(p, p + 3, rvec_);
    std::copy(p + 3, p + 6, tvec_);
    float R[9];
    rodrigues(rvec_, R);
    // R: néző keret -> kamera keret. A néző keretbeli fejforgatás: C * R, ahol C = diag(1,-1,-1)
    float V[9] = {R[0], R[1], R[2], -R[3], -R[4], -R[5], -R[6], -R[7], -R[8]};
    res.head = matToQuat(V);
    res.trans = Vec3(tvec_[0], tvec_[1], tvec_[2]);
}

void FaceTracker::features(FaceResult& res) {
    auto& L = res.lms;
    auto dist = [&](int a, int b) { return length(Vec3(L[a].x - L[b].x, L[a].y - L[b].y, 0)); };
    // arc "fel" iránya (áll -> orrnyereg) és az arra merőleges
    Vec3 up = normalize(Vec3(L[27].x - L[8].x, L[27].y - L[8].y, 0));
    auto along = [&](const Vec2& a, const Vec2& b) { return (a.x - b.x) * up.x + (a.y - b.y) * up.y; };
    float normY = (dist(27, 28) + dist(28, 29) + dist(29, 30)) / 3.0f;
    float faceW = std::max(1.0f, dist(0, 16));
    if (normY < 1e-3f) return;
    // szemek: szemhéj nyílás / szemszélesség
    float earR = (dist(37, 41) + dist(38, 40)) / (2 * std::max(1e-3f, dist(36, 39)));
    float earL = (dist(43, 47) + dist(44, 46)) / (2 * std::max(1e-3f, dist(42, 45)));
    earR_.add(earR);
    earL_.add(earL);
    auto openness = [&](AdaptiveRange& ar, float v) {
        if (!ar.ready()) return 1.0f;
        float open = ar.percentile(0.85f);
        float closed = std::min(ar.percentile(0.03f), open * 0.6f);
        return saturate((v - closed) / std::max(1e-4f, (open - closed) * 0.75f));
    };
    res.eyeOpenR = openness(earR_, earR);
    res.eyeOpenL = openness(earL_, earL);
    // száj
    Vec2 upper = (L[60] + L[61] + L[62]) * (1.0f / 3), lower = (L[63] + L[64] + L[65]) * (1.0f / 3);
    float mo = std::fabs(along(upper, lower)) / normY;
    mouth_.add(mo);
    float closedM = mouth_.ready() ? mouth_.percentile(0.1f) : mo;
    float maxM = mouth_.ready() ? std::max(mouth_.percentile(0.98f), closedM + 0.8f) : closedM + 1.2f;
    res.mouthOpen = saturate((mo - closedM - 0.05f) / std::max(0.3f, maxM - closedM));
    float mw = dist(48, 54) / faceW;
    wide_.add(mw);
    float midW = wide_.ready() ? wide_.percentile(0.5f) : mw;
    res.mouthWide = clampf((mw - midW) / std::max(0.02f, midW * 0.18f), -1, 1);
    res.mouthRound = saturate(-res.mouthWide);
    // mosoly: a szájzugok a középvonalhoz képest
    Vec2 center = (L[51] + L[57]) * 0.5f;
    float sl = along(L[54], center) / normY, sr = along(L[48], center) / normY;
    smileL_.add(sl);
    smileR_.add(sr);
    if (smileL_.ready()) res.smileL = saturate((sl - smileL_.percentile(0.5f)) / 0.35f);
    if (smileR_.ready()) res.smileR = saturate((sr - smileR_.percentile(0.5f)) / 0.35f);
    // szemöldök
    float bl = along((L[22] + L[23] + L[24] + L[25] + L[26]) * 0.2f, L[27]) / normY;
    float br = along((L[17] + L[18] + L[19] + L[20] + L[21]) * 0.2f, L[27]) / normY;
    browL_.add(bl);
    browR_.add(br);
    if (browL_.ready()) res.browL = clampf((bl - browL_.percentile(0.5f)) / 0.3f, -1, 1);
    if (browR_.ready()) res.browR = clampf((br - browR_.percentile(0.5f)) / 0.3f, -1, 1);
}
