// C 接口的实现：参数、模型、GL 上下文与渲染管线的组装
#include "lumina/lumina.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "LmAvatar.hpp"
#include "LmFace.hpp"
#include "LmFaceDetector.hpp"
#include "LmGLContext.hpp"
#include "LmImageLoader.hpp"
#include "LmLog.hpp"
#include "LmLook.hpp"
#include "LmMakeup.hpp"
#include "LmParams.hpp"
#include "LmPipeline.hpp"
#include "LmSegmenter.hpp"
#include "LmTuning.hpp"

struct lumina_engine {
    std::string modelDir;

    // 参数：任意线程写入，处理线程按帧拷贝
    std::mutex paramsMutex;
    LmParams params;
    float values[LUMINA_PARAM_COUNT] = {0};
    std::vector<uint8_t> pendingBackground;  ///< 待上传的背景图，RGBA
    int pendingBackgroundW = 0;
    int pendingBackgroundH = 0;
    bool backgroundDirty = false;

    // 以下只在处理线程使用
    LmGLContext context;
    LmPipeline pipeline;
    bool glReady = false;
    bool glFailed = false;
    LmFaceDetector detector;
    bool detectorReady = false;
    LmSegmenter segmenter;
    std::vector<LmWarpOp> warpOps;
    std::vector<LmRegion> regions;
    std::vector<LmMakeupVertex> makeup;
    std::vector<LmAvatarFace> avatars;
    LmAvatarState avatarStates[LM_MAX_FACES];
    std::vector<uint8_t> packedIn;   ///< 非紧凑排列的输入整理成紧凑 I420
    std::vector<uint8_t> packedOut;
};

