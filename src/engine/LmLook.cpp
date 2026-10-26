#include "LmLook.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>
#include "LmJson.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

struct GradeRow {
    const char* name;
    float v[13];  ///< 色温、色调、饱和、对比、亮度、褪色、暗部染色 rgb、亮部染色 rgb、皮肤提亮
};

struct MakeupRow {
    float lip[3];
    float lipOpacity;
    float blush[3];
    float blushOpacity;
    float blushSize;
    float brow[3];
    float browOpacity;
    float shadow[3];
    float shadowOpacity;
    float contour[3];
    float contourOpacity;
    float foundation;
    float linerOpacity;
    float lashOpacity;
};

// 滤镜：只有调色。顺序与 lumina_filter 一致（从 1 开始）
//                       色温    色调    饱和    对比    亮度   褪色    暗部染色              亮部染色              皮肤提亮
const GradeRow kFilterRows[] = {
    {"暖阳", {0.118f, 0.009f, 0.002f, 0.090f, 0.149f, 0.014f, -0.011f, -0.003f, -0.006f, 0.007f, 0.002f, -0.015f, 0.042f}},
    {"胶片", {0.06f, -0.04f, -0.16f, 0.14f, -0.02f, 0.12f, 0.00f, 0.03f, 0.04f, 0.04f, 0.03f, 0.00f, 0.0f}},
    {"氧气", {-0.118f, -0.096f, -0.100f, 0.095f, 0.357f, 0.041f, -0.008f, 0.019f, -0.007f, -0.003f, -0.008f, 0.025f, 0.082f}},
    {"桔梗", {-0.08f, 0.08f, -0.08f, 0.02f, 0.04f, 0.08f, 0.02f, 0.00f, 0.04f, 0.02f, 0.01f, 0.04f, 0.0f}},
    {"白皙", {0.013f, 0.009f, -0.128f, 0.075f, 0.177f, -0.020f, -0.003f, -0.006f, 0.001f, 0.007f, -0.000f, 0.004f, 0.140f}},
    {"温柔", {-0.046f, -0.019f, 0.022f, -0.029f, 0.321f, 0.021f, -0.003f, -0.011f, 0.006f, -0.012f, -0.019f, 0.009f, 0.046f}},
    {"初见", {-0.066f, -0.054f, 0.031f, -0.045f, 0.400f, -0.107f, 0.008f, 0.003f, -0.014f, 0.018f, -0.025f, 0.017f, 0.107f}},
    {"高级灰", {-0.04f, 0.00f, -0.30f, 0.08f, 0.00f, 0.10f, 0.01f, 0.01f, 0.02f, 0.01f, 0.01f, 0.01f, 0.0f}},
    {"海岛", {-0.054f, -0.038f, 0.192f, 0.032f, 0.310f, -0.032f, -0.000f, -0.001f, -0.007f, -0.013f, -0.009f, 0.011f, 0.048f}},
    {"浅夏", {-0.028f, -0.045f, 0.276f, 0.039f, 0.228f, -0.029f, 0.006f, -0.001f, 0.005f, -0.026f, -0.008f, 0.010f, 0.137f}},
    {"清透", {0.013f, 0.009f, -0.128f, 0.075f, 0.177f, -0.020f, -0.003f, -0.006f, 0.001f, 0.007f, -0.000f, 0.004f, 0.140f}},
    {"冷白", {-0.066f, 0.016f, 0.010f, 0.032f, 0.019f, -0.025f, -0.010f, 0.003f, 0.002f, -0.016f, -0.017f, 0.009f, 0.126f}},
    {"自然", {-0.063f, -0.016f, 0.038f, 0.095f, 0.157f, -0.014f, -0.003f, 0.005f, -0.009f, 0.002f, -0.014f, 0.010f, 0.050f}},
    {"焕肤", {-0.048f, -0.001f, 0.029f, 0.055f, 0.179f, -0.004f, 0.000f, -0.003f, -0.004f, 0.003f, -0.009f, 0.005f, 0.083f}},
    {"粉瓷", {-0.024f, 0.050f, -0.036f, 0.018f, 0.070f, 0.005f, -0.000f, -0.005f, -0.000f, 0.008f, -0.007f, 0.007f, 0.057f}},
};

