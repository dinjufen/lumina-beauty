#include "LmPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "LmGL.hpp"
#include "LmTuning.hpp"
#include "LmLog.hpp"
#include <chrono>

namespace {

// 全屏三角形带，纹理第 0 行对应图像顶行，全程不翻转
const char* kVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// BT.601 全范围矩阵。进出使用同一组矩阵，往返不裁剪。
const char* kYuv2RgbShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTexY;
uniform sampler2D uTexU;
uniform sampler2D uTexV;
// 输入的色彩空间：uRange = (亮度偏移, 亮度缩放, 色度缩放)，uKr/uKb 为矩阵系数（BT.601 或 BT.709）
uniform vec3 uRange;
uniform vec2 uKrKb;
void main() {
    float y = (texture(uTexY, vUV).r - uRange.x) * uRange.y;
    float u = (texture(uTexU, vUV).r - 0.5) * uRange.z;
    float v = (texture(uTexV, vUV).r - 0.5) * uRange.z;
    float kr = uKrKb.x;
    float kb = uKrKb.y;
    float kg = 1.0 - kr - kb;
    float r = y + 2.0 * (1.0 - kr) * v;
    float b = y + 2.0 * (1.0 - kb) * u;
    float g = (y - kr * r - kb * b) / kg;
    oColor = vec4(r, g, b, 1.0);
}
)";

// 可分离的保边模糊：空间权重为高斯，颜色差越大权重越小，边缘因此得以保留
const char* kSkinBlurShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform sampler2D uLow;
uniform vec2 uStep;
uniform float uEdge;
// 颜色差先减去大半径模糊的差，只看局部起伏：鼻梁侧面这类平缓的明暗过渡差值近似为 0，照常参与平均；
// 直接用颜色差时，过渡带两侧会被各自抹平，中间挤出一条锯齿状的硬边
void main() {
    vec3 center = texture(uTex, vUV).rgb;
    vec3 lowCenter = texture(uLow, vUV).rgb;
    vec3 sum = center;
    float weightSum = 1.0;
    for (int i = 1; i <= 6; ++i) {
        float fi = float(i);
        float spatial = exp(-fi * fi / 18.0);
        for (int s = -1; s <= 1; s += 2) {
            vec2 uv = vUV + uStep * fi * float(s);
            vec3 tap = texture(uTex, uv).rgb;
            vec3 diff = (tap - center) - (texture(uLow, uv).rgb - lowCenter);
            float weight = spatial * exp(-dot(diff, diff) * uEdge);
            sum += tap * weight;
            weightSum += weight;
        }
    }
    oColor = vec4(sum / weightSum, 1.0);
}
)";

const char* kGaussShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform vec2 uStep;
void main() {
    vec3 sum = texture(uTex, vUV).rgb * 0.2270270;
    sum += (texture(uTex, vUV + uStep * 1.3846154).rgb + texture(uTex, vUV - uStep * 1.3846154).rgb) * 0.3162162;
    sum += (texture(uTex, vUV + uStep * 3.2307692).rgb + texture(uTex, vUV - uStep * 3.2307692).rgb) * 0.0702703;
    oColor = vec4(sum, 1.0);
}
)";

const char* kComposeShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uSrc;
uniform sampler2D uSmoothTex;
uniform sampler2D uBlurTex;
uniform sampler2D uMakeup;
uniform int uMakeupOn;
uniform float uFoundation;
uniform vec2 uTexel;
uniform float uSmooth;
uniform float uWhiten;
uniform float uSharpen;
uniform float uClarity;
// 可调参数，含义见 LmTuning
uniform float uSmoothKeep;
uniform float uWhitenBeta;
uniform float uWhitenHueKeep;
uniform float uWhitenRosy;
uniform float uWhitenGlobal;
uniform float uSharpenGain;
uniform float uClarityGain;
uniform float uRegionBlurMix;
uniform float uPouchLift;
uniform float uFoldsLift;
// 调色参数：x 色温，y 色调，z 饱和度，w 对比度
uniform vec4 uGradeA[3];
// x 亮度，y 褪色，z 混合强度
uniform vec4 uGradeB[3];
uniform vec3 uShadowTint[3];
uniform vec3 uHighlightTint[3];
// 局部处理区域：uRegionA 为 (cx, cy, 强度, 类型)，uRegionB 为两条半轴 (ux, uy, vx, vy)，单位像素
uniform int uRegionCount;
uniform vec4 uRegionA[12];
uniform vec4 uRegionB[12];
uniform float uSmoothProtect;
uniform float uSmoothGrain;
uniform float uSmoothThreshold;
uniform float uSmoothLift;

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// 色度平面上的肤色概率，取值 [0, 1]
float skinMask(vec3 c) {
    float y = luma(c);
    float cb = -0.168736 * c.r - 0.331264 * c.g + 0.5 * c.b;
    float cr = 0.5 * c.r - 0.418688 * c.g - 0.081312 * c.b;
    vec2 d = (vec2(cb, cr) - vec2(-0.065, 0.095)) / vec2(0.075, 0.070);
    float m = 1.0 - smoothstep(0.6, 1.3, length(d));
    return m * smoothstep(0.12, 0.30, y);
}

// 宽松的肤色判断，只在人脸轮廓内使用：脸颊阴影、胡茬、泛红处的颜色偏离典型肤色，严格判断会漏掉
float skinLoose(vec3 c) {
    float y = luma(c);
    float cb = -0.168736 * c.r - 0.331264 * c.g + 0.5 * c.b;
    float cr = 0.5 * c.r - 0.418688 * c.g - 0.081312 * c.b;
    vec2 d = (vec2(cb, cr) - vec2(-0.065, 0.095)) / vec2(0.075, 0.070);
    return (1.0 - smoothstep(1.0, 2.4, length(d))) * smoothstep(0.06, 0.20, y);
}

vec3 applyGrade(vec3 c, int i) {
    float amount = uGradeB[i].z;
    if (amount <= 0.0) {
        return c;
    }
    vec3 g = c;
    float temperature = uGradeA[i].x;
    float tint = uGradeA[i].y;
    g.r += temperature * 0.18 + tint * 0.05;
    g.g -= tint * 0.10;
    g.b += tint * 0.05 - temperature * 0.18;
    g = pow(max(g, vec3(0.0)), vec3(exp(-uGradeB[i].x * 0.7)));
    g = (g - 0.5) * (1.0 + uGradeA[i].w) + 0.5;
    float l = luma(g);
    g = mix(vec3(l), g, 1.0 + uGradeA[i].z);
    float fade = uGradeB[i].y;
    g = g * (1.0 - fade * 0.15) + fade * 0.10;
    l = clamp(luma(g), 0.0, 1.0);
    g += uShadowTint[i] * (1.0 - l) * (1.0 - l) + uHighlightTint[i] * l * l;
    g += (1.0 - g) * uGradeB[i].w * skinMask(c);
    return mix(c, g, amount);
}

