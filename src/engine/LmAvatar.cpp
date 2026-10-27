#include "LmAvatar.hpp"

#include <algorithm>
#include <cmath>

namespace {

// MediaPipe Face Mesh 的关键点序号（"左""右"指画面中的左右）
const int kFaceLeft = 234;
const int kFaceRight = 454;
const int kForeheadTop = 10;
const int kChin = 152;
const int kNoseTip = 1;
const int kIrisLeft = 468;
const int kIrisRight = 473;
const int kEyeOuterLeft = 33;
const int kEyeInnerLeft = 133;
const int kEyeUpperLeft = 159;
const int kEyeLowerLeft = 145;
const int kEyeOuterRight = 263;
const int kEyeInnerRight = 362;
const int kEyeUpperRight = 386;
const int kEyeLowerRight = 374;
const int kBrowLeft = 105;
const int kBrowRight = 334;
const int kLipUpper = 13;
const int kLipLower = 14;
const int kMouthLeft = 61;
const int kMouthRight = 291;

struct Vec2 {
    float x;
    float y;
};

Vec2 P(const LmFace& f, int i) {
    return Vec2{f.x[i], f.y[i]};
}

float Dist(const Vec2& a, const Vec2& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

float Clamp(float v, float lo, float hi) {
    return std::min(std::max(v, lo), hi);
}

/// 指数平滑：变化大时跟得快，变化小时压抖动
float Follow(float previous, float value, float slow, float fast, float range) {
    const float change = std::fabs(value - previous);
    const float weight = slow + (fast - slow) * Clamp(change / range, 0.0f, 1.0f);
    return previous + (value - previous) * weight;
}

} // namespace

void LmBuildAvatarFace(const LmFace& face, LmAvatarState& state, LmAvatarFace& out) {
    const Vec2 irisL = P(face, kIrisLeft);
    const Vec2 irisR = P(face, kIrisRight);
    const Vec2 faceL = P(face, kFaceLeft);
    const Vec2 faceR = P(face, kFaceRight);
    const float faceWidth = std::max(Dist(faceL, faceR), 1.0f);
    const float s = faceWidth * 0.5f;

    // 眼角连线比瞳孔稳定，用来定 x 轴方向；原点取两眼眼角的中点
    const Vec2 eyeL{(face.x[kEyeOuterLeft] + face.x[kEyeInnerLeft]) * 0.5f,
                    (face.y[kEyeOuterLeft] + face.y[kEyeInnerLeft]) * 0.5f};
    const Vec2 eyeR{(face.x[kEyeOuterRight] + face.x[kEyeInnerRight]) * 0.5f,
                    (face.y[kEyeOuterRight] + face.y[kEyeInnerRight]) * 0.5f};
    float ux = eyeR.x - eyeL.x;
    float uy = eyeR.y - eyeL.y;
    const float eyeDist = std::max(std::sqrt(ux * ux + uy * uy), 1.0f);
    ux /= eyeDist;
    uy /= eyeDist;
    const Vec2 c{(eyeL.x + eyeR.x) * 0.5f, (eyeL.y + eyeR.y) * 0.5f};

    // 画面坐标转局部坐标（y 向上）
    auto local = [&](const Vec2& p) {
        const float dx = p.x - c.x;
        const float dy = p.y - c.y;
        return Vec2{(dx * ux + dy * uy) / s, -(-dx * uy + dy * ux) / s};
    };

    LmAvatarFace f;
    f.cx = c.x;
    f.cy = c.y;
    f.ux = ux;
    f.uy = uy;
    f.scale = s;
    f.eyeHalf = eyeDist * 0.5f / s;

    // 转头：鼻尖相对脸两侧中点的偏移；抬头：鼻尖相对眼睛与下巴的位置
    const Vec2 nose = local(P(face, kNoseTip));
    const Vec2 mid = local(Vec2{(faceL.x + faceR.x) * 0.5f, (faceL.y + faceR.y) * 0.5f});
    f.yaw = Clamp((nose.x - mid.x) * 2.2f, -1.0f, 1.0f);
    const Vec2 chin = local(P(face, kChin));
    const float noseRatio = nose.y / std::min(chin.y, -0.1f);  // 正脸约 0.45，抬头变小
    f.pitch = Clamp((0.45f - noseRatio) * 4.0f, -1.0f, 1.0f);

    // 头顶：额头最高点再往上留出头发
    f.top = local(P(face, kForeheadTop)).y + 0.35f;
    f.mouthY = local(Vec2{(face.x[kLipUpper] + face.x[kLipLower]) * 0.5f,
                          (face.y[kLipUpper] + face.y[kLipLower]) * 0.5f}).y;
    f.mouthHalf = Dist(P(face, kMouthLeft), P(face, kMouthRight)) * 0.5f / s;
    f.chinY = chin.y;
    // 耳朵：脸两侧外缘点大致在耳屏前，耳垂再往下一点
    const Vec2 earL = local(faceL);
    const Vec2 earR = local(faceR);
    f.earLeftX = earL.x;
    f.earRightX = earR.x;
    f.earY = (earL.y + earR.y) * 0.5f - 0.2f;

    // 睁眼程度：上下眼睑距离 / 眼宽，按近期最大值归一化
    const float eyeRatioL = Dist(P(face, kEyeUpperLeft), P(face, kEyeLowerLeft)) /
                            std::max(Dist(P(face, kEyeOuterLeft), P(face, kEyeInnerLeft)), 1.0f);
    const float eyeRatioR = Dist(P(face, kEyeUpperRight), P(face, kEyeLowerRight)) /
                            std::max(Dist(P(face, kEyeOuterRight), P(face, kEyeInnerRight)), 1.0f);
    // 嘴：上下唇内侧距离 / 嘴宽；微笑：嘴角相对唇中心上扬的量与嘴变宽的量
    const float mouthWidth = Dist(P(face, kMouthLeft), P(face, kMouthRight));
    const float mouthRatio = Dist(P(face, kLipUpper), P(face, kLipLower)) / std::max(mouthWidth, 1.0f);
    const Vec2 lipCenter = local(Vec2{(face.x[kLipUpper] + face.x[kLipLower]) * 0.5f,
                                      (face.y[kLipUpper] + face.y[kLipLower]) * 0.5f});
    const float cornerY = (local(P(face, kMouthLeft)).y + local(P(face, kMouthRight)).y) * 0.5f;
    const float widthRatio = mouthWidth / faceWidth;
    // 挑眉：眉毛到眼睛的距离
    const float browHeight = (local(P(face, kBrowLeft)).y + local(P(face, kBrowRight)).y) * 0.5f;

    if (!state.valid) {
        state.eyeMaxLeft = std::max(eyeRatioL, 0.22f);
        state.eyeMaxRight = std::max(eyeRatioR, 0.22f);
        state.browMean = browHeight;
        state.mouthWidthMean = widthRatio;
    }
    // 最大值缓慢回落，适应不同的人；均值缓慢跟随
    state.eyeMaxLeft = std::max(state.eyeMaxLeft * 0.998f, std::max(eyeRatioL, 0.18f));
    state.eyeMaxRight = std::max(state.eyeMaxRight * 0.998f, std::max(eyeRatioR, 0.18f));
    state.browMean += (browHeight - state.browMean) * 0.01f;
    state.mouthWidthMean += (widthRatio - state.mouthWidthMean) * 0.005f;

    f.eyeLeft = Clamp((eyeRatioL / state.eyeMaxLeft - 0.35f) / 0.45f, 0.0f, 1.0f);
    f.eyeRight = Clamp((eyeRatioR / state.eyeMaxRight - 0.35f) / 0.45f, 0.0f, 1.0f);
    f.mouthOpen = Clamp((mouthRatio - 0.06f) / 0.45f, 0.0f, 1.0f);
    f.smile = Clamp((cornerY - lipCenter.y) * 6.0f + (widthRatio - state.mouthWidthMean) * 12.0f, 0.0f, 1.0f);
    f.brow = Clamp((browHeight - state.browMean) / 0.08f, 0.0f, 1.0f);

    // 视线：瞳孔在眼眶内的相对位置
    const Vec2 irisLocalL = local(irisL);
    const Vec2 irisLocalR = local(irisR);
    const Vec2 eyeLocalL = local(eyeL);
    const Vec2 eyeLocalR = local(eyeR);
    const float eyeWidth = std::max(Dist(P(face, kEyeOuterLeft), P(face, kEyeInnerLeft)) / s, 0.05f);
    f.gazeX = Clamp(((irisLocalL.x - eyeLocalL.x) + (irisLocalR.x - eyeLocalR.x)) / eyeWidth * 2.0f, -1.0f, 1.0f);
    f.gazeY = Clamp(((irisLocalL.y - eyeLocalL.y) + (irisLocalR.y - eyeLocalR.y)) / eyeWidth * 3.0f, -1.0f, 1.0f);

    if (state.valid) {
        const LmAvatarFace& p = state.last;
        // 位置和角度已经由关键点跟踪平滑过，这里只平滑表情，眨眼要跟得快
        f.eyeLeft = Follow(p.eyeLeft, f.eyeLeft, 0.35f, 0.9f, 0.4f);
        f.eyeRight = Follow(p.eyeRight, f.eyeRight, 0.35f, 0.9f, 0.4f);
        f.mouthOpen = Follow(p.mouthOpen, f.mouthOpen, 0.3f, 0.8f, 0.3f);
        f.smile = Follow(p.smile, f.smile, 0.15f, 0.5f, 0.4f);
        f.brow = Follow(p.brow, f.brow, 0.15f, 0.5f, 0.4f);
        f.gazeX = Follow(p.gazeX, f.gazeX, 0.2f, 0.6f, 0.5f);
        f.gazeY = Follow(p.gazeY, f.gazeY, 0.2f, 0.6f, 0.5f);
        f.yaw = Follow(p.yaw, f.yaw, 0.3f, 0.8f, 0.3f);
        f.pitch = Follow(p.pitch, f.pitch, 0.3f, 0.8f, 0.3f);
    }
    state.last = f;
    state.valid = true;
    out = f;
}
