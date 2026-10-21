#include "gfx/Gfx.h"
#include "core/Util.h"
#include <d3dcompiler.h>
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "core/I18n.h"

bool Gfx::init(std::string& err) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2, D3D11_SDK_VERSION, &dev_, &got, &ctx_);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, 1, D3D11_SDK_VERSION, &dev_, &got, &ctx_);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels + 1, 1, D3D11_SDK_VERSION, &dev_, &got, &ctx_);
    if (FAILED(hr)) {
        err = T("Could not create the Direct3D 11 device.");
        return false;
    }
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(ctx_.As(&mt))) mt->SetMultithreadProtected(TRUE);
    ComPtr<IDXGIDevice> dxgiDev;
    dev_.As(&dxgiDev);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDev->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&factory_));
    DXGI_ADAPTER_DESC ad;
    adapter->GetDesc(&ad);
    logMsg("GPU: %s", narrow(ad.Description).c_str());

    auto rs = [&](D3D11_CULL_MODE cull, ComPtr<ID3D11RasterizerState>& out, int bias = 0, float slope = 0) {
        D3D11_RASTERIZER_DESC d = {};
        d.FillMode = D3D11_FILL_SOLID;
        d.CullMode = cull;
        d.FrontCounterClockwise = TRUE;
        d.DepthClipEnable = TRUE;
        d.MultisampleEnable = TRUE;
        d.AntialiasedLineEnable = FALSE;
        d.DepthBias = bias;
        d.SlopeScaledDepthBias = slope;
        dev_->CreateRasterizerState(&d, &out);
    };
    rs(D3D11_CULL_BACK, rsCullBack);
    rs(D3D11_CULL_FRONT, rsCullFront);
    rs(D3D11_CULL_NONE, rsCullNone);
    rs(D3D11_CULL_NONE, rsShadow, 800, 2.0f);

    auto bs = [&](ComPtr<ID3D11BlendState>& out, bool enable, D3D11_BLEND src, D3D11_BLEND dst, D3D11_BLEND srcA, D3D11_BLEND dstA, bool a2c = false, UINT mask = D3D11_COLOR_WRITE_ENABLE_ALL) {
        D3D11_BLEND_DESC d = {};
        d.AlphaToCoverageEnable = a2c;
        auto& rt = d.RenderTarget[0];
        rt.BlendEnable = enable;
        rt.SrcBlend = src;
        rt.DestBlend = dst;
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = srcA;
        rt.DestBlendAlpha = dstA;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = (UINT8)mask;
        dev_->CreateBlendState(&d, &out);
    };
    bs(bsOpaque, false, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_ZERO);
    bs(bsAlpha, true, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA);
    bs(bsAdd, true, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE);
    bs(bsAlphaToCoverage, false, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, true);
    bs(bsPremul, true, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA);
    bs(bsNoColor, false, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, false, 0);

    auto ds = [&](ComPtr<ID3D11DepthStencilState>& out, bool test, bool write) {
        D3D11_DEPTH_STENCIL_DESC d = {};
        d.DepthEnable = test;
        d.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        d.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        dev_->CreateDepthStencilState(&d, &out);
    };
    ds(dsLessWrite, true, true);
    ds(dsLessNoWrite, true, false);
    ds(dsNone, false, false);

    auto ss = [&](ComPtr<ID3D11SamplerState>& out, D3D11_FILTER f, D3D11_TEXTURE_ADDRESS_MODE addr, bool cmp = false) {
        D3D11_SAMPLER_DESC d = {};
        d.Filter = f;
        d.AddressU = d.AddressV = d.AddressW = addr;
        d.MaxAnisotropy = f == D3D11_FILTER_ANISOTROPIC ? 8 : 1;
        d.ComparisonFunc = cmp ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER;
        d.MaxLOD = D3D11_FLOAT32_MAX;
        if (cmp) {
            d.BorderColor[0] = d.BorderColor[1] = d.BorderColor[2] = d.BorderColor[3] = 1.0f;
        }
        dev_->CreateSamplerState(&d, &out);
    };
    ss(sLinearWrap, D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP);
    ss(sLinearClamp, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
    ss(sPointClamp, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    ss(sShadow, D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_BORDER, true);

    uint8_t white[4] = {255, 255, 255, 255};
    white_ = createTextureRGBA8(white, 1, 1, false, false);
    uint8_t flat[4] = {128, 128, 255, 255};
    flatNormal_ = createTextureRGBA8(flat, 1, 1, false, false);
    return true;
}

ComPtr<ID3D11Buffer> Gfx::createBuffer(UINT size, UINT bind, D3D11_USAGE usage, const void* data, UINT cpuAccess, UINT misc, UINT stride) {
    D3D11_BUFFER_DESC d = {};
    d.ByteWidth = size;
    d.BindFlags = bind;
    d.Usage = usage;
    d.CPUAccessFlags = cpuAccess;
    d.MiscFlags = misc;
    d.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA sd = {data, 0, 0};
    ComPtr<ID3D11Buffer> b;
    dev_->CreateBuffer(&d, data ? &sd : nullptr, &b);
    return b;
}

void Gfx::update(ID3D11Buffer* buf, const void* data, size_t size) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx_->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, data, size);
        ctx_->Unmap(buf, 0);
    }
}

