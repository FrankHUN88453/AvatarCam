#pragma once
#include "core/Util.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

struct CameraFrame {
    int w = 0, h = 0;
    std::vector<uint8_t> bgra;  // felülről lefelé, BGRA
    uint64_t index = 0;
    double time = 0;
};

std::vector<std::string> enumerateCameras();

// Webkamera rögzítés Media Foundation-nel (külön szálon)
class Webcam {
public:
    ~Webcam();
    bool open(const std::string& name, int index, int wantW, int wantH, std::string& err);
    void close();
    bool isOpen() const { return running_; }
    bool latest(CameraFrame& out, uint64_t afterIndex);  // true, ha van újabb képkocka
    void waitFrame(uint64_t afterIndex, int timeoutMs);  // vár, amíg újabb képkocka érkezik (vagy lejár az idő)
    float fps() const { return fps_; }
    std::string status;

private:
    void loop(std::string name, int index, int wantW, int wantH);
    std::thread th_;
    std::atomic<bool> running_{false}, stop_{false};
    std::mutex mx_;
    std::condition_variable cv_;
    CameraFrame frame_;
    float fps_ = 0;
    std::atomic<int> openState_{0};  // 0 folyamatban, 1 kész, -1 hiba
    std::string openErr_;
};