namespace {

float Clamp01(float v) {
    return std::min(std::max(v, 0.0f), 1.0f);
}

int ToIndex(float v, int count) {
    const int i = static_cast<int>(std::lround(v));
    return (i >= 0 && i < count) ? i : 0;
}

/// 把 C 接口的数值写进渲染参数
void Apply(LmParams& p, lumina_param param, float v) {
    switch (param) {
        case LUMINA_SMOOTH: p.smooth = Clamp01(v); break;
        case LUMINA_WHITEN: p.whiten = Clamp01(v); break;
        case LUMINA_SHARPEN: p.sharpen = Clamp01(v); break;
        case LUMINA_CLARITY: p.clarity = Clamp01(v); break;
        case LUMINA_EYE_SIZE: p.eyeSize = Clamp01(v); break;
        case LUMINA_FACE_SLIM: p.faceOverall = Clamp01(v); break;
        case LUMINA_FACE_NARROW: p.faceCut = Clamp01(v); break;
        case LUMINA_CHEEKBONE: p.cheekbone = Clamp01(v); break;
        case LUMINA_JAW: p.jaw = Clamp01(v); break;
        case LUMINA_NOSE: p.noseWing = Clamp01(v); break;
        case LUMINA_FOREHEAD: p.forehead = Clamp01(v); break;
        case LUMINA_SHRINK_HEAD: p.shrinkHead = Clamp01(v); break;
        case LUMINA_WHITEN_TEETH: p.whitenTeeth = Clamp01(v); break;
        case LUMINA_REMOVE_POUCH: p.removePouch = Clamp01(v); break;
        case LUMINA_REMOVE_SMILE_FOLDS: p.removeSmileFolds = Clamp01(v); break;
        case LUMINA_TEMPERATURE: p.qualityTemperature = Clamp01(v); break;
        case LUMINA_TINT: p.qualityHue = Clamp01(v); break;
        case LUMINA_SATURATION: p.qualitySaturation = Clamp01(v); break;
        case LUMINA_BRIGHTNESS: p.qualityLight = Clamp01(v); break;
        case LUMINA_CONTRAST: p.qualityContrast = Clamp01(v); break;
        case LUMINA_FILTER: p.filterType = ToIndex(v, LUMINA_FILTER_COUNT); break;
        case LUMINA_FILTER_INTENSITY: p.filterIntensity = Clamp01(v); break;
        case LUMINA_STYLE: p.styleType = ToIndex(v, LUMINA_STYLE_COUNT); break;
        case LUMINA_STYLE_COLOR: p.makeupFilterIntensity = Clamp01(v); break;
        case LUMINA_STYLE_MAKEUP: p.makeupMakeupIntensity = Clamp01(v); break;
        case LUMINA_BACKGROUND: p.background = ToIndex(v, 3); break;
        case LUMINA_AVATAR: p.avatar = ToIndex(v, LUMINA_AVATAR_COUNT); break;
        case LUMINA_HEADWEAR: p.headwear = ToIndex(v, LUMINA_HEADWEAR_COUNT); break;
        default: break;
    }
}

bool NeedFace(const LmParams& p) {
    return p.eyeSize > 0.001f || p.faceOverall > 0.001f || p.faceCut > 0.001f || p.cheekbone > 0.001f ||
           p.jaw > 0.001f || p.shrinkHead > 0.001f || std::fabs(p.noseWing - 0.5f) > 0.001f ||
           std::fabs(p.forehead - 0.5f) > 0.001f || p.whitenTeeth > 0.001f || p.removePouch > 0.001f ||
           p.removeSmileFolds > 0.001f || p.smooth > 0.001f || p.makeupMakeupIntensity > 0.001f || p.avatar > 0 ||
           p.headwear > 0;
}

bool EnsureGL(lumina_engine* e) {
    if (e->glReady) {
        return true;
    }
    if (e->glFailed) {
        return false;
    }
    // GL 上下文在第一次处理时创建，归属调用处理函数的线程
    if (!e->context.Init() || !e->context.MakeCurrent() || !e->pipeline.Init()) {
        LM_LOG_E << "Failed to create the OpenGL pipeline";
        e->pipeline.Release();
        e->context.Release();
        e->glFailed = true;
        return false;
    }
    e->context.DoneCurrent();
    e->glReady = true;
    return true;
}

/// 处理紧凑排列的 I420
bool Process(lumina_engine* e, int w, int h, const uint8_t* in, uint8_t* out) {
    if (!e || !in || !out || w < 16 || h < 16 || (w & 1) || (h & 1)) {
        return false;
    }
    if (!EnsureGL(e)) {
        return false;
    }

    LmParams params;
    std::vector<uint8_t> background;
    int backgroundW = 0;
    int backgroundH = 0;
    bool backgroundChanged = false;
    {
        std::lock_guard<std::mutex> lock(e->paramsMutex);
        params = e->params;
        if (e->backgroundDirty) {
            background.swap(e->pendingBackground);
            backgroundW = e->pendingBackgroundW;
            backgroundH = e->pendingBackgroundH;
            e->backgroundDirty = false;
            backgroundChanged = true;
        }
    }

    // 关键点：只有开了依赖人脸的效果时才跑
    e->warpOps.clear();
    e->regions.clear();
    e->makeup.clear();
    e->avatars.clear();
    if (e->detectorReady && NeedFace(params)) {
        LmFace faces[LM_MAX_FACES];
        const int count = e->detector.Detect(w, h, in, faces, LM_MAX_FACES);
        // 人脸消失后表情基准作废，下次出现的可能是另一个人
        for (int i = count; i < LM_MAX_FACES; ++i) {
            e->avatarStates[i].valid = false;
        }
        for (int i = 0; i < count; ++i) {
            LmBuildWarpOps(faces[i], params, e->warpOps);
            LmBuildRegions(faces[i], params, e->regions);
            LmBuildMakeup(faces[i], params, e->makeup);
            if (params.avatar > 0 || params.headwear > 0) {
                LmAvatarFace avatar;
                LmBuildAvatarFace(faces[i], e->avatarStates[i], avatar);
                e->avatars.push_back(avatar);
            }
        }
    }

    const uint8_t* mask = nullptr;
    if (params.background != LM_BG_NONE && e->segmenter.IsReady()) {
        mask = e->segmenter.Run(w, h, in);
    } else {
        e->segmenter.Reset();
    }

    if (!e->context.MakeCurrent()) {
        return false;
    }
    if (backgroundChanged) {
        e->pipeline.SetBackgroundImage(background.empty() ? nullptr : background.data(), backgroundW, backgroundH);
    }
    const bool ok =
        e->pipeline.Render(w, h, in, out, params, e->warpOps, e->regions, e->makeup, e->avatars, mask);
    e->context.DoneCurrent();
    return ok;
}

void CopyPlane(const uint8_t* src, int srcStride, uint8_t* dst, int dstStride, int width, int height) {
    if (src == dst && srcStride == dstStride) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        memmove(dst + static_cast<size_t>(y) * dstStride, src + static_cast<size_t>(y) * srcStride, width);
    }
}

} // namespace