void main() {
    vec3 src = texture(uSrc, vUV).rgb;
    vec3 col = src;
    // 肤色判断用磨皮模糊后的颜色并在邻域取平均：原图色度是 2x2 采样的，直接判断时肤色区边界呈锯齿，
    // 磨皮强度跟着锯齿突变，鼻梁侧面这类明暗交界处会出现台阶状的硬边
    vec3 maskColor = uSmooth > 0.001 ? texture(uSmoothTex, vUV).rgb : src;
    float skin = skinMask(maskColor);
    float skinWide = skinLoose(maskColor);
    if (uSmooth > 0.001) {
        vec2 o = uTexel * 3.0;
        vec3 c1 = texture(uSmoothTex, vUV + vec2(o.x, 0.0)).rgb;
        vec3 c2 = texture(uSmoothTex, vUV - vec2(o.x, 0.0)).rgb;
        vec3 c3 = texture(uSmoothTex, vUV + vec2(0.0, o.y)).rgb;
        vec3 c4 = texture(uSmoothTex, vUV - vec2(0.0, o.y)).rgb;
        skin = (skin * 2.0 + skinMask(c1) + skinMask(c2) + skinMask(c3) + skinMask(c4)) / 6.0;
        skinWide = (skinWide * 2.0 + skinLoose(c1) + skinLoose(c2) + skinLoose(c3) + skinLoose(c4)) / 6.0;
    }

    // 区域的归一化距离权重：中心为 1，边缘平滑衰减到 0
    vec2 pixel = vUV / uTexel;
    float regionWeight[12];
    float protect = 0.0;
    float face = 0.0;
    for (int i = 0; i < 12; ++i) {
        regionWeight[i] = 0.0;
        if (i >= uRegionCount) {
            continue;
        }
        vec2 offset = pixel - uRegionA[i].xy;
        float du = dot(offset, uRegionB[i].xy);
        float dv = dot(offset, uRegionB[i].zw);
        regionWeight[i] = (1.0 - smoothstep(0.35, 1.0, du * du + dv * dv)) * uRegionA[i].z;
        if (uRegionA[i].w > 3.5) {
            face = max(face, regionWeight[i]);
        } else if (uRegionA[i].w > 2.5) {
            protect = max(protect, regionWeight[i]);
        }
    }
    skin = max(skin, face * skinWide);

    // 磨皮：只在肤色区域、且避开五官。保边模糊抹掉坑洼斑驳这类中等尺度的起伏，
    // 再叠回一点最细的颗粒（3x3 邻域之差），保留皮肤质感，避免塑料感
    float smoothAmount = skin * uSmooth * (1.0 - protect * uSmoothProtect);
    if (uSmooth > 0.001) {
        vec3 smoothed = texture(uSmoothTex, vUV).rgb;
        vec3 around = (texture(uSrc, vUV + vec2(uTexel.x, 0.0)).rgb + texture(uSrc, vUV - vec2(uTexel.x, 0.0)).rgb +
                       texture(uSrc, vUV + vec2(0.0, uTexel.y)).rgb + texture(uSrc, vUV - vec2(0.0, uTexel.y)).rgb) * 0.25;
        // 只抹掉小幅度的起伏（坑洼、斑点、毛孔），鼻梁两侧这类大幅度的明暗过渡原样保留，
        // 否则过渡带被抹平后会变成一条生硬的台阶
        vec3 diff = (src - smoothed) - (src - around) * uSmoothGrain;
        float limit = 1.0 - smoothstep(uSmoothThreshold, uSmoothThreshold * 2.5, length(src - smoothed));
        col = src - diff * limit * uSmoothKeep * smoothAmount;
        // 磨皮后皮肤略微提亮
        col += (1.0 - col) * uSmoothLift * smoothAmount;
    }

    // 局部处理：白牙、黑眼圈、法令纹
    for (int i = 0; i < 12; ++i) {
        float weight = regionWeight[i];
        if (weight <= 0.0 || uRegionA[i].w > 2.5) {  // 保护区与人脸范围不在这里处理
            continue;
        }
        if (uRegionA[i].w < 0.5) {
            // 白牙：区域内只处理够亮、饱和度够低的像素，嘴唇和口腔暗部不动
            float l = luma(col);
            float chroma = max(col.r, max(col.g, col.b)) - min(col.r, min(col.g, col.b));
            float tooth = smoothstep(0.30, 0.55, l) * (1.0 - smoothstep(0.16, 0.34, chroma));
            vec3 white = mix(vec3(l), col, 0.5) * 1.14 + 0.03;
            col = mix(col, white, weight * tooth);
        } else {
            // 柔化提亮：向大半径模糊靠拢抹掉细纹，再把暗部抬起来
            vec3 soft = mix(col, texture(uBlurTex, vUV).rgb, uRegionBlurMix);
            soft += (1.0 - soft) * (uRegionA[i].w > 1.5 ? uFoldsLift : uPouchLift);
            col = mix(col, soft, weight);
        }
    }

    // 美白：对数曲线提亮，肤色区域权重更高
    if (uWhiten > 0.001) {
        float beta = 1.0 + uWhiten * uWhitenBeta;
        vec3 whitened = log(max(col, vec3(0.0)) * (beta - 1.0) + 1.0) / log(beta);
        // 只提亮度的版本：按亮度的提亮比例整体缩放，色相与饱和度不变
        float l = max(luma(col), 0.001);
        vec3 lumaOnly = col * ((log(l * (beta - 1.0) + 1.0) / log(beta)) / l);
        whitened = mix(whitened, lumaOnly, uWhitenHueKeep);
        col = mix(col, whitened, mix(uWhitenGlobal, 1.0, skin));
        // 粉调：只加在皮肤上，提红、略压绿
        col += uWhiten * uWhitenRosy * skin * vec3(0.6, -0.35, 0.1);
    }

    // 锐化：小半径反锐化掩模，已磨皮的皮肤上减弱
    if (uSharpen > 0.001) {
        vec3 around = texture(uSrc, vUV + vec2(uTexel.x, 0.0)).rgb + texture(uSrc, vUV - vec2(uTexel.x, 0.0)).rgb +
                      texture(uSrc, vUV + vec2(0.0, uTexel.y)).rgb + texture(uSrc, vUV - vec2(0.0, uTexel.y)).rgb;
        col += (src - around * 0.25) * uSharpen * uSharpenGain * (1.0 - 0.7 * smoothAmount);
    }

    // 清晰：大半径局部对比度，只作用于中间调，避免高光和暗部溢出
    if (uClarity > 0.001) {
        float local = luma(src) - luma(texture(uBlurTex, vUV).rgb);
        float l = clamp(luma(col), 0.0, 1.0);
        float midtone = 1.0 - (2.0 * l - 1.0) * (2.0 * l - 1.0);
        col += local * uClarity * uClarityGain * midtone * (1.0 - 0.5 * skin);
    }

    // 粉底：脸部皮肤提亮，避开五官
    col += (1.0 - col) * uFoundation * face * skin * (1.0 - protect);

    // 美妆：正片叠底，叠底色由妆容图层的预乘颜色还原
    if (uMakeupOn != 0) {
        vec4 m = texture(uMakeup, vUV);
        if (m.a > 0.002) {
            // 只画在脸上：腮红、修容靠近脸的边缘时，不会染到背景或头发上
            vec3 tint = m.rgb / m.a;
            col = mix(col, col * tint, m.a * max(face, protect));
        }
    }

    col = applyGrade(col, 0);
    col = applyGrade(col, 1);
    col = applyGrade(col, 2);

    oColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)";