// 风格妆的调色部分，顺序与 lumina_style 一致（从 1 开始）
const GradeRow kStyleGradeRows[] = {
    {"白皙", {-0.063f, 0.110f, -0.363f, -0.004f, 0.320f, 0.140f, -0.018f, -0.008f, 0.006f, 0.042f, -0.002f, 0.012f, 0.000f}},
    {"韩系", {-0.008f, -0.008f, -0.085f, 0.027f, 0.192f, 0.005f, 0.004f, -0.006f, 0.010f, 0.007f, -0.006f, 0.009f, 0.000f}},
    {"元气", {-0.047f, -0.016f, -0.232f, 0.103f, 0.316f, 0.012f, -0.005f, -0.017f, 0.011f, 0.017f, -0.008f, 0.005f, 0.000f}},
    {"温暖", {0.16f, 0.03f, 0.04f, -0.02f, 0.05f, 0.04f, 0.03f, 0.01f, 0.00f, 0.04f, 0.03f, 0.00f, 0.0f}},
    {"欧美", {-0.008f, -0.008f, -0.060f, -0.005f, 0.121f, 0.012f, 0.002f, -0.002f, 0.003f, 0.009f, 0.002f, 0.002f, 0.000f}},
    {"优雅", {0.00f, 0.04f, -0.10f, 0.04f, 0.04f, 0.06f, 0.01f, 0.00f, 0.02f, 0.02f, 0.02f, 0.02f, 0.0f}},
    {"甜美", {0.04f, 0.12f, 0.06f, -0.06f, 0.08f, 0.05f, 0.03f, 0.00f, 0.02f, 0.04f, 0.01f, 0.02f, 0.0f}},
    {"深邃", {-0.013f, 0.043f, -0.123f, -0.055f, 0.009f, 0.036f, -0.002f, -0.001f, 0.012f, 0.003f, 0.000f, 0.007f, 0.000f}},
    {"伪素颜", {-0.010f, 0.002f, -0.031f, 0.064f, 0.175f, -0.003f, -0.001f, 0.001f, -0.001f, 0.002f, 0.001f, 0.001f, 0.000f}},
    {"学姐", {-0.016f, -0.005f, 0.050f, 0.062f, 0.085f, -0.000f, 0.002f, -0.006f, 0.002f, 0.004f, 0.002f, 0.004f, 0.000f}},
    {"学妹", {-0.018f, 0.010f, -0.020f, 0.008f, 0.078f, 0.021f, -0.001f, -0.001f, 0.002f, 0.002f, 0.001f, 0.003f, 0.000f}},
};

