#ifndef LUMINA_ORT_MODEL_HPP
#define LUMINA_ORT_MODEL_HPP

#include <cstddef>
#include <string>
#include <vector>

/**
 * @brief 一个 ONNX Runtime 推理会话的薄封装
 * @remark 只支持本库用到的形态：单个 float 输入，形状为 [1, 高, 宽, 3]；输出均为 float。
 *         推理在调用线程上执行，默认单线程：小模型多线程反而会抢占编码和渲染的 CPU。
 */
class LmOrtModel {
public:
    LmOrtModel();
    ~LmOrtModel();

    bool Init(const std::string& path, int threads = 1);
    void Release();
    bool IsReady() const { return m_impl != nullptr; }

    /**
     * @brief 跑一次推理
     * @remark 输出数据在下一次 Run 或 Release 之前有效
     */
    bool Run(float* input, int height, int width);

    size_t OutputCount() const { return m_outputs.size(); }
    const float* OutputData(size_t index) const { return m_outputs[index].first; }
    size_t OutputSize(size_t index) const { return m_outputs[index].second; }

private:
    void ReleaseOutputs();

private:
    struct Impl;
    Impl* m_impl = nullptr;
    std::vector<std::pair<const float*, size_t>> m_outputs;
};

#endif // LUMINA_ORT_MODEL_HPP