// 美型变形用规则网格绘制：每个顶点的位置固定，纹理坐标在 CPU 上按变形操作算好，
// 网格内部由 GPU 线性插值。相比逐像素循环的着色器，编译快、开销与变形操作的数量无关。
// 妆容图层：顶点坐标为像素，颜色为叠底色，alpha 为浓度。输出预乘 alpha，多层妆按"上层盖下层"叠加
const char* kMakeupVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform vec2 uSize;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPos / uSize * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kMakeupShader = R"(#version 300 es
precision highp float;
in vec4 vColor;
out vec4 oColor;
void main() {
    oColor = vec4(vColor.rgb * vColor.a, vColor.a);
}
)";

#include "LmAvatarShader.inc"

const char* kMeshVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kTextureShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
void main() {
    oColor = texture(uTex, vUV);
}
)";

// 网格密度：720p 下每格 16 像素
const int kMeshCols = 80;
const int kMeshRows = 45;

// 虚拟背景合成。掩膜分辨率很低，先取邻域均值再用 smoothstep 收紧，得到干净又不生硬的边缘。
// 人像掩膜细化（快速导向滤波）：模型掩膜分辨率低、在头发等处置信度不高，
// 用画面亮度做引导，让掩膜边缘贴合画面里人与背景的真实边界，发梢这类细节不会被裁掉。
// 第一步：四分之一分辨率上打包 亮度 I、掩膜 p、I*p、I*I
const char* kGuidePackShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uImage;
uniform sampler2D uMask;
uniform vec2 uTexel;
void main() {
    vec3 c = (texture(uImage, vUV + vec2(-uTexel.x, -uTexel.y)).rgb + texture(uImage, vUV + vec2(uTexel.x, -uTexel.y)).rgb +
              texture(uImage, vUV + vec2(-uTexel.x, uTexel.y)).rgb + texture(uImage, vUV + vec2(uTexel.x, uTexel.y)).rgb) * 0.25;
    float i = dot(c, vec3(0.299, 0.587, 0.114));
    float p = texture(uMask, vUV).r;
    oColor = vec4(i, p, i * p, i * i);
}
)";

// 可分离的方框均值
const char* kBoxShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform vec2 uStep;
uniform int uRadius;
void main() {
    vec4 sum = vec4(0.0);
    for (int k = -uRadius; k <= uRadius; ++k) {
        sum += texture(uTex, vUV + uStep * float(k));
    }
    oColor = sum / float(2 * uRadius + 1);
}
)";

// 第二步：由局部均值求线性系数 q = a * I + b
const char* kGuideCoefShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform float uEps;
void main() {
    vec4 m = texture(uTex, vUV);
    float variance = max(m.w - m.x * m.x, 0.0);
    float a = (m.z - m.x * m.y) / (variance + uEps);
    float b = m.y - a * m.x;
    // 可信度：窗口内画面明暗起伏足够大，人和背景才分得开；头发和背景一样暗时细化只会得到一片模糊
    float confidence = variance / (variance + uEps);
    oColor = vec4(a, b, confidence, 1.0);
}
)";

const char* kCompositeShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uFg;
uniform sampler2D uBg;
uniform sampler2D uMask;
uniform vec2 uMaskTexel;
// 背景纹理坐标 = 画面坐标 * uBgScale + uBgOffset，用于按比例裁切背景图
uniform vec2 uBgScale;
uniform vec2 uBgOffset;
uniform sampler2D uCoef;
uniform int uRefine;
void main() {
    vec3 fg = texture(uFg, vUV).rgb;
    float alpha = texture(uMask, vUV).r * 0.4;
    alpha += texture(uMask, vUV + vec2(uMaskTexel.x, 0.0)).r * 0.15;
    alpha += texture(uMask, vUV - vec2(uMaskTexel.x, 0.0)).r * 0.15;
    alpha += texture(uMask, vUV + vec2(0.0, uMaskTexel.y)).r * 0.15;
    alpha += texture(uMask, vUV - vec2(0.0, uMaskTexel.y)).r * 0.15;
    // 过渡带要窄：模型拿不准的地方（头发挨着同样暗的背景）若给半透明，会透出原背景，看起来像一圈糊掉的头发
    alpha = smoothstep(0.42, 0.58, alpha);
    if (uRefine != 0) {
        // 系数在低分辨率上求得并平滑，用全分辨率的亮度求值，边缘跟着画面走
        vec3 ab = texture(uCoef, vUV).rgb;
        float refined = clamp(ab.x * dot(fg, vec3(0.299, 0.587, 0.114)) + ab.y, 0.0, 1.0);
        refined = smoothstep(0.40, 0.60, refined);
        // 只在模型掩膜的边缘带内采用细化结果：远离人像的背景里颜色相近的暗处，
        // 细化会把它误判成人像，留下原背景的残影
        float lo = 1.0;
        float hi = 0.0;
        for (int k = 0; k < 8; ++k) {
            float angle = float(k) * 0.785398;
            float m = texture(uMask, vUV + vec2(cos(angle), sin(angle)) * uMaskTexel * 2.0).r;
            lo = min(lo, m);
            hi = max(hi, m);
        }
        // 附近要有把握是人像（hi 足够大）才细化：背景里模型给出的零星弱响应不会被放大成残影
        float band = smoothstep(0.35, 0.65, hi) * (1.0 - smoothstep(0.85, 0.98, lo));
        // 画面分不开人和背景的地方退回模型掩膜，不让细化把边缘抹成半透明的一片
        alpha = mix(alpha, refined, band * smoothstep(0.4, 0.8, ab.z));
    }
    oColor = vec4(mix(texture(uBg, vUV * uBgScale + uBgOffset).rgb, fg, alpha), 1.0);
}
)";

const char* kRgb2YShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform vec3 uRange;
uniform vec2 uKrKb;
void main() {
    vec3 c = texture(uTex, vUV).rgb;
    float y = dot(c, vec3(uKrKb.x, 1.0 - uKrKb.x - uKrKb.y, uKrKb.y));
    oColor = vec4(y / uRange.y + uRange.x, 0.0, 0.0, 1.0);
}
)";