// 风格妆的妆容部分，顺序同上。这些妆容按风格名称自行设计，不来自任何第三方素材
//   唇色, 唇浓度, 腮红色, 腮红浓度, 腮红大小, 眉色, 眉浓度, 眼影色, 眼影浓度, 修容色, 修容浓度, 粉底, 眼线浓度, 睫毛浓度
const MakeupRow kStyleMakeupRows[] = {
    // 白皙：浅豆沙唇，淡粉腮红，妆感轻
    {{0.95f, 0.62f, 0.66f}, 0.55f, {1.00f, 0.84f, 0.86f}, 0.40f, 0.17f, {0.72f, 0.62f, 0.56f}, 0.30f, {0.96f, 0.84f, 0.82f}, 0.30f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.25f, 0.50f, 0.50f},
    // 韩系：珊瑚唇，蜜桃腮红，浅棕眉
    {{1.00f, 0.56f, 0.52f}, 0.60f, {1.00f, 0.80f, 0.78f}, 0.50f, 0.17f, {0.72f, 0.62f, 0.55f}, 0.35f, {0.98f, 0.80f, 0.74f}, 0.35f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.25f, 0.45f, 0.55f},
    // 元气：亮红唇，大面积腮红
    {{1.00f, 0.46f, 0.50f}, 0.65f, {1.00f, 0.76f, 0.76f}, 0.55f, 0.20f, {0.72f, 0.62f, 0.56f}, 0.30f, {1.00f, 0.84f, 0.78f}, 0.30f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.30f, 0.40f, 0.60f},
    // 温暖：橘棕唇，暖杏色腮红与眼影
    {{0.95f, 0.56f, 0.44f}, 0.60f, {1.00f, 0.80f, 0.70f}, 0.45f, 0.17f, {0.66f, 0.54f, 0.44f}, 0.40f, {0.94f, 0.78f, 0.66f}, 0.40f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.15f, 0.50f, 0.50f},
    // 欧美：深红唇，大地色眼影，修容
    {{0.80f, 0.36f, 0.36f}, 0.70f, {0.96f, 0.76f, 0.70f}, 0.35f, 0.16f, {0.56f, 0.46f, 0.40f}, 0.50f, {0.76f, 0.62f, 0.56f}, 0.55f, {0.86f, 0.76f, 0.70f}, 0.45f, 0.08f, 0.80f, 0.80f},
    // 优雅：玫瑰唇，轻修容
    {{0.86f, 0.42f, 0.48f}, 0.60f, {0.98f, 0.80f, 0.82f}, 0.35f, 0.16f, {0.62f, 0.54f, 0.50f}, 0.40f, {0.88f, 0.76f, 0.76f}, 0.40f, {0.90f, 0.82f, 0.78f}, 0.25f, 0.12f, 0.65f, 0.60f},
    // 甜美：粉唇，粉色腮红
    {{1.00f, 0.52f, 0.62f}, 0.65f, {1.00f, 0.76f, 0.82f}, 0.55f, 0.18f, {0.72f, 0.62f, 0.56f}, 0.30f, {1.00f, 0.80f, 0.84f}, 0.35f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.20f, 0.45f, 0.70f},
    // 深邃：暗红唇，深色眼影与修容，眉色加深
    {{0.74f, 0.36f, 0.36f}, 0.65f, {0.92f, 0.76f, 0.72f}, 0.30f, 0.15f, {0.50f, 0.42f, 0.38f}, 0.55f, {0.68f, 0.56f, 0.52f}, 0.60f, {0.82f, 0.72f, 0.66f}, 0.50f, 0.08f, 0.85f, 0.80f},
    // 伪素颜：接近唇色的淡唇，极淡腮红，不画眼影
    {{0.98f, 0.70f, 0.68f}, 0.40f, {1.00f, 0.86f, 0.86f}, 0.30f, 0.16f, {0.76f, 0.66f, 0.60f}, 0.25f, {1.0f, 1.0f, 1.0f}, 0.0f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.18f, 0.15f, 0.30f},
    // 学姐：干枯玫瑰唇，大地色眼影，轻修容
    {{0.88f, 0.46f, 0.46f}, 0.60f, {0.98f, 0.80f, 0.80f}, 0.35f, 0.16f, {0.60f, 0.52f, 0.48f}, 0.45f, {0.86f, 0.72f, 0.68f}, 0.45f, {0.88f, 0.80f, 0.76f}, 0.30f, 0.15f, 0.60f, 0.60f},
    // 学妹：水粉唇，粉嫩腮红
    {{1.00f, 0.58f, 0.62f}, 0.55f, {1.00f, 0.80f, 0.82f}, 0.50f, 0.18f, {0.74f, 0.64f, 0.58f}, 0.30f, {1.00f, 0.86f, 0.82f}, 0.30f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.20f, 0.40f, 0.65f},
};

const int kFilterCount = sizeof(kFilterRows) / sizeof(kFilterRows[0]);
const int kStyleCount = sizeof(kStyleGradeRows) / sizeof(kStyleGradeRows[0]);
static_assert(sizeof(kStyleMakeupRows) / sizeof(kStyleMakeupRows[0]) == sizeof(kStyleGradeRows) / sizeof(kStyleGradeRows[0]),
              "every style needs a color grade and a makeup row");

