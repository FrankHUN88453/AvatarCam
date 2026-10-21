#include "tracking/Webcam.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include "stb_image.h"
#include "core/I18n.h"

using Microsoft::WRL::ComPtr;

namespace {
struct MfInit {
    bool ok = false;
    MfInit() { ok = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET)); }
    ~MfInit() {
        if (ok) MFShutdown();
    }
};
MfInit& mf() {
    static MfInit m;
    return m;
}

struct ComScope {
    bool init;
    ComScope() { init = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~ComScope() {
        if (init) CoUninitialize();
    }
};

std::vector<ComPtr<IMFActivate>> listDevices() {
    std::vector<ComPtr<IMFActivate>> out;
    mf();
    ComPtr<IMFAttributes> attr;
    if (FAILED(MFCreateAttributes(&attr, 1))) return out;
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devs = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attr.Get(), &devs, &count))) return out;
    for (UINT32 i = 0; i < count; i++) {
        out.emplace_back(devs[i]);
        devs[i]->Release();
    }
    CoTaskMemFree(devs);
    return out;
}

std::string deviceName(IMFActivate* a) {
    WCHAR* name = nullptr;
    UINT32 len = 0;
    std::string n;
    if (SUCCEEDED(a->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len))) {
        n = narrow(name);
        CoTaskMemFree(name);
    }
    return n;
}
}  // namespace

std::vector<std::string> enumerateCameras() {
    ComScope com;
    std::vector<std::string> names;
    for (auto& d : listDevices()) names.push_back(deviceName(d.Get()));
    return names;
}

Webcam::~Webcam() { close(); }

bool Webcam::open(const std::string& name, int index, int wantW, int wantH, std::string& err) {
    close();
    stop_ = false;
    openState_ = 0;
    running_ = true;
    th_ = std::thread([=] { loop(name, index, wantW, wantH); });
    // megvárjuk a megnyitást (max. 8 mp)
    for (int i = 0; i < 800 && openState_ == 0; i++) Sleep(10);
    if (openState_ != 1) {
        err = openErr_.empty() ? T("The camera could not be opened") : openErr_;
        close();
        return false;
    }
    return true;
}

void Webcam::waitFrame(uint64_t afterIndex, int timeoutMs) {
    std::unique_lock<std::mutex> lk(mx_);
    cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return frame_.index > afterIndex || stop_ || !running_; });
}

void Webcam::close() {
    stop_ = true;
    cv_.notify_all();
    if (th_.joinable()) th_.join();
    running_ = false;
}

bool Webcam::latest(CameraFrame& out, uint64_t afterIndex) {
    std::lock_guard<std::mutex> lk(mx_);
    if (frame_.index <= afterIndex || frame_.bgra.empty()) return false;
    out = frame_;
    return true;
}

