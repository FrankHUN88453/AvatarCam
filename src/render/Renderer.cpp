#include "render/Renderer.h"
#include "render/GpuTimer.h"
#include "render/Shaders.h"
#include "avatar/Avatar.h"
#include "app/Settings.h"
#include "core/Util.h"
#include <algorithm>

namespace {
struct LightGPU {
    float posType[4], dirRange[4], color[4], spot[4];
};
struct FrameCBData {
    Mat4 viewProj, view, shadowVP;
    float cameraPos[4], ambientSky[4], ambientGround[4], shadeTint[4], rimParams[4], shadowParams[4], outlineParams[4], viewport[4], misc[4];
    LightGPU lights[8];
};
struct ObjectCBData {
    float objParams[4];
};
struct MaterialCBData {
    float baseColor[4], emissive[4], hsv[4], pbr[4], uvST[4], alphaST[4], shade[4], rim[4], flags[4], flags2[4];
    float mcColor[4][4], mcParams[4][4], mcParams2[4][4];
    float flags3[4];
};
struct GroundCBData {
    Mat4 viewProj, shadowVP;
    float center[4], params[4];
};
struct PostCBData {
    float texel[4], bloomP[4], bg1[4], bg2[4], bgP[4], tone[4], outP[4];
};
void set4(float* d, float a, float b, float c, float e) {
    d[0] = a, d[1] = b, d[2] = c, d[3] = e;
}
void set4(float* d, const Vec3& v, float w) { set4(d, v.x, v.y, v.z, w); }
Vec3 lin(const Vec3& c) { return srgbToLinear(c); }
}  // namespace

bool Renderer::init(Gfx& gfx, std::string& err) {
    gfx_ = &gfx;
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 1, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (!gfx.compileShader(mesh_, kMeshShader, "VSMain", "PSMain", layout, 6, nullptr, err)) return false;
    if (!gfx.compileShader(outline_, kMeshShader, "VSOutline", "PSOutline", layout, 6, nullptr, err)) return false;
    if (!gfx.compileShader(shadow_, kMeshShader, "VSShadow", nullptr, layout, 6, nullptr, err)) return false;
    if (!gfx.compileShader(shadowMask_, kMeshShader, "VSShadowMask", "PSShadowMask", layout, 6, nullptr, err)) return false;
    if (!gfx.compileShader(ground_, kGroundShader, "VSGround", "PSGround", nullptr, 0, nullptr, err)) return false;
    if (!gfx.compileShader(bright_, kPostShader, "VSFull", "PSBright", nullptr, 0, nullptr, err)) return false;
    if (!gfx.compileShader(down_, kPostShader, nullptr, "PSDown", nullptr, 0, nullptr, err)) return false;
    if (!gfx.compileShader(up_, kPostShader, nullptr, "PSUp", nullptr, 0, nullptr, err)) return false;
    if (!gfx.compileShader(composite_, kPostShader, nullptr, "PSComposite", nullptr, 0, nullptr, err)) return false;
    if (!gfx.compileShader(copy_, kPostShader, nullptr, "PSCopy", nullptr, 0, nullptr, err)) return false;
    frameCB_ = gfx.createConstantBuffer(sizeof(FrameCBData));
    objectCB_ = gfx.createConstantBuffer(sizeof(ObjectCBData));
    materialCB_ = gfx.createConstantBuffer(sizeof(MaterialCBData));
    groundCB_ = gfx.createConstantBuffer(sizeof(GroundCBData));
    postCB_ = gfx.createConstantBuffer(sizeof(PostCBData));
    shadowMap_ = gfx.createDepth(2048, 2048, 1, true);
    return true;
}

