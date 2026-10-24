#include "LmSegmenter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include "LmLog.hpp"

namespace {

const size_t kMaskPixels = static_cast<size_t>(LM_MASK_WIDTH) * LM_MASK_HEIGHT;
const int kFastWidth = 256;
const int kFastHeight = 144;
const size_t kFastPixels = static_cast<size_t>(kFastWidth) * kFastHeight;
const int kClassCount = 6;
// 多类别模型只在快速模型轮廓外这么多个掩膜像素内补充（约为画面宽度的 1.2%）
const int kNearRadius = 3;
// 画面亮度变化超过它的地方不再采用多类别模型的旧结果
const float kChangeLow = 0.04f;
const float kChangeHigh = 0.12f;
// 时域平滑中新一帧所占的权重：越小边缘越稳，但人移动时拖尾越明显
const float kTemporalWeight = 0.65f;

float Clamp01(float v) {
    return std::min(std::max(v, 0.0f), 1.0f);
}

/// 整帧缩小为 outW x outH 的 RGB（[0, 1]），每个输出点取源图对应区域内 4x4 个点的亮度均值以减轻锯齿，
/// 色度取区域中心。luma 非空时同时输出亮度。
void Prepare(int w, int h, const uint8_t* i420, int outW, int outH, float* out, float* luma) {
    const uint8_t* planeY = i420;
    const uint8_t* planeU = planeY + static_cast<size_t>(w) * h;
    const uint8_t* planeV = planeU + static_cast<size_t>(w / 2) * (h / 2);
    const float cellW = static_cast<float>(w) / outW;
    const float cellH = static_cast<float>(h) / outH;
    for (int v = 0; v < outH; ++v) {
        int rows[4];
        for (int k = 0; k < 4; ++k) {
            rows[k] = std::min(static_cast<int>((v + (k + 0.5f) / 4.0f) * cellH), h - 1);
        }
        const int cy = std::min(static_cast<int>((v + 0.5f) * cellH), h - 1);
        for (int u = 0; u < outW; ++u) {
            int sum = 0;
            for (int k = 0; k < 4; ++k) {
                const uint8_t* row = planeY + static_cast<size_t>(rows[k]) * w;
                for (int j = 0; j < 4; ++j) {
                    sum += row[std::min(static_cast<int>((u + (j + 0.5f) / 4.0f) * cellW), w - 1)];
                }
            }
            const float y = sum / 16.0f;
            const int cx = std::min(static_cast<int>((u + 0.5f) * cellW), w - 1);
            const size_t chroma = static_cast<size_t>(cy / 2) * (w / 2) + cx / 2;
            const float cb = planeU[chroma] - 128.0f;
            const float cr = planeV[chroma] - 128.0f;
            *out++ = Clamp01((y + 1.402f * cr) / 255.0f);
            *out++ = Clamp01((y - 0.344136f * cb - 0.714136f * cr) / 255.0f);
            *out++ = Clamp01((y + 1.772f * cb) / 255.0f);
            if (luma) {
                *luma++ = y / 255.0f;
            }
        }
    }
}

/// 可分离的方形最大值滤波，半径 r
void MaxFilter(const float* in, float* out, float* temp, int w, int h, int r) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float m = 0.0f;
            for (int d = std::max(0, x - r); d <= std::min(w - 1, x + r); ++d) {
                m = std::max(m, in[y * w + d]);
            }
            temp[y * w + x] = m;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float m = 0.0f;
            for (int d = std::max(0, y - r); d <= std::min(h - 1, y + r); ++d) {
                m = std::max(m, temp[d * w + x]);
            }
            out[y * w + x] = m;
        }
    }
}

} // namespace

LmSegmenter::LmSegmenter() {}

LmSegmenter::~LmSegmenter() {
    Release();
}