LmColorGrade ToGrade(const GradeRow& row) {
    LmColorGrade g;
    g.temperature = row.v[0];
    g.tint = row.v[1];
    g.saturation = row.v[2];
    g.contrast = row.v[3];
    g.brightness = row.v[4];
    g.fade = row.v[5];
    for (int i = 0; i < 3; ++i) {
        g.shadowTint[i] = row.v[6 + i];
        g.highlightTint[i] = row.v[9 + i];
    }
    g.skin = row.v[12];
    return g;
}

void Copy3(float* dst, const float* src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

LmMakeupLook ToMakeup(const MakeupRow& row) {
    LmMakeupLook m;
    Copy3(m.lip, row.lip);
    m.lipOpacity = row.lipOpacity;
    Copy3(m.blush, row.blush);
    m.blushOpacity = row.blushOpacity;
    m.blushSize = row.blushSize;
    Copy3(m.brow, row.brow);
    m.browOpacity = row.browOpacity;
    Copy3(m.shadow, row.shadow);
    m.shadowOpacity = row.shadowOpacity;
    Copy3(m.contour, row.contour);
    m.contourOpacity = row.contourOpacity;
    m.foundation = row.foundation;
    m.linerOpacity = row.linerOpacity;
    m.lashOpacity = row.lashOpacity;
    return m;
}

std::vector<LmLook> MakeFilters() {
    std::vector<LmLook> out(kFilterCount);
    for (int i = 0; i < kFilterCount; ++i) {
        out[i].name = kFilterRows[i].name;
        out[i].hasGrade = true;
        out[i].grade = ToGrade(kFilterRows[i]);
    }
    return out;
}

std::vector<LmLook> MakeStyles() {
    std::vector<LmLook> out(kStyleCount);
    for (int i = 0; i < kStyleCount; ++i) {
        out[i].name = kStyleGradeRows[i].name;
        out[i].hasGrade = true;
        out[i].grade = ToGrade(kStyleGradeRows[i]);
        out[i].hasMakeup = true;
        out[i].makeup = ToMakeup(kStyleMakeupRows[i]);
    }
    return out;
}

// ---------------------------------------------------------------- 读取

float Clampf(double v, float lo, float hi) {
    const float f = static_cast<float>(v);
    return f < lo ? lo : (f > hi ? hi : f);
}

/// 读一个数字成员，不存在时保持原值
bool ReadNumber(const LmJson& obj, const char* key, float lo, float hi, float& out, std::string& error) {
    const LmJson* v = obj.Find(key);
    if (!v) {
        return true;
    }
    if (!v->IsNumber()) {
        error = std::string("\"") + key + "\" should be a number";
        return false;
    }
    out = Clampf(v->Number(), lo, hi);
    return true;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// 颜色可以写成 [r, g, b]（0 到 1）或 "#RRGGBB"
bool ReadColor(const LmJson& obj, const char* key, float lo, float hi, float* out, std::string& error) {
    const LmJson* v = obj.Find(key);
    if (!v) {
        return true;
    }
    if (v->IsArray() && v->Array().size() == 3) {
        for (int i = 0; i < 3; ++i) {
            if (!v->Array()[i].IsNumber()) {
                error = std::string("\"") + key + "\" should hold three numbers";
                return false;
            }
            out[i] = Clampf(v->Array()[i].Number(), lo, hi);
        }
        return true;
    }
    if (v->IsString() && v->String().size() == 7 && v->String()[0] == '#') {
        const std::string& s = v->String();
        for (int i = 0; i < 3; ++i) {
            const int hi4 = HexDigit(s[1 + i * 2]);
            const int lo4 = HexDigit(s[2 + i * 2]);
            if (hi4 < 0 || lo4 < 0) {
                break;
            }
            out[i] = Clampf((hi4 * 16 + lo4) / 255.0, lo, hi);
            if (i == 2) {
                return true;
            }
        }
    }
    error = std::string("\"") + key + "\" should be [r, g, b] or \"#RRGGBB\"";
    return false;
}

/// 读一个妆容部位：{"color": ..., "opacity": ...}，size 只有腮红用
bool ReadPart(const LmJson& makeup, const char* key, float* color, float& opacity, float* size,
              std::string& error) {
    const LmJson* part = makeup.Find(key);
    if (!part) {
        return true;
    }
    if (!part->IsObject()) {
        error = std::string("\"") + key + "\" should be an object";
        return false;
    }
    const std::string prefix = std::string(key) + ": ";
    std::string e;
    const bool ok = ReadColor(*part, "color", 0.0f, 1.0f, color, e) && ReadNumber(*part, "opacity", 0.0f, 1.0f, opacity, e) &&
                    (!size || ReadNumber(*part, "size", 0.05f, 0.4f, *size, e));
    if (!ok) {
        error = prefix + e;
    }
    return ok;
}

// ---------------------------------------------------------------- 写出

std::string Num(float v) {
    // 三位小数足够表达内置参数；写出后再读回与原值完全相同
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s = buf;
    if (s == "-0.000") {
        s = "0.000";
    }
    return s;
}

std::string Color(const float* c) {
    return "[" + Num(c[0]) + ", " + Num(c[1]) + ", " + Num(c[2]) + "]";
}

std::string Escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

void Part(std::ostringstream& os, const char* key, const float* color, float opacity, const float* size,
          bool last) {
    os << "    \"" << key << "\": {\"color\": " << Color(color) << ", \"opacity\": " << Num(opacity);
    if (size) {
        os << ", \"size\": " << Num(*size);
    }
    os << "}" << (last ? "\n" : ",\n");
}

bool ReadFile(const char* path, std::string& text) {
#ifdef _WIN32
    // 路径是 UTF-8，转成宽字符再打开，中文路径才不会乱
    const int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (n <= 0) {
        return false;
    }
    std::wstring wide(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, &wide[0], n);
    FILE* f = _wfopen(wide.c_str(), L"rb");
#else
    FILE* f = fopen(path, "rb");
#endif
    if (!f) {
        return false;
    }
    char buf[4096];
    size_t got = 0;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, got);
        if (text.size() > (1u << 20)) {  // 素材文件只有几 KB，超过 1MB 肯定不对
            fclose(f);
            return false;
        }
    }
    fclose(f);
    return true;
}

} // namespace

