#include "LmParams.hpp"

namespace {

LmColorGrade MakeGrade(float temperature, float tint, float saturation, float contrast, float brightness, float fade,
                       float sr, float sg, float sb, float hr, float hg, float hb, float skin = 0.0f) {
    LmColorGrade g;
    g.temperature = temperature;
    g.tint = tint;
    g.saturation = saturation;
    g.contrast = contrast;
    g.brightness = brightness;
    g.fade = fade;
    g.shadowTint[0] = sr;
    g.shadowTint[1] = sg;
    g.shadowTint[2] = sb;
    g.highlightTint[0] = hr;
    g.highlightTint[1] = hg;
    g.highlightTint[2] = hb;
    g.skin = skin;
    return g;
}

} // namespace

bool LmGetFilterGrade(int filter, LmColorGrade& grade) {
    switch (filter) {
        // 滤镜              色温    色调    饱和    对比    亮度   褪色    暗部染色              亮部染色              皮肤提亮
        case 1: grade = MakeGrade(0.118f, 0.009f, 0.002f, 0.090f, 0.149f, 0.014f, -0.011f, -0.003f, -0.006f, 0.007f, 0.002f, -0.015f, 0.042f); return true;  // 暖阳
        case 2: grade = MakeGrade(0.06f, -0.04f, -0.16f, 0.14f, -0.02f, 0.12f, 0.00f, 0.03f, 0.04f, 0.04f, 0.03f, 0.00f); return true;  // 胶片
        case 3: grade = MakeGrade(-0.118f, -0.096f, -0.100f, 0.095f, 0.357f, 0.041f, -0.008f, 0.019f, -0.007f, -0.003f, -0.008f, 0.025f, 0.082f); return true;  // 氧气
        case 4: grade = MakeGrade(-0.08f, 0.08f, -0.08f, 0.02f, 0.04f, 0.08f, 0.02f, 0.00f, 0.04f, 0.02f, 0.01f, 0.04f); return true;  // 桔梗
        case 5: grade = MakeGrade(0.013f, 0.009f, -0.128f, 0.075f, 0.177f, -0.020f, -0.003f, -0.006f, 0.001f, 0.007f, -0.000f, 0.004f, 0.140f); return true;  // 白皙
        case 6: grade = MakeGrade(-0.046f, -0.019f, 0.022f, -0.029f, 0.321f, 0.021f, -0.003f, -0.011f, 0.006f, -0.012f, -0.019f, 0.009f, 0.046f); return true;  // 温柔
        case 7: grade = MakeGrade(-0.066f, -0.054f, 0.031f, -0.045f, 0.400f, -0.107f, 0.008f, 0.003f, -0.014f, 0.018f, -0.025f, 0.017f, 0.107f); return true;  // 初见
        case 8: grade = MakeGrade(-0.04f, 0.00f, -0.30f, 0.08f, 0.00f, 0.10f, 0.01f, 0.01f, 0.02f, 0.01f, 0.01f, 0.01f); return true;  // 高级灰
        case 9: grade = MakeGrade(-0.054f, -0.038f, 0.192f, 0.032f, 0.310f, -0.032f, -0.000f, -0.001f, -0.007f, -0.013f, -0.009f, 0.011f, 0.048f); return true;  // 海岛
        case 10: grade = MakeGrade(-0.028f, -0.045f, 0.276f, 0.039f, 0.228f, -0.029f, 0.006f, -0.001f, 0.005f, -0.026f, -0.008f, 0.010f, 0.137f); return true;  // 浅夏
        case 11: grade = MakeGrade(0.013f, 0.009f, -0.128f, 0.075f, 0.177f, -0.020f, -0.003f, -0.006f, 0.001f, 0.007f, -0.000f, 0.004f, 0.140f); return true;  // 清透
        case 12: grade = MakeGrade(-0.066f, 0.016f, 0.010f, 0.032f, 0.019f, -0.025f, -0.010f, 0.003f, 0.002f, -0.016f, -0.017f, 0.009f, 0.126f); return true;  // 冷白
        case 13: grade = MakeGrade(-0.063f, -0.016f, 0.038f, 0.095f, 0.157f, -0.014f, -0.003f, 0.005f, -0.009f, 0.002f, -0.014f, 0.010f, 0.050f); return true;  // 自然
        case 14: grade = MakeGrade(-0.048f, -0.001f, 0.029f, 0.055f, 0.179f, -0.004f, 0.000f, -0.003f, -0.004f, 0.003f, -0.009f, 0.005f, 0.083f); return true;  // 焕肤
        case 15: grade = MakeGrade(-0.024f, 0.050f, -0.036f, 0.018f, 0.070f, 0.005f, -0.000f, -0.005f, -0.000f, 0.008f, -0.007f, 0.007f, 0.057f); return true;  // 粉瓷
        default: return false;
    }
}

