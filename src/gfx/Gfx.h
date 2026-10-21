#pragma once
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;

struct GpuTexture {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    int w = 0, h = 0, samples = 1;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    bool valid() const { return tex != nullptr; }
};
using GpuTextureRef = std::shared_ptr<GpuTexture>;

struct Shader {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
};

class Gfx {
public:
    bool init(std::string& err);
    ID3D11Device* dev() const { return dev_.Get(); }
    ID3D11DeviceContext* ctx() const { return ctx_.Get(); }
    IDXGIFactory2* factory() const { return factory_.Get(); }

    ComPtr<ID3D11Buffer> createBuffer(UINT size, UINT bind, D3D11_USAGE usage, const void* data = nullptr, UINT cpuAccess = 0, UINT misc = 0, UINT stride = 0);
    ComPtr<ID3D11Buffer> createConstantBuffer(UINT size) {
        return createBuffer((size + 15) & ~15u, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, nullptr, D3D11_CPU_ACCESS_WRITE);
    }
    void update(ID3D11Buffer* buf, const void* data, size_t size);

    // RGBA8 textúra CPU-n generált mipmapekkel (szálbiztos: csak az eszközt használja)
    GpuTextureRef createTextureRGBA8(const uint8_t* px, int w, int h, bool srgb, bool mips);
    GpuTextureRef loadTextureFile(const std::wstring& path, bool srgb);
    GpuTextureRef loadTextureMemory(const void* data, size_t size, bool srgb);
    GpuTextureRef whiteTexture() const { return white_; }
    GpuTextureRef flatNormalTexture() const { return flatNormal_; }

    GpuTexture createRenderTarget(int w, int h, DXGI_FORMAT fmt, int samples = 1, bool srv = true);
    GpuTexture createDepth(int w, int h, int samples, bool srv = false);

    bool compileShader(Shader& out, const char* src, const char* vsEntry, const char* psEntry, const D3D11_INPUT_ELEMENT_DESC* layout, UINT layoutCount,
                       const D3D_SHADER_MACRO* defines, std::string& err);

    // gyakran használt állapotok
    ComPtr<ID3D11RasterizerState> rsCullBack, rsCullFront, rsCullNone, rsShadow;
    ComPtr<ID3D11BlendState> bsOpaque, bsAlpha, bsAdd, bsAlphaToCoverage, bsPremul, bsNoColor;
    ComPtr<ID3D11DepthStencilState> dsLessWrite, dsLessNoWrite, dsNone;
    ComPtr<ID3D11SamplerState> sLinearWrap, sLinearClamp, sPointClamp, sShadow;

private:
    ComPtr<ID3D11Device> dev_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGIFactory2> factory_;
    GpuTextureRef white_, flatNormal_;
};
