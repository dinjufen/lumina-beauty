#include "LmFaceDetector.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "LmLog.hpp"

namespace {

const int kDetectSize = 128;
const int kLandmarkSize = 256;
const float kDetectScoreThreshold = 0.5f;
const float kPresenceThreshold = 0.5f;
// 人脸区域相对检测框或关键点包围盒的放大倍数，留出额头和下巴外的余量
const float kRoiExpand = 1.5f;

// MediaPipe Face Mesh 中两只眼睛外眼角的序号，用来估计人脸的倾斜角
const int kEyeOuterImageLeft = 33;
const int kEyeOuterImageRight = 263;

float Sigmoid(float v) {
    return 1.0f / (1.0f + std::exp(-v));
}

} // namespace

LmFaceDetector::LmFaceDetector() {}

LmFaceDetector::~LmFaceDetector() {
    Release();
}

bool LmFaceDetector::Init(const std::string& modelDir) {
    Release();

    if (!m_detectModel.Init(modelDir + "face_detector.onnx") ||
        !m_landmarkModel.Init(modelDir + "face_landmarks.onnx")) {
        Release();
        return false;
    }

    // 检测模型的锚点：16x16 网格每格 2 个，8x8 网格每格 6 个，共 896 个，只用到中心点
    m_anchors.clear();
    const int grids[2] = {16, 8};
    const int perCell[2] = {2, 6};
    for (int layer = 0; layer < 2; ++layer) {
        for (int y = 0; y < grids[layer]; ++y) {
            for (int x = 0; x < grids[layer]; ++x) {
                for (int n = 0; n < perCell[layer]; ++n) {
                    m_anchors.push_back((x + 0.5f) / grids[layer]);
                    m_anchors.push_back((y + 0.5f) / grids[layer]);
                }
            }
        }
    }

    m_detectInput.resize(static_cast<size_t>(kDetectSize) * kDetectSize * 3);
    m_landmarkInput.resize(static_cast<size_t>(kLandmarkSize) * kLandmarkSize * 3);
    m_tracking = false;
    m_hasPrev = false;
    LM_LOG_I << "face detector init, model dir = " << modelDir;
    return true;
}

void LmFaceDetector::Release() {
    m_detectModel.Release();
    m_landmarkModel.Release();
    m_tracking = false;
    m_hasPrev = false;
}

/**
 * @brief 从 I420 帧里取出一块旋转的正方形区域，转成模型需要的 RGB 浮点数据
 * @param[in] size  输出边长
 * @param[in] scale、bias 像素值 [0, 255] 映射为 v * scale + bias
 * @remark 区域超出画面的部分填黑。亮度双线性采样，色度取最近点。
 */
void LmFaceDetector::CropToRgb(int w, int h, const uint8_t* i420, const Roi& roi, int size, float scale,
                                  float bias, float* dst) {
    const uint8_t* planeY = i420;
    const uint8_t* planeU = planeY + static_cast<size_t>(w) * h;
    const uint8_t* planeV = planeU + static_cast<size_t>(w / 2) * (h / 2);
    const float cosR = std::cos(roi.rotation);
    const float sinR = std::sin(roi.rotation);
    const float step = roi.size / size;

    for (int v = 0; v < size; ++v) {
        const float ly = (v + 0.5f - size * 0.5f) * step;
        for (int u = 0; u < size; ++u) {
            const float lx = (u + 0.5f - size * 0.5f) * step;
            const float sx = roi.cx + lx * cosR - ly * sinR - 0.5f;
            const float sy = roi.cy + lx * sinR + ly * cosR - 0.5f;
            float* out = dst + (static_cast<size_t>(v) * size + u) * 3;

            const int x0 = static_cast<int>(std::floor(sx));
            const int y0 = static_cast<int>(std::floor(sy));
            if (x0 < 0 || y0 < 0 || x0 >= w - 1 || y0 >= h - 1) {
                out[0] = out[1] = out[2] = bias;
                continue;
            }

            const float fx = sx - x0;
            const float fy = sy - y0;
            const uint8_t* row0 = planeY + static_cast<size_t>(y0) * w + x0;
            const uint8_t* row1 = row0 + w;
            const float luma = (row0[0] * (1.0f - fx) + row0[1] * fx) * (1.0f - fy) +
                               (row1[0] * (1.0f - fx) + row1[1] * fx) * fy;
            const size_t chroma = static_cast<size_t>(y0 / 2) * (w / 2) + x0 / 2;
            const float cb = planeU[chroma] - 128.0f;
            const float cr = planeV[chroma] - 128.0f;

            const float r = luma + 1.402f * cr;
            const float g = luma - 0.344136f * cb - 0.714136f * cr;
            const float b = luma + 1.772f * cb;
            out[0] = std::min(std::max(r, 0.0f), 255.0f) * scale + bias;
            out[1] = std::min(std::max(g, 0.0f), 255.0f) * scale + bias;
            out[2] = std::min(std::max(b, 0.0f), 255.0f) * scale + bias;
        }
    }
}

