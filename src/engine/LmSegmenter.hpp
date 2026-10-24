#ifndef LUMINA_SEGMENTER_HPP
#define LUMINA_SEGMENTER_HPP

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "LmOrtModel.hpp"

/// 人像掩膜的尺寸
#define LM_MASK_WIDTH 256
#define LM_MASK_HEIGHT 256

/**
 * @brief 人像分割
 * @remark 两个 MediaPipe 开源模型配合使用：
 *         - 快速模型（自拍分割横屏版，256x144，约 1.5ms）：每帧同步运行，给出当前帧的人像轮廓，人移动时不滞后；
 *         - 多类别模型（256x256，约 70ms）：头发单独成类，头发挨着暗背景时也能分出来。太重，放在独立线程上异步运行，
 *           结果会比画面晚几帧。
 *         融合时以快速模型为准，多类别模型只在快速模型拿不准的地方和轮廓外一圈内补充（主要是头发）；
 *         并且只在画面自它推理以来没有变化的地方采用它的结果，人移动后留下的旧轮廓不会显示出来。
 *         整帧不保持比例缩放到模型输入尺寸，掩膜在渲染时按画面坐标采样，比例自然还原。
 */
class LmSegmenter {
public:
    LmSegmenter();
    ~LmSegmenter();

    bool Init(const std::string& modelDir);
    void Release();
    bool IsReady() const { return m_fast.IsReady(); }

    /**
     * @brief 对一帧 I420 做分割
     * @return 掩膜数据，LM_MASK_WIDTH x LM_MASK_HEIGHT，255 为人像；失败返回 nullptr
     */
    const uint8_t* Run(int w, int h, const uint8_t* i420);

    /// 画面内容发生跳变（例如切换摄像头）时调用，丢弃时域平滑的历史
    void Reset();

private:
    void WorkerLoop();
    void Fuse(const float* fast);

private:
    LmOrtModel m_fast;
    LmOrtModel m_model;   ///< 多类别模型，加载失败时只用快速模型

    // 推理线程与本线程之间交换的数据，受 m_mutex 保护
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
    bool m_pending = false;            ///< m_input 里有待推理的画面
    bool m_resultReady = false;        ///< m_result 里有尚未取走的新结果
    std::vector<float> m_input;        ///< 交给推理线程的输入，RGB
    std::vector<float> m_inputLuma;    ///< 该输入的亮度，随结果一起返回，用来判断画面后来有没有变化
    std::vector<float> m_result;       ///< 推理线程写入的人像概率
    std::vector<float> m_resultLuma;

    // 只在调用 Run 的线程上使用
    std::vector<float> m_staging;      ///< 当前帧缩小到 256x256 的 RGB
    std::vector<float> m_stagingLuma;
    std::vector<float> m_fastInput;    ///< 当前帧缩小到快速模型输入尺寸的 RGB
    std::vector<float> m_fastMask;     ///< 快速模型结果放大到掩膜尺寸
    std::vector<float> m_latest;       ///< 最近一次多类别模型结果
    std::vector<float> m_latestLuma;
    bool m_hasLatest = false;
    std::vector<float> m_fused;
    std::vector<float> m_scratch;
    std::vector<float> m_smoothed;     ///< 时域平滑后的掩膜，[0, 1]
    std::vector<uint8_t> m_mask;
    bool m_hasHistory = false;
};

#endif // LUMINA_SEGMENTER_HPP