void Webcam::loop(std::string name, int index, int wantW, int wantH) {
    ComScope com;
    // teszteléshez: AVATARCAM_FAKECAM=<képfájl> esetén a kép ismétlődő képkockaként
    if (const char* fake = getenv("AVATARCAM_FAKECAM")) {
        int w = 0, h = 0, c = 0;
        unsigned char* px = stbi_load(fake, &w, &h, &c, 4);
        if (!px) {
            openErr_ = "Could not load the test image";
            openState_ = -1;
            running_ = false;
            return;
        }
        std::vector<uint8_t> bgra((size_t)w * h * 4);
        for (int i = 0; i < w * h; i++) bgra[i * 4] = px[i * 4 + 2], bgra[i * 4 + 1] = px[i * 4 + 1], bgra[i * 4 + 2] = px[i * 4], bgra[i * 4 + 3] = 255;
        stbi_image_free(px);
        status = format("test image %dx%d", w, h);
        openState_ = 1;
        uint64_t idx = 0;
        while (!stop_) {
            {
                std::lock_guard<std::mutex> lk(mx_);
                frame_.w = w, frame_.h = h, frame_.bgra = bgra, frame_.index = ++idx, frame_.time = nowSeconds();
            }
            cv_.notify_all();
            fps_ = 30;
            Sleep(33);
        }
        running_ = false;
        return;
    }
    auto fail = [&](const std::string& e) {
        openErr_ = e;
        openState_ = -1;
        running_ = false;
    };
    auto devs = listDevices();
    if (devs.empty()) return fail(T("No webcam found"));
    int pick = -1;
    for (size_t i = 0; i < devs.size(); i++)
        if (!name.empty() && deviceName(devs[i].Get()) == name) pick = (int)i;
    if (pick < 0) pick = std::clamp(index, 0, (int)devs.size() - 1);
    ComPtr<IMFMediaSource> source;
    if (FAILED(devs[pick]->ActivateObject(IID_PPV_ARGS(&source)))) return fail(T("The camera is busy or unavailable"));
    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 2);
    attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    attr->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromMediaSource(source.Get(), attr.Get(), &reader))) return fail(T("Could not create the camera reader"));

    // a kívánt felbontáshoz legközelebbi natív formátum (lehetőleg 30+ fps)
    DWORD bestIdx = 0;
    double bestScore = 1e18;
    for (DWORD i = 0;; i++) {
        ComPtr<IMFMediaType> t;
        if (FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &t))) break;
        UINT32 w = 0, h = 0, num = 0, den = 1;
        MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, &num, &den);
        double fps = den ? (double)num / den : 0;
        double score = std::abs((double)w * h - (double)wantW * wantH) / 1000.0 + (fps < 24 ? 5000 : 0) - fps;
        if (score < bestScore) bestScore = score, bestIdx = i;
    }
    ComPtr<IMFMediaType> native;
    reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, bestIdx, &native);
    reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, native.Get());
    ComPtr<IMFMediaType> rgb;
    MFCreateMediaType(&rgb);
    rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, rgb.Get()))) return fail(T("The camera's image format cannot be converted"));
    ComPtr<IMFMediaType> cur;
    reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur);
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &w, &h);
    INT32 stride = (INT32)MFGetAttributeUINT32(cur.Get(), MF_MT_DEFAULT_STRIDE, (UINT32)(w * 4));
    status = format("%ux%u", w, h);
    logMsg("Camera opened: %s (%ux%u)", deviceName(devs[pick].Get()).c_str(), w, h);
    openState_ = 1;

    uint64_t idx = 0;
    double fpsT = nowSeconds();
    int fpsN = 0;
    std::vector<uint8_t> buf((size_t)w * h * 4);
    while (!stop_) {
        DWORD stream = 0, flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &ts, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            status = T("The camera connection was lost");
            break;
        }
        if (!sample) continue;
        ComPtr<IMFMediaBuffer> mb;
        if (FAILED(sample->ConvertToContiguousBuffer(&mb))) continue;
        BYTE* data = nullptr;
        LONG pitch = stride;
        ComPtr<IMF2DBuffer> b2;
        bool locked2d = SUCCEEDED(mb.As(&b2)) && SUCCEEDED(b2->Lock2D(&data, &pitch));
        DWORD len = 0;
        if (!locked2d && FAILED(mb->Lock(&data, nullptr, &len))) continue;
        for (UINT32 y = 0; y < h; y++) {
            const BYTE* row;
            if (locked2d)
                row = data + (ptrdiff_t)y * pitch;  // a Lock2D mindig a felső sorra mutat
            else
                row = stride >= 0 ? data + (size_t)y * stride : data + (size_t)(h - 1 - y) * (size_t)(-stride);
            memcpy(&buf[(size_t)y * w * 4], row, (size_t)w * 4);
        }
        if (locked2d) b2->Unlock2D();
        else mb->Unlock();
        {
            std::lock_guard<std::mutex> lk(mx_);
            frame_.w = w;
            frame_.h = h;
            frame_.bgra = buf;
            frame_.index = ++idx;
            frame_.time = nowSeconds();
        }
        cv_.notify_all();
        fpsN++;
        double now = nowSeconds();
        if (now - fpsT > 1) {
            fps_ = (float)(fpsN / (now - fpsT));
            fpsT = now;
            fpsN = 0;
        }
    }
    reader.Reset();
    source->Shutdown();
    running_ = false;
}