bool LmSegmenter::Init(const std::string& modelDir) {
    Release();
    if (!m_fast.Init(modelDir + "selfie_segmenter.onnx")) {
        return false;
    }
    m_staging.assign(kMaskPixels * 3, 0.0f);
    m_stagingLuma.assign(kMaskPixels, 0.0f);
    m_fastInput.assign(kFastPixels * 3, 0.0f);
    m_fastMask.assign(kMaskPixels, 0.0f);
    m_latest.assign(kMaskPixels, 0.0f);
    m_latestLuma.assign(kMaskPixels, 0.0f);
    m_fused.assign(kMaskPixels, 0.0f);
    m_scratch.assign(kMaskPixels * 3, 0.0f);
    m_smoothed.assign(kMaskPixels, 0.0f);
    m_mask.assign(kMaskPixels, 0);
    m_hasLatest = false;
    m_hasHistory = false;

    if (m_model.Init(modelDir + "selfie_multiclass.onnx")) {
        m_input.assign(kMaskPixels * 3, 0.0f);
        m_inputLuma.assign(kMaskPixels, 0.0f);
        m_result.assign(kMaskPixels, 0.0f);
        m_resultLuma.assign(kMaskPixels, 0.0f);
        m_stop = false;
        m_pending = false;
        m_resultReady = false;
        m_worker = std::thread(&LmSegmenter::WorkerLoop, this);
    } else {
        LM_LOG_W << "multiclass segmenter unavailable, use fast segmenter only";
    }
    LM_LOG_I << "segmenter init, model dir = " << modelDir;
    return true;
}

void LmSegmenter::Release() {
    if (m_worker.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        m_worker.join();
    }
    m_model.Release();
    m_fast.Release();
    m_hasLatest = false;
    m_hasHistory = false;
}

void LmSegmenter::Reset() {
    m_hasHistory = false;
    m_hasLatest = false;
}

void LmSegmenter::WorkerLoop() {
    std::vector<float> input(kMaskPixels * 3);
    std::vector<float> luma(kMaskPixels);
    std::vector<float> person(kMaskPixels);
    double totalMs = 0.0;
    int runs = 0;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || m_pending; });
            if (m_stop) {
                return;
            }
            input.swap(m_input);
            luma.swap(m_inputLuma);
        }

        const auto start = std::chrono::steady_clock::now();
        const bool ok = m_model.Run(input.data(), LM_MASK_HEIGHT, LM_MASK_WIDTH) && m_model.OutputCount() >= 1 &&
                        m_model.OutputSize(0) == kMaskPixels * kClassCount;
        if (ok) {
            // 输出是各类的原始分数，按 softmax 求背景概率，人像概率 = 1 - 背景
            const float* logits = m_model.OutputData(0);
            for (size_t i = 0; i < kMaskPixels; ++i) {
                const float* c = logits + i * kClassCount;
                const float top = *std::max_element(c, c + kClassCount);
                float sum = 0.0f;
                for (int k = 0; k < kClassCount; ++k) {
                    sum += std::exp(c[k] - top);
                }
                person[i] = 1.0f - std::exp(c[0] - top) / sum;
            }
        }
        totalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (++runs == 100) {
            LM_LOG_I << "multiclass segmenter average " << totalMs / runs << " ms";
            totalMs = 0.0;
            runs = 0;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = false;
        if (ok) {
            m_result.swap(person);
            m_resultLuma.swap(luma);
            m_resultReady = true;
        }
        // 交换回来的缓冲区留给下一次使用
        m_input.swap(input);
        m_inputLuma.swap(luma);
        input.resize(kMaskPixels * 3);
        luma.resize(kMaskPixels);
        person.resize(kMaskPixels);
    }
}

