#include "tracking/Mic.h"
#include "core/Math.h"
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <ksmedia.h>
#include "core/I18n.h"

using Microsoft::WRL::ComPtr;

namespace {
struct ComScope {
    bool init;
    ComScope() { init = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~ComScope() {
        if (init) CoUninitialize();
    }
};

std::string friendlyName(IMMDevice* d) {
    ComPtr<IPropertyStore> ps;
    std::string n;
    if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) n = narrow(v.pwszVal);
        PropVariantClear(&v);
    }
    return n;
}
}  // namespace

std::vector<std::string> enumerateMicrophones() {
    ComScope com;
    std::vector<std::string> out;
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return out;
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col))) return out;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; i++) {
        ComPtr<IMMDevice> d;
        if (SUCCEEDED(col->Item(i, &d))) out.push_back(friendlyName(d.Get()));
    }
    return out;
}

MicLipSync::~MicLipSync() { stop(); }

bool MicLipSync::start(const std::string& name, std::string& err) {
    stop();
    stop_ = false;
    openState_ = 0;
    running_ = true;
    th_ = std::thread([this, name] { loop(name); });
    for (int i = 0; i < 300 && openState_ == 0; i++) Sleep(10);
    if (openState_ != 1) {
        err = openErr_.empty() ? T("The microphone could not be opened") : openErr_;
        stop();
        return false;
    }
    return true;
}

void MicLipSync::stop() {
    stop_ = true;
    if (th_.joinable()) th_.join();
    running_ = false;
    std::lock_guard<std::mutex> lk(mx_);
    for (auto& v : vis_) v = 0;
    level_ = 0;
}

void MicLipSync::get(float visemes[15], float& level) {
    std::lock_guard<std::mutex> lk(mx_);
    for (int i = 0; i < 15; i++) visemes[i] = vis_[i];
    level = level_;
}

void MicLipSync::loop(std::string name) {
    ComScope com;
    auto fail = [&](const char* e) {
        openErr_ = e;
        openState_ = -1;
        running_ = false;
    };
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return fail(T("Audio system error"));
    ComPtr<IMMDevice> dev;
    if (!name.empty()) {
        ComPtr<IMMDeviceCollection> col;
        if (SUCCEEDED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col))) {
            UINT n = 0;
            col->GetCount(&n);
            for (UINT i = 0; i < n && !dev; i++) {
                ComPtr<IMMDevice> d;
                if (SUCCEEDED(col->Item(i, &d)) && friendlyName(d.Get()) == name) dev = d;
            }
        }
    }
    if (!dev && FAILED(en->GetDefaultAudioEndpoint(eCapture, eCommunications, &dev)) && FAILED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev)))
        return fail(T("No microphone"));
    ComPtr<IAudioClient> client;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)client.GetAddressOf()))) return fail(T("The microphone could not be activated"));
    WAVEFORMATEX* fmt = nullptr;
    client->GetMixFormat(&fmt);
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, fmt, nullptr))) {
        CoTaskMemFree(fmt);
        return fail(T("The microphone format is not supported"));
    }
    bool isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                   (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ((WAVEFORMATEXTENSIBLE*)fmt)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    int channels = fmt->nChannels, bits = fmt->wBitsPerSample;
    float rate = (float)fmt->nSamplesPerSec;
    CoTaskMemFree(fmt);
    ComPtr<IAudioCaptureClient> cap;
    if (FAILED(client->GetService(IID_PPV_ARGS(&cap)))) return fail(T("The microphone could not be read"));
    client->Start();
    status = format("%s (%.0f Hz)", name.empty() ? T("default") : name.c_str(), rate);
    openState_ = 1;

    // elemzés 20 ms-os ablakokban
    std::vector<float> win;
    size_t winLen = (size_t)(rate * 0.02f);
    float lp = 0, prevS = 0;
    float sm[15] = {};
    while (!stop_) {
        Sleep(5);
        UINT32 packet = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            for (UINT32 i = 0; i < frames; i++) {
                float s = 0;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                    for (int c = 0; c < channels; c++) {
                        if (isFloat && bits == 32) s += ((float*)data)[i * channels + c];
                        else if (bits == 16) s += ((int16_t*)data)[i * channels + c] / 32768.0f;
                        else if (bits == 32) s += ((int32_t*)data)[i * channels + c] / 2147483648.0f;
                        else if (bits == 24) {
                            const uint8_t* p = data + ((size_t)i * channels + c) * 3;
                            int v = (p[0] << 8 | p[1] << 16 | p[2] << 24) >> 8;
                            s += v / 8388608.0f;
                        }
                    }
                    s /= channels;
                }
                win.push_back(s);
            }
            cap->ReleaseBuffer(frames);
        }
        while (win.size() >= winLen) {
            double e = 0, eLow = 0;
            int zc = 0;
            for (size_t i = 0; i < winLen; i++) {
                float s = win[i];
                e += s * s;
                lp += (s - lp) * 0.08f;  // kb. 600 Hz aluláteresztő 48 kHz-en
                eLow += lp * lp;
                if ((s >= 0) != (prevS >= 0)) zc++;
                prevS = s;
            }
            win.erase(win.begin(), win.begin() + winLen);
            float rms = (float)std::sqrt(e / winLen) * gain_;
            raw_ = rms;
            float zcr = (float)zc / winLen;
            float lowRatio = e > 1e-12 ? (float)(eLow / e) : 0;
            float level = saturate((rms - threshold_) * 9.0f);
            float tgt[15] = {};
            if (level > 0.01f) {
                if (zcr > 0.25f && lowRatio < 0.15f) {
                    tgt[7] = level * 0.8f;  // ss
                    tgt[11] = level * 0.3f;
                } else if (lowRatio > 0.75f) {
                    tgt[14] = level * 0.7f;  // ou
                    tgt[13] = level * 0.6f;  // oh
                } else if (lowRatio < 0.35f) {
                    tgt[11] = level * 0.7f;  // e
                    tgt[12] = level * 0.5f;  // ih
                    tgt[10] = level * 0.4f;
                } else
                    tgt[10] = level;  // aa
            }
            for (int k = 0; k < 15; k++) sm[k] += (tgt[k] - sm[k]) * (tgt[k] > sm[k] ? 0.6f : 0.25f);
            std::lock_guard<std::mutex> lk(mx_);
            for (int k = 0; k < 15; k++) vis_[k] = sm[k];
            level_ = level_ * 0.6f + level * 0.4f;
        }
    }
    client->Stop();
    running_ = false;
}
