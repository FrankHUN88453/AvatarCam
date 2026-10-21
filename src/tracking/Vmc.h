#pragma once
#include "core/Math.h"
#include "core/Util.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include "core/I18n.h"

// VMC protokoll (OSC/UDP) fogadó: blendshape-ek és a fej csont forgatása
class VmcReceiver {
public:
    ~VmcReceiver();
    bool start(int port, std::string& err);
    void stop();
    bool running() const { return running_; }
    bool active() const { return nowSeconds() - lastPacket_ < 1.0; }
    bool get(std::unordered_map<std::string, float>& blends, Quat& head, bool& hasHead);
    std::string status = T("Not enabled");

private:
    void loop();
    void handleMessage(const char* data, size_t len);
    void handlePacket(const char* data, size_t len, int depth);
    std::thread th_;
    std::atomic<bool> running_{false}, stop_{false};
    uintptr_t sock_ = ~(uintptr_t)0;
    std::mutex mx_;
    std::unordered_map<std::string, float> pending_, blends_;
    Quat neck_{}, head_{};
    bool hasHead_ = false;
    std::atomic<double> lastPacket_{-10};
    int port_ = 0;
};
