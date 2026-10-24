#include "LmFace.hpp"

#include <algorithm>
#include <cmath>
#include "LmTuning.hpp"

namespace {

// MediaPipe Face Mesh 的关键点序号
enum FacePoint {
    kForeheadTop = 10,
    kChin = 152,
    kNoseTip = 1,
    kFaceLeft = 234,
    kFaceRight = 454,
    kCheekLeft = 123,
    kCheekRight = 352,
    kCheekboneLeft = 116,
    kCheekboneRight = 345,
    kJawUpperLeft = 132,
    kJawUpperRight = 361,
    kJawAngleLeft = 58,
    kJawAngleRight = 288,
    kJawLowerLeft = 172,
    kJawLowerRight = 397,
    kNoseWingLeft = 129,
    kNoseWingRight = 358,
    kEyeOuterLeft = 33,
    kEyeInnerLeft = 133,
    kEyeOuterRight = 263,
    kEyeInnerRight = 362,
    kIrisLeft = 468,
    kIrisRight = 473,
    kEyeLowerLeft = 145,
    kEyeLowerRight = 374,
    kBrowInnerLeft = 107,
    kBrowOuterLeft = 70,
    kBrowInnerRight = 336,
    kBrowOuterRight = 300,
    kLipInnerUpper = 13,
    kLipInnerLower = 14,
    kLipInnerLeft = 78,
    kLipInnerRight = 308,
    kMouthLeft = 61,
    kMouthRight = 291,
};

struct Vec2 {
    float x;
    float y;
};

Vec2 Point(const LmFace& face, int index) {
    return Vec2{face.x[index], face.y[index]};
}

float Distance(const Vec2& a, const Vec2& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

void AddOp(std::vector<LmWarpOp>& ops, const LmWarpOp& op) {
    if (ops.size() < LM_MAX_WARP_OPS) {
        ops.push_back(op);
    }
}

// 把 from 处的内容朝 target 方向推 amount 像素
void AddPull(std::vector<LmWarpOp>& ops, const Vec2& from, const Vec2& target, float amount, float radius) {
    const float length = Distance(from, target);
    if (length < 1.0f || std::fabs(amount) < 0.01f) {
        return;
    }
    LmWarpOp op;
    op.cx = from.x;
    op.cy = from.y;
    op.dx = (target.x - from.x) / length * amount;
    op.dy = (target.y - from.y) / length * amount;
    op.radius = radius;
    op.kind = 0.0f;
    AddOp(ops, op);
}

void AddScale(std::vector<LmWarpOp>& ops, const Vec2& center, float amount, float radius) {
    if (std::fabs(amount) < 0.001f || radius < 1.0f) {
        return;
    }
    LmWarpOp op;
    op.cx = center.x;
    op.cy = center.y;
    op.dx = amount;
    op.radius = radius;
    op.kind = 1.0f;
    AddOp(ops, op);
}

} // namespace

int LmBuildWarpOps(const LmFace& face, const LmParams& params, std::vector<LmWarpOp>& ops) {
    const size_t before = ops.size();
    const LmTuning& tuning = LmGetTuning();

    const Vec2 top = Point(face, kForeheadTop);
    const Vec2 chin = Point(face, kChin);
    const Vec2 nose = Point(face, kNoseTip);
    const float faceWidth = Distance(Point(face, kFaceLeft), Point(face, kFaceRight));
    const float faceHeight = Distance(top, chin);
    if (faceWidth < 8.0f || faceHeight < 8.0f) {
        return 0;
    }

    // 脸部中轴线上的两个参考点：上半脸朝鼻尖收，下半脸朝鼻尖与下巴的中点收
    const Vec2 lower = Vec2{(nose.x + chin.x) * 0.5f, (nose.y + chin.y) * 0.5f};

    // 瘦脸：脸颊到下颌整体内收
    if (params.faceOverall > 0.001f) {
        const float amount = params.faceOverall * faceWidth * tuning.reshapeOverall;
        const float radius = faceWidth * 0.42f;
        AddPull(ops, Point(face, kJawUpperLeft), lower, amount, radius);
        AddPull(ops, Point(face, kJawUpperRight), lower, amount, radius);
        AddPull(ops, Point(face, kJawAngleLeft), lower, amount, radius);
        AddPull(ops, Point(face, kJawAngleRight), lower, amount, radius);
    }

    // 窄脸：脸两侧最宽处内收
    if (params.faceCut > 0.001f) {
        const float amount = params.faceCut * faceWidth * tuning.reshapeFaceCut;
        const float radius = faceWidth * 0.40f;
        AddPull(ops, Point(face, kFaceLeft), nose, amount, radius);
        AddPull(ops, Point(face, kFaceRight), nose, amount, radius);
        AddPull(ops, Point(face, kCheekLeft), nose, amount * 0.7f, radius);
        AddPull(ops, Point(face, kCheekRight), nose, amount * 0.7f, radius);
    }

    // 颧骨
    if (params.cheekbone > 0.001f) {
        const float amount = params.cheekbone * faceWidth * tuning.reshapeCheek;
        const float radius = faceWidth * 0.30f;
        AddPull(ops, Point(face, kCheekboneLeft), nose, amount, radius);
        AddPull(ops, Point(face, kCheekboneRight), nose, amount, radius);
    }

    // 下颌骨
    if (params.jaw > 0.001f) {
        const float amount = params.jaw * faceWidth * tuning.reshapeJaw;
        const float radius = faceWidth * 0.30f;
        AddPull(ops, Point(face, kJawAngleLeft), lower, amount, radius);
        AddPull(ops, Point(face, kJawAngleRight), lower, amount, radius);
        AddPull(ops, Point(face, kJawLowerLeft), lower, amount * 0.8f, radius);
        AddPull(ops, Point(face, kJawLowerRight), lower, amount * 0.8f, radius);
    }

    // 瘦鼻：0.5 为中性，大于 0.5 鼻翼内收，小于 0.5 外扩
    const float noseAmount = (params.noseWing - 0.5f) * 2.0f;
    if (std::fabs(noseAmount) > 0.002f) {
        const float amount = noseAmount * faceWidth * tuning.reshapeNose;
        const float radius = faceWidth * 0.16f;
        AddPull(ops, Point(face, kNoseWingLeft), nose, amount, radius);
        AddPull(ops, Point(face, kNoseWingRight), nose, amount, radius);
    }

    // 额头：0.5 为中性，大于 0.5 发际线上移（额头变高），小于 0.5 下移
    const float foreheadAmount = (params.forehead - 0.5f) * 2.0f;
    if (std::fabs(foreheadAmount) > 0.002f) {
        // 沿下巴指向额头的方向外推
        const Vec2 beyond = Vec2{top.x + (top.x - chin.x), top.y + (top.y - chin.y)};
        AddPull(ops, top, beyond, foreheadAmount * faceHeight * tuning.reshapeForehead, faceWidth * 0.55f);
    }

    // 大眼：以虹膜为中心局部放大
    if (params.eyeSize > 0.001f) {
        const float leftWidth = Distance(Point(face, kEyeOuterLeft), Point(face, kEyeInnerLeft));
        const float rightWidth = Distance(Point(face, kEyeOuterRight), Point(face, kEyeInnerRight));
        AddScale(ops, Point(face, kIrisLeft), params.eyeSize * tuning.reshapeEye, leftWidth * 1.15f);
        AddScale(ops, Point(face, kIrisRight), params.eyeSize * tuning.reshapeEye, rightWidth * 1.15f);
    }

    // 小头：以脸中心整体缩小，放在最后，作用在前面的结果之上
    if (params.shrinkHead > 0.001f) {
        const Vec2 center = Vec2{(top.x + chin.x) * 0.5f, (top.y + chin.y) * 0.5f};
        AddScale(ops, center, -params.shrinkHead * tuning.reshapeShrinkHead, faceHeight * 1.25f);
    }

    return static_cast<int>(ops.size() - before);
}

namespace {

// 以 center 为中心、长轴沿 axis 方向的椭圆区域
void AddRegion(std::vector<LmRegion>& regions, const Vec2& center, const Vec2& axis, float majorRadius,
               float minorRadius, float strength, LmRegionKind kind) {
    const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y);
    if (regions.size() >= LM_MAX_REGIONS || length < 0.001f || majorRadius < 1.0f || minorRadius < 1.0f ||
        strength < 0.001f) {
        return;
    }
    const float ax = axis.x / length;
    const float ay = axis.y / length;
    LmRegion region;
    region.cx = center.x;
    region.cy = center.y;
    region.ux = ax / majorRadius;
    region.uy = ay / majorRadius;
    region.vx = -ay / minorRadius;
    region.vy = ax / minorRadius;
    region.strength = strength;
    region.kind = static_cast<float>(kind);
    regions.push_back(region);
}

} // namespace

