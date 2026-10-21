#pragma once
#include "gfx/Gfx.h"
#include "core/Math.h"
#include <string>

class Avatar;
struct Settings;

struct CameraState {
    Vec3 eye{0, 1.4f, 1.5f}, target{0, 1.35f, 0};
    Vec3 up{0, 1, 0};
    float fovY = 30 * kDeg2Rad;
    float aspect = 16.0f / 9.0f;
    Mat4 view() const { return Mat4::lookAt(eye, target, up); }
    Mat4 proj() const { return Mat4::perspective(fovY, aspect, 0.03f, 60.0f); }
};

class Renderer {
public:
    bool init(Gfx& gfx, std::string& err);
    void render(Avatar* av, const Settings& s, const CameraState& cam, float time);
    GpuTexture& output() { return out_; }
    int width() const { return w_; }
    int height() const { return h_; }
    void setBackgroundImage(const std::string& utf8Path);
    std::string backgroundImagePath() const { return bgPath_; }
    bool backgroundImageOk() const { return bgTex_ != nullptr; }
    ID3D11ShaderResourceView* backgroundSrv() const { return bgTex_ ? bgTex_->srv.Get() : nullptr; }
    float backgroundAspect() const { return bgTex_ && bgTex_->h ? (float)bgTex_->w / bgTex_->h : 1.0f; }
    // a kimeneti kép másolása egy másik render targetre (pl. kimeneti ablak), arány-helyesen
    void blit(ID3D11RenderTargetView* rtv, int w, int h, bool letterbox);
    Vec3 lightDirection(int i) const { return i < 8 ? lightDirs_[i] : Vec3(0, 1, 0); }
    float lastGpuMs = 0;
    class GpuTimer* timer = nullptr;  // opcionális GPU időmérés

private:
    void ensureTargets(int w, int h, int msaa);
    void drawAvatar(Avatar& av, int pass, const Settings& s);  // 0 opak, 1 maszk, 2 átlátszó, 3 körvonal, 4 árnyék
    Gfx* gfx_ = nullptr;
    Shader mesh_, outline_, shadow_, shadowMask_, ground_;
    Shader bright_, down_, up_, composite_, copy_;
    ComPtr<ID3D11Buffer> frameCB_, objectCB_, materialCB_, groundCB_, postCB_;
    GpuTexture sceneMS_, depthMS_, scene_, out_, shadowMap_, glowMS_, glow_;
    GpuTexture bloom_[6];
    int w_ = 0, h_ = 0, msaa_ = 0;
    GpuTextureRef bgTex_;
    std::string bgPath_;
    Vec3 lightDirs_[8];
    Vec3 camEye_{0, 0, 0};
};