/**
 * @brief 在整帧里找最显著的人脸，给出关键点模型要用的区域
 */
bool LmFaceDetector::FindFace(int w, int h, const uint8_t* i420, Roi& roi) {
    // 整帧等比缩进 128x128，不足的部分留黑
    Roi full;
    full.cx = w * 0.5f;
    full.cy = h * 0.5f;
    full.size = static_cast<float>(std::max(w, h));
    CropToRgb(w, h, i420, full, kDetectSize, 2.0f / 255.0f, -1.0f, m_detectInput.data());

    if (!m_detectModel.Run(m_detectInput.data(), kDetectSize, kDetectSize)) {
        return false;
    }

    // 两个输出按元素个数区分：每个锚点 16 个回归值和 1 个分数
    const size_t anchorCount = m_anchors.size() / 2;
    const float* regressors = nullptr;
    const float* scores = nullptr;
    for (size_t i = 0; i < m_detectModel.OutputCount(); ++i) {
        if (m_detectModel.OutputSize(i) == anchorCount * 16) regressors = m_detectModel.OutputData(i);
        if (m_detectModel.OutputSize(i) == anchorCount) scores = m_detectModel.OutputData(i);
    }
    if (!regressors || !scores) {
        return false;
    }

    size_t best = 0;
    float bestScore = 0.0f;
    for (size_t i = 0; i < anchorCount; ++i) {
        const float score = Sigmoid(scores[i]);
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (bestScore < kDetectScoreThreshold) {
        return false;
    }

    // 回归值以 128 像素为单位，相对锚点中心：框中心与宽高，随后是 6 个关键点，前两个是两只眼睛
    const float* box = regressors + best * 16;
    const float ax = m_anchors[best * 2];
    const float ay = m_anchors[best * 2 + 1];
    const float toPixel = full.size;
    const float originX = full.cx - full.size * 0.5f;
    const float originY = full.cy - full.size * 0.5f;

    const float boxW = box[2] / kDetectSize * toPixel;
    const float boxH = box[3] / kDetectSize * toPixel;
    const float eye0x = (box[4] / kDetectSize + ax) * toPixel + originX;
    const float eye0y = (box[5] / kDetectSize + ay) * toPixel + originY;
    const float eye1x = (box[6] / kDetectSize + ax) * toPixel + originX;
    const float eye1y = (box[7] / kDetectSize + ay) * toPixel + originY;

    roi.cx = (box[0] / kDetectSize + ax) * toPixel + originX;
    roi.cy = (box[1] / kDetectSize + ay) * toPixel + originY;
    roi.size = std::max(boxW, boxH) * kRoiExpand;
    roi.rotation = std::atan2(eye1y - eye0y, eye1x - eye0x);
    return roi.size > 8.0f;
}

bool LmFaceDetector::RunLandmarks(int w, int h, const uint8_t* i420, const Roi& roi, LmFace& face) {
    CropToRgb(w, h, i420, roi, kLandmarkSize, 1.0f / 255.0f, 0.0f, m_landmarkInput.data());

    if (!m_landmarkModel.Run(m_landmarkInput.data(), kLandmarkSize, kLandmarkSize)) {
        return false;
    }

    // 关键点输出是 478 个 (x, y, z)，单位是 256 像素；人脸存在分数是第一个只有 1 个元素的输出
    const float* points = nullptr;
    const float* presence = nullptr;
    for (size_t i = 0; i < m_landmarkModel.OutputCount(); ++i) {
        if (m_landmarkModel.OutputSize(i) == LM_FACE_POINT_COUNT * 3) {
            points = m_landmarkModel.OutputData(i);
        } else if (m_landmarkModel.OutputSize(i) == 1 && !presence) {
            presence = m_landmarkModel.OutputData(i);
        }
    }
    if (!points || !presence || Sigmoid(presence[0]) < kPresenceThreshold) {
        return false;
    }

    const float cosR = std::cos(roi.rotation);
    const float sinR = std::sin(roi.rotation);
    const float step = roi.size / kLandmarkSize;
    for (int i = 0; i < LM_FACE_POINT_COUNT; ++i) {
        const float lx = (points[i * 3] - kLandmarkSize * 0.5f) * step;
        const float ly = (points[i * 3 + 1] - kLandmarkSize * 0.5f) * step;
        face.x[i] = roi.cx + lx * cosR - ly * sinR;
        face.y[i] = roi.cy + lx * sinR + ly * cosR;
    }
    return true;
}

/**
 * @brief 由本帧关键点推算下一帧的人脸区域
 */
LmFaceDetector::Roi LmFaceDetector::RoiFromFace(const LmFace& face) const {
    Roi roi;
    roi.rotation = std::atan2(face.y[kEyeOuterImageRight] - face.y[kEyeOuterImageLeft],
                              face.x[kEyeOuterImageRight] - face.x[kEyeOuterImageLeft]);

    // 在转正后的坐标系里取包围盒，倾斜的脸才不会把区域撑大
    const float cosR = std::cos(-roi.rotation);
    const float sinR = std::sin(-roi.rotation);
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int i = 0; i < LM_FACE_POINT_COUNT; ++i) {
        const float rx = face.x[i] * cosR - face.y[i] * sinR;
        const float ry = face.x[i] * sinR + face.y[i] * cosR;
        minX = std::min(minX, rx);
        maxX = std::max(maxX, rx);
        minY = std::min(minY, ry);
        maxY = std::max(maxY, ry);
    }
    const float centerX = (minX + maxX) * 0.5f;
    const float centerY = (minY + maxY) * 0.5f;
    roi.cx = centerX * std::cos(roi.rotation) - centerY * std::sin(roi.rotation);
    roi.cy = centerX * std::sin(roi.rotation) + centerY * std::cos(roi.rotation);
    roi.size = std::max(maxX - minX, maxY - minY) * kRoiExpand;
    return roi;
}

