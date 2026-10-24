#ifndef LUMINA_PARAMS_HPP
#define LUMINA_PARAMS_HPP

#include <string>

/**
 * @brief 调色预设，滤镜与风格妆的色彩部分都用它描述
 * @remark 全部是自研参数，不依赖任何外部 LUT 资源
 */
struct LmColorGrade {
    float temperature = 0.0f;  ///< 色温 [-1, 1]，正值偏暖
    float tint = 0.0f;         ///< 色调 [-1, 1]，正值偏洋红
    float saturation = 0.0f;   ///< 饱和度 [-1, 1]
    float contrast = 0.0f;     ///< 对比度 [-1, 1]
    float brightness = 0.0f;   ///< 亮度 [-1, 1]
    float fade = 0.0f;         ///< 褪色（抬高暗部）[0, 1]
    float shadowTint[3] = {0.0f, 0.0f, 0.0f};     ///< 暗部染色
    float highlightTint[3] = {0.0f, 0.0f, 0.0f};  ///< 亮部染色
    float skin = 0.0f;         ///< 皮肤额外提亮，人像类滤镜让脸比环境更亮
};

/**
 * @brief 一帧渲染所需的全部参数
 * @remark 由设置参数的线程写入、处理线程按帧拷贝读取
 */
struct LmParams {
    // 美颜，取值 [0, 1]
    float smooth = 0.0f;
    float whiten = 0.0f;
    float clarity = 0.0f;
    float sharpen = 0.0f;

    // 美型
    float eyeSize = 0.0f;
    float faceOverall = 0.0f;
    float faceCut = 0.0f;
    float cheekbone = 0.0f;
    float jaw = 0.0f;
    float noseWing = 0.5f;     ///< 0.5 为中性
    float forehead = 0.5f;     ///< 0.5 为中性
    float shrinkHead = 0.0f;
    float whitenTeeth = 0.0f;
    float removePouch = 0.0f;
    float removeSmileFolds = 0.0f;

    // 画质，0.5 为中性
    float qualityTemperature = 0.5f;
    float qualityHue = 0.5f;
    float qualitySaturation = 0.5f;
    float qualityLight = 0.5f;
    float qualityContrast = 0.5f;

    // 滤镜：序号见 lumina_filter，0 为不使用
    int filterType = 0;
    float filterIntensity = 0.0f;

    // 风格妆：序号见 lumina_style，0 为不使用；色彩部分（调色）与美妆部分（上妆）强度分开
    int styleType = 0;
    float makeupFilterIntensity = 0.0f;
    float makeupMakeupIntensity = 0.0f;

    // 卡通头像与头饰，取 LmAvatarKind / LmHeadwearKind，0 为不使用
    int avatar = 0;
    int headwear = 0;

    // 输入画面的色彩空间：矩阵 601 或 709，是否全范围
    int colorMatrix = 601;
    bool fullRange = true;

    // 虚拟背景：0 不使用，1 背景虚化，2 背景图片
    int background = 0;
};

/// 背景模式，与 lumina_background 一致
enum LmBackground {
    LM_BG_NONE = 0,
    LM_BG_BLUR = 1,
    LM_BG_PICTURE = 2,
};

/**
 * @brief 取滤镜、风格妆色彩部分的调色预设
 * @return 未定义的序号返回 false
 */
bool LmGetFilterGrade(int filter, LmColorGrade& grade);
bool LmGetStyleGrade(int style, LmColorGrade& grade);

#endif // LUMINA_PARAMS_HPP
