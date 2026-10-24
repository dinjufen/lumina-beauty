#include "LmOrtModel.hpp"

#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include "onnxruntime_c_api.h"
#include "LmLog.hpp"

namespace {

// 进程内所有会话共用一个环境，按引用计数释放
std::mutex g_envMutex;
const OrtApi* g_api = nullptr;
OrtEnv* g_env = nullptr;
int g_envRefs = 0;

bool Check(OrtStatus* status, const char* what) {
    if (!status) {
        return true;
    }
    LM_LOG_E << "onnxruntime " << what << " failed: " << g_api->GetErrorMessage(status);
    g_api->ReleaseStatus(status);
    return false;
}

bool AcquireEnv() {
    std::lock_guard<std::mutex> lock(g_envMutex);
    if (!g_api) {
        g_api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (!g_api) {
            // 进程里加载到了版本更老的 onnxruntime.dll（例如系统目录自带的那份）
            LM_LOG_E << "onnxruntime api version " << ORT_API_VERSION << " is not available";
            return false;
        }
    }
    if (!g_env && !Check(g_api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "lumina", &g_env), "CreateEnv")) {
        return false;
    }
    ++g_envRefs;
    return true;
}

void ReleaseEnv() {
    std::lock_guard<std::mutex> lock(g_envMutex);
    if (g_envRefs > 0 && --g_envRefs == 0 && g_env) {
        g_api->ReleaseEnv(g_env);
        g_env = nullptr;
    }
}

#ifdef _WIN32
std::wstring ToOrtPath(const std::string& utf8) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
    if (length > 1) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
    }
    return wide;
}
#else
std::string ToOrtPath(const std::string& utf8) {
    return utf8;
}
#endif

} // namespace

struct LmOrtModel::Impl {
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;
    std::string inputName;
    std::vector<std::string> outputNames;
    std::vector<OrtValue*> values;
};

LmOrtModel::LmOrtModel() {}

LmOrtModel::~LmOrtModel() {
    Release();
}

bool LmOrtModel::Init(const std::string& path, int threads) {
    Release();
    if (!AcquireEnv()) {
        return false;
    }

    Impl* impl = new Impl();
    m_impl = impl;

    OrtSessionOptions* options = nullptr;
    bool ok = Check(g_api->CreateSessionOptions(&options), "CreateSessionOptions");
    if (ok) {
        Check(g_api->SetIntraOpNumThreads(options, threads), "SetIntraOpNumThreads");
        // 线程池默认在两次推理之间自旋等待，会一直占着 CPU 并拖慢渲染线程，关掉
        Check(g_api->AddSessionConfigEntry(options, "session.intra_op.allow_spinning", "0"), "AllowSpinning");
        Check(g_api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL), "SetOptimizationLevel");
        ok = Check(g_api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &impl->memory),
                   "CreateCpuMemoryInfo") &&
             Check(g_api->CreateSession(g_env, ToOrtPath(path).c_str(), options, &impl->session), "CreateSession");
    }
    if (options) {
        g_api->ReleaseSessionOptions(options);
    }

    OrtAllocator* allocator = nullptr;
    char* name = nullptr;
    size_t outputCount = 0;
    ok = ok && Check(g_api->GetAllocatorWithDefaultOptions(&allocator), "GetAllocator") &&
         Check(g_api->SessionGetInputName(impl->session, 0, allocator, &name), "GetInputName");
    if (ok) {
        impl->inputName = name;
        g_api->AllocatorFree(allocator, name);
        ok = Check(g_api->SessionGetOutputCount(impl->session, &outputCount), "GetOutputCount");
    }
    for (size_t i = 0; ok && i < outputCount; ++i) {
        ok = Check(g_api->SessionGetOutputName(impl->session, i, allocator, &name), "GetOutputName");
        if (ok) {
            impl->outputNames.push_back(name);
            g_api->AllocatorFree(allocator, name);
        }
    }

    if (!ok) {
        LM_LOG_E << "Failed to load model " << path;
        Release();
        return false;
    }
    return true;
}

void LmOrtModel::ReleaseOutputs() {
    if (m_impl) {
        for (OrtValue* value : m_impl->values) {
            if (value) {
                g_api->ReleaseValue(value);
            }
        }
        m_impl->values.clear();
    }
    m_outputs.clear();
}

void LmOrtModel::Release() {
    if (!m_impl) {
        return;
    }
    ReleaseOutputs();
    if (m_impl->session) g_api->ReleaseSession(m_impl->session);
    if (m_impl->memory) g_api->ReleaseMemoryInfo(m_impl->memory);
    delete m_impl;
    m_impl = nullptr;
    ReleaseEnv();
}

bool LmOrtModel::Run(float* input, int height, int width) {
    if (!m_impl || !input) {
        return false;
    }
    ReleaseOutputs();

    const int64_t shape[4] = {1, height, width, 3};
    OrtValue* inputValue = nullptr;
    if (!Check(g_api->CreateTensorWithDataAsOrtValue(m_impl->memory, input,
                                                     sizeof(float) * static_cast<size_t>(height) * width * 3, shape,
                                                     4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputValue),
               "CreateTensor")) {
        return false;
    }

    const char* inputNames[1] = {m_impl->inputName.c_str()};
    std::vector<const char*> outputNames;
    for (const std::string& name : m_impl->outputNames) {
        outputNames.push_back(name.c_str());
    }
    m_impl->values.assign(outputNames.size(), nullptr);
    const bool ok = Check(g_api->Run(m_impl->session, nullptr, inputNames, &inputValue, 1, outputNames.data(),
                                     outputNames.size(), m_impl->values.data()),
                          "Run");
    g_api->ReleaseValue(inputValue);
    if (!ok) {
        ReleaseOutputs();
        return false;
    }

    for (OrtValue* value : m_impl->values) {
        float* data = nullptr;
        size_t count = 0;
        OrtTensorTypeAndShapeInfo* info = nullptr;
        if (!Check(g_api->GetTensorMutableData(value, reinterpret_cast<void**>(&data)), "GetTensorData") ||
            !Check(g_api->GetTensorTypeAndShape(value, &info), "GetTensorShape")) {
            ReleaseOutputs();
            return false;
        }
        Check(g_api->GetTensorShapeElementCount(info, &count), "GetElementCount");
        g_api->ReleaseTensorTypeAndShapeInfo(info);
        m_outputs.push_back(std::make_pair(static_cast<const float*>(data), count));
    }
    return true;
}