GpuTextureRef Gfx::createTextureRGBA8(const uint8_t* px, int w, int h, bool srgb, bool mips) {
    int levels = 1;
    if (mips) {
        int s = std::max(w, h);
        while (s > 1) {
            s >>= 1;
            levels++;
        }
    }
    std::vector<std::vector<uint8_t>> chain;
    std::vector<D3D11_SUBRESOURCE_DATA> init(levels);
    init[0] = {px, (UINT)(w * 4), 0};
    chain.reserve(levels);
    int cw = w, ch = h;
    const uint8_t* prev = px;
    for (int l = 1; l < levels; l++) {
        int nw = std::max(1, cw / 2), nh = std::max(1, ch / 2);
        chain.emplace_back((size_t)nw * nh * 4);
        if (srgb)
            stbir_resize_uint8_srgb(prev, cw, ch, 0, chain.back().data(), nw, nh, 0, STBIR_RGBA);
        else
            stbir_resize_uint8_linear(prev, cw, ch, 0, chain.back().data(), nw, nh, 0, STBIR_RGBA);
        init[l] = {chain.back().data(), (UINT)(nw * 4), 0};
        prev = chain.back().data();
        cw = nw;
        ch = nh;
    }
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = levels;
    d.ArraySize = 1;
    d.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    auto t = std::make_shared<GpuTexture>();
    if (FAILED(dev_->CreateTexture2D(&d, init.data(), &t->tex))) return nullptr;
    dev_->CreateShaderResourceView(t->tex.Get(), nullptr, &t->srv);
    t->w = w;
    t->h = h;
    t->fmt = d.Format;
    return t;
}

GpuTextureRef Gfx::loadTextureMemory(const void* data, size_t size, bool srgb) {
    int w, h, c;
    stbi_uc* px = stbi_load_from_memory((const stbi_uc*)data, (int)size, &w, &h, &c, 4);
    if (!px) return nullptr;
    auto t = createTextureRGBA8(px, w, h, srgb, true);
    stbi_image_free(px);
    return t;
}

GpuTextureRef Gfx::loadTextureFile(const std::wstring& path, bool srgb) {
    std::string bytes;
    if (!readFile(path, bytes)) return nullptr;
    return loadTextureMemory(bytes.data(), bytes.size(), srgb);
}

GpuTexture Gfx::createRenderTarget(int w, int h, DXGI_FORMAT fmt, int samples, bool srv) {
    GpuTexture t;
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = std::max(1, w);
    d.Height = std::max(1, h);
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = fmt;
    d.SampleDesc.Count = samples;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | (srv ? D3D11_BIND_SHADER_RESOURCE : 0);
    if (FAILED(dev_->CreateTexture2D(&d, nullptr, &t.tex))) return t;
    dev_->CreateRenderTargetView(t.tex.Get(), nullptr, &t.rtv);
    if (srv) dev_->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv);
    t.w = d.Width;
    t.h = d.Height;
    t.samples = samples;
    t.fmt = fmt;
    return t;
}

GpuTexture Gfx::createDepth(int w, int h, int samples, bool srv) {
    GpuTexture t;
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = std::max(1, w);
    d.Height = std::max(1, h);
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = srv ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_D32_FLOAT;
    d.SampleDesc.Count = samples;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_DEPTH_STENCIL | (srv ? D3D11_BIND_SHADER_RESOURCE : 0);
    if (FAILED(dev_->CreateTexture2D(&d, nullptr, &t.tex))) return t;
    D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.ViewDimension = samples > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    dev_->CreateDepthStencilView(t.tex.Get(), &dd, &t.dsv);
    if (srv) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_R32_FLOAT;
        sd.ViewDimension = samples > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        dev_->CreateShaderResourceView(t.tex.Get(), &sd, &t.srv);
    }
    t.w = d.Width;
    t.h = d.Height;
    t.samples = samples;
    t.fmt = d.Format;
    return t;
}

bool Gfx::compileShader(Shader& out, const char* src, const char* vsEntry, const char* psEntry, const D3D11_INPUT_ELEMENT_DESC* layout, UINT layoutCount,
                        const D3D_SHADER_MACRO* defines, std::string& err) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& blob) {
        ComPtr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(src, strlen(src), nullptr, defines, nullptr, entry, target, flags, 0, &blob, &errors);
        if (FAILED(hr)) {
            err = std::string(entry) + ": " + (errors ? std::string((const char*)errors->GetBufferPointer(), errors->GetBufferSize()) : "ismeretlen hiba");
            return false;
        }
        return true;
    };
    if (vsEntry) {
        ComPtr<ID3DBlob> vb;
        if (!compile(vsEntry, "vs_5_0", vb)) return false;
        dev_->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &out.vs);
        if (layout && layoutCount) dev_->CreateInputLayout(layout, layoutCount, vb->GetBufferPointer(), vb->GetBufferSize(), &out.layout);
    }
    if (psEntry) {
        ComPtr<ID3DBlob> pb;
        if (!compile(psEntry, "ps_5_0", pb)) return false;
        dev_->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &out.ps);
    }
    return true;
}