/**
 * @brief 时域平滑
 * @remark 位移相对人脸尺寸很小时强平滑以消除抖动，位移大时跟随新值以避免拖尾
 */
void LmFaceDetector::Smooth(LmFace& face) {
    if (m_hasPrev) {
        const float faceSize = std::max(m_roi.size / kRoiExpand, 1.0f);
        const float still = faceSize * 0.004f;
        const float moving = faceSize * 0.03f;
        for (int i = 0; i < LM_FACE_POINT_COUNT; ++i) {
            const float dx = face.x[i] - m_prev.x[i];
            const float dy = face.y[i] - m_prev.y[i];
            const float distance = std::sqrt(dx * dx + dy * dy);
            float alpha = (distance - still) / (moving - still);
            alpha = std::min(std::max(alpha, 0.0f), 1.0f) * 0.8f + 0.2f;
            face.x[i] = m_prev.x[i] + dx * alpha;
            face.y[i] = m_prev.y[i] + dy * alpha;
        }
    }
    m_prev = face;
    m_hasPrev = true;
}

int LmFaceDetector::Detect(int w, int h, const uint8_t* i420, LmFace* faces, int maxFaces) {
    if (!m_landmarkModel.IsReady() || !i420 || !faces || maxFaces < 1 || w < 16 || h < 16) {
        return 0;
    }

    if (!m_tracking) {
        m_hasPrev = false;
        if (!FindFace(w, h, i420, m_roi)) {
            return 0;
        }
    }

    if (!RunLandmarks(w, h, i420, m_roi, faces[0])) {
        // 跟丢了：如果本帧用的是跟踪区域，下一帧重新检测
        m_tracking = false;
        return 0;
    }

    if (!m_tracking) {
        // 检测框给出的区域比较粗，用第一轮关键点修正区域后再跑一轮，首帧结果才稳定
        const Roi refined = RoiFromFace(faces[0]);
        LmFace second;
        if (RunLandmarks(w, h, i420, refined, second)) {
            faces[0] = second;
        }
    }

    m_roi = RoiFromFace(faces[0]);
    Smooth(faces[0]);
    m_tracking = true;
    return 1;
}