void Renderer::ensureTargets(int w, int h, int msaa) {
    w = std::clamp(w, 64, 7680);
    h = std::clamp(h, 64, 4320);
    if (msaa != 1 && msaa != 2 && msaa != 4 && msaa != 8) msaa = 4;
    if (w == w_ && h == h_ && msaa == msaa_) return;
    UINT q = 0;
    while (msaa > 1 && (FAILED(gfx_->dev()->CheckMultisampleQualityLevels(DXGI_FORMAT_R16G16B16A16_FLOAT, msaa, &q)) || q == 0)) msaa /= 2;
    w_ = w, h_ = h, msaa_ = msaa;
    if (msaa > 1) {
        sceneMS_ = gfx_->createRenderTarget(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, msaa, false);
        glowMS_ = gfx_->createRenderTarget(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, msaa, false);
        depthMS_ = gfx_->createDepth(w, h, msaa, false);
    } else {
        sceneMS_ = GpuTexture{};
        glowMS_ = GpuTexture{};
        depthMS_ = gfx_->createDepth(w, h, 1, false);
    }
    scene_ = gfx_->createRenderTarget(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, true);
    glow_ = gfx_->createRenderTarget(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, true);
    out_ = gfx_->createRenderTarget(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 1, true);
    int bw = w / 2, bh = h / 2;
    for (int i = 0; i < 6; i++) {
        bloom_[i] = gfx_->createRenderTarget(std::max(1, bw), std::max(1, bh), DXGI_FORMAT_R11G11B10_FLOAT, 1, true);
        bw /= 2, bh /= 2;
    }
}

void Renderer::setBackgroundImage(const std::string& path) {
    if (path == bgPath_) return;
    bgPath_ = path;
    bgTex_ = path.empty() ? nullptr : gfx_->loadTextureFile(widen(path), false);
    if (!path.empty() && !bgTex_) logMsg("Could not load the background image: %s", path.c_str());
}