const LmLook* LmBuiltinFilter(int filter) {
    static const std::vector<LmLook> filters = MakeFilters();
    return (filter >= 1 && filter <= kFilterCount) ? &filters[filter - 1] : nullptr;
}

const LmLook* LmBuiltinStyle(int style) {
    static const std::vector<LmLook> styles = MakeStyles();
    return (style >= 1 && style <= kStyleCount) ? &styles[style - 1] : nullptr;
}

bool LmParseLook(const std::string& text, LmLook& look, std::string& error) {
    look = LmLook();
    LmJson root;
    if (!LmJson::Parse(text, root, error)) {
        return false;
    }
    if (!root.IsObject()) {
        error = "the file should hold one JSON object";
        return false;
    }
    const LmJson* format = root.Find("format");
    if (!format || !format->IsString() || format->String() != "lumina-look") {
        error = "\"format\" should be \"lumina-look\"";
        return false;
    }
    const LmJson* version = root.Find("version");
    if (version && (!version->IsNumber() || version->Number() > 1)) {
        error = "unsupported \"version\", this build reads version 1";
        return false;
    }
    if (const LmJson* name = root.Find("name")) {
        if (name->IsString()) {
            look.name = name->String();
        }
    }

    if (const LmJson* color = root.Find("color")) {
        if (!color->IsObject()) {
            error = "\"color\" should be an object";
            return false;
        }
        LmColorGrade& g = look.grade;
        std::string e;
        const bool ok = ReadNumber(*color, "temperature", -1.0f, 1.0f, g.temperature, e) &&
                        ReadNumber(*color, "tint", -1.0f, 1.0f, g.tint, e) &&
                        ReadNumber(*color, "saturation", -1.0f, 1.0f, g.saturation, e) &&
                        ReadNumber(*color, "contrast", -1.0f, 1.0f, g.contrast, e) &&
                        ReadNumber(*color, "brightness", -1.0f, 1.0f, g.brightness, e) &&
                        ReadNumber(*color, "fade", -1.0f, 1.0f, g.fade, e) &&
                        ReadColor(*color, "shadow_tint", -0.5f, 0.5f, g.shadowTint, e) &&
                        ReadColor(*color, "highlight_tint", -0.5f, 0.5f, g.highlightTint, e) &&
                        ReadNumber(*color, "skin", 0.0f, 1.0f, g.skin, e);
        if (!ok) {
            error = "color: " + e;
            return false;
        }
        look.hasGrade = true;
    }

    if (const LmJson* makeup = root.Find("makeup")) {
        if (!makeup->IsObject()) {
            error = "\"makeup\" should be an object";
            return false;
        }
        LmMakeupLook& m = look.makeup;
        std::string e;
        const bool ok = ReadPart(*makeup, "lips", m.lip, m.lipOpacity, nullptr, e) &&
                        ReadPart(*makeup, "blush", m.blush, m.blushOpacity, &m.blushSize, e) &&
                        ReadPart(*makeup, "brows", m.brow, m.browOpacity, nullptr, e) &&
                        ReadPart(*makeup, "eye_shadow", m.shadow, m.shadowOpacity, nullptr, e) &&
                        ReadPart(*makeup, "contour", m.contour, m.contourOpacity, nullptr, e) &&
                        ReadPart(*makeup, "eyeliner", m.liner, m.linerOpacity, nullptr, e) &&
                        ReadPart(*makeup, "lashes", m.lash, m.lashOpacity, nullptr, e) &&
                        ReadNumber(*makeup, "foundation", 0.0f, 1.0f, m.foundation, e);
        if (!ok) {
            error = "makeup: " + e;
            return false;
        }
        look.hasMakeup = true;
    }

    if (!look.hasGrade && !look.hasMakeup) {
        error = "the look has neither \"color\" nor \"makeup\"";
        return false;
    }
    return true;
}

