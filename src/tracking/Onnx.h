#pragma once
#include "core/Util.h"
#include <vector>
#include <cstdint>

// ONNX Runtime (C API) vékony burkoló. A onnxruntime.dll-t futásidőben, az exe mappájából tölti be,
// így ha hiányzik, a program követés nélkül még működik.
struct OnnxOutput {
    std::vector<float> data;
    std::vector<int64_t> shape;
};

class OnnxSession {
public:
    ~OnnxSession();
    static bool runtimeAvailable(std::string* err = nullptr);
    bool load(const fs::path& model, int threads, std::string& err);
    bool run(const std::vector<float>& input, const std::vector<int64_t>& shape, std::vector<OnnxOutput>& outputs, std::string* err = nullptr);
    bool loaded() const { return session_ != nullptr; }
    const std::string& inputName() const { return inputName_; }
    const std::vector<std::string>& outputNames() const { return outputNames_; }

private:
    struct OrtSession* session_ = nullptr;
    std::string inputName_;
    std::vector<std::string> outputNames_;
};