void Renderer::drawAvatar(Avatar& av, int pass, const Settings& s) {
    auto* ctx = gfx_->ctx();
    struct Item {
        const AvMesh* mesh;
        const ModelSubmesh* sm;
        const AvMaterial* mat;
        float dist;
        AlphaMode mode;
    };
    std::vector<Item> items;
    for (auto& m : av.meshes) {
        if (!m.visible) continue;
        for (auto& sm : m.src->submeshes) {
            int mi = sm.slot < (int)m.mats.size() ? m.mats[sm.slot] : -1;
            if (mi < 0) continue;
            const AvMaterial& mat = av.materials[mi];
            AlphaMode mode = mat.alphaMode;
            if (mat.cur.opacity < 0.995f && (mode == AlphaMode::Opaque || mode == AlphaMode::Mask)) mode = AlphaMode::Blend;
            if (mat.cur.opacity <= 0.001f) continue;
            bool want = false;
            switch (pass) {
                case 0: want = mode == AlphaMode::Opaque; break;
                case 1: want = mode == AlphaMode::Mask; break;
                case 2: want = mode == AlphaMode::Blend || mode == AlphaMode::Add; break;
                case 3: want = mode == AlphaMode::Opaque || mode == AlphaMode::Mask; break;
                case 4: want = mode == AlphaMode::Opaque || mode == AlphaMode::Mask; break;
            }
            if (!want) continue;
            items.push_back({&m, &sm, &mat, 0, mode});
        }
    }
    if (pass == 2) {
        for (auto& it : items) it.dist = length(it.mesh->centerWorld - camEye_);
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            if (a.mat->renderQueue != b.mat->renderQueue) return a.mat->renderQueue < b.mat->renderQueue;
            return a.dist > b.dist;
        });
    }
    const Shader& sh = pass == 3 ? outline_ : pass == 4 ? shadowMask_ : mesh_;
    ctx->IASetInputLayout(sh.layout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(sh.vs.Get(), nullptr, 0);
    ctx->PSSetShader(sh.ps.Get(), nullptr, 0);
    ID3D11Buffer* cbs[3] = {frameCB_.Get(), objectCB_.Get(), materialCB_.Get()};
    ctx->VSSetConstantBuffers(0, 3, cbs);
    ctx->PSSetConstantBuffers(0, 3, cbs);
    ID3D11SamplerState* samp[3] = {gfx_->sLinearWrap.Get(), gfx_->sLinearClamp.Get(), gfx_->sShadow.Get()};
    ctx->PSSetSamplers(0, 3, samp);
    const AvMesh* lastMesh = nullptr;
    const AvMaterial* lastMat = nullptr;
    auto srv = [&](const GpuTextureRef& t, const GpuTextureRef& fallback) -> ID3D11ShaderResourceView* {
        return t ? t->srv.Get() : (fallback ? fallback->srv.Get() : nullptr);
    };
    for (auto& it : items) {
        if (it.mesh != lastMesh) {
            lastMesh = it.mesh;
            ID3D11Buffer* vbs[2] = {it.mesh->vbPosNrm.Get(), it.mesh->vbStatic.Get()};
            UINT strides[2] = {24, 48}, offs[2] = {0, 0};
            ctx->IASetVertexBuffers(0, 2, vbs, strides, offs);
            ctx->IASetIndexBuffer(it.mesh->ib.Get(), DXGI_FORMAT_R32_UINT, 0);
            ID3D11ShaderResourceView* b = it.mesh->bonesSrv.Get();
            ctx->VSSetShaderResources(0, 1, &b);
        }
        if (it.mat != lastMat) {
            lastMat = it.mat;
            const AvMaterial& m = *it.mat;
            const MatParams& p = m.cur;
            MaterialCBData d = {};
            Vec3 bc(p.color[0], p.color[1], p.color[2]);
            set4(d.baseColor, bc, p.opacity);
            Vec3 em(p.emissive[0], p.emissive[1], p.emissive[2]);
            set4(d.emissive, em * p.emissiveIntensity, 0);
            set4(d.hsv, p.hueShift, p.saturation, p.brightness, p.hueShiftOn);
            set4(d.pbr, p.metalness, p.roughness, p.normalScale, p.alphaCutoff);
            memcpy(d.uvST, m.uvST, 16);
            // a Unity alfa maszk ST-t is át kell váltani D3D UV-re
            if (m.flipV)
                set4(d.alphaST, p.alphaMapST[0], p.alphaMapST[1], p.alphaMapST[2], 1.0f - p.alphaMapST[1] - p.alphaMapST[3]);
            else
                set4(d.alphaST, p.alphaMapST[0], p.alphaMapST[1], p.alphaMapST[2], p.alphaMapST[3]);
            if (p.shade[0] >= 0) set4(d.shade, p.shade[0], p.shade[1], p.shade[2], 1);
            set4(d.rim, p.rim[0], p.rim[1], p.rim[2], 0);
            set4(d.flags, (float)(int)it.mode, m.map ? 1.f : 0.f, m.normalMap ? 1.f : 0.f, m.emissiveMap ? 1.f : 0.f);
            set4(d.flags2, m.alphaMap ? 1.f : 0.f, m.unlit ? 1.f : 0.f, 1.0f, m.side == 2 ? 1.f : 0.f);
            for (int k = 0; k < 4; k++) {
                const MatcapLayer& L = p.matcaps[k];
                set4(d.mcColor[k], L.color[0] * L.intensity, L.color[1] * L.intensity, L.color[2] * L.intensity, L.enabled);
                set4(d.mcParams[k], L.replace, L.multiply, L.add, L.emission);
                set4(d.mcParams2[k], L.hueShift, L.hueShiftOn, L.hasTex ? 1.f : 0.f, L.hasMask ? 1.f : 0.f);
            }
            set4(d.flags3, p.emissiveUseBase, m.emissiveMask ? 1.f : 0.f, 0, 0);
            gfx_->update(materialCB_.Get(), &d, sizeof(d));
            auto white = gfx_->whiteTexture();
            ID3D11ShaderResourceView* t[12] = {srv(m.map, white), srv(m.normalMap, gfx_->flatNormalTexture()), srv(m.emissiveMap, white), srv(m.alphaMap, white),
                                               srv(m.matcapTex[0], white), srv(m.matcapTex[1], white), srv(m.matcapTex[2], white), srv(m.matcapTex[3], white),
                                               srv(m.matcapMask[0], white), srv(m.matcapMask[1], white), srv(m.matcapMask[2], white), srv(m.matcapMask[3], white)};
            ctx->PSSetShaderResources(0, 12, t);
            ID3D11ShaderResourceView* emMask = srv(m.emissiveMask, white);
            ctx->PSSetShaderResources(13, 1, &emMask);
            // állapotok
            if (pass == 3) {
                ctx->RSSetState(gfx_->rsCullFront.Get());
                ctx->OMSetBlendState(gfx_->bsOpaque.Get(), nullptr, 0xffffffff);
                ctx->OMSetDepthStencilState(gfx_->dsLessWrite.Get(), 0);
            } else if (pass == 4) {
                ctx->RSSetState(gfx_->rsShadow.Get());
            } else {
                ID3D11RasterizerState* rs = m.side == 2 ? gfx_->rsCullNone.Get() : m.side == 1 ? gfx_->rsCullFront.Get() : gfx_->rsCullBack.Get();
                ctx->RSSetState(rs);
                if (it.mode == AlphaMode::Opaque) {
                    ctx->OMSetBlendState(gfx_->bsOpaque.Get(), nullptr, 0xffffffff);
                    ctx->OMSetDepthStencilState(gfx_->dsLessWrite.Get(), 0);
                } else if (it.mode == AlphaMode::Mask) {
                    ctx->OMSetBlendState(msaa_ > 1 ? gfx_->bsAlphaToCoverage.Get() : gfx_->bsOpaque.Get(), nullptr, 0xffffffff);
                    ctx->OMSetDepthStencilState(gfx_->dsLessWrite.Get(), 0);
                } else if (it.mode == AlphaMode::Add) {
                    ctx->OMSetBlendState(gfx_->bsAdd.Get(), nullptr, 0xffffffff);
                    ctx->OMSetDepthStencilState(gfx_->dsLessNoWrite.Get(), 0);
                } else {
                    ctx->OMSetBlendState(gfx_->bsAlpha.Get(), nullptr, 0xffffffff);
                    ctx->OMSetDepthStencilState(gfx_->dsLessNoWrite.Get(), 0);
                }
            }
        }
        ctx->DrawIndexed(it.sm->count, it.sm->start, 0);
    }
}