bool LmLoadLookFile(const char* path, LmLook& look, std::string& error) {
    std::string text;
    if (!path || !path[0] || !ReadFile(path, text)) {
        error = "cannot read the file";
        return false;
    }
    return LmParseLook(text, look, error);
}

std::string LmLookToJson(const LmLook& look) {
    std::ostringstream os;
    os << "{\n  \"format\": \"lumina-look\",\n  \"version\": 1,\n  \"name\": \"" << Escape(look.name) << "\"";
    if (look.hasGrade) {
        const LmColorGrade& g = look.grade;
        os << ",\n  \"color\": {\n"
           << "    \"temperature\": " << Num(g.temperature) << ",\n"
           << "    \"tint\": " << Num(g.tint) << ",\n"
           << "    \"saturation\": " << Num(g.saturation) << ",\n"
           << "    \"contrast\": " << Num(g.contrast) << ",\n"
           << "    \"brightness\": " << Num(g.brightness) << ",\n"
           << "    \"fade\": " << Num(g.fade) << ",\n"
           << "    \"shadow_tint\": " << Color(g.shadowTint) << ",\n"
           << "    \"highlight_tint\": " << Color(g.highlightTint) << ",\n"
           << "    \"skin\": " << Num(g.skin) << "\n  }";
    }
    if (look.hasMakeup) {
        const LmMakeupLook& m = look.makeup;
        os << ",\n  \"makeup\": {\n";
        Part(os, "lips", m.lip, m.lipOpacity, nullptr, false);
        Part(os, "blush", m.blush, m.blushOpacity, &m.blushSize, false);
        Part(os, "brows", m.brow, m.browOpacity, nullptr, false);
        Part(os, "eye_shadow", m.shadow, m.shadowOpacity, nullptr, false);
        Part(os, "contour", m.contour, m.contourOpacity, nullptr, false);
        Part(os, "eyeliner", m.liner, m.linerOpacity, nullptr, false);
        Part(os, "lashes", m.lash, m.lashOpacity, nullptr, false);
        os << "    \"foundation\": " << Num(m.foundation) << "\n  }";
    }
    os << "\n}\n";
    return os.str();
}
