#include "tracking/Vmc.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "core/I18n.h"

namespace {
struct WsaInit {
    WsaInit() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
};
uint32_t be32(const char* p) {
    return (uint32_t)(uint8_t)p[0] << 24 | (uint32_t)(uint8_t)p[1] << 16 | (uint32_t)(uint8_t)p[2] << 8 | (uint32_t)(uint8_t)p[3];
}
size_t pad4(size_t n) { return (n + 3) & ~(size_t)3; }
}  // namespace

VmcReceiver::~VmcReceiver() { stop(); }

bool VmcReceiver::start(int port, std::string& err) {
    static WsaInit wsa;
    stop();
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        err = T("UDP socket error");
        return false;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(s);
        err = format("A %d port foglalt", port);
        return false;
    }
    DWORD timeout = 200;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    sock_ = (uintptr_t)s;
    port_ = port;
    stop_ = false;
    running_ = true;
    status = format(T("Waiting on port %d…"), port);
    th_ = std::thread([this] { loop(); });
    return true;
}

void VmcReceiver::stop() {
    stop_ = true;
    if (th_.joinable()) th_.join();
    if (sock_ != ~(uintptr_t)0) closesocket((SOCKET)sock_);
    sock_ = ~(uintptr_t)0;
    running_ = false;
}

void VmcReceiver::loop() {
    std::vector<char> buf(65536);
    while (!stop_) {
        int n = recv((SOCKET)sock_, buf.data(), (int)buf.size(), 0);
        if (n <= 0) continue;
        lastPacket_ = nowSeconds();
        handlePacket(buf.data(), (size_t)n, 0);
    }
}

void VmcReceiver::handlePacket(const char* d, size_t len, int depth) {
    if (len < 8 || depth > 4) return;
    if (memcmp(d, "#bundle", 8) == 0) {
        size_t p = 16;  // "#bundle\0" + időbélyeg
        while (p + 4 <= len) {
            uint32_t sz = be32(d + p);
            p += 4;
            if (p + sz > len) break;
            handlePacket(d + p, sz, depth + 1);
            p += sz;
        }
        return;
    }
    handleMessage(d, len);
}

void VmcReceiver::handleMessage(const char* d, size_t len) {
    size_t alen = strnlen(d, len);
    std::string addr(d, alen);
    size_t p = pad4(alen + 1);
    if (p >= len || d[p] != ',') return;
    size_t tlen = strnlen(d + p, len - p);
    std::string tags(d + p + 1, tlen - 1);
    p += pad4(tlen + 1);
    std::vector<float> f;
    std::vector<std::string> s;
    for (char t : tags) {
        if (t == 'f' || t == 'i') {
            if (p + 4 > len) return;
            uint32_t v = be32(d + p);
            p += 4;
            if (t == 'f') {
                float x;
                memcpy(&x, &v, 4);
                f.push_back(x);
            } else
                f.push_back((float)(int32_t)v);
        } else if (t == 's') {
            size_t sl = strnlen(d + p, len - p);
            s.emplace_back(d + p, sl);
            p += pad4(sl + 1);
        }
    }
    std::lock_guard<std::mutex> lk(mx_);
    if (addr == "/VMC/Ext/Blend/Val" && !s.empty() && !f.empty()) {
        pending_[toLower(s[0])] = f[0];
    } else if (addr == "/VMC/Ext/Blend/Apply") {
        blends_ = pending_;
        status = format(T("Receiving (%zu expressions)"), blends_.size());
    } else if (addr == "/VMC/Ext/Bone/Pos" && !s.empty() && f.size() >= 7) {
        // Unity (bal-kezes) lokális forgatás -> jobb-kezes
        Quat q(f[3], -f[4], -f[5], f[6]);
        if (s[0] == "Head") {
            head_ = q;
            hasHead_ = true;
        } else if (s[0] == "Neck")
            neck_ = q;
    }
}

bool VmcReceiver::get(std::unordered_map<std::string, float>& blends, Quat& head, bool& hasHead) {
    std::lock_guard<std::mutex> lk(mx_);
    blends = blends_;
    head = (neck_ * head_).normalized();
    hasHead = hasHead_;
    return active();
}
