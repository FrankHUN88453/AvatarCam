#include "render/GpuTimer.h"

void GpuTimer::init(ID3D11Device* dev) {
    ok_ = true;
    for (auto& f : f_) {
        D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        if (FAILED(dev->CreateQuery(&qd, &f.disjoint))) ok_ = false;
        qd.Query = D3D11_QUERY_TIMESTAMP;
        for (auto& t : f.ts)
            if (FAILED(dev->CreateQuery(&qd, &t))) ok_ = false;
    }
}

void GpuTimer::collect(ID3D11DeviceContext* ctx, Frame& f) {
    if (!f.pending) return;
    f.pending = false;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
    // ha még nincs kész, ezt a mintát eldobjuk (nem várunk)
    if (ctx->GetData(f.disjoint.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK || dj.Disjoint || !dj.Frequency) return;
    UINT64 t[kMarks] = {};
    for (int i = 0; i < kMarks; i++)
        if (f.used[i] && ctx->GetData(f.ts[i].Get(), &t[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
    double toMs = 1000.0 / (double)dj.Frequency;
    UINT64 prev = t[0], first = t[0], lastT = t[0];
    for (int i = 1; i < kMarks; i++) {
        float v = 0;
        if (f.used[i]) {
            v = (float)((double)(t[i] - prev) * toMs);
            prev = lastT = t[i];
        }
        ms[i] = ms[i] * 0.92f + v * 0.08f;
    }
    totalMs = totalMs * 0.92f + (float)((double)(lastT - first) * toMs) * 0.08f;
}

void GpuTimer::begin(ID3D11DeviceContext* ctx) {
    if (!ok_) return;
    Frame& f = f_[cur_];
    collect(ctx, f);  // a legrégebbi képkocka eredménye
    for (bool& u : f.used) u = false;
    ctx->Begin(f.disjoint.Get());
    ctx->End(f.ts[0].Get());
    f.used[0] = true;
    inFrame_ = true;
}

void GpuTimer::mark(ID3D11DeviceContext* ctx, int i) {
    if (!ok_ || !inFrame_ || i <= 0 || i >= kMarks) return;
    Frame& f = f_[cur_];
    ctx->End(f.ts[i].Get());
    f.used[i] = true;
}

void GpuTimer::end(ID3D11DeviceContext* ctx) {
    if (!ok_ || !inFrame_) return;
    Frame& f = f_[cur_];
    ctx->End(f.disjoint.Get());
    f.pending = true;
    inFrame_ = false;
    cur_ = (cur_ + 1) % kFrames;
}
