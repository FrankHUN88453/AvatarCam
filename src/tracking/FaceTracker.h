#pragma once
#include "core/Math.h"
#include "tracking/Onnx.h"
#include "tracking/Webcam.h"
#include <array>
#include <deque>
#include "core/I18n.h"

// Egy arc követési eredménye (képtér és a néző terébe forgatott fejirány)
struct FaceResult {
    bool valid = false;
    float conf = 0;
    Quat head{};            // fej forgatás a néző terében (x jobbra, y fel, z a kamera felé)
    Vec3 trans{0, 0, 0};    // kamera tér (modell egységek)
    float eyeOpenL = 1, eyeOpenR = 1;  // az alany bal/jobb szeme, 0..1
    float mouthOpen = 0, mouthWide = 0, mouthRound = 0;
    float smileL = 0, smileR = 0;
    float browL = 0, browR = 0;
    float gazeX = 0, gazeY = 0;  // -1..1 (x: kép jobbra, y: lefelé)
    bool gazeValid = false;
    std::array<Vec2, 66> lms{};
    Vec2 pupils[2];
    int imgW = 0, imgH = 0;
    double time = 0;
};

// Egyszerű önkalibráló tartomány (percentilisek egy csúszó ablakon)
class AdaptiveRange {
public:
    void add(float v);
    float percentile(float p);
    bool ready() const { return values_.size() > 30; }

private:
    std::deque<float> values_;
    std::vector<float> sorted_;
    int dirty_ = 0;
};

class FaceTracker {
public:
    ~FaceTracker();
    bool start(Webcam* cam, std::string& err);  // a kamerát a hívó kezeli
    void stop();
    bool running() const { return running_; }
    FaceResult latest();
    std::string status();
    float procMs() const { return running_ ? procMs_ : 0.0f; }
    bool previewFrame(CameraFrame& frame, FaceResult& res);
    // egyetlen kép feldolgozása (teszteléshez)
    bool processImage(const CameraFrame& f, FaceResult& out, std::string& err);

private:
    struct Rect {
        float x, y, w, h;
    };
    bool loadModels(std::string& err);
    bool detect(const CameraFrame& f, Rect& out);
    bool landmarks(const CameraFrame& f, const Rect& r, FaceResult& res);
    void eyes(const CameraFrame& f, FaceResult& res);
    void solvePose(FaceResult& res);
    void features(FaceResult& res);
    void loop();

    Webcam* cam_ = nullptr;
    OnnxSession det_, lm_, gaze_;
    std::thread th_;
    std::atomic<bool> running_{false}, stop_{false};
    std::mutex mx_;
    FaceResult result_;
    CameraFrame lastFrame_;
    bool haveRect_ = false;
    Rect rect_{};
    // PnP állapot
    float rvec_[3] = {kPi, 0, 0};
    float tvec_[3] = {0, 0, 10};
    bool poseInit_ = false;
    AdaptiveRange earL_, earR_, mouth_, wide_, smileL_, smileR_, browL_, browR_;
    std::string status_ = T("Not enabled");
    float procMs_ = 0;
};
