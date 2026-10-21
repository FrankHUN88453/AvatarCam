#pragma once
#include "core/Math.h"
#include "tracking/Onnx.h"
#include "tracking/Webcam.h"
#include "core/I18n.h"

// Egy kéz megfigyelése a kamera képén (MediaPipe kézpont modell, OpenCV Model Zoo ONNX változat)
struct HandObs {
    bool valid = false;
    bool userRight = false;     // a felhasználó jobb keze (nem tükrözött képen)
    float score = 0;
    float handedness = 0.5f;    // a modell nyers kimenete (tükrözött "selfie" konvenció)
    Vec2 lms[21];               // képpontok
    Vec3 world[21];             // méter, a képpel párhuzamos tengelyek (x jobbra, y le, z a kamerától távolodva)
    Vec3 wristCam{0, 0, 0};     // a csukló kamera térben (méter)
};

struct HandResult {
    HandObs hands[2];
    int imgW = 0, imgH = 0;
    double time = 0;
};

class HandTracker {
public:
    ~HandTracker();
    bool start(Webcam* cam, std::string& err);
    void stop();
    bool running() const { return running_; }
    HandResult latest();
    std::string status();
    float procMs() const { return running_ ? procMs_ : 0.0f; }
    bool process(const CameraFrame& f, HandResult& out, std::string& err);  // egy képkocka (teszteléshez is)

private:
    struct Roi {
        Vec2 c;
        float size = 0;
        float angle = 0;  // radián, kép koordináták (y lefelé)
    };
    struct Track {
        bool active = false;
        Roi roi;
        float sideVote = 0.5f;  // simított "jobb kéz" valószínűség
        int age = 0;
    };
    bool loadModels(std::string& err);
    void detectPalms(const CameraFrame& f, std::vector<Roi>& out);
    bool landmarks(const CameraFrame& f, const Roi& roi, HandObs& obs);
    Roi roiFromLandmarks(const HandObs& h) const;
    void loop();

    Webcam* cam_ = nullptr;
    OnnxSession palm_, lm_;
    std::vector<Vec2> anchors_;
    Track tracks_[2];
    int frame_ = 0;
    std::thread th_;
    std::atomic<bool> running_{false}, stop_{false};
    std::mutex mx_;
    HandResult result_;
    std::string status_ = T("Not enabled");
    float procMs_ = 0;
};
