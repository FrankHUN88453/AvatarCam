#pragma once
#include <d3d11.h>
#include <wrl/client.h>

// GPU időmérés időbélyeg lekérdezésekkel. Néhány képkockányi késéssel olvassa vissza az
// eredményt, így nem várakoztatja a CPU-t a GPU-ra.
class GpuTimer {
public:
    enum { kMarks = 8, kFrames = 5 };
    void init(ID3D11Device* dev);
    void begin(ID3D11DeviceContext* ctx);        // képkocka eleje
    void mark(ID3D11DeviceContext* ctx, int i);  // az i. szakasz vége (1..kMarks-1)
    void end(ID3D11DeviceContext* ctx);
    float ms[kMarks] = {};  // simított szakaszidők: ms[i] = az i. jel és az előző jel közötti idő
    float totalMs = 0;

private:
    struct Frame {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint, ts[kMarks];
        bool used[kMarks] = {};
        bool pending = false;
    };
    void collect(ID3D11DeviceContext* ctx, Frame& f);
    Frame f_[kFrames];
    int cur_ = 0;
    bool ok_ = false, inFrame_ = false;
};
