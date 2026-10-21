#pragma once
#include "gfx/Gfx.h"
#include <string>
#include <memory>
#include "core/I18n.h"

class spoutDX;

// Spout2 küldő (D3D11 megosztott textúra). OBS-ben a "Spout2 Plugin for OBS" bővítménnyel fogadható.
class SpoutSender {
public:
    SpoutSender();
    ~SpoutSender();
    bool send(Gfx& gfx, ID3D11Texture2D* tex, int w, int h, const std::string& name);
    void release();
    bool active() const { return active_; }
    std::string status = T("Not enabled");

private:
    std::unique_ptr<spoutDX> sx_;
    std::string name_;
    bool active_ = false;
};
