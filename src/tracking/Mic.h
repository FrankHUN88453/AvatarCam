#pragma once
#include "core/Util.h"
#include <thread>
#include <atomic>
#include <mutex>
#include "core/I18n.h"

std::vector<std::string> enumerateMicrophones();

// Mikrofon alapú szájmozgás (WASAPI megosztott mód)
class MicLipSync {
public:
    ~MicLipSync();
    bool start(const std::string& name, std::string& err);
    void stop();
    bool running() const { return running_; }
    void setParams(float gain, float threshold, bool autoGain, float mouth) {
        gain_ = gain;
        threshold_ = threshold;
        autoGain_ = autoGain;
        mouth_ = mouth;
    }
    float currentGain() const { return effGain_; }  // a ténylegesen használt erősítés (automatikus módban változik)
    float currentGate() const { return gate_; }     // a ténylegesen használt küszöb
    // visemes: sil..ou (15), level: 0..1
    void get(float visemes[15], float& level);
    float rawLevel() const { return raw_; }
    std::string status = T("Not enabled");

private:
    void loop(std::string name);
    std::thread th_;
    std::atomic<bool> running_{false}, stop_{false};
    std::atomic<int> openState_{0};
    std::string openErr_;
    std::mutex mx_;
    float vis_[15] = {};
    float level_ = 0;
    std::atomic<float> raw_{0};
    std::atomic<float> gain_{1}, threshold_{0.015f}, mouth_{1}, effGain_{1}, gate_{0.015f};
    std::atomic<bool> autoGain_{false};
};
