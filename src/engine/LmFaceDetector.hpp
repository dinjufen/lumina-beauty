#ifndef LUMINA_ORT_FACE_DETECTOR_HPP
#define LUMINA_ORT_FACE_DETECTOR_HPP

#include <string>
#include <vector>
#include "LmFace.hpp"
#include "LmOrtModel.hpp"

/**
 * @brief 基于 ONNX Runtime 的人脸关键点检测
 * @remark 两个模型：人脸检测（128x128）与 478 点关键点（256x256），均为 MediaPipe 的开源模型转换而来。
 *         采用"检测一次、之后跟踪"的方式：有上一帧关键点时直接由它推算本帧的人脸区域，只跑关键点模型；
 *         跟丢后才重新跑检测模型。当前只跟踪画面中最显著的一张人脸。
 */
class LmFaceDetector : public ILmFaceDetector {
public:
    LmFaceDetector();
    virtual ~LmFaceDetector();

    /**
     * @brief 加载模型
     * @param[in] modelDir 模型目录，以路径分隔符结尾
     */
    bool Init(const std::string& modelDir);
    void Release();

    virtual int Detect(int w, int h, const uint8_t* i420, LmFace* faces, int maxFaces) override;

private:
    /// 画面中的一块旋转矩形区域，单位像素
    struct Roi {
        float cx = 0.0f;
        float cy = 0.0f;
        float size = 0.0f;
        float rotation = 0.0f;
    };

    bool FindFace(int w, int h, const uint8_t* i420, Roi& roi);
    bool RunLandmarks(int w, int h, const uint8_t* i420, const Roi& roi, LmFace& face);
    void CropToRgb(int w, int h, const uint8_t* i420, const Roi& roi, int size, float scale, float bias, float* dst);
    Roi RoiFromFace(const LmFace& face) const;
    void Smooth(LmFace& face);

private:
    LmOrtModel m_detectModel;
    LmOrtModel m_landmarkModel;

    std::vector<float> m_anchors;       ///< 检测模型的锚点中心，(x, y) 成对存放，归一化坐标
    std::vector<float> m_detectInput;
    std::vector<float> m_landmarkInput;

    bool m_tracking = false;
    Roi m_roi;
    bool m_hasPrev = false;
    LmFace m_prev;
};

#endif // LUMINA_ORT_FACE_DETECTOR_HPP