// 输出目标宽 w/2、高 h：上半是 U 平面，下半是 V 平面，回读后正好是 I420 的后两个平面。
// 采样点落在 2x2 像素块中心，线性过滤即完成色度下采样。
const char* kRgb2UVShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform vec3 uRange;
uniform vec2 uKrKb;
void main() {
    bool isU = vUV.y < 0.5;
    vec2 uv = vec2(vUV.x, isU ? vUV.y * 2.0 : (vUV.y - 0.5) * 2.0);
    vec3 c = texture(uTex, uv).rgb;
    float kr = uKrKb.x;
    float kb = uKrKb.y;
    float y = dot(c, vec3(kr, 1.0 - kr - kb, kb));
    float value = isU ? (c.b - y) / (2.0 * (1.0 - kb)) : (c.r - y) / (2.0 * (1.0 - kr));
    oColor = vec4(value / uRange.z + 0.5, 0.0, 0.0, 1.0);
}
)";

GLuint CompileShader(GLenum type, const char* source) {
    // 着色器按 GLSL ES 3.00 书写；桌面 OpenGL 3.3 Core 下换成 GLSL 3.30，其余写法两者通用
    std::string text = source;
    const std::string esVersion = "#version 300 es";
    if (text.compare(0, esVersion.size(), esVersion) == 0) {
        text.replace(0, esVersion.size(), "#version 330 core");
    }
    const char* code = text.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &code, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024] = {0};
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        LM_LOG_E << "shader compile failed: " << log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint BuildProgram(const char* fragmentSource, const char* vertexSource = kVertexShader) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024] = {0};
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        LM_LOG_E << "program link failed: " << log;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

GLuint CreatePlaneTexture(int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    return tex;
}

void SetInt(GLuint program, const char* name, int value) {
    glUniform1i(glGetUniformLocation(program, name), value);
}

void SetFloat(GLuint program, const char* name, float value) {
    glUniform1f(glGetUniformLocation(program, name), value);
}

void SetVec2(GLuint program, const char* name, float x, float y) {
    glUniform2f(glGetUniformLocation(program, name), x, y);
}

// 把调色预设按强度写入合成着色器的第 index 组参数
void SetGrade(GLuint program, int index, const LmColorGrade& grade, float gain, float amount) {
    const std::string suffix = "[" + std::to_string(index) + "]";
    glUniform4f(glGetUniformLocation(program, ("uGradeA" + suffix).c_str()),
                grade.temperature * gain, grade.tint * gain, grade.saturation * gain, grade.contrast * gain);
    glUniform4f(glGetUniformLocation(program, ("uGradeB" + suffix).c_str()),
                grade.brightness * gain, grade.fade * gain, amount, grade.skin * gain);
    glUniform3f(glGetUniformLocation(program, ("uShadowTint" + suffix).c_str()),
                grade.shadowTint[0] * gain, grade.shadowTint[1] * gain, grade.shadowTint[2] * gain);
    glUniform3f(glGetUniformLocation(program, ("uHighlightTint" + suffix).c_str()),
                grade.highlightTint[0] * gain, grade.highlightTint[1] * gain, grade.highlightTint[2] * gain);
}


} // namespace

LmPipeline::LmPipeline() {}

LmPipeline::~LmPipeline() {}

bool LmPipeline::Init() {
    m_progYuv2Rgb = BuildProgram(kYuv2RgbShader);
    m_progSkinBlur = BuildProgram(kSkinBlurShader);
    m_progGauss = BuildProgram(kGaussShader);
    m_progCompose = BuildProgram(kComposeShader);
    m_progComposite = BuildProgram(kCompositeShader);
    m_progGuidePack = BuildProgram(kGuidePackShader);
    m_progBox = BuildProgram(kBoxShader);
    m_progGuideCoef = BuildProgram(kGuideCoefShader);
    m_progWarp = BuildProgram(kTextureShader, kMeshVertexShader);
    m_progMakeup = BuildProgram(kMakeupShader, kMakeupVertexShader);
    m_progRgb2Y = BuildProgram(kRgb2YShader);
    m_progRgb2UV = BuildProgram(kRgb2UVShader);
    m_progAvatar = BuildProgram(kAvatarShader);
    if (!m_progYuv2Rgb || !m_progSkinBlur || !m_progGauss || !m_progCompose ||
        !m_progComposite || !m_progWarp || !m_progRgb2Y || !m_progRgb2UV || !m_progGuidePack || !m_progBox ||
        !m_progGuideCoef || !m_progMakeup || !m_progAvatar) {
        return false;
    }

    const float quad[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    // 美型网格：每个顶点 (x, y, u, v)，位置不变，纹理坐标每帧更新
    m_meshVertices.resize(static_cast<size_t>(kMeshCols + 1) * (kMeshRows + 1) * 4);
    std::vector<uint16_t> indices;
    indices.reserve(static_cast<size_t>(kMeshCols) * kMeshRows * 6);
    for (int row = 0; row < kMeshRows; ++row) {
        for (int col = 0; col < kMeshCols; ++col) {
            const uint16_t topLeft = static_cast<uint16_t>(row * (kMeshCols + 1) + col);
            const uint16_t bottomLeft = static_cast<uint16_t>(topLeft + kMeshCols + 1);
            indices.push_back(topLeft);
            indices.push_back(static_cast<uint16_t>(topLeft + 1));
            indices.push_back(bottomLeft);
            indices.push_back(static_cast<uint16_t>(topLeft + 1));
            indices.push_back(static_cast<uint16_t>(bottomLeft + 1));
            indices.push_back(bottomLeft);
        }
    }
    m_meshIndexCount = static_cast<int>(indices.size());
    glGenVertexArrays(1, &m_meshVao);
    glBindVertexArray(m_meshVao);
    glGenBuffers(1, &m_meshVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_meshVbo);
    glBufferData(GL_ARRAY_BUFFER, m_meshVertices.size() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(2 * sizeof(float)));
    glGenBuffers(1, &m_meshIbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_meshIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &m_makeupVao);
    glBindVertexArray(m_makeupVao);
    glGenBuffers(1, &m_makeupVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_makeupVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(LmMakeupVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LmMakeupVertex),
                          reinterpret_cast<const void*>(2 * sizeof(float)));
    glBindVertexArray(m_vao);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    m_bInit = true;
    return true;
}

void LmPipeline::Release() {
    if (!m_bInit) {
        return;
    }

    DestroyTargets();
    const GLuint programs[] = {m_progYuv2Rgb, m_progSkinBlur, m_progGauss, m_progCompose,
                               m_progComposite, m_progWarp,  m_progRgb2Y, m_progRgb2UV,
                               m_progGuidePack, m_progBox,   m_progGuideCoef, m_progMakeup, m_progAvatar};
    for (GLuint program : programs) {
        if (program != 0) {
            glDeleteProgram(program);
        }
    }
    m_progYuv2Rgb = m_progSkinBlur = m_progGauss = m_progCompose = m_progComposite = 0;
    m_progWarp = m_progRgb2Y = m_progRgb2UV = 0;
    m_progGuidePack = m_progBox = m_progGuideCoef = m_progMakeup = m_progAvatar = 0;

    const GLuint textures[] = {m_texMask, m_texBackground};
    for (GLuint tex : textures) {
        if (tex != 0) {
            glDeleteTextures(1, &tex);
        }
    }
    m_texMask = m_texBackground = 0;

    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_meshVbo != 0) {
        glDeleteBuffers(1, &m_meshVbo);
        m_meshVbo = 0;
    }
    if (m_meshIbo != 0) {
        glDeleteBuffers(1, &m_meshIbo);
        m_meshIbo = 0;
    }
    if (m_meshVao != 0) {
        glDeleteVertexArrays(1, &m_meshVao);
        m_meshVao = 0;
    }
    if (m_makeupVbo != 0) {
        glDeleteBuffers(1, &m_makeupVbo);
        m_makeupVbo = 0;
    }
    if (m_makeupVao != 0) {
        glDeleteVertexArrays(1, &m_makeupVao);
        m_makeupVao = 0;
    }
    m_bInit = false;
}

