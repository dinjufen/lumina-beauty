#ifndef LUMINA_TUNING_HPP
#define LUMINA_TUNING_HPP

#include <string>

/**
 * @brief 各效果的强度与形状参数
 * @remark 默认值即发布使用的值，在真人视频上量化评测后校准得到。开发时可设置环境变量 LUMINA_TUNING 指向一个
 *         "名字 = 数值"的文本文件，启动时覆盖这里的默认值，便于不重新编译就迭代参数。调好后把数值写回默认值。
 */
struct LmTuning {
    // 磨皮
    float smoothEdge = 30.0f;       ///< 保边模糊的颜色权重系数，越大越保边。五官已由关键点单独保护，这里不宜过大，否则坑洼斑点也被当成边缘留下
    float smoothKeep = 0.75f;      ///< 皮肤上用模糊结果替换原图的最大比例
    float smoothCurve = 1.2f;       ///< 滑杆到磨皮强度的指数曲线，小于 1 时低档位更明显，大于 1 时高档位更明显
    float smoothProtect = 1.0f;     ///< 五官保护区内磨皮减弱的比例，1 为完全不磨
    float smoothGrain = 0.25f;      ///< 磨皮后叠回的最细颗粒比例，保留皮肤质感
    float smoothThreshold = 0.3f;   ///< 起伏幅度超过它的地方逐渐不磨，到 2.5 倍时完全不磨（RGB 0~1 单位）
    float smoothLift = 0.01f;       ///< 磨皮时皮肤的提亮量

    // 美白
    float whitenGain = 0.152f;        ///< 滑杆值的放大系数
    float whitenBeta = 4.0f;        ///< 提亮曲线的弯曲程度
    float whitenHueKeep = 0.0f;     ///< 0 为逐通道提亮（会冲淡肤色），1 为只提亮度、保持色相与饱和度
    float whitenRosy = 0.10f;      ///< 美白时给皮肤加的粉调
    float whitenGlobal = 0.55f;     ///< 美白在非皮肤区域的强度比例，皮肤区域为 1

    // 锐化、清晰
    float sharpenGain = 1.71f;
    float clarityGain = 0.118f;

    // 黑眼圈、法令纹
    float regionBlurMix = 0.65f;    ///< 向大半径模糊靠拢的比例
    float pouchLift = 0.041f;        ///< 黑眼圈区域的提亮量
    float foldsLift = 0.013f;        ///< 法令纹区域的提亮量

    // 画质：滑杆从中性到端点时的作用强度
    float qualityTemperature = 0.217f;
    float qualityTint = 0.116f;
    float qualitySaturation = 0.439f;
    float qualityLight = 0.365f;      ///< 亮度滑杆调亮方向的强度
    float qualityLightDown = 0.117f;  ///< 亮度滑杆调暗方向的强度，调暗时幅度宜小于调亮
    float qualityContrast = 0.153f;

    // 滤镜与风格妆的调色预设整体强度
    float presetGain = 1.3f;

    // 美型：滑杆拉满时的位移量，相对脸宽或脸高
    float reshapeOverall = 0.0341f;
    float reshapeFaceCut = 0.0099f;
    float reshapeCheek = 0.0114f;
    float reshapeJaw = 0.0103f;
    float reshapeNose = 0.0215f;
    float reshapeForehead = 0.0051f;
    float reshapeEye = 0.101f;
    float reshapeShrinkHead = 0.142f;

    // 背景虚化的模糊步长，720p 下的像素数
    float maskGuideRadius = 4.0f;   ///< 人像掩膜细化的窗口半径（720p 下四分之一分辨率的像素）
    float maskGuideEps = 0.001f;    ///< 掩膜细化的正则项，越小越贴合画面边缘，越大越接近模型原掩膜
    float backgroundBlurStep = 2.0f;
};

/// 当前生效的参数
const LmTuning& LmGetTuning();

/**
 * @brief 从文件覆盖参数
 * @return 成功读取的参数个数；文件打不开返回 -1
 */
int LmLoadTuning(const std::string& path);

#endif // LUMINA_TUNING_HPP