int LmBuildRegions(const LmFace& face, const LmParams& params, std::vector<LmRegion>& regions) {
    const size_t before = regions.size();

    const Vec2 top = Point(face, kForeheadTop);
    const Vec2 chin = Point(face, kChin);
    const float faceHeight = Distance(top, chin);
    if (faceHeight < 8.0f) {
        return 0;
    }
    // 脸朝下的方向，脸倾斜时各区域跟着转
    const Vec2 down = Vec2{(chin.x - top.x) / faceHeight, (chin.y - top.y) / faceHeight};

    // 磨皮保护区：眼睛、眉毛、嘴唇。肤色判断会把这些部位的一部分当成皮肤，不避开就会被一起磨糊
    // 美妆的粉底也要用到人脸范围和保护区
    if (params.smooth > 0.001f || params.makeupMakeupIntensity > 0.001f) {
        const float faceWidth = Distance(Point(face, kFaceLeft), Point(face, kFaceRight));
        AddRegion(regions, Vec2{(top.x + chin.x) * 0.5f, (top.y + chin.y) * 0.5f}, Vec2{chin.x - top.x, chin.y - top.y},
                  faceHeight * 0.56f, faceWidth * 0.56f, 1.0f, LM_REGION_FACE);
        const int outer[2] = {kEyeOuterLeft, kEyeOuterRight};
        const int inner[2] = {kEyeInnerLeft, kEyeInnerRight};
        for (int i = 0; i < 2; ++i) {
            const Vec2 a = Point(face, outer[i]);
            const Vec2 b = Point(face, inner[i]);
            const float eyeWidth = Distance(a, b);
            AddRegion(regions, Vec2{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, Vec2{b.x - a.x, b.y - a.y},
                      eyeWidth * 0.80f, eyeWidth * 0.45f, 1.0f, LM_REGION_PROTECT);
        }
        const int browInner[2] = {kBrowInnerLeft, kBrowInnerRight};
        const int browOuter[2] = {kBrowOuterLeft, kBrowOuterRight};
        for (int i = 0; i < 2; ++i) {
            const Vec2 a = Point(face, browOuter[i]);
            const Vec2 b = Point(face, browInner[i]);
            const float length = Distance(a, b);
            AddRegion(regions, Vec2{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, Vec2{b.x - a.x, b.y - a.y},
                      length * 0.62f, length * 0.20f, 1.0f, LM_REGION_PROTECT);
        }
        const Vec2 left = Point(face, kMouthLeft);
        const Vec2 right = Point(face, kMouthRight);
        const Vec2 upper = Point(face, kLipInnerUpper);
        const Vec2 lower = Point(face, kLipInnerLower);
        const float mouthWidth = Distance(left, right);
        AddRegion(regions, Vec2{(left.x + right.x + upper.x + lower.x) * 0.25f, (left.y + right.y + upper.y + lower.y) * 0.25f},
                  Vec2{right.x - left.x, right.y - left.y}, mouthWidth * 0.58f,
                  std::max(mouthWidth * 0.22f, Distance(upper, lower) * 0.9f), 1.0f, LM_REGION_PROTECT);
    }

    // 白牙：内唇围成的区域。嘴闭着时内唇上下几乎重合，区域高度接近零，不做处理
    if (params.whitenTeeth > 0.001f) {
        const Vec2 left = Point(face, kLipInnerLeft);
        const Vec2 right = Point(face, kLipInnerRight);
        const Vec2 upper = Point(face, kLipInnerUpper);
        const Vec2 lower = Point(face, kLipInnerLower);
        const float width = Distance(left, right);
        const float opening = Distance(upper, lower);
        if (opening > width * 0.06f) {
            const Vec2 center = Vec2{(left.x + right.x + upper.x + lower.x) * 0.25f,
                                     (left.y + right.y + upper.y + lower.y) * 0.25f};
            AddRegion(regions, center, Vec2{right.x - left.x, right.y - left.y}, width * 0.52f, opening * 0.62f,
                      params.whitenTeeth, LM_REGION_TEETH);
        }
    }

    // 黑眼圈：每只眼睛下眼睑的下方
    if (params.removePouch > 0.001f) {
        const int outer[2] = {kEyeOuterLeft, kEyeOuterRight};
        const int inner[2] = {kEyeInnerLeft, kEyeInnerRight};
        const int lowerLid[2] = {kEyeLowerLeft, kEyeLowerRight};
        for (int i = 0; i < 2; ++i) {
            const Vec2 a = Point(face, outer[i]);
            const Vec2 b = Point(face, inner[i]);
            const Vec2 lid = Point(face, lowerLid[i]);
            const float eyeWidth = Distance(a, b);
            const Vec2 center = Vec2{lid.x + down.x * eyeWidth * 0.38f, lid.y + down.y * eyeWidth * 0.38f};
            AddRegion(regions, center, Vec2{b.x - a.x, b.y - a.y}, eyeWidth * 0.72f, eyeWidth * 0.34f,
                      params.removePouch, LM_REGION_SOFTEN);
        }
    }

    // 法令纹：从鼻翼外侧到嘴角外侧的一条窄带
    if (params.removeSmileFolds > 0.001f) {
        const int wing[2] = {kNoseWingLeft, kNoseWingRight};
        const int mouth[2] = {kMouthLeft, kMouthRight};
        const Vec2 nose = Point(face, kNoseTip);
        for (int i = 0; i < 2; ++i) {
            const Vec2 w = Point(face, wing[i]);
            const Vec2 m = Point(face, mouth[i]);
            // 两个端点都从中线向外推一点，落在纹路上而不是鼻翼和嘴角上
            const Vec2 start = Vec2{w.x + (w.x - nose.x) * 0.35f, w.y + (w.y - nose.y) * 0.35f};
            const Vec2 end = Vec2{m.x + (m.x - nose.x) * 0.30f, m.y + (m.y - nose.y) * 0.30f};
            const float length = Distance(start, end);
            const Vec2 center = Vec2{(start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f};
            AddRegion(regions, center, Vec2{end.x - start.x, end.y - start.y}, length * 0.62f, length * 0.24f,
                      params.removeSmileFolds, LM_REGION_FOLDS);
        }
    }

    return static_cast<int>(regions.size() - before);
}