extern "C" {

LUMINA_API lumina_engine* lumina_create(const char* model_dir) {
    lumina_engine* e = new lumina_engine();
    e->modelDir = model_dir ? model_dir : "";
    if (!e->modelDir.empty() && e->modelDir.back() != '/' && e->modelDir.back() != '\\') {
        e->modelDir += '/';
    }

    // 开发调参用：指定了参数文件时覆盖默认值
    const char* tuningPath = getenv("LUMINA_TUNING");
    if (tuningPath && tuningPath[0]) {
        LmLoadTuning(tuningPath);
    }

    for (int i = 0; i < LUMINA_PARAM_COUNT; ++i) {
        // 0.5 为中性的参数
        const bool neutral = i == LUMINA_NOSE || i == LUMINA_FOREHEAD || (i >= LUMINA_TEMPERATURE && i <= LUMINA_CONTRAST);
        e->values[i] = neutral ? 0.5f : 0.0f;
    }

    // 模型加载失败不影响其余效果：关键点失败时美型等不生效，分割失败时虚拟背景不生效
    e->detectorReady = e->detector.Init(e->modelDir);
    if (!e->detectorReady) {
        LM_LOG_W << "Face models not found in " << e->modelDir << ", face effects are disabled";
    }
    if (!e->segmenter.Init(e->modelDir)) {
        LM_LOG_W << "Segmentation models not found in " << e->modelDir << ", virtual background is disabled";
    }
    return e;
}

LUMINA_API void lumina_destroy(lumina_engine* e) {
    if (!e) {
        return;
    }
    if (e->glReady && e->context.MakeCurrent()) {
        e->pipeline.Release();
        e->context.DoneCurrent();
    }
    e->context.Release();
    e->detector.Release();
    e->segmenter.Release();
    delete e;
}

LUMINA_API void lumina_set_param(lumina_engine* e, lumina_param param, float value) {
    if (!e || param < 0 || param >= LUMINA_PARAM_COUNT) {
        return;
    }
    std::lock_guard<std::mutex> lock(e->paramsMutex);
    e->values[param] = value;
    Apply(e->params, param, value);
}

LUMINA_API float lumina_get_param(lumina_engine* e, lumina_param param) {
    if (!e || param < 0 || param >= LUMINA_PARAM_COUNT) {
        return 0.0f;
    }
    std::lock_guard<std::mutex> lock(e->paramsMutex);
    return e->values[param];
}

LUMINA_API void lumina_set_color_space(lumina_engine* e, int matrix, int full_range) {
    if (!e) {
        return;
    }
    std::lock_guard<std::mutex> lock(e->paramsMutex);
    e->params.colorMatrix = matrix == 709 ? 709 : 601;
    e->params.fullRange = full_range != 0;
}

LUMINA_API int lumina_set_background_image(lumina_engine* e, const char* path) {
    if (!e) {
        return 0;
    }
    std::vector<uint8_t> rgba;
    int w = 0;
    int h = 0;
    // 解码失败时传空图，背景图被清除，画面保持原样而不是显示旧图
    const bool ok = path && path[0] && LmLoadImageRGBA(path, 1920, rgba, w, h);
    if (!ok) {
        rgba.clear();
        w = h = 0;
    }
    std::lock_guard<std::mutex> lock(e->paramsMutex);
    e->pendingBackground.swap(rgba);
    e->pendingBackgroundW = w;
    e->pendingBackgroundH = h;
    e->backgroundDirty = true;
    return ok ? 1 : 0;
}

namespace {

int SetLook(lumina_engine* e, std::shared_ptr<const LmLook> look) {
    std::lock_guard<std::mutex> lock(e->paramsMutex);
    e->params.look = std::move(look);
    return 1;
}

int LoadLook(lumina_engine* e, const char* source, bool isFile) {
    if (!e) {
        return 0;
    }
    if (!source || !source[0]) {
        return SetLook(e, nullptr);
    }
    auto look = std::make_shared<LmLook>();
    std::string error;
    const bool ok = isFile ? LmLoadLookFile(source, *look, error) : LmParseLook(source, *look, error);
    if (!ok) {
        LM_LOG_E << "look " << (isFile ? source : "(json)") << ": " << error;
        return 0;
    }
    LM_LOG_I << "look loaded: " << (look->name.empty() ? std::string(isFile ? source : "(json)") : look->name);
    return SetLook(e, look);
}

} // namespace

LUMINA_API int lumina_set_look_file(lumina_engine* e, const char* path) {
    return LoadLook(e, path, true);
}

LUMINA_API int lumina_set_look_json(lumina_engine* e, const char* json) {
    return LoadLook(e, json, false);
}

LUMINA_API int lumina_set_background_rgba(lumina_engine* e, const uint8_t* rgba, int width, int height) {
    if (!e) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(e->paramsMutex);
    if (rgba && width > 0 && height > 0) {
        e->pendingBackground.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
        e->pendingBackgroundW = width;
        e->pendingBackgroundH = height;
    } else {
        e->pendingBackground.clear();
        e->pendingBackgroundW = e->pendingBackgroundH = 0;
    }
    e->backgroundDirty = true;
    return 1;
}

LUMINA_API int lumina_process_i420(lumina_engine* e, int width, int height, const uint8_t* in, uint8_t* out) {
    return Process(e, width, height, in, out) ? 1 : 0;
}

LUMINA_API int lumina_process_i420_planes(lumina_engine* e, int w, int h, const uint8_t* y, int ys, const uint8_t* u,
                                          int us, const uint8_t* v, int vs, uint8_t* oy, int oys, uint8_t* ou, int ous,
                                          uint8_t* ov, int ovs) {
    if (!e || !y || !u || !v || !oy || !ou || !ov) {
        return 0;
    }
    const size_t ySize = static_cast<size_t>(w) * h;
    const size_t cSize = static_cast<size_t>(w / 2) * (h / 2);
    e->packedIn.resize(ySize + cSize * 2);
    e->packedOut.resize(ySize + cSize * 2);
    uint8_t* p = e->packedIn.data();
    CopyPlane(y, ys, p, w, w, h);
    CopyPlane(u, us, p + ySize, w / 2, w / 2, h / 2);
    CopyPlane(v, vs, p + ySize + cSize, w / 2, w / 2, h / 2);
    if (!Process(e, w, h, p, e->packedOut.data())) {
        return 0;
    }
    const uint8_t* q = e->packedOut.data();
    CopyPlane(q, w, oy, oys, w, h);
    CopyPlane(q + ySize, w / 2, ou, ous, w / 2, h / 2);
    CopyPlane(q + ySize + cSize, w / 2, ov, ovs, w / 2, h / 2);
    return 1;
}

LUMINA_API int lumina_process_nv12(lumina_engine* e, int w, int h, const uint8_t* y, int ys, const uint8_t* uv, int uvs,
                                   uint8_t* oy, int oys, uint8_t* ouv, int ouvs) {
    if (!e || !y || !uv || !oy || !ouv) {
        return 0;
    }
    const size_t ySize = static_cast<size_t>(w) * h;
    const size_t cSize = static_cast<size_t>(w / 2) * (h / 2);
    e->packedIn.resize(ySize + cSize * 2);
    e->packedOut.resize(ySize + cSize * 2);
    uint8_t* p = e->packedIn.data();
    CopyPlane(y, ys, p, w, w, h);
    uint8_t* pu = p + ySize;
    uint8_t* pv = pu + cSize;
    for (int r = 0; r < h / 2; ++r) {
        const uint8_t* row = uv + static_cast<size_t>(r) * uvs;
        for (int c = 0; c < w / 2; ++c) {
            pu[r * (w / 2) + c] = row[c * 2];
            pv[r * (w / 2) + c] = row[c * 2 + 1];
        }
    }
    if (!Process(e, w, h, p, e->packedOut.data())) {
        return 0;
    }
    const uint8_t* q = e->packedOut.data();
    CopyPlane(q, w, oy, oys, w, h);
    const uint8_t* qu = q + ySize;
    const uint8_t* qv = qu + cSize;
    for (int r = 0; r < h / 2; ++r) {
        uint8_t* row = ouv + static_cast<size_t>(r) * ouvs;
        for (int c = 0; c < w / 2; ++c) {
            row[c * 2] = qu[r * (w / 2) + c];
            row[c * 2 + 1] = qv[r * (w / 2) + c];
        }
    }
    return 1;
}

} // extern "C"
