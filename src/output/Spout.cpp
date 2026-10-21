#include "output/Spout.h"
#include "core/Util.h"
#include "SpoutDX.h"
#include "core/I18n.h"

SpoutSender::SpoutSender() = default;
SpoutSender::~SpoutSender() { release(); }

bool SpoutSender::send(Gfx& gfx, ID3D11Texture2D* tex, int, int, const std::string& name) {
    if (!sx_) {
        sx_ = std::make_unique<spoutDX>();
        if (!sx_->OpenDirectX11(gfx.dev())) {
            status = T("Spout could not be started");
            sx_.reset();
            return false;
        }
    }
    std::string n = name.empty() ? "AvatarCam" : name;
    if (n != name_) {
        if (active_) sx_->ReleaseSender();
        sx_->SetSenderName(n.c_str());
        name_ = n;
        active_ = false;
    }
    bool ok = sx_->SendTexture(tex);
    if (ok && !active_) logMsg("Spout sender started: %s", n.c_str());
    active_ = ok;
    status = ok ? T("sending") : T("send error");
    return ok;
}

void SpoutSender::release() {
    if (sx_) {
        if (active_) sx_->ReleaseSender();
        sx_->CloseDirectX11();
        sx_.reset();
    }
    active_ = false;
    name_.clear();
    status = T("Not enabled");
}