bool LmPipeline::CreateTarget(Target& target, int w, int h, bool singleChannel) {
    return singleChannel ? CreateTarget(target, w, h, GL_R8, GL_RED, GL_UNSIGNED_BYTE)
                         : CreateTarget(target, w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
}

bool LmPipeline::CreateTarget(Target& target, int w, int h, unsigned int internalFormat, unsigned int format,
                              unsigned int type) {
    target.w = w;
    target.h = h;
    glGenTextures(1, &target.tex);
    glBindTexture(GL_TEXTURE_2D, target.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, nullptr);

    glGenFramebuffers(1, &target.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.tex, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        LM_LOG_E << "framebuffer incomplete, size = " << w << "x" << h;
    }
    return complete;
}

void LmPipeline::DestroyTarget(Target& target) {
    if (target.fbo != 0) {
        glDeleteFramebuffers(1, &target.fbo);
    }
    if (target.tex != 0) {
        glDeleteTextures(1, &target.tex);
    }
    target = Target();
}

void LmPipeline::DestroyTargets() {
    Target* targets[] = {&m_rgb,     &m_halfA,   &m_halfB,      &m_quarterA, &m_quarterB, &m_final,
                         &m_bgBlurA, &m_bgBlurB, &m_composited, &m_warped,   &m_maskWarped, &m_outY, &m_outUV,
                         &m_guideA,  &m_guideB,  &m_guideC,     &m_makeupA,  &m_makeupB,     &m_avatarOut};
    for (Target* target : targets) {
        DestroyTarget(*target);
    }

    const GLuint planes[] = {m_texY, m_texU, m_texV};
    for (GLuint tex : planes) {
        if (tex != 0) {
            glDeleteTextures(1, &tex);
        }
    }
    m_texY = m_texU = m_texV = 0;
    m_guideFailed = false;
    m_width = 0;
    m_height = 0;
}

bool LmPipeline::Resize(int w, int h) {
    DestroyTargets();

    m_texY = CreatePlaneTexture(w, h);
    m_texU = CreatePlaneTexture(w / 2, h / 2);
    m_texV = CreatePlaneTexture(w / 2, h / 2);

    const int halfW = (w / 2 > 1) ? w / 2 : 1;
    const int halfH = (h / 2 > 1) ? h / 2 : 1;
    const int quarterW = (w / 4 > 1) ? w / 4 : 1;
    const int quarterH = (h / 4 > 1) ? h / 4 : 1;

    bool ok = CreateTarget(m_rgb, w, h, false);
    ok = CreateTarget(m_halfA, halfW, halfH, false) && ok;
    ok = CreateTarget(m_halfB, halfW, halfH, false) && ok;
    ok = CreateTarget(m_makeupA, halfW, halfH, false) && ok;
    ok = CreateTarget(m_makeupB, halfW, halfH, false) && ok;
    ok = CreateTarget(m_quarterA, quarterW, quarterH, false) && ok;
    ok = CreateTarget(m_quarterB, quarterW, quarterH, false) && ok;
    ok = CreateTarget(m_final, w, h, false) && ok;
    ok = CreateTarget(m_bgBlurA, quarterW, quarterH, false) && ok;
    ok = CreateTarget(m_bgBlurB, quarterW, quarterH, false) && ok;
    ok = CreateTarget(m_composited, w, h, false) && ok;
    ok = CreateTarget(m_warped, w, h, false) && ok;
    ok = CreateTarget(m_avatarOut, w, h, false) && ok;
    ok = CreateTarget(m_outY, w, h, true) && ok;
    ok = CreateTarget(m_outUV, w / 2, h, true) && ok;
    if (!ok) {
        DestroyTargets();
        return false;
    }

    m_width = w;
    m_height = h;
    LM_LOG_I << "pipeline size is " << w << "x" << h;
    return true;
}

void LmPipeline::DrawTo(const Target& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.w, target.h);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void LmPipeline::SetBackgroundImage(const uint8_t* rgba, int w, int h) {
    if (m_texBackground != 0) {
        glDeleteTextures(1, &m_texBackground);
        m_texBackground = 0;
    }
    if (!rgba || w <= 0 || h <= 0) {
        return;
    }

    glGenTextures(1, &m_texBackground);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texBackground);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    m_backgroundW = w;
    m_backgroundH = h;
}

