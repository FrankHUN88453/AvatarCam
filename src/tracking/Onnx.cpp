#include "tracking/Onnx.h"
#include <windows.h>
#include <mutex>
#include "onnxruntime_c_api.h"
#include "core/I18n.h"

namespace {
const OrtApi* gApi = nullptr;
OrtEnv* gEnv = nullptr;
std::mutex gMx;
std::string gErr;

bool initRuntime() {
    std::lock_guard<std::mutex> lk(gMx);
    if (gApi) return true;
    if (!gErr.empty()) return false;
    // a rendszerben lehet régebbi onnxruntime.dll (Windows ML), ezért teljes útvonallal töltjük
    fs::path dll = exeDir() / L"onnxruntime.dll";
    HMODULE h = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) {
        gErr = T("onnxruntime.dll was not found next to the exe");
        return false;
    }
    using GetApiBase = const OrtApiBase*(ORT_API_CALL*)();
    auto fn = (GetApiBase)GetProcAddress(h, "OrtGetApiBase");
    if (!fn) {
        gErr = T("invalid onnxruntime.dll");
        return false;
    }
    const OrtApi* api = fn()->GetApi(ORT_API_VERSION);
    if (!api) {
        gErr = T("incompatible onnxruntime.dll version");
        return false;
    }
    OrtStatus* st = api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "avatarcam", &gEnv);
    if (st) {
        gErr = api->GetErrorMessage(st);
        api->ReleaseStatus(st);
        return false;
    }
    gApi = api;
    logMsg("ONNX Runtime loaded");
    return true;
}

bool check(OrtStatus* st, std::string* err) {
    if (!st) return true;
    if (err) *err = gApi->GetErrorMessage(st);
    gApi->ReleaseStatus(st);
    return false;
}
}  // namespace

bool OnnxSession::runtimeAvailable(std::string* err) {
    bool ok = initRuntime();
    if (!ok && err) *err = gErr;
    return ok;
}

OnnxSession::~OnnxSession() {
    if (session_ && gApi) gApi->ReleaseSession(session_);
}

bool OnnxSession::load(const fs::path& model, int threads, std::string& err) {
    if (!initRuntime()) {
        err = gErr;
        return false;
    }
    OrtSessionOptions* opts = nullptr;
    if (!check(gApi->CreateSessionOptions(&opts), &err)) return false;
    if (const char* e = getenv("AVATARCAM_ORT_THREADS")) threads = std::max(1, atoi(e));  // méréshez
    gApi->SetIntraOpNumThreads(opts, threads);
    gApi->SetInterOpNumThreads(opts, 1);
    // alapból az ONNX Runtime szálai munka után még pörögnek (aktívan várnak) – játék mellett ez
    // feleslegesen eszi a processzort, ezért kikapcsoljuk
    gApi->AddSessionConfigEntry(opts, "session.intra_op.allow_spinning", "0");
    gApi->AddSessionConfigEntry(opts, "session.inter_op.allow_spinning", "0");
    gApi->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL);
    gApi->SetSessionExecutionMode(opts, ORT_SEQUENTIAL);
    bool ok = check(gApi->CreateSession(gEnv, model.c_str(), opts, &session_), &err);
    gApi->ReleaseSessionOptions(opts);
    if (!ok) return false;
    OrtAllocator* alloc = nullptr;
    gApi->GetAllocatorWithDefaultOptions(&alloc);
    char* name = nullptr;
    if (check(gApi->SessionGetInputName(session_, 0, alloc, &name), &err)) {
        inputName_ = name;
        alloc->Free(alloc, name);
    }
    size_t nOut = 0;
    gApi->SessionGetOutputCount(session_, &nOut);
    for (size_t i = 0; i < nOut; i++) {
        if (check(gApi->SessionGetOutputName(session_, i, alloc, &name), &err)) {
            outputNames_.push_back(name);
            alloc->Free(alloc, name);
        }
    }
    return true;
}

bool OnnxSession::run(const std::vector<float>& input, const std::vector<int64_t>& shape, std::vector<OnnxOutput>& outputs, std::string* err) {
    if (!session_) return false;
    OrtMemoryInfo* mem = nullptr;
    if (!check(gApi->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem), err)) return false;
    OrtValue* in = nullptr;
    bool ok = check(gApi->CreateTensorWithDataAsOrtValue(mem, (void*)input.data(), input.size() * sizeof(float), shape.data(), shape.size(),
                                                         ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in),
                    err);
    gApi->ReleaseMemoryInfo(mem);
    if (!ok) return false;
    std::vector<const char*> outNames;
    for (auto& n : outputNames_) outNames.push_back(n.c_str());
    std::vector<OrtValue*> outs(outNames.size(), nullptr);
    const char* inName = inputName_.c_str();
    ok = check(gApi->Run(session_, nullptr, &inName, &in, 1, outNames.data(), outNames.size(), outs.data()), err);
    gApi->ReleaseValue(in);
    if (!ok) return false;
    outputs.resize(outs.size());
    for (size_t i = 0; i < outs.size(); i++) {
        float* p = nullptr;
        gApi->GetTensorMutableData(outs[i], (void**)&p);
        OrtTensorTypeAndShapeInfo* info = nullptr;
        gApi->GetTensorTypeAndShape(outs[i], &info);
        size_t dims = 0, count = 0;
        gApi->GetDimensionsCount(info, &dims);
        outputs[i].shape.resize(dims);
        gApi->GetDimensions(info, outputs[i].shape.data(), dims);
        gApi->GetTensorShapeElementCount(info, &count);
        gApi->ReleaseTensorTypeAndShapeInfo(info);
        outputs[i].data.assign(p, p + count);
        gApi->ReleaseValue(outs[i]);
    }
    return true;
}
