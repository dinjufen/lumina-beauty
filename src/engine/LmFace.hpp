#ifndef LUMINA_FACE_HPP
#define LUMINA_FACE_HPP

#include <cstdint>
#include <vector>
#include "LmParams.hpp"

/// 关键点数量，采用 MediaPipe Face Mesh 的 478 点拓扑（468 个面部点 + 10 个虹膜点）
#define LM_FACE_POINT_COUNT 478
/// 单帧最多处理的人脸数
#define LM_MAX_FACES 2
/// 单帧最多的变形操作数
#define LM_MAX_WARP_OPS 32
/// 单帧最多的局部处理区域数，与合成着色器中的数组长度一致
#define LM_MAX_REGIONS 12

/**
 * @brief 一张人脸的关键点，坐标为像素，原点在图像左上角
 */
struct LmFace {
    float x[LM_FACE_POINT_COUNT];
    float y[LM_FACE_POINT_COUNT];
};

/**
 * @brief 人脸关键点检测器接口
 * @remark 在美颜工作线程调用。输入为 I420 帧，输出最多 LM_MAX_FACES 张人脸。
 */
class ILmFaceDetector {
public:
    virtual ~ILmFaceDetector() {}
    virtual int Detect(int w, int h, const uint8_t* i420, LmFace* faces, int maxFaces) = 0;
};

/**
 * @brief 一次局部变形
 * @remark 渲染时做反向映射：输出像素 p 从 p 减去位移处采样。
 *         kind 为 0 时是平移，以 (cx, cy) 为中心把内容推向 (dx, dy) 方向；
 *         kind 为 1 时是缩放，dx 为缩放量，正值放大、负值缩小。影响随到中心的距离平滑衰减到 radius 处为零。
 */
struct LmWarpOp {
    float cx = 0.0f;
    float cy = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float radius = 0.0f;
    float kind = 0.0f;
};

/**
 * @brief 根据关键点和美型参数生成变形操作
 * @return 追加到 ops 的操作数
 */
int LmBuildWarpOps(const LmFace& face, const LmParams& params, std::vector<LmWarpOp>& ops);

/// 局部处理的类型
enum LmRegionKind {
    LM_REGION_TEETH = 0,   ///< 白牙：只提亮区域内偏白的像素
    LM_REGION_SOFTEN = 1,  ///< 柔化提亮：黑眼圈
    LM_REGION_FOLDS = 2,   ///< 柔化提亮：法令纹，提亮量单独调
    LM_REGION_PROTECT = 3, ///< 磨皮保护区：眼睛、眉毛、嘴唇，不参与磨皮
    LM_REGION_FACE = 4,    ///< 人脸范围：范围内放宽肤色判断，脸颊阴影等处也能磨到
};

/**
 * @brief 一块椭圆形的局部处理区域
 * @remark (ux, uy) 与 (vx, vy) 是两条半轴方向的单位向量除以各自的半轴长度，
 *         像素 p 到中心的归一化距离平方为 dot(p - c, u)^2 + dot(p - c, v)^2，小于 1 即在区域内。
 */
struct LmRegion {
    float cx = 0.0f;
    float cy = 0.0f;
    float ux = 0.0f;
    float uy = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float strength = 0.0f;
    float kind = 0.0f;
};

/**
 * @brief 根据关键点生成白牙、黑眼圈、法令纹的处理区域，以及磨皮时要避开的五官区域
 * @return 追加到 regions 的区域数
 */
int LmBuildRegions(const LmFace& face, const LmParams& params, std::vector<LmRegion>& regions);

#endif // LUMINA_FACE_HPP