bool LmGetStyleGrade(int style, LmColorGrade& grade) {
    switch (style) {
        case 1: grade = MakeGrade(-0.063f, 0.110f, -0.363f, -0.004f, 0.320f, 0.140f, -0.018f, -0.008f, 0.006f, 0.042f, -0.002f, 0.012f, 0.000f); return true;  // 白皙
        case 2: grade = MakeGrade(-0.008f, -0.008f, -0.085f, 0.027f, 0.192f, 0.005f, 0.004f, -0.006f, 0.010f, 0.007f, -0.006f, 0.009f, 0.000f); return true;  // 韩系
        case 3: grade = MakeGrade(-0.047f, -0.016f, -0.232f, 0.103f, 0.316f, 0.012f, -0.005f, -0.017f, 0.011f, 0.017f, -0.008f, 0.005f, 0.000f); return true;  // 元气
        case 4: grade = MakeGrade(0.16f, 0.03f, 0.04f, -0.02f, 0.05f, 0.04f, 0.03f, 0.01f, 0.00f, 0.04f, 0.03f, 0.00f); return true;  // 温暖
        case 5: grade = MakeGrade(-0.008f, -0.008f, -0.060f, -0.005f, 0.121f, 0.012f, 0.002f, -0.002f, 0.003f, 0.009f, 0.002f, 0.002f, 0.000f); return true;  // 欧美
        case 6: grade = MakeGrade(0.00f, 0.04f, -0.10f, 0.04f, 0.04f, 0.06f, 0.01f, 0.00f, 0.02f, 0.02f, 0.02f, 0.02f); return true;  // 优雅
        case 7: grade = MakeGrade(0.04f, 0.12f, 0.06f, -0.06f, 0.08f, 0.05f, 0.03f, 0.00f, 0.02f, 0.04f, 0.01f, 0.02f); return true;  // 甜美
        case 8: grade = MakeGrade(-0.013f, 0.043f, -0.123f, -0.055f, 0.009f, 0.036f, -0.002f, -0.001f, 0.012f, 0.003f, 0.000f, 0.007f, 0.000f); return true;  // 深邃
        case 9: grade = MakeGrade(-0.010f, 0.002f, -0.031f, 0.064f, 0.175f, -0.003f, -0.001f, 0.001f, -0.001f, 0.002f, 0.001f, 0.001f, 0.000f); return true;  // 伪素颜
        case 10: grade = MakeGrade(-0.016f, -0.005f, 0.050f, 0.062f, 0.085f, -0.000f, 0.002f, -0.006f, 0.002f, 0.004f, 0.002f, 0.004f, 0.000f); return true;  // 学姐
        case 11: grade = MakeGrade(-0.018f, 0.010f, -0.020f, 0.008f, 0.078f, 0.021f, -0.001f, -0.001f, 0.002f, 0.002f, 0.001f, 0.003f, 0.000f); return true;  // 学妹
        default: return false;
    }
}
