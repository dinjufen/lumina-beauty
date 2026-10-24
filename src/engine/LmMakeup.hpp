#ifndef LUMINA_MAKEUP_HPP
#define LUMINA_MAKEUP_HPP

#include <vector>
#include "LmFace.hpp"
#include "LmParams.hpp"

/**
 * @brief 妆容图层的一个顶点
 * @remark 妆容按人脸关键点拼成三角形，画到一张与画面对齐的图层上，再以"正片叠底"的方式叠到皮肤上。
 *         颜色是叠底色（1 为不变），alpha 是该处的上妆浓度；三角形内部按顶点线性插值，
 *         腮红、眼影这类需要柔和过渡的妆，靠边缘顶点的 alpha 为 0 实现渐隐。
 *         每三个顶点组成一个三角形，坐标为像素，原点在图像左上角。
 */
struct LmMakeupVertex {
    float x = 0.0f;
    float y = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 0.0f;
};

/**
 * @brief 根据关键点和风格妆参数生成妆容三角形
 * @return 追加到 vertices 的顶点数
 */
int LmBuildMakeup(const LmFace& face, const LmParams& params, std::vector<LmMakeupVertex>& vertices);

/// 当前风格妆的粉底提亮量（已乘滑杆强度），在脸部皮肤上提亮
float LmMakeupFoundation(const LmParams& params);

#endif // LUMINA_MAKEUP_HPP
