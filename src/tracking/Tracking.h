#pragma once
#include "core/Math.h"
#include "core/Util.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

struct TrackingSettings;
struct ID3D11ShaderResourceView;
class Gfx;

// Egy követési képkocka (arc + hang)
struct TrackingFrame {
    bool face = false;        // van érvényes arc adat
    Quat head{};              // fej forgatás (avatár térben, +Z előre)
    Vec3 headPos{0, 0, 0};    // fej eltolás (méter, kb.)
    float blinkL = 0, blinkR = 0;
    float eyeYaw = 0, eyePitch = 0;
    std::unordered_map<std::string, float> arkit;  // kisbetűs ARKit nevek
    float visemes[15] = {};
    float mouthOpen = 0;
    float voice = 0;          // mikrofon hangerő 0..1
    double time = 0;
    struct Hand {
        bool valid = false;
        Vec3 rel{0, 0, 0}, dir{0, 1, 0}, side{1, 0, 0};
        float finger[5][3] = {};
        double lastSeen = -10;
    } hands[2];  // avatár bal / jobb
};

class FaceTracker;
class HandTracker;
class Webcam;
class MicLipSync;
class VmcReceiver;

class Tracking {
public:
    Tracking();
    ~Tracking();
    void update(const TrackingSettings& s, Gfx& gfx, float dt);
    TrackingFrame frame() const;
    std::vector<std::string> cameras();
    std::vector<std::string> microphones();
    std::string faceStatus() const;
    std::string handStatus() const;
    float faceMs() const;  // arckövetés ideje képkockánként (ms)
    float handMs() const;  // kézkövetés ideje képkockánként (ms)
    std::string micStatus() const;
    std::string vmcStatus() const;
    float micLevel() const;  // a mikrofon szintje erősítés után (RMS)
    float micGain() const;   // a ténylegesen használt erősítés
    float micGate() const;   // a ténylegesen használt küszöb
    ID3D11ShaderResourceView* cameraPreview(Gfx& gfx);  // a kamera képe (opcionális)
    void recenter();  // a jelenlegi fejtartás legyen az alap

private:
    std::unique_ptr<Webcam> cam_;
    std::unique_ptr<HandTracker> hand_;
    std::unique_ptr<FaceTracker> face_;
    std::unique_ptr<MicLipSync> mic_;
    std::unique_ptr<VmcReceiver> vmc_;
    TrackingFrame smooth_;
    mutable std::mutex mx_;
    Quat calib_{};
    bool wantRecenter_ = false;
};