void Renderer::render(Avatar* av, const Settings& s, const CameraState& cam, float time) {
    ensureTargets(s.output.width, s.output.height, s.effects.msaa);
    camEye_ = cam.eye;
    auto* ctx = gfx_->ctx();
    const EffectSettings& fx = s.effects;

    // fények
    FrameCBData f = {};
    int lc = 0, mainLight = -1, shadowLight = -1;
    Vec3 camFwd = normalize(cam.target - cam.eye);
    float camYaw = std::atan2(-camFwd.x, -camFwd.z);
    for (auto& L : s.lights) {
        if (!L.enabled || lc >= 8) continue;
        LightGPU& g = f.lights[lc];
        float yaw = (L.yaw + (L.followCamera ? camYaw * kRad2Deg : 0)) * kDeg2Rad, pitch = L.pitch * kDeg2Rad;
        Vec3 dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
        lightDirs_[lc] = dir;
        Vec3 col = lin(L.color) * L.intensity;
        if (L.type == 0) {
            set4(g.posType, 0, 0, 0, 0);
            set4(g.dirRange, dir, 0);
            if (mainLight < 0) mainLight = lc;
            if (L.shadow && shadowLight < 0 && fx.shadows) shadowLight = lc;
        } else {
            set4(g.posType, L.position, (float)L.type);
            set4(g.dirRange, dir, L.range);  // spot: a fény a pozícióból a dir ellentétes irányába világít
            if (L.type == 2) {
                // a spot az avatár felé néz: irány = pozíció - célpont
                Vec3 aim = normalize(L.position - Vec3(0, 1.2f, 0));
                set4(g.dirRange, aim * -1.0f, L.range);
                float outer = std::cos(L.spotAngle * kDeg2Rad);
                float inner = std::cos(L.spotAngle * (1 - L.spotSoftness) * kDeg2Rad);
                set4(g.spot, outer, inner, 0, 0);
            }
        }
        set4(g.color, col, 0);
        if (lc == shadowLight) g.spot[2] = 1;
        lc++;
    }
    if (mainLight < 0) mainLight = 0;

    Mat4 view = cam.view(), proj = cam.proj();
    f.viewProj = proj * view;
    f.view = view;
    // árnyék mátrix
    Vec3 center(0, 1.0f, 0);
    float radius = 1.3f;
    if (av) {
        Vec3 bmin = av->boundsMin, bmax = av->boundsMax;
        Vec3 c = av->nodes[0].world.transformPoint((bmin + bmax) * 0.5f);
        center = c;
        radius = std::max(0.6f, length(bmax - bmin) * 0.55f * s.pose.scale);
    }
    Mat4 shadowVP;
    if (shadowLight >= 0) {
        Vec3 d = lightDirs_[shadowLight];
        Vec3 up = std::fabs(d.y) > 0.95f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
        Mat4 lv = Mat4::lookAt(center + d * 6.0f, center, up);
        Mat4 lp = Mat4::ortho(-radius, radius, -radius, radius, 0.5f, 12.0f);
        shadowVP = lp * lv;
    }
    f.shadowVP = shadowVP;
    set4(f.cameraPos, cam.eye, time);
    set4(f.ambientSky, lin(fx.ambientSky) * fx.ambientIntensity, fx.toonThreshold);
    set4(f.ambientGround, lin(fx.ambientGround) * fx.ambientIntensity, std::max(0.001f, fx.toonSoftness));
    set4(f.shadeTint, fx.shadeTint, (float)fx.shading);
    set4(f.rimParams, fx.rim ? lin(fx.rimColor) * fx.rimIntensity : Vec3(0, 0, 0), fx.rimPower);
    set4(f.shadowParams, shadowLight >= 0 ? 1.f : 0.f, fx.shadowStrength, 1.0f / 2048, 0.0015f);
    set4(f.outlineParams, lin(fx.outlineColor), fx.outlineWidth);
    set4(f.viewport, (float)w_, (float)h_, 1.0f / w_, 1.0f / h_);
    set4(f.misc, (float)lc, fx.emissionScale, (float)mainLight, (float)fx.matcapFallback);
    gfx_->update(frameCB_.Get(), &f, sizeof(f));

    ID3D11ShaderResourceView* nullSrv[14] = {};
    ctx->PSSetShaderResources(0, 14, nullSrv);

    // árnyék
    if (av && shadowLight >= 0) {
        ctx->OMSetRenderTargets(0, nullptr, shadowMap_.dsv.Get());
        ctx->ClearDepthStencilView(shadowMap_.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        D3D11_VIEWPORT vp = {0, 0, (float)shadowMap_.w, (float)shadowMap_.h, 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetDepthStencilState(gfx_->dsLessWrite.Get(), 0);
        ctx->OMSetBlendState(gfx_->bsNoColor.Get(), nullptr, 0xffffffff);
        drawAvatar(*av, 4, s);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
    if (timer) timer->mark(ctx, 1);

    // fő jelenet
    // a második (glow) célpont csak az emisszió alapú bloomhoz kell; nélküle a shader írása elvész
    bool needGlow = fx.bloom && fx.bloomEmissiveOnly;
    ID3D11RenderTargetView* rtvs[2] = {msaa_ > 1 ? sceneMS_.rtv.Get() : scene_.rtv.Get(), msaa_ > 1 ? glowMS_.rtv.Get() : glow_.rtv.Get()};
    float clear[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(rtvs[0], clear);
    if (needGlow) ctx->ClearRenderTargetView(rtvs[1], clear);
    ctx->ClearDepthStencilView(depthMS_.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->OMSetRenderTargets(needGlow ? 2 : 1, rtvs, depthMS_.dsv.Get());
    D3D11_VIEWPORT vp = {0, 0, (float)w_, (float)h_, 0, 1};
    ctx->RSSetViewports(1, &vp);
    if (av) {
        ID3D11ShaderResourceView* sm = shadowLight >= 0 ? shadowMap_.srv.Get() : nullptr;
        ctx->PSSetShaderResources(12, 1, &sm);
        drawAvatar(*av, 0, s);
        drawAvatar(*av, 1, s);
        if (fx.outline) drawAvatar(*av, 3, s);
        // talaj árnyék
        if (fx.groundShadow) {
            GroundCBData g;
            g.viewProj = f.viewProj;
            g.shadowVP = shadowVP;
            Vec3 foot = av->nodes[0].world.getTranslation();
            set4(g.center, foot.x, foot.y + 0.002f, foot.z, 0.9f * s.pose.scale);
            set4(g.params, fx.groundShadowOpacity, shadowLight >= 0 ? 1.f : 0.f, 0.8f, 1.0f / 2048);
            gfx_->update(groundCB_.Get(), &g, sizeof(g));
            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(ground_.vs.Get(), nullptr, 0);
            ctx->PSSetShader(ground_.ps.Get(), nullptr, 0);
            ID3D11Buffer* gcb = groundCB_.Get();
            ctx->VSSetConstantBuffers(0, 1, &gcb);
            ctx->PSSetConstantBuffers(0, 1, &gcb);
            ID3D11ShaderResourceView* gs = shadowLight >= 0 ? shadowMap_.srv.Get() : gfx_->whiteTexture()->srv.Get();
            if (shadowLight < 0) gs = nullptr;
            ctx->PSSetShaderResources(0, 1, &gs);
            ID3D11SamplerState* ss = gfx_->sShadow.Get();
            ctx->PSSetSamplers(0, 1, &ss);
            ctx->RSSetState(gfx_->rsCullNone.Get());
            ctx->OMSetBlendState(gfx_->bsAlpha.Get(), nullptr, 0xffffffff);
            ctx->OMSetDepthStencilState(gfx_->dsLessNoWrite.Get(), 0);
            ctx->Draw(6, 0);
            ctx->PSSetShaderResources(0, 1, nullSrv);
        }
        drawAvatar(*av, 2, s);
    }
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->PSSetShaderResources(0, 14, nullSrv);
    if (msaa_ > 1) {
        ctx->ResolveSubresource(scene_.tex.Get(), 0, sceneMS_.tex.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
        if (needGlow) ctx->ResolveSubresource(glow_.tex.Get(), 0, glowMS_.tex.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
    }
    if (timer) timer->mark(ctx, 2);

    // utófeldolgozás
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(bright_.vs.Get(), nullptr, 0);
    ctx->RSSetState(gfx_->rsCullNone.Get());
    ctx->OMSetDepthStencilState(gfx_->dsNone.Get(), 0);
    ID3D11SamplerState* ps[2] = {gfx_->sLinearClamp.Get(), gfx_->sPointClamp.Get()};
    ctx->PSSetSamplers(0, 2, ps);
    ID3D11Buffer* pcb = postCB_.Get();
    ctx->PSSetConstantBuffers(0, 1, &pcb);
    PostCBData p = {};
    auto pass = [&](const Shader& sh, GpuTexture& dst, ID3D11ShaderResourceView* src, int srcW, int srcH, ID3D11BlendState* bs) {
        set4(p.texel, 1.0f / srcW, 1.0f / srcH, 0, fx.bloomRadius * 1.5f + 0.25f);
        gfx_->update(postCB_.Get(), &p, sizeof(p));
        ID3D11RenderTargetView* r = dst.rtv.Get();
        ctx->OMSetRenderTargets(1, &r, nullptr);
        D3D11_VIEWPORT v = {0, 0, (float)dst.w, (float)dst.h, 0, 1};
        ctx->RSSetViewports(1, &v);
        float r0 = std::clamp(fx.bloomRadius, 0.0f, 1.0f) * 0.85f;
        float bfac[4] = {r0, r0, r0, r0};
        ctx->OMSetBlendState(bs, bfac, 0xffffffff);
        ctx->PSSetShader(sh.ps.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &src);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* n = nullptr;
        ctx->PSSetShaderResources(0, 1, &n);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    };
    // forrás: csak a világító részek (glow puffer), vagy a teljes kép fényes részei
    float rr = std::clamp(fx.bloomRadius, 0.0f, 1.0f) * 0.85f, norm = 0, wgt = 1;
    for (int i = 0; i < 6; i++) norm += wgt, wgt *= rr;
    set4(p.bloomP, fx.bloomEmissiveOnly ? 0.05f : fx.bloomThreshold, 0.5f, fx.bloomStrength / norm, fx.bloom ? 1.f : 0.f);
    if (fx.bloom) {
        pass(bright_, bloom_[0], fx.bloomEmissiveOnly ? glow_.srv.Get() : scene_.srv.Get(), w_, h_, gfx_->bsOpaque.Get());
        for (int i = 1; i < 6; i++) pass(down_, bloom_[i], bloom_[i - 1].srv.Get(), bloom_[i - 1].w, bloom_[i - 1].h, gfx_->bsOpaque.Get());
        // felskálázás: a kisebb (szélesebb) szint súlyozva adódik a nagyobbhoz (blend factor)
        static ComPtr<ID3D11BlendState> addBlend;
        if (!addBlend) {
            D3D11_BLEND_DESC bd = {};
            bd.RenderTarget[0].BlendEnable = TRUE;
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
            bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            gfx_->dev()->CreateBlendState(&bd, &addBlend);
        }
        float r = std::clamp(fx.bloomRadius, 0.0f, 1.0f) * 0.85f;
        float bf[4] = {r, r, r, r};
        for (int i = 5; i > 0; i--) {
            pass(up_, bloom_[i - 1], bloom_[i].srv.Get(), bloom_[i].w, bloom_[i].h, addBlend.Get());
            (void)bf;
        }
    }
    // kompozitálás
    const BackgroundSettings& bg = s.background;
    Vec3 c1 = bg.mode == 4 ? lin(bg.chroma) : lin(bg.color);
    set4(p.bg1, c1, (float)bg.mode);
    set4(p.bg2, lin(bg.color2), bg.gradientAngle * kDeg2Rad);
    float imgAspect = bgTex_ ? (float)bgTex_->w / std::max(1, bgTex_->h) : 1;
    set4(p.bgP, (float)bg.imageFit, bg.imageDim, imgAspect, (float)w_ / h_);
    set4(p.tone, (float)fx.toneMap, fx.exposure, fx.contrast, fx.saturation);
    set4(p.outP, s.output.straightAlpha ? 1.f : 0.f, s.output.mirror ? 1.f : 0.f, bgTex_ ? 1.f : 0.f, 0);
    set4(p.texel, 1.0f / w_, 1.0f / h_, 0, 0);
    gfx_->update(postCB_.Get(), &p, sizeof(p));
    ID3D11RenderTargetView* o = out_.rtv.Get();
    ctx->OMSetRenderTargets(1, &o, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(gfx_->bsOpaque.Get(), nullptr, 0xffffffff);
    ctx->PSSetShader(composite_.ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srcs[3] = {scene_.srv.Get(), bloom_[0].srv.Get(), bgTex_ ? bgTex_->srv.Get() : nullptr};
    ctx->PSSetShaderResources(0, 3, srcs);
    ctx->Draw(3, 0);
    ctx->PSSetShaderResources(0, 3, nullSrv);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    if (timer) timer->mark(ctx, 3);
}

void Renderer::blit(ID3D11RenderTargetView* rtv, int w, int h, bool letterbox) {
    auto* ctx = gfx_->ctx();
    float clear[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    float x = 0, y = 0, vw = (float)w, vh = (float)h;
    if (letterbox && w_ > 0 && h_ > 0) {
        float a = (float)w_ / h_, b = (float)w / h;
        if (b > a) vw = h * a, x = (w - vw) * 0.5f;
        else vh = w / a, y = (h - vh) * 0.5f;
    }
    D3D11_VIEWPORT vp = {x, y, vw, vh, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(copy_.vs ? copy_.vs.Get() : bright_.vs.Get(), nullptr, 0);
    ctx->PSSetShader(copy_.ps.Get(), nullptr, 0);
    ctx->RSSetState(gfx_->rsCullNone.Get());
    ctx->OMSetDepthStencilState(gfx_->dsNone.Get(), 0);
    ctx->OMSetBlendState(gfx_->bsOpaque.Get(), nullptr, 0xffffffff);
    ID3D11SamplerState* ss = gfx_->sLinearClamp.Get();
    ctx->PSSetSamplers(0, 1, &ss);
    ID3D11ShaderResourceView* srv = out_.srv.Get();
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* n = nullptr;
    ctx->PSSetShaderResources(0, 1, &n);
}