void LmSegmenter::Fuse(const float* fast) {
    // 快速模型结果从 256x144 双线性放大到掩膜尺寸
    for (int v = 0; v < LM_MASK_HEIGHT; ++v) {
        const float fy = std::min(std::max((v + 0.5f) * kFastHeight / LM_MASK_HEIGHT - 0.5f, 0.0f),
                                  static_cast<float>(kFastHeight - 1));
        const int y0 = static_cast<int>(fy);
        const int y1 = std::min(y0 + 1, kFastHeight - 1);
        const float ty = fy - y0;
        for (int u = 0; u < LM_MASK_WIDTH; ++u) {
            const float* r0 = fast + y0 * kFastWidth;
            const float* r1 = fast + y1 * kFastWidth;
            const float value = r0[u] + (r1[u] - r0[u]) * ty;
            m_fastMask[static_cast<size_t>(v) * LM_MASK_WIDTH + u] = Clamp01(value);
        }
    }

    if (!m_hasLatest) {
        m_fused = m_fastMask;
        return;
    }

    // 画面自多类别模型推理以来变化过的地方（人移动了），它的结果作废，用快速模型的
    float* change = m_scratch.data();
    float* near = m_scratch.data() + kMaskPixels;
    float* temp = m_scratch.data() + kMaskPixels * 2;
    for (size_t i = 0; i < kMaskPixels; ++i) {
        m_fused[i] = std::fabs(m_stagingLuma[i] - m_latestLuma[i]);
    }
    MaxFilter(m_fused.data(), change, temp, LM_MASK_WIDTH, LM_MASK_HEIGHT, 1);
    MaxFilter(m_fastMask.data(), near, temp, LM_MASK_WIDTH, LM_MASK_HEIGHT, kNearRadius);

    for (size_t i = 0; i < kMaskPixels; ++i) {
        const float s = m_fastMask[i];
        const float valid = 1.0f - Clamp01((change[i] - kChangeLow) / (kChangeHigh - kChangeLow));
        const float m = s + (m_latest[i] - s) * valid;
        // 快速模型拿不准（接近 0.5）的地方听多类别模型的；轮廓外一圈内允许多类别模型补上头发
        const float uncertain = 1.0f - std::fabs(2.0f * s - 1.0f);
        const float base = s + (m - s) * uncertain;
        m_fused[i] = std::max(base, m * near[i]);
    }
}

const uint8_t* LmSegmenter::Run(int w, int h, const uint8_t* i420) {
    if (!m_fast.IsReady() || !i420 || w < 16 || h < 16) {
        return nullptr;
    }

    const bool useModel = m_model.IsReady();
    if (useModel) {
        Prepare(w, h, i420, LM_MASK_WIDTH, LM_MASK_HEIGHT, m_staging.data(), m_stagingLuma.data());
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_resultReady) {
            m_latest.swap(m_result);
            m_latestLuma.swap(m_resultLuma);
            m_resultReady = false;
            m_hasLatest = true;
        }
        // 推理线程空闲时交给它当前画面；忙时跳过
        if (!m_pending) {
            std::copy(m_staging.begin(), m_staging.end(), m_input.begin());
            std::copy(m_stagingLuma.begin(), m_stagingLuma.end(), m_inputLuma.begin());
            m_pending = true;
            m_cv.notify_all();
        }
    }

    Prepare(w, h, i420, kFastWidth, kFastHeight, m_fastInput.data(), nullptr);
    if (!m_fast.Run(m_fastInput.data(), kFastHeight, kFastWidth) || m_fast.OutputCount() < 1 ||
        m_fast.OutputSize(0) != kFastPixels) {
        return nullptr;
    }
    Fuse(m_fast.OutputData(0));

    for (size_t i = 0; i < kMaskPixels; ++i) {
        const float value = Clamp01(m_fused[i]);
        // 变化小的像素（边缘的帧间抖动）重平滑；变化大的像素（人在移动）直接跟随新值，避免拖影
        const float diff = std::fabs(value - m_smoothed[i]);
        const float weight = m_hasHistory ? kTemporalWeight + (1.0f - kTemporalWeight) * std::min(diff / 0.3f, 1.0f)
                                          : 1.0f;
        m_smoothed[i] += (value - m_smoothed[i]) * weight;
        m_mask[i] = static_cast<uint8_t>(m_smoothed[i] * 255.0f + 0.5f);
    }
    m_hasHistory = true;
    return m_mask.data();
}
