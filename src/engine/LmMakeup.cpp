#include "LmMakeup.hpp"

#include <algorithm>
#include <cmath>

namespace {

/**
 * @brief 一个风格的妆容
 * @remark 颜色是正片叠底色，1 为不变，越小该通道压得越暗；opacity 是滑杆拉满时的浓度。
 *         这些妆容按风格名称自行设计，不来自任何第三方素材，可按设计稿直接调整。
 */
struct MakeupLook {
    float lip[3];
    float lipOpacity;
    float blush[3];
    float blushOpacity;
    float blushSize;     ///< 腮红半径，相对脸宽
    float brow[3];
    float browOpacity;
    float shadow[3];
    float shadowOpacity;
    float contour[3];
    float contourOpacity;
    float foundation;    ///< 粉底提亮量，只作用于脸部皮肤
};

// 顺序与 lumina_style 一致（从 1 开始）
const MakeupLook kLooks[] = {
    // 白皙：浅豆沙唇，淡粉腮红，妆感轻
    {{0.95f, 0.62f, 0.66f}, 0.55f, {1.00f, 0.84f, 0.86f}, 0.40f, 0.17f, {0.72f, 0.62f, 0.56f}, 0.30f,
     {0.96f, 0.84f, 0.82f}, 0.30f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.25f},
    // 韩系：珊瑚唇，蜜桃腮红，浅棕眉
    {{1.00f, 0.56f, 0.52f}, 0.60f, {1.00f, 0.80f, 0.78f}, 0.50f, 0.17f, {0.72f, 0.62f, 0.55f}, 0.35f,
     {0.98f, 0.80f, 0.74f}, 0.35f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.25f},
    // 元气：亮红唇，大面积腮红
    {{1.00f, 0.46f, 0.50f}, 0.65f, {1.00f, 0.76f, 0.76f}, 0.55f, 0.20f, {0.72f, 0.62f, 0.56f}, 0.30f,
     {1.00f, 0.84f, 0.78f}, 0.30f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.30f},
    // 温暖：橘棕唇，暖杏色腮红与眼影
    {{0.95f, 0.56f, 0.44f}, 0.60f, {1.00f, 0.80f, 0.70f}, 0.45f, 0.17f, {0.66f, 0.54f, 0.44f}, 0.40f,
     {0.94f, 0.78f, 0.66f}, 0.40f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.15f},
    // 欧美：深红唇，大地色眼影，修容
    {{0.80f, 0.36f, 0.36f}, 0.70f, {0.96f, 0.76f, 0.70f}, 0.35f, 0.16f, {0.56f, 0.46f, 0.40f}, 0.50f,
     {0.76f, 0.62f, 0.56f}, 0.55f, {0.86f, 0.76f, 0.70f}, 0.45f, 0.08f},
    // 优雅：玫瑰唇，轻修容
    {{0.86f, 0.42f, 0.48f}, 0.60f, {0.98f, 0.80f, 0.82f}, 0.35f, 0.16f, {0.62f, 0.54f, 0.50f}, 0.40f,
     {0.88f, 0.76f, 0.76f}, 0.40f, {0.90f, 0.82f, 0.78f}, 0.25f, 0.12f},
    // 甜美：粉唇，粉色腮红
    {{1.00f, 0.52f, 0.62f}, 0.65f, {1.00f, 0.76f, 0.82f}, 0.55f, 0.18f, {0.72f, 0.62f, 0.56f}, 0.30f,
     {1.00f, 0.80f, 0.84f}, 0.35f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.20f},
    // 深邃：暗红唇，深色眼影与修容，眉色加深
    {{0.74f, 0.36f, 0.36f}, 0.65f, {0.92f, 0.76f, 0.72f}, 0.30f, 0.15f, {0.50f, 0.42f, 0.38f}, 0.55f,
     {0.68f, 0.56f, 0.52f}, 0.60f, {0.82f, 0.72f, 0.66f}, 0.50f, 0.08f},
    // 伪素颜：接近唇色的淡唇，极淡腮红，不画眼影
    {{0.98f, 0.70f, 0.68f}, 0.40f, {1.00f, 0.86f, 0.86f}, 0.30f, 0.16f, {0.76f, 0.66f, 0.60f}, 0.25f,
     {1.0f, 1.0f, 1.0f}, 0.0f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.18f},
    // 学姐：干枯玫瑰唇，大地色眼影，轻修容
    {{0.88f, 0.46f, 0.46f}, 0.60f, {0.98f, 0.80f, 0.80f}, 0.35f, 0.16f, {0.60f, 0.52f, 0.48f}, 0.45f,
     {0.86f, 0.72f, 0.68f}, 0.45f, {0.88f, 0.80f, 0.76f}, 0.30f, 0.15f},
    // 学妹：水粉唇，粉嫩腮红
    {{1.00f, 0.58f, 0.62f}, 0.55f, {1.00f, 0.80f, 0.82f}, 0.50f, 0.18f, {0.74f, 0.64f, 0.58f}, 0.30f,
     {1.00f, 0.86f, 0.82f}, 0.30f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.20f},
};
const int kLookCount = sizeof(kLooks) / sizeof(kLooks[0]);

// 眼线与睫毛的浓度，顺序同 kLooks：白皙、韩系、元气、温暖、欧美、优雅、甜美、深邃、伪素颜、学姐、学妹
const float kLinerOpacity[] = {0.50f, 0.45f, 0.40f, 0.50f, 0.80f, 0.65f, 0.45f, 0.85f, 0.15f, 0.60f, 0.40f};
const float kLashOpacity[] = {0.50f, 0.55f, 0.60f, 0.50f, 0.80f, 0.60f, 0.70f, 0.80f, 0.30f, 0.60f, 0.65f};
const float kLinerColor[3] = {0.22f, 0.18f, 0.18f};
const float kLashColor[3] = {0.12f, 0.10f, 0.10f};

// MediaPipe Face Mesh 的关键点序号
const int kLipOuter[] = {61, 185, 40, 39, 37, 0, 267, 269, 270, 409, 291, 375, 321, 405, 314, 17, 84, 181, 91, 146};
const int kLipInner[] = {78, 191, 80, 81, 82, 13, 312, 311, 310, 415, 308, 324, 318, 402, 317, 14, 87, 178, 88, 95};
// 眉毛上沿与下沿，各 5 点，从眉头到眉尾
const int kBrowUpperLeft[] = {107, 66, 105, 63, 70};
const int kBrowLowerLeft[] = {55, 65, 52, 53, 46};
const int kBrowUpperRight[] = {336, 296, 334, 293, 300};
const int kBrowLowerRight[] = {285, 295, 282, 283, 276};
// 上眼睑（睫毛根），从内眼角到外眼角
const int kLidLeft[] = {133, 173, 157, 158, 159, 160, 161, 246, 33};
const int kLidRight[] = {362, 398, 384, 385, 386, 387, 388, 466, 263};
const int kCheekLeft = 50;
const int kCheekRight = 280;
const int kContourLeft = 147;
const int kContourRight = 376;
const int kFaceLeft = 234;
const int kFaceRight = 454;
const int kNoseTip = 1;

struct Vec2 {
    float x;
    float y;
};

Vec2 P(const LmFace& face, int i) {
    return Vec2{face.x[i], face.y[i]};
}

Vec2 Lerp(const Vec2& a, const Vec2& b, float t) {
    return Vec2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

float Length(float x, float y) {
    return std::sqrt(x * x + y * y);
}

class Builder {
public:
    explicit Builder(std::vector<LmMakeupVertex>& out) : m_out(out) {}

    void Tri(const Vec2& a, float aa, const Vec2& b, float ba, const Vec2& c, float ca, const float* color) {
        Push(a, aa, color);
        Push(b, ba, color);
        Push(c, ca, color);
    }

    /// 两条等长折线之间的带状区域，两条线各自的浓度
    void Strip(const std::vector<Vec2>& p, float pa, const std::vector<Vec2>& q, float qa, const float* color,
               bool closed) {
        const size_t n = p.size();
        const size_t segments = closed ? n : n - 1;
        for (size_t i = 0; i < segments; ++i) {
            const size_t j = (i + 1) % n;
            Tri(p[i], pa, p[j], pa, q[i], qa, color);
            Tri(q[i], qa, p[j], pa, q[j], qa, color);
        }
    }

    /// 柔和的椭圆：中心浓度 1，按距离平滑衰减到边缘为 0
    void SoftEllipse(const Vec2& c, const Vec2& u, const Vec2& v, float opacity, const float* color) {
        const int kSegments = 24;
        const float kRings[] = {0.0f, 0.35f, 0.7f, 1.0f};
        const int kRingCount = 4;
        std::vector<Vec2> prev;
        float prevA = 0.0f;
        for (int r = 0; r < kRingCount; ++r) {
            const float t = kRings[r];
            const float a = opacity * (1.0f - t * t) * (1.0f - t * t);
            std::vector<Vec2> ring(kSegments);
            for (int k = 0; k < kSegments; ++k) {
                const float angle = 6.2831853f * k / kSegments;
                ring[k] = Vec2{c.x + (u.x * std::cos(angle) + v.x * std::sin(angle)) * t,
                               c.y + (u.y * std::cos(angle) + v.y * std::sin(angle)) * t};
            }
            if (r > 0) {
                Strip(prev, prevA, ring, a, color, true);
            }
            prev.swap(ring);
            prevA = a;
        }
    }

private:
    void Push(const Vec2& p, float a, const float* color) {
        LmMakeupVertex v;
        v.x = p.x;
        v.y = p.y;
        v.r = color[0];
        v.g = color[1];
        v.b = color[2];
        v.a = std::min(std::max(a, 0.0f), 1.0f);
        m_out.push_back(v);
    }

    std::vector<LmMakeupVertex>& m_out;
};

/// 沿折线法向外扩一圈，用来做边缘的羽化
std::vector<Vec2> Expand(const std::vector<Vec2>& ring, const Vec2& center, float amount) {
    std::vector<Vec2> out(ring.size());
    for (size_t i = 0; i < ring.size(); ++i) {
        const float dx = ring[i].x - center.x;
        const float dy = ring[i].y - center.y;
        const float len = std::max(Length(dx, dy), 1e-3f);
        out[i] = Vec2{ring[i].x + dx / len * amount, ring[i].y + dy / len * amount};
    }
    return out;
}

void AddLips(Builder& b, const LmFace& face, const MakeupLook& look, float intensity, float faceWidth) {
    const float opacity = look.lipOpacity * intensity;
    if (opacity <= 0.001f) {
        return;
    }
    std::vector<Vec2> outer;
    std::vector<Vec2> inner;
    Vec2 center{0.0f, 0.0f};
    for (int i = 0; i < 20; ++i) {
        outer.push_back(P(face, kLipOuter[i]));
        inner.push_back(P(face, kLipInner[i]));
        center.x += outer.back().x / 20.0f;
        center.y += outer.back().y / 20.0f;
    }
    // 唇内侧再往里收一点，口红不染到牙齿上；外沿羽化一小圈
    std::vector<Vec2> innerShrunk = Expand(inner, center, -faceWidth * 0.004f);
    b.Strip(outer, opacity, innerShrunk, opacity * 0.8f, look.lip, true);
    b.Strip(Expand(outer, center, faceWidth * 0.012f), 0.0f, outer, opacity, look.lip, true);
}

void AddBrow(Builder& b, const LmFace& face, const int* upper, const int* lower, const MakeupLook& look,
             float intensity, float faceWidth) {
    // 眉色是叠底色，眉毛本身已经很深，浓度打七折，避免画成一条粗黑眉
    const float opacity = look.browOpacity * intensity * 0.7f;
    if (opacity <= 0.001f) {
        return;
    }
    std::vector<Vec2> top;
    std::vector<Vec2> bottom;
    std::vector<Vec2> mid;
    for (int i = 0; i < 5; ++i) {
        top.push_back(P(face, upper[i]));
        bottom.push_back(P(face, lower[i]));
        mid.push_back(Lerp(top.back(), bottom.back(), 0.5f));
    }
    // 中线最浓，向上下沿渐淡；眉头略淡，更自然
    std::vector<float> weight = {0.55f, 0.85f, 1.0f, 1.0f, 0.8f};
    for (int i = 0; i < 4; ++i) {
        const float a0 = opacity * weight[i];
        const float a1 = opacity * weight[i + 1];
        b.Tri(top[i], a0 * 0.4f, top[i + 1], a1 * 0.4f, mid[i], a0, look.brow);
        b.Tri(mid[i], a0, top[i + 1], a1 * 0.4f, mid[i + 1], a1, look.brow);
        b.Tri(mid[i], a0, mid[i + 1], a1, bottom[i], a0 * 0.4f, look.brow);
        b.Tri(bottom[i], a0 * 0.4f, mid[i + 1], a1, bottom[i + 1], a1 * 0.4f, look.brow);
    }
    (void)faceWidth;
}

void AddShadow(Builder& b, const LmFace& face, const int* lid, const int* browLower, const MakeupLook& look,
               float intensity) {
    const float opacity = look.shadowOpacity * intensity;
    if (opacity <= 0.001f) {
        return;
    }
    // 眼影从睫毛根向眉毛方向晕开约 45% 的距离，睫毛根最浓
    Vec2 browCenter{0.0f, 0.0f};
    for (int i = 0; i < 5; ++i) {
        browCenter.x += face.x[browLower[i]] / 5.0f;
        browCenter.y += face.y[browLower[i]] / 5.0f;
    }
    Vec2 lidCenter{0.0f, 0.0f};
    for (int i = 0; i < 9; ++i) {
        lidCenter.x += face.x[lid[i]] / 9.0f;
        lidCenter.y += face.y[lid[i]] / 9.0f;
    }
    const Vec2 up{browCenter.x - lidCenter.x, browCenter.y - lidCenter.y};
    std::vector<Vec2> base;
    std::vector<Vec2> middle;
    std::vector<Vec2> top;
    for (int i = 0; i < 9; ++i) {
        const Vec2 p = P(face, lid[i]);
        // 两端（内外眼角）晕染范围小一些
        const float t = static_cast<float>(i) / 8.0f;
        const float reach = 0.25f + 0.25f * std::sin(t * 3.14159265f);
        base.push_back(p);
        middle.push_back(Vec2{p.x + up.x * reach * 0.45f, p.y + up.y * reach * 0.45f});
        top.push_back(Vec2{p.x + up.x * reach, p.y + up.y * reach});
    }
    b.Strip(base, opacity, middle, opacity * 0.6f, look.shadow, false);
    b.Strip(middle, opacity * 0.6f, top, 0.0f, look.shadow, false);
}

/// 沿上睫毛根画眼线（外眼角处加粗并带一个小眼尾）和一排睫毛
void AddLinerAndLashes(Builder& b, const LmFace& face, const int* lid, const int* browLower, float linerOpacity,
                       float lashOpacity, float faceWidth) {
    if (linerOpacity <= 0.001f && lashOpacity <= 0.001f) {
        return;
    }
    Vec2 browCenter{0.0f, 0.0f};
    for (int i = 0; i < 5; ++i) {
        browCenter.x += face.x[browLower[i]] / 5.0f;
        browCenter.y += face.y[browLower[i]] / 5.0f;
    }
    Vec2 lidCenter{0.0f, 0.0f};
    for (int i = 0; i < 9; ++i) {
        lidCenter.x += face.x[lid[i]] / 9.0f;
        lidCenter.y += face.y[lid[i]] / 9.0f;
    }
    const float upLen = std::max(Length(browCenter.x - lidCenter.x, browCenter.y - lidCenter.y), 1e-3f);
    const Vec2 up{(browCenter.x - lidCenter.x) / upLen, (browCenter.y - lidCenter.y) / upLen};
    const Vec2 inner = P(face, lid[0]);
    const Vec2 outer = P(face, lid[8]);
    const float eyeLen = std::max(Length(outer.x - inner.x, outer.y - inner.y), 1e-3f);
    const Vec2 out{(outer.x - inner.x) / eyeLen, (outer.y - inner.y) / eyeLen};

    if (linerOpacity > 0.001f) {
        // 眼线：内眼角细，外眼角粗；睫毛根一侧最浓，上沿渐淡，边缘不生硬
        std::vector<Vec2> base;
        std::vector<Vec2> top;
        for (int i = 0; i < 9; ++i) {
            const Vec2 p = P(face, lid[i]);
            const float t = static_cast<float>(i) / 8.0f;
            const float width = faceWidth * (0.004f + 0.010f * t * t);
            base.push_back(p);
            top.push_back(Vec2{p.x + up.x * width, p.y + up.y * width});
        }
        b.Strip(base, linerOpacity, top, linerOpacity * 0.5f, kLinerColor, false);
        // 眼尾：从外眼角向外上方挑出一小段
        const Vec2 tipBase = top[8];
        const Vec2 tip{outer.x + (out.x * 0.9f + up.x * 0.45f) * faceWidth * 0.045f,
                       outer.y + (out.y * 0.9f + up.y * 0.45f) * faceWidth * 0.045f};
        b.Tri(outer, linerOpacity, tipBase, linerOpacity * 0.5f, tip, 0.0f, kLinerColor);
    }

    if (lashOpacity > 0.001f) {
        // 睫毛：从睫毛根向上、向外长出的细条，外侧更长更斜，根部浓、梢部淡
        const int kLashes = 14;
        const float thick = faceWidth * 0.0035f;
        for (int k = 0; k < kLashes; ++k) {
            const float t = (k + 0.5f) / kLashes;
            // 在睫毛根折线上取点
            const float pos = t * 8.0f;
            const int i = std::min(static_cast<int>(pos), 7);
            const float f = pos - i;
            const Vec2 a = P(face, lid[i]);
            const Vec2 c = P(face, lid[i + 1]);
            const Vec2 root{a.x + (c.x - a.x) * f, a.y + (c.y - a.y) * f};
            const float len = faceWidth * (0.022f + 0.028f * t);
            const float lean = 0.15f + 0.75f * t * t;  // 越靠外越向外倒
            Vec2 dir{up.x + out.x * lean, up.y + out.y * lean};
            const float dl = std::max(Length(dir.x, dir.y), 1e-3f);
            dir = Vec2{dir.x / dl, dir.y / dl};
            const Vec2 side{-dir.y * thick, dir.x * thick};
            const Vec2 tip{root.x + dir.x * len, root.y + dir.y * len};
            const Vec2 r0{root.x - side.x, root.y - side.y};
            const Vec2 r1{root.x + side.x, root.y + side.y};
            b.Tri(r0, lashOpacity, r1, lashOpacity, tip, lashOpacity * 0.15f, kLashColor);
        }
    }
}

const MakeupLook* FindLook(const LmParams& params, float& intensity) {
    const int index = params.styleType - 1;
    intensity = std::min(std::max(params.makeupMakeupIntensity, 0.0f), 1.0f);
    if (index < 0 || index >= kLookCount || intensity <= 0.001f) {
        return nullptr;
    }
    return &kLooks[index];
}

} // namespace

float LmMakeupFoundation(const LmParams& params) {
    float intensity = 0.0f;
    const MakeupLook* look = FindLook(params, intensity);
    return look ? look->foundation * intensity * 0.25f : 0.0f;
}

int LmBuildMakeup(const LmFace& face, const LmParams& params, std::vector<LmMakeupVertex>& vertices) {
    float intensity = 0.0f;
    const MakeupLook* found = FindLook(params, intensity);
    if (!found) {
        return 0;
    }
    const MakeupLook& look = *found;
    const size_t before = vertices.size();
    Builder b(vertices);

    const Vec2 left = P(face, kFaceLeft);
    const Vec2 right = P(face, kFaceRight);
    const float faceWidth = Length(right.x - left.x, right.y - left.y);
    if (faceWidth < 8.0f) {
        return 0;
    }
    // 脸的横向与纵向单位向量，椭圆按脸的朝向摆放
    const Vec2 ax{(right.x - left.x) / faceWidth, (right.y - left.y) / faceWidth};
    const Vec2 ay{-ax.y, ax.x};

    // 修容与腮红先画，眼影、眉毛、口红后画，叠在上面
    if (look.contourOpacity * intensity > 0.001f) {
        const float w = faceWidth * 0.13f;
        const float h = faceWidth * 0.07f;
        const int points[2] = {kContourLeft, kContourRight};
        for (int side = 0; side < 2; ++side) {
            // 颧骨下方的阴影，略向下巴方向倾斜
            const Vec2 c = Lerp(P(face, points[side]), side == 0 ? left : right, 0.35f);
            const float tilt = side == 0 ? 0.5f : -0.5f;
            const Vec2 u{(ax.x + ay.x * tilt) * w, (ax.y + ay.y * tilt) * w};
            const Vec2 v{ay.x * h, ay.y * h};
            b.SoftEllipse(c, u, v, look.contourOpacity * intensity, look.contour);
        }
    }
    if (look.blushOpacity * intensity > 0.001f) {
        const float w = faceWidth * look.blushSize;
        const float h = w * 0.7f;
        const Vec2 nose = P(face, kNoseTip);
        const int points[2] = {kCheekLeft, kCheekRight};
        for (int side = 0; side < 2; ++side) {
            // 腮红在苹果肌上，从鼻翼外侧稍往外
            const Vec2 c = Lerp(nose, P(face, points[side]), 1.05f);
            b.SoftEllipse(c, Vec2{ax.x * w, ax.y * w}, Vec2{ay.x * h, ay.y * h}, look.blushOpacity * intensity,
                          look.blush);
        }
    }
    AddShadow(b, face, kLidLeft, kBrowLowerLeft, look, intensity);
    AddShadow(b, face, kLidRight, kBrowLowerRight, look, intensity);
    const int index = params.styleType - 1;
    AddLinerAndLashes(b, face, kLidLeft, kBrowLowerLeft, kLinerOpacity[index] * intensity,
                      kLashOpacity[index] * intensity, faceWidth);
    AddLinerAndLashes(b, face, kLidRight, kBrowLowerRight, kLinerOpacity[index] * intensity,
                      kLashOpacity[index] * intensity, faceWidth);
    AddBrow(b, face, kBrowUpperLeft, kBrowLowerLeft, look, intensity, faceWidth);
    AddBrow(b, face, kBrowUpperRight, kBrowLowerRight, look, intensity, faceWidth);
    AddLips(b, face, look, intensity, faceWidth);
    return static_cast<int>(vertices.size() - before);
}