bool LmPipeline::RefineMask(unsigned int imageTex, unsigned int maskTex) {
    if (m_guideFailed) {
        return false;
    }
    // 统计量需要浮点精度，8 位存不下方差；不支持浮点渲染目标时退回直接使用模型掩膜
    const int gw = (m_width / 4 > 1) ? m_width / 4 : 1;
    const int gh = (m_height / 4 > 1) ? m_height / 4 : 1;
    if (m_guideA.fbo == 0) {
        bool ok = CreateTarget(m_guideA, gw, gh, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
        ok = ok && CreateTarget(m_guideB, gw, gh, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
        ok = ok && CreateTarget(m_guideC, gw, gh, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
        if (!ok) {
            LM_LOG_W << "float render target unsupported, mask refinement disabled";
            DestroyTarget(m_guideA);
            DestroyTarget(m_guideB);
            DestroyTarget(m_guideC);
            m_guideFailed = true;
            return false;
        }
    }
    const LmTuning& tuning = LmGetTuning();
    // 窗口半径按 720p 下的四分之一分辨率像素计，随分辨率缩放
    const int radius = std::max(1, static_cast<int>(tuning.maskGuideRadius * m_height / 720.0f + 0.5f));

    glUseProgram(m_progGuidePack);
    SetInt(m_progGuidePack, "uImage", 0);
    SetInt(m_progGuidePack, "uMask", 1);
    SetVec2(m_progGuidePack, "uTexel", 1.0f / m_width, 1.0f / m_height);
    BindTexture(0, imageTex);
    BindTexture(1, maskTex);
    DrawTo(m_guideA);

    glUseProgram(m_progBox);
    SetInt(m_progBox, "uTex", 0);
    SetInt(m_progBox, "uRadius", radius);
    auto box = [&](const Target& from, const Target& temp, const Target& to) {
        BindTexture(0, from.tex);
        SetVec2(m_progBox, "uStep", 1.0f / gw, 0.0f);
        DrawTo(temp);
        BindTexture(0, temp.tex);
        SetVec2(m_progBox, "uStep", 0.0f, 1.0f / gh);
        DrawTo(to);
    };
    box(m_guideA, m_guideB, m_guideC);

    glUseProgram(m_progGuideCoef);
    SetInt(m_progGuideCoef, "uTex", 0);
    SetFloat(m_progGuideCoef, "uEps", tuning.maskGuideEps);
    BindTexture(0, m_guideC.tex);
    DrawTo(m_guideA);

    glUseProgram(m_progBox);
    box(m_guideA, m_guideC, m_guideB);
    return true;
}

void LmPipeline::DrawMesh(unsigned int source, const Target& target) {
    // 调用前已绑定网格的 VAO 并上传本帧的顶点
    glUseProgram(m_progWarp);
    SetInt(m_progWarp, "uTex", 0);
    BindTexture(0, source);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.w, target.h);
    glDrawElements(GL_TRIANGLES, m_meshIndexCount, GL_UNSIGNED_SHORT, nullptr);
    glBindVertexArray(m_vao);
}

void LmPipeline::BindTexture(int unit, unsigned int tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

bool LmPipeline::Render(int w, int h, const uint8_t* in, uint8_t* out, const LmParams& params,
                        const std::vector<LmWarpOp>& warpOps, const std::vector<LmRegion>& regions,
                        const std::vector<LmMakeupVertex>& makeup, const std::vector<LmAvatarFace>& avatars,
                        const uint8_t* mask) {
    if (!m_bInit || !in || !out) {
        return false;
    }

    // I420 的色度平面按偶数尺寸排布，奇数尺寸不处理，原样返回
    if (w < 2 || h < 2 || (w & 1) != 0 || (h & 1) != 0) {
        memcpy(out, in, static_cast<size_t>(w) * h * 3 / 2);
        return true;
    }

    if (w != m_width || h != m_height) {
        if (!Resize(w, h)) {
            return false;
        }
    }

    glBindVertexArray(m_vao);

    // 1. 上传三个平面并转 RGB
    const uint8_t* planeU = in + static_cast<size_t>(w) * h;
    const uint8_t* planeV = planeU + static_cast<size_t>(w / 2) * (h / 2);
    BindTexture(0, m_texY);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, in);
    BindTexture(1, m_texU);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w / 2, h / 2, GL_RED, GL_UNSIGNED_BYTE, planeU);
    BindTexture(2, m_texV);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w / 2, h / 2, GL_RED, GL_UNSIGNED_BYTE, planeV);

    // 色彩空间：有限范围的亮度在 [16, 235]、色度在 [16, 240]
    const float rangeY0 = params.fullRange ? 0.0f : 16.0f / 255.0f;
    const float rangeYS = params.fullRange ? 1.0f : 255.0f / 219.0f;
    const float rangeCS = params.fullRange ? 1.0f : 255.0f / 224.0f;
    const float kr = params.colorMatrix == 709 ? 0.2126f : 0.299f;
    const float kb = params.colorMatrix == 709 ? 0.0722f : 0.114f;
    for (GLuint program : {m_progYuv2Rgb, m_progRgb2Y, m_progRgb2UV}) {
        glUseProgram(program);
        glUniform3f(glGetUniformLocation(program, "uRange"), rangeY0, rangeYS, rangeCS);
        glUniform2f(glGetUniformLocation(program, "uKrKb"), kr, kb);
    }

    glUseProgram(m_progYuv2Rgb);
    SetInt(m_progYuv2Rgb, "uTexY", 0);
    SetInt(m_progYuv2Rgb, "uTexU", 1);
    SetInt(m_progYuv2Rgb, "uTexV", 2);
    DrawTo(m_rgb);

    // 模糊半径随分辨率缩放，以 720p 为基准
    const float scale = static_cast<float>(h) / 720.0f;
    const LmTuning& tuning = LmGetTuning();

    // 2. 大半径高斯模糊，清晰用
    // 黑眼圈和法令纹的柔化、磨皮的保边判断也要用到它
    bool hasSoftenRegion = false;
    for (const LmRegion& region : regions) {
        hasSoftenRegion = hasSoftenRegion || (region.kind > 0.5f && region.kind < 2.5f);
    }
    const bool needBlur = params.clarity > 0.001f || hasSoftenRegion || params.smooth > 0.001f;
    if (needBlur) {
        const float step = 2.0f * scale;
        glUseProgram(m_progGauss);
        SetInt(m_progGauss, "uTex", 0);
        BindTexture(0, m_rgb.tex);
        SetVec2(m_progGauss, "uStep", step / m_quarterA.w, 0.0f);
        DrawTo(m_quarterA);
        BindTexture(0, m_quarterA.tex);
        SetVec2(m_progGauss, "uStep", 0.0f, step / m_quarterA.h);
        DrawTo(m_quarterB);
    }

    // 3. 磨皮用的保边模糊
    const bool needSmooth = params.smooth > 0.001f;
    if (needSmooth) {
        const float step = 1.6f * scale;
        glUseProgram(m_progSkinBlur);
        SetInt(m_progSkinBlur, "uTex", 0);
        SetInt(m_progSkinBlur, "uLow", 1);
        BindTexture(1, m_quarterB.tex);
        SetFloat(m_progSkinBlur, "uEdge", tuning.smoothEdge);
        BindTexture(0, m_rgb.tex);
        SetVec2(m_progSkinBlur, "uStep", step / m_halfA.w, 0.0f);
        DrawTo(m_halfA);
        BindTexture(0, m_halfA.tex);
        SetVec2(m_progSkinBlur, "uStep", 0.0f, step / m_halfA.h);
        DrawTo(m_halfB);
    }

    // 美妆图层：半分辨率上画妆容三角形，再轻微模糊一次让边缘柔和
    const bool hasMakeupLayer = !makeup.empty();
    if (hasMakeupLayer) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_makeupA.fbo);
        glViewport(0, 0, m_makeupA.w, m_makeupA.h);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(m_progMakeup);
        SetVec2(m_progMakeup, "uSize", static_cast<float>(w), static_cast<float>(h));
        glBindVertexArray(m_makeupVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_makeupVbo);
        glBufferData(GL_ARRAY_BUFFER, makeup.size() * sizeof(LmMakeupVertex), makeup.data(), GL_STREAM_DRAW);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(makeup.size()));
        glDisable(GL_BLEND);
        glBindVertexArray(m_vao);

        // 方框均值保留 alpha（高斯着色器输出的 alpha 恒为 1，不能用在这里）
        glUseProgram(m_progBox);
        SetInt(m_progBox, "uTex", 0);
        SetInt(m_progBox, "uRadius", 1);
        BindTexture(0, m_makeupA.tex);
        SetVec2(m_progBox, "uStep", scale / m_makeupA.w, 0.0f);
        DrawTo(m_makeupB);
        BindTexture(0, m_makeupB.tex);
        SetVec2(m_progBox, "uStep", 0.0f, scale / m_makeupA.h);
        DrawTo(m_makeupA);
    }

    // 4. 合成
    glUseProgram(m_progCompose);
    SetInt(m_progCompose, "uSrc", 0);
    SetInt(m_progCompose, "uSmoothTex", 1);
    SetInt(m_progCompose, "uBlurTex", 2);
    BindTexture(0, m_rgb.tex);
    BindTexture(1, m_halfB.tex);
    BindTexture(2, m_quarterB.tex);
    SetVec2(m_progCompose, "uTexel", 1.0f / w, 1.0f / h);
    // 滑杆到强度的曲线：低档位也要有明显效果，低档位也有可见的变化
    SetFloat(m_progCompose, "uSmooth", needSmooth ? std::pow(params.smooth, tuning.smoothCurve) : 0.0f);
    SetFloat(m_progCompose, "uWhiten", params.whiten * tuning.whitenGain);
    SetFloat(m_progCompose, "uSmoothKeep", tuning.smoothKeep);
    SetFloat(m_progCompose, "uSmoothProtect", tuning.smoothProtect);
    SetFloat(m_progCompose, "uSmoothGrain", tuning.smoothGrain);
    SetFloat(m_progCompose, "uSmoothThreshold", tuning.smoothThreshold);
    SetFloat(m_progCompose, "uSmoothLift", tuning.smoothLift);
    SetFloat(m_progCompose, "uWhitenBeta", tuning.whitenBeta);
    SetFloat(m_progCompose, "uWhitenHueKeep", tuning.whitenHueKeep);
    SetFloat(m_progCompose, "uWhitenRosy", tuning.whitenRosy);
    SetFloat(m_progCompose, "uWhitenGlobal", tuning.whitenGlobal);
    SetFloat(m_progCompose, "uSharpenGain", tuning.sharpenGain);
    SetFloat(m_progCompose, "uClarityGain", tuning.clarityGain);
    SetFloat(m_progCompose, "uRegionBlurMix", tuning.regionBlurMix);
    SetFloat(m_progCompose, "uPouchLift", tuning.pouchLift);
    SetFloat(m_progCompose, "uFoldsLift", tuning.foldsLift);
    SetInt(m_progCompose, "uMakeup", 3);
    SetInt(m_progCompose, "uMakeupOn", hasMakeupLayer ? 1 : 0);
    SetFloat(m_progCompose, "uFoundation", hasMakeupLayer ? LmMakeupFoundation(params) : 0.0f);
    BindTexture(3, hasMakeupLayer ? m_makeupA.tex : 0);
    SetFloat(m_progCompose, "uSharpen", params.sharpen);
    SetFloat(m_progCompose, "uClarity", params.clarity);

    const int regionCount = static_cast<int>(regions.size() < LM_MAX_REGIONS ? regions.size() : LM_MAX_REGIONS);
    float regionA[LM_MAX_REGIONS * 4] = {0};
    float regionB[LM_MAX_REGIONS * 4] = {0};
    for (int i = 0; i < regionCount; ++i) {
        regionA[i * 4 + 0] = regions[i].cx;
        regionA[i * 4 + 1] = regions[i].cy;
        regionA[i * 4 + 2] = regions[i].strength;
        regionA[i * 4 + 3] = regions[i].kind;
        regionB[i * 4 + 0] = regions[i].ux;
        regionB[i * 4 + 1] = regions[i].uy;
        regionB[i * 4 + 2] = regions[i].vx;
        regionB[i * 4 + 3] = regions[i].vy;
    }
    SetInt(m_progCompose, "uRegionCount", regionCount);
    glUniform4fv(glGetUniformLocation(m_progCompose, "uRegionA"), LM_MAX_REGIONS, regionA);
    glUniform4fv(glGetUniformLocation(m_progCompose, "uRegionB"), LM_MAX_REGIONS, regionB);

    // 第 0 组：画质调节，0.5 为中性
    LmColorGrade quality;
    quality.temperature = (params.qualityTemperature - 0.5f) * 2.0f * tuning.qualityTemperature;
    quality.tint = (params.qualityHue - 0.5f) * 2.0f * tuning.qualityTint;
    quality.saturation = (params.qualitySaturation - 0.5f) * 2.0f * tuning.qualitySaturation;
    quality.brightness = (params.qualityLight - 0.5f) * 2.0f *
                         (params.qualityLight >= 0.5f ? tuning.qualityLight : tuning.qualityLightDown);
    quality.contrast = (params.qualityContrast - 0.5f) * 2.0f * tuning.qualityContrast;
    const bool hasQuality = quality.temperature != 0.0f || quality.tint != 0.0f || quality.saturation != 0.0f ||
                            quality.brightness != 0.0f || quality.contrast != 0.0f;
    SetGrade(m_progCompose, 0, quality, 1.0f, hasQuality ? 1.0f : 0.0f);

    // 第 1 组：滤镜；第 2 组：风格妆的色彩。两者可以叠加
    LmColorGrade filter;
    const bool hasFilter = LmGetFilterGrade(params.filterType, filter);
    SetGrade(m_progCompose, 1, filter, tuning.presetGain, hasFilter ? params.filterIntensity : 0.0f);

    LmColorGrade styleGrade;
    const bool hasStyleGrade =
        LmGetStyleGrade(params.styleType, styleGrade);
    SetGrade(m_progCompose, 2, styleGrade, tuning.presetGain, hasStyleGrade ? params.makeupFilterIntensity : 0.0f);
    DrawTo(m_final);

    // 5. 美型变形：只作用在人像上。反向映射，对每个网格顶点依次套用各个变形操作，得到它应当采样的位置。
    //    必须在合成虚拟背景之前做，否则替换进来的背景也会被一起拉扯，人一动背景就跟着变形。
    unsigned int resultTex = m_final.tex;
    const bool hasWarp = !warpOps.empty();
    if (hasWarp) {
        float* vertex = m_meshVertices.data();
        for (int row = 0; row <= kMeshRows; ++row) {
            for (int col = 0; col <= kMeshCols; ++col) {
                const float gx = static_cast<float>(col) / kMeshCols;
                const float gy = static_cast<float>(row) / kMeshRows;
                float px = gx * w;
                float py = gy * h;
                for (const LmWarpOp& op : warpOps) {
                    const float ox = px - op.cx;
                    const float oy = py - op.cy;
                    const float ratio = (ox * ox + oy * oy) / (op.radius * op.radius);
                    if (ratio >= 1.0f) {
                        continue;
                    }
                    const float weight = (1.0f - ratio) * (1.0f - ratio);
                    if (op.kind < 0.5f) {
                        px -= op.dx * weight;
                        py -= op.dy * weight;
                    } else {
                        px = op.cx + ox * (1.0f - op.dx * weight);
                        py = op.cy + oy * (1.0f - op.dx * weight);
                    }
                }
                *vertex++ = gx * 2.0f - 1.0f;
                *vertex++ = gy * 2.0f - 1.0f;
                *vertex++ = px / w;
                *vertex++ = py / h;
            }
        }
        glBindVertexArray(m_meshVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_meshVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, m_meshVertices.size() * sizeof(float), m_meshVertices.data());
        DrawMesh(m_final.tex, m_warped);
        resultTex = m_warped.tex;
    }

    // 6. 虚拟背景：图片模式要求已经设置了背景图，否则不处理
    const bool usePicture = params.background == LM_BG_PICTURE && m_texBackground != 0;
    if (mask && (params.background == LM_BG_BLUR || usePicture)) {
        if (m_texMask == 0) {
            m_texMask = CreatePlaneTexture(LM_MASK_WIDTH, LM_MASK_HEIGHT);
        }
        BindTexture(2, m_texMask);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LM_MASK_WIDTH, LM_MASK_HEIGHT, GL_RED, GL_UNSIGNED_BYTE, mask);

        // 人像做过美型变形时，掩膜也要按同一个网格变形，才能和变形后的人像对齐
        unsigned int maskTex = m_texMask;
        if (hasWarp) {
            if (m_maskWarped.fbo == 0) {
                CreateTarget(m_maskWarped, LM_MASK_WIDTH, LM_MASK_HEIGHT, true);
            }
            glBindVertexArray(m_meshVao);
            DrawMesh(m_texMask, m_maskWarped);
            maskTex = m_maskWarped.tex;
        }

        unsigned int backgroundTex = m_texBackground;
        if (!usePicture) {
            // 背景虚化：用未变形的画面做模糊，四分之一分辨率上两轮高斯，第二轮步长加倍，得到大半径的模糊
            glUseProgram(m_progGauss);
            SetInt(m_progGauss, "uTex", 0);
            const float steps[2] = {tuning.backgroundBlurStep * scale, tuning.backgroundBlurStep * 2.0f * scale};
            unsigned int source = m_final.tex;
            for (float step : steps) {
                BindTexture(0, source);
                SetVec2(m_progGauss, "uStep", step / m_bgBlurA.w, 0.0f);
                DrawTo(m_bgBlurA);
                BindTexture(0, m_bgBlurA.tex);
                SetVec2(m_progGauss, "uStep", 0.0f, step / m_bgBlurA.h);
                DrawTo(m_bgBlurB);
                source = m_bgBlurB.tex;
            }
            backgroundTex = m_bgBlurB.tex;
        }

        const bool refine = RefineMask(resultTex, maskTex);

        glUseProgram(m_progComposite);
        SetInt(m_progComposite, "uCoef", 3);
        SetInt(m_progComposite, "uRefine", refine ? 1 : 0);
        BindTexture(3, refine ? m_guideB.tex : 0);
        SetInt(m_progComposite, "uFg", 0);
        SetInt(m_progComposite, "uBg", 1);
        SetInt(m_progComposite, "uMask", 2);
        SetVec2(m_progComposite, "uMaskTexel", 1.0f / LM_MASK_WIDTH, 1.0f / LM_MASK_HEIGHT);
        // 图片背景：保持图片比例铺满画面，多出的部分居中裁掉
        float bgScaleX = 1.0f;
        float bgScaleY = 1.0f;
        if (usePicture && m_backgroundW > 0 && m_backgroundH > 0) {
            const float frameAspect = static_cast<float>(w) / h;
            const float imageAspect = static_cast<float>(m_backgroundW) / m_backgroundH;
            if (imageAspect > frameAspect) {
                bgScaleX = frameAspect / imageAspect;
            } else {
                bgScaleY = imageAspect / frameAspect;
            }
        }
        SetVec2(m_progComposite, "uBgScale", bgScaleX, bgScaleY);
        SetVec2(m_progComposite, "uBgOffset", (1.0f - bgScaleX) * 0.5f, (1.0f - bgScaleY) * 0.5f);
        BindTexture(0, resultTex);
        BindTexture(1, backgroundTex);
        BindTexture(2, maskTex);
        DrawTo(m_composited);
        resultTex = m_composited.tex;
    }

    // 卡通头像与头饰：画在最上层，虚拟背景之后，伸出人像轮廓的部分（耳朵、帽子）也能完整显示
    if ((params.avatar > 0 || params.headwear > 0) && !avatars.empty()) {
        const int count = static_cast<int>(avatars.size() < 2 ? avatars.size() : 2);
        float pose[8] = {0};
        float pose2[8] = {0};
        float expr[8] = {0};
        float expr2[8] = {0};
        for (int i = 0; i < count; ++i) {
            const LmAvatarFace& a = avatars[i];
            const float values[4][4] = {{a.cx, a.cy, a.ux, a.uy},
                                        {a.scale, a.yaw, a.pitch, a.top},
                                        {a.eyeLeft, a.eyeRight, a.mouthOpen, a.smile},
                                        {a.brow, a.gazeX, a.gazeY, a.eyeHalf}};
            for (int k = 0; k < 4; ++k) {
                pose[i * 4 + k] = values[0][k];
                pose2[i * 4 + k] = values[1][k];
                expr[i * 4 + k] = values[2][k];
                expr2[i * 4 + k] = values[3][k];
            }
        }
        static const auto start = std::chrono::steady_clock::now();
        const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        glUseProgram(m_progAvatar);
        SetInt(m_progAvatar, "uTex", 0);
        SetVec2(m_progAvatar, "uSize", static_cast<float>(w), static_cast<float>(h));
        SetInt(m_progAvatar, "uFaceCount", count);
        SetInt(m_progAvatar, "uAvatar", params.avatar);
        SetInt(m_progAvatar, "uHeadwear", params.headwear);
        SetFloat(m_progAvatar, "uTime", seconds);
        glUniform4fv(glGetUniformLocation(m_progAvatar, "uPose"), 2, pose);
        glUniform4fv(glGetUniformLocation(m_progAvatar, "uPose2"), 2, pose2);
        glUniform4fv(glGetUniformLocation(m_progAvatar, "uExpr"), 2, expr);
        glUniform4fv(glGetUniformLocation(m_progAvatar, "uExpr2"), 2, expr2);
        BindTexture(0, resultTex);
        DrawTo(m_avatarOut);
        resultTex = m_avatarOut.tex;
    }

    // 7. 转回 I420 并回读
    BindTexture(0, resultTex);
    glUseProgram(m_progRgb2Y);
    SetInt(m_progRgb2Y, "uTex", 0);
    DrawTo(m_outY);
    glReadPixels(0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, out);

    glUseProgram(m_progRgb2UV);
    SetInt(m_progRgb2UV, "uTex", 0);
    DrawTo(m_outUV);
    glReadPixels(0, 0, w / 2, h, GL_RED, GL_UNSIGNED_BYTE, out + static_cast<size_t>(w) * h);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return glGetError() == GL_NO_ERROR;
}
