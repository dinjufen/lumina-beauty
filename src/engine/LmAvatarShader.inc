// 卡通头像与配饰（头顶、眼部、耳饰、颈部、嘴部）的片段着色器，由 LmPipeline.cpp 包含。
// 所有图形用二维距离场（SDF）在局部坐标系里画出：原点在两眼中点，x 沿两眼连线，y 向上，1 个单位为半个脸宽。
// 图形全部为自行设计，不使用任何外部素材。
const char* kAvatarShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform vec2 uSize;
uniform int uFaceCount;
uniform vec4 uPose[2];      // cx, cy（像素）, ux, uy
uniform vec4 uPose2[2];     // scale（像素/单位）, yaw, pitch, top
uniform vec4 uExpr[2];      // eyeLeft, eyeRight, mouthOpen, smile
uniform vec4 uExpr2[2];     // brow, gazeX, gazeY, eyeHalf
uniform vec4 uFace3[2];     // mouthY, mouthHalf, chinY, earY
uniform vec4 uFace4[2];     // earLeftX, earRightX
uniform int uAvatar;
uniform int uAccHead;
uniform int uAccEyes;
uniform int uAccEars;
uniform int uAccNeck;
uniform int uAccMouth;
uniform float uTime;

float AA;
const vec3 kInk = vec3(0.18, 0.12, 0.14);

float sdCircle(vec2 p, float r) { return length(p) - r; }
float sdEllipse(vec2 p, vec2 r) {
    float k0 = length(p / r);
    float k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / max(k1, 1e-5);
}
float sdBox(vec2 p, vec2 b, float rad) {
    vec2 q = abs(p) - b + rad;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rad;
}
float sdSeg(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}
float sdTri(vec2 p, vec2 p0, vec2 p1, vec2 p2) {
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}
vec2 rot(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(c * p.x + s * p.y, -s * p.x + c * p.y);
}

// 预乘颜色的"上层盖下层"
void put(inout vec4 acc, vec3 col, float d) {
    float a = clamp(0.5 - d / AA, 0.0, 1.0);
    acc = vec4(col, 1.0) * a + acc * (1.0 - a);
}
void putA(inout vec4 acc, vec3 col, float d, float alpha) {
    float a = clamp(0.5 - d / AA, 0.0, 1.0) * alpha;
    acc = vec4(col, 1.0) * a + acc * (1.0 - a);
}
// 带描边的色块
void putO(inout vec4 acc, vec3 col, float d, float w) {
    put(acc, kInk, d - w);
    put(acc, col, d);
}

// 眼睛：open 为睁开程度，闭眼时画成一条弯线；style 1 为白眼球加黑瞳孔
void eye(inout vec4 acc, vec2 p, vec2 c, float r, float open, vec2 gaze, int style) {
    vec2 q = p - c;
    if (open < 0.22) {
        // 闭眼：向下弯的弧线
        float d = abs(sdEllipse(q - vec2(0.0, r * 0.25), vec2(r * 1.05, r * 0.55))) - 0.028;
        d = max(d, q.y - r * 0.2);
        put(acc, kInk, d);
        return;
    }
    float h = r * mix(0.35, 1.15, clamp((open - 0.22) / 0.78, 0.0, 1.0));
    if (style == 1) {
        putO(acc, vec3(1.0), sdEllipse(q, vec2(r * 1.15, h * 1.1)), 0.03);
        vec2 pc = gaze * r * 0.35;
        put(acc, kInk, max(sdCircle(q - pc, r * 0.55), sdEllipse(q, vec2(r * 1.15, h * 1.1))));
        put(acc, vec3(1.0), sdCircle(q - pc - vec2(r * 0.2, r * 0.2), r * 0.16));
    } else {
        vec2 pc = gaze * r * 0.15;
        put(acc, kInk, sdEllipse(q - pc, vec2(r * 0.85, h)));
        put(acc, vec3(1.0), max(sdCircle(q - pc - vec2(r * 0.28, r * 0.35), r * 0.25), sdEllipse(q - pc, vec2(r * 0.85, h))));
    }
}

// 嘴：闭嘴时是一条随微笑上扬的弧线，张嘴时是带舌头的口腔
void mouth(inout vec4 acc, vec2 p, vec2 c, float w, float open, float smile) {
    vec2 q = p - c;
    if (open < 0.12) {
        float bend = 0.04 + 0.14 * smile;
        float x = clamp(q.x, -w, w);
        float y = bend * (x * x / (w * w)) - bend * 0.5;
        float d = length(vec2(q.x - x, q.y - y)) - 0.03;
        put(acc, kInk, d);
        return;
    }
    vec2 r = vec2(w * (0.75 + 0.25 * smile), 0.05 + 0.32 * open);
    float m = sdEllipse(q, r);
    putO(acc, vec3(0.55, 0.12, 0.18), m, 0.03);
    put(acc, vec3(1.0, 0.45, 0.5), max(sdEllipse(q - vec2(0.0, -r.y * 0.6), vec2(r.x * 0.6, r.y * 0.5)), m));
}

void brows(inout vec4 acc, vec2 p, float ex, float y, float raise, float w) {
    for (int i = 0; i < 2; ++i) {
        float sx = i == 0 ? -1.0 : 1.0;
        vec2 a = vec2(sx * ex - 0.11, y + raise * 0.1 + (sx < 0.0 ? 0.02 * raise : 0.0));
        vec2 b = vec2(sx * ex + 0.11, y + raise * 0.1 + (sx > 0.0 ? 0.02 * raise : 0.0));
        put(acc, kInk, sdSeg(p, a, b) - w);
    }
}

void cheeks(inout vec4 acc, vec2 p, vec2 c, float r, vec3 col, float alpha) {
    for (int i = 0; i < 2; ++i) {
        float sx = i == 0 ? -1.0 : 1.0;
        float d = length(p - vec2(sx * c.x, c.y)) / r;
        float a = (1.0 - smoothstep(0.3, 1.0, d)) * alpha;
        acc = vec4(col, 1.0) * a + acc * (1.0 - a);
    }
}

vec4 drawAvatar(vec2 p, int kind, vec4 pose2, vec4 expr, vec4 expr2) {
    vec4 acc = vec4(0.0);
    float yaw = pose2.y;
    float pitch = pose2.z;
    // 五官随转头、抬头整体平移，头部轮廓移动得少一些，产生立体感
    vec2 shift = vec2(yaw * 0.28, pitch * 0.12);
    vec2 hp = p - vec2(yaw * 0.08, 0.0);
    vec2 fp = p - shift;
    float eyeL = expr.x, eyeR = expr.y, open = expr.z, smile = expr.w;
    float raise = expr2.x;
    vec2 gaze = vec2(expr2.y, expr2.z);
    // 微笑时眼睛略眯
    eyeL = min(eyeL, 1.0 - smile * 0.35);
    eyeR = min(eyeR, 1.0 - smile * 0.35);
    vec2 head = vec2(0.0, -0.3);

    if (kind == 1) {          // 小猪
        vec3 skin = vec3(1.0, 0.76, 0.82);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 q = rot(hp - vec2(sx * 0.8, 0.82), sx * 0.5);
            putO(acc, vec3(0.98, 0.62, 0.72), sdTri(q, vec2(-0.3, -0.15), vec2(0.3, -0.15), vec2(0.0, 0.42)), 0.04);
            put(acc, vec3(1.0, 0.82, 0.88), sdTri(q, vec2(-0.16, -0.08), vec2(0.16, -0.08), vec2(0.0, 0.26)));
        }
        putO(acc, skin, sdEllipse(hp - head, vec2(1.25, 1.12)), 0.045);
        cheeks(acc, fp, vec2(0.78, -0.5), 0.26, vec3(1.0, 0.5, 0.6), 0.55);
        brows(acc, fp, 0.42, 0.3, raise, 0.025);
        eye(acc, fp, vec2(-0.42, 0.05), 0.12, eyeL, gaze, 0);
        eye(acc, fp, vec2(0.42, 0.05), 0.12, eyeR, gaze, 0);
        putO(acc, vec3(1.0, 0.62, 0.72), sdEllipse(fp - vec2(0.0, -0.42), vec2(0.42, 0.29)), 0.04);
        put(acc, vec3(0.55, 0.22, 0.3), sdEllipse(fp - vec2(-0.13, -0.42), vec2(0.06, 0.11)));
        put(acc, vec3(0.55, 0.22, 0.3), sdEllipse(fp - vec2(0.13, -0.42), vec2(0.06, 0.11)));
        mouth(acc, fp, vec2(0.0, -0.9), 0.2, open, smile);
    } else if (kind == 2) {   // 小兔
        vec3 fur = vec3(0.98, 0.97, 0.96);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 q = rot(hp - vec2(sx * 0.45, 1.25), sx * (0.18 + 0.08 * raise));
            putO(acc, fur, sdEllipse(q, vec2(0.27, 0.85)), 0.04);
            put(acc, vec3(1.0, 0.76, 0.84), sdEllipse(q - vec2(0.0, -0.05), vec2(0.13, 0.62)));
        }
        putO(acc, fur, sdEllipse(hp - head, vec2(1.2, 1.1)), 0.045);
        cheeks(acc, fp, vec2(0.72, -0.48), 0.24, vec3(1.0, 0.6, 0.7), 0.5);
        brows(acc, fp, 0.42, 0.3, raise, 0.022);
        eye(acc, fp, vec2(-0.42, 0.05), 0.13, eyeL, gaze, 0);
        eye(acc, fp, vec2(0.42, 0.05), 0.13, eyeR, gaze, 0);
        put(acc, vec3(1.0, 0.55, 0.65), sdTri(fp, vec2(-0.09, -0.32), vec2(0.09, -0.32), vec2(0.0, -0.44)) - 0.02);
        if (open < 0.12) {
            // 三瓣嘴
            vec2 q = fp - vec2(0.0, -0.55);
            float d = min(abs(length(q - vec2(-0.09, 0.0)) - 0.09), abs(length(q - vec2(0.09, 0.0)) - 0.09));
            d = max(d - 0.025, q.y);
            put(acc, kInk, min(d, sdSeg(fp, vec2(0.0, -0.44), vec2(0.0, -0.55)) - 0.025));
        } else {
            mouth(acc, fp, vec2(0.0, -0.66), 0.16, open, smile);
            put(acc, vec3(1.0), sdBox(fp - vec2(-0.045, -0.66 + 0.05 + 0.32 * open - 0.08), vec2(0.04, 0.06), 0.015));
            put(acc, vec3(1.0), sdBox(fp - vec2(0.045, -0.66 + 0.05 + 0.32 * open - 0.08), vec2(0.04, 0.06), 0.015));
        }
    } else if (kind == 3) {   // 小猫
        vec3 fur = vec3(1.0, 0.72, 0.38);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 q = rot(hp - vec2(sx * 0.72, 0.78), sx * 0.35);
            putO(acc, fur, sdTri(q, vec2(-0.34, -0.2), vec2(0.34, -0.2), vec2(0.0, 0.52)), 0.04);
            put(acc, vec3(1.0, 0.78, 0.82), sdTri(q, vec2(-0.18, -0.1), vec2(0.18, -0.1), vec2(0.0, 0.32)));
        }
        putO(acc, fur, sdEllipse(hp - head, vec2(1.25, 1.08)), 0.045);
        for (int i = -1; i <= 1; ++i) {
            float x = float(i) * 0.18;
            put(acc, vec3(0.88, 0.5, 0.22), sdSeg(hp, vec2(x, 0.72), vec2(x * 0.8, 0.45 + 0.08 * abs(float(i)))) - 0.04);
        }
        put(acc, vec3(1.0, 0.96, 0.9), sdEllipse(fp - vec2(0.0, -0.58), vec2(0.5, 0.33)));
        cheeks(acc, fp, vec2(0.8, -0.42), 0.22, vec3(1.0, 0.5, 0.5), 0.45);
        brows(acc, fp, 0.42, 0.3, raise, 0.022);
        eye(acc, fp, vec2(-0.42, 0.05), 0.13, eyeL, gaze, 0);
        eye(acc, fp, vec2(0.42, 0.05), 0.13, eyeR, gaze, 0);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            for (int k = -1; k <= 1; ++k) {
                vec2 a = vec2(sx * 0.42, -0.5 + float(k) * 0.07);
                vec2 b = vec2(sx * 0.95, -0.45 + float(k) * 0.14);
                put(acc, kInk, sdSeg(fp, a, b) - 0.014);
            }
        }
        put(acc, vec3(1.0, 0.5, 0.6), sdTri(fp, vec2(-0.08, -0.36), vec2(0.08, -0.36), vec2(0.0, -0.46)) - 0.02);
        mouth(acc, fp, vec2(0.0, -0.66), 0.14, open, smile);
    } else if (kind == 4) {   // 小熊
        vec3 fur = vec3(0.64, 0.45, 0.3);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            putO(acc, fur, sdCircle(hp - vec2(sx * 0.86, 0.72), 0.34), 0.045);
            put(acc, vec3(0.86, 0.66, 0.5), sdCircle(hp - vec2(sx * 0.86, 0.72), 0.19));
        }
        putO(acc, fur, sdEllipse(hp - head, vec2(1.25, 1.12)), 0.045);
        put(acc, vec3(0.92, 0.78, 0.62), sdEllipse(fp - vec2(0.0, -0.58), vec2(0.48, 0.36)));
        cheeks(acc, fp, vec2(0.8, -0.42), 0.22, vec3(1.0, 0.5, 0.45), 0.4);
        brows(acc, fp, 0.42, 0.3, raise, 0.024);
        eye(acc, fp, vec2(-0.42, 0.05), 0.12, eyeL, gaze, 0);
        eye(acc, fp, vec2(0.42, 0.05), 0.12, eyeR, gaze, 0);
        put(acc, vec3(0.25, 0.15, 0.12), sdEllipse(fp - vec2(0.0, -0.4), vec2(0.15, 0.1)));
        put(acc, vec3(1.0), sdCircle(fp - vec2(-0.05, -0.37), 0.03));
        mouth(acc, fp, vec2(0.0, -0.72), 0.15, open, smile);
    } else if (kind == 5) {   // 熊猫
        vec3 black = vec3(0.13, 0.12, 0.14);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            put(acc, black, sdCircle(hp - vec2(sx * 0.86, 0.75), 0.34) - 0.045);
        }
        putO(acc, vec3(0.98, 0.98, 0.97), sdEllipse(hp - head, vec2(1.25, 1.1)), 0.045);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            put(acc, black, sdEllipse(rot(fp - vec2(sx * 0.42, -0.02), sx * 0.55), vec2(0.21, 0.29)));
        }
        cheeks(acc, fp, vec2(0.82, -0.45), 0.2, vec3(1.0, 0.55, 0.6), 0.45);
        brows(acc, fp, 0.42, 0.36, raise, 0.022);
        eye(acc, fp, vec2(-0.42, 0.02), 0.1, eyeL, gaze, 1);
        eye(acc, fp, vec2(0.42, 0.02), 0.1, eyeR, gaze, 1);
        put(acc, black, sdEllipse(fp - vec2(0.0, -0.4), vec2(0.14, 0.09)));
        mouth(acc, fp, vec2(0.0, -0.68), 0.14, open, smile);
    } else if (kind == 6) {   // 青蛙
        vec3 green = vec3(0.47, 0.8, 0.4);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            putO(acc, green, sdCircle(hp - vec2(sx * 0.5, 0.42), 0.38), 0.045);
        }
        putO(acc, green, sdEllipse(hp - vec2(0.0, -0.4), vec2(1.35, 0.95)), 0.045);
        // 眼睛凸起处补一块，遮住头部描边
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            put(acc, green, sdCircle(hp - vec2(sx * 0.5, 0.42), 0.38));
        }
        cheeks(acc, fp, vec2(0.85, -0.5), 0.22, vec3(1.0, 0.55, 0.55), 0.5);
        brows(acc, fp, 0.5, 0.78, raise, 0.022);
        eye(acc, fp, vec2(-0.5, 0.45), 0.2, eyeL, gaze, 1);
        eye(acc, fp, vec2(0.5, 0.45), 0.2, eyeR, gaze, 1);
        put(acc, vec3(0.3, 0.55, 0.25), sdCircle(fp - vec2(-0.1, -0.3), 0.03));
        put(acc, vec3(0.3, 0.55, 0.25), sdCircle(fp - vec2(0.1, -0.3), 0.03));
        mouth(acc, fp, vec2(0.0, -0.65), 0.42, open, smile);
    }
    return acc;
}

float dot2(vec2 v) { return dot(v, v); }
// 心形，底尖在原点，高约 1
float sdHeart(vec2 p) {
    p.x = abs(p.x);
    if (p.y + p.x > 1.0) {
        return sqrt(dot2(p - vec2(0.25, 0.75))) - sqrt(2.0) / 4.0;
    }
    return sqrt(min(dot2(p - vec2(0.0, 1.0)), dot2(p - 0.5 * max(p.x + p.y, 0.0)))) * sign(p.x - p.y);
}
float hash(float n) { return fract(sin(n) * 43758.5453); }

// ---------------------------------------------------------------- 头顶
vec4 drawHead(vec2 p, int kind, float top, float yaw) {
    vec4 acc = vec4(0.0);
    vec2 q = p - vec2(yaw * 0.12, 0.0);
    if (kind == 1) {          // 猫耳
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 e = rot(q - vec2(sx * 0.62, top - 0.08), sx * 0.3);
            putO(acc, vec3(0.32, 0.3, 0.34), sdTri(e, vec2(-0.3, -0.12), vec2(0.3, -0.12), vec2(0.0, 0.5)), 0.035);
            put(acc, vec3(1.0, 0.72, 0.8), sdTri(e, vec2(-0.16, -0.05), vec2(0.16, -0.05), vec2(0.0, 0.3)));
        }
    } else if (kind == 2) {   // 兔耳
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 e = rot(q - vec2(sx * 0.35, top + 0.42), sx * 0.2);
            putO(acc, vec3(0.99), sdEllipse(e, vec2(0.21, 0.62)), 0.035);
            put(acc, vec3(1.0, 0.76, 0.84), sdEllipse(e - vec2(0.0, -0.04), vec2(0.1, 0.44)));
        }
    } else if (kind == 3) {   // 皇冠
        vec2 c = (q - vec2(0.0, top - 0.12)) / 1.35;
        vec3 gold = vec3(1.0, 0.8, 0.26);
        float d = sdBox(c, vec2(0.55, 0.1), 0.03);
        for (int i = -2; i <= 2; ++i) {
            float x = float(i) * 0.22;
            float h = i == 0 ? 0.42 : (abs(i) == 1 ? 0.34 : 0.28);
            d = min(d, sdTri(c, vec2(x - 0.12, 0.05), vec2(x + 0.12, 0.05), vec2(x, h)));
        }
        putO(acc, gold, d, 0.03);
        for (int i = -2; i <= 2; ++i) {
            float x = float(i) * 0.22;
            float h = i == 0 ? 0.42 : (abs(i) == 1 ? 0.34 : 0.28);
            put(acc, vec3(1.0, 0.95, 0.75), sdCircle(c - vec2(x, h), 0.04));
        }
        put(acc, vec3(0.9, 0.15, 0.25), sdCircle(c, 0.06));
        put(acc, vec3(0.2, 0.45, 0.95), sdCircle(c - vec2(-0.3, 0.0), 0.045));
        put(acc, vec3(0.2, 0.45, 0.95), sdCircle(c - vec2(0.3, 0.0), 0.045));
    } else if (kind == 4) {   // 花环：沿头顶的弧线排一圈小花
        vec3 colors[3] = vec3[3](vec3(1.0, 0.62, 0.75), vec3(1.0, 0.98, 0.95), vec3(0.8, 0.68, 1.0));
        for (int i = 0; i < 9; ++i) {
            float t = mix(2.75, 0.39, float(i) / 8.0);
            vec2 c = vec2(cos(t) * 1.02, top - 0.55 + sin(t) * 0.62);
            if (i < 8) {
                float t2 = t - 0.15;
                vec2 l = vec2(cos(t2) * 1.02, top - 0.55 + sin(t2) * 0.62);
                putO(acc, vec3(0.4, 0.72, 0.35), sdEllipse(rot(q - l, t2), vec2(0.06, 0.12)), 0.02);
            }
            vec3 col = colors[i % 3];
            float d = 1e5;
            for (int k = 0; k < 5; ++k) {
                float a = float(k) * 1.2566 + float(i);
                d = min(d, sdCircle(q - c - vec2(cos(a), sin(a)) * 0.085, 0.075));
            }
            putO(acc, col, d, 0.02);
            put(acc, vec3(1.0, 0.82, 0.3), sdCircle(q - c, 0.05));
        }
    } else if (kind == 5) {   // 光环，带一点柔光和缓慢的明暗起伏
        vec2 c = q - vec2(0.0, top + 0.42);
        float ring = abs(sdEllipse(c, vec2(0.72, 0.17))) - 0.045;
        float glow = (1.0 - smoothstep(0.0, 0.22, ring)) * (0.35 + 0.15 * sin(uTime * 2.0));
        acc = vec4(vec3(1.0, 0.93, 0.6), 1.0) * glow + acc * (1.0 - glow);
        put(acc, vec3(1.0, 0.86, 0.35), ring);
        put(acc, vec3(1.0, 0.97, 0.8), abs(sdEllipse(c - vec2(0.0, 0.015), vec2(0.72, 0.17))) - 0.015);
    } else if (kind == 6) {   // 小恶魔角
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 e = rot(q - vec2(sx * 0.52, top - 0.05), sx * 0.35) / 1.5;
            // 弯角：两个圆之差
            float d = max(sdCircle(e - vec2(-sx * 0.12, 0.05), 0.32), -sdCircle(e - vec2(-sx * 0.3, 0.12), 0.34));
            d = max(d, -e.y);
            putO(acc, vec3(0.86, 0.12, 0.18), d, 0.03);
        }
    } else if (kind == 7) {   // 派对帽：斜戴的圆锥，彩色条纹
        vec2 e = rot(q - vec2(0.3, top - 0.1), 0.3);
        float d = sdTri(e, vec2(-0.36, 0.0), vec2(0.36, 0.0), vec2(0.0, 0.85));
        put(acc, kInk, d - 0.03);
        float stripe = step(0.5, fract(e.y * 5.0 + e.x * 1.5));
        put(acc, mix(vec3(0.3, 0.7, 1.0), vec3(1.0, 0.85, 0.3), stripe), d);
        for (int k = 0; k < 6; ++k) {
            float a = float(k) * 1.047;
            put(acc, vec3(1.0, 0.4, 0.55), sdCircle(e - vec2(0.0, 0.88) - vec2(cos(a), sin(a)) * 0.06, 0.06));
        }
        putO(acc, vec3(1.0, 0.95, 0.95), sdBox(e - vec2(0.0, 0.02), vec2(0.4, 0.05), 0.04), 0.025);
    } else if (kind == 8) {   // 圣诞帽：红色软帽，帽尖向一侧垂下，白色帽檐和绒球
        vec2 e = q - vec2(0.0, top - 0.12);
        vec3 red = vec3(0.86, 0.12, 0.16);
        // 帽身：底边宽，向右上方弯过去
        float d = sdTri(e, vec2(-0.78, 0.05), vec2(0.78, 0.05), vec2(0.42, 0.95));
        d = min(d, sdSeg(e, vec2(0.42, 0.9), vec2(0.82, 0.62)) - 0.09);
        putO(acc, red, d, 0.03);
        put(acc, vec3(0.7, 0.08, 0.12), max(d, -sdSeg(e, vec2(-0.2, 0.1), vec2(0.5, 0.85)) + 0.03) + 0.06);
        float brim = sdBox(e - vec2(0.0, 0.04), vec2(0.86, 0.11), 0.1);
        putO(acc, vec3(0.98), brim, 0.03);
        putO(acc, vec3(0.98), sdCircle(e - vec2(0.86, 0.56), 0.15), 0.03);
    }
    return acc;
}

// ---------------------------------------------------------------- 眼部
vec4 drawEyes(vec2 p, int kind, float eyeHalf, float eyeY, float yaw) {
    vec4 acc = vec4(0.0);
    vec2 g = p - vec2(yaw * 0.04, eyeY);
    float temple = eyeHalf + 0.58;   // 镜腿伸到脸侧
    if (kind == 1) {          // 墨镜
        float lens = min(sdBox(g - vec2(-eyeHalf, 0.0), vec2(0.27, 0.16), 0.1),
                         sdBox(g - vec2(eyeHalf, 0.0), vec2(0.27, 0.16), 0.1));
        float frame = min(sdSeg(g, vec2(-eyeHalf + 0.25, 0.06), vec2(eyeHalf - 0.25, 0.06)) - 0.025,
                          min(sdSeg(g, vec2(-eyeHalf - 0.27, 0.05), vec2(-temple, 0.1)) - 0.025,
                              sdSeg(g, vec2(eyeHalf + 0.27, 0.05), vec2(temple, 0.1)) - 0.025));
        put(acc, kInk, min(frame, lens - 0.03));
        putA(acc, vec3(0.08, 0.09, 0.12), lens, 0.95);
        // 镜片上的反光
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 r = g - vec2(sx * eyeHalf, 0.0);
            putA(acc, vec3(1.0), max(sdSeg(r, vec2(-0.12, 0.02), vec2(0.0, 0.12)) - 0.025, lens), 0.6);
        }
    } else if (kind == 2) {   // 近视眼镜：细金属圆框，镜片几乎透明
        vec3 metal = vec3(0.55, 0.45, 0.3);
        float lens = min(sdCircle(g - vec2(-eyeHalf, 0.0), 0.2), sdCircle(g - vec2(eyeHalf, 0.0), 0.2));
        putA(acc, vec3(0.85, 0.92, 1.0), lens, 0.12);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 r = g - vec2(sx * eyeHalf, 0.0);
            putA(acc, vec3(1.0), max(sdSeg(r, vec2(-0.1, 0.04), vec2(-0.02, 0.13)) - 0.015, sdCircle(r, 0.2)), 0.5);
        }
        float frame = abs(lens) - 0.014;
        // 鼻梁：两片镜片之间略向上拱的一小段
        frame = min(frame, min(sdSeg(g, vec2(-eyeHalf + 0.2, 0.04), vec2(0.0, 0.07)),
                               sdSeg(g, vec2(0.0, 0.07), vec2(eyeHalf - 0.2, 0.04))) - 0.012);
        frame = min(frame, min(sdSeg(g, vec2(-eyeHalf - 0.2, 0.03), vec2(-temple, 0.08)),
                               sdSeg(g, vec2(eyeHalf + 0.2, 0.03), vec2(temple, 0.08))) - 0.012);
        put(acc, metal, frame);
    } else if (kind == 3) {   // 黑框眼镜：粗方框
        float lens = min(sdBox(g - vec2(-eyeHalf, 0.0), vec2(0.24, 0.17), 0.06),
                         sdBox(g - vec2(eyeHalf, 0.0), vec2(0.24, 0.17), 0.06));
        putA(acc, vec3(0.85, 0.92, 1.0), lens, 0.1);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 r = g - vec2(sx * eyeHalf, 0.0);
            putA(acc, vec3(1.0), max(sdSeg(r, vec2(-0.13, 0.02), vec2(-0.04, 0.12)) - 0.018, lens), 0.45);
        }
        float frame = abs(lens) - 0.04;
        frame = min(frame, sdSeg(g, vec2(-eyeHalf + 0.22, 0.07), vec2(eyeHalf - 0.22, 0.07)) - 0.035);
        frame = min(frame, min(sdSeg(g, vec2(-eyeHalf - 0.24, 0.1), vec2(-temple, 0.12)),
                               sdSeg(g, vec2(eyeHalf + 0.24, 0.1), vec2(temple, 0.12))) - 0.03);
        put(acc, vec3(0.08, 0.07, 0.08), frame);
    } else if (kind == 4) {   // 心形眼镜：粉色镜片，红框
        float lens = 1e5;
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 r = (g - vec2(sx * eyeHalf, -0.24)) / 0.46;
            lens = min(lens, sdHeart(r) * 0.46);
        }
        float frame = min(abs(lens) - 0.03, sdSeg(g, vec2(-eyeHalf + 0.15, 0.06), vec2(eyeHalf - 0.15, 0.06)) - 0.025);
        frame = min(frame, min(sdSeg(g, vec2(-eyeHalf - 0.24, 0.06), vec2(-temple, 0.1)),
                               sdSeg(g, vec2(eyeHalf + 0.24, 0.06), vec2(temple, 0.1))) - 0.025);
        putA(acc, vec3(1.0, 0.35, 0.6), lens, 0.7);
        put(acc, vec3(0.9, 0.1, 0.3), frame);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            putA(acc, vec3(1.0), max(sdCircle(g - vec2(sx * eyeHalf - 0.08, 0.06), 0.04), lens), 0.8);
        }
    } else if (kind == 5) {   // 海盗眼罩：盖住画面右侧的眼睛，带子斜过额头
        vec2 c = g - vec2(eyeHalf, -0.01);
        float patch = sdEllipse(c, vec2(0.25, 0.2));
        // 带子从眼罩斜过额头绕到脑后，另一头从眼罩外侧绕到耳后
        float strap = min(sdSeg(g, vec2(eyeHalf - 0.15, 0.12), vec2(-0.92, 0.62)),
                          sdSeg(g, vec2(eyeHalf + 0.2, 0.08), vec2(temple, 0.02))) - 0.025;
        put(acc, vec3(0.1, 0.09, 0.1), strap);
        putO(acc, vec3(0.12, 0.11, 0.12), patch, 0.025);
        putA(acc, vec3(1.0), max(sdSeg(c, vec2(-0.12, 0.06), vec2(-0.02, 0.12)) - 0.018, patch), 0.25);
    }
    return acc;
}

// ---------------------------------------------------------------- 耳饰
// 一只耳朵上的耳饰，e 以耳垂为原点
void earring(inout vec4 acc, vec2 e, int kind, float side, float sway) {
    vec3 gold = vec3(1.0, 0.8, 0.28);
    if (kind == 1) {          // 金耳环：大圆环
        vec2 c = e - vec2(0.0, -0.13);
        put(acc, vec3(0.6, 0.42, 0.1), abs(sdCircle(c, 0.12)) - 0.03);
        put(acc, gold, abs(sdCircle(c, 0.12)) - 0.02);
        putA(acc, vec3(1.0, 0.97, 0.8), max(abs(sdCircle(c, 0.12)) - 0.008, -(c.x * side + c.y)), 0.6);
    } else if (kind == 2) {   // 珍珠耳钉
        putO(acc, vec3(0.97, 0.95, 0.9), sdCircle(e, 0.055), 0.012);
        put(acc, vec3(1.0), sdCircle(e - vec2(-0.018, 0.018), 0.018));
    } else if (kind == 3) {   // 流苏耳坠：金扣加红色流苏，随时间轻轻摆
        put(acc, gold, sdCircle(e, 0.035));
        vec2 b = rot(e - vec2(0.0, -0.04), sway);
        put(acc, gold, sdBox(b - vec2(0.0, -0.05), vec2(0.035, 0.03), 0.01));
        float d = 1e5;
        for (int k = -2; k <= 2; ++k) {
            float x = float(k) * 0.014;
            d = min(d, sdSeg(b, vec2(x * 0.5, -0.08), vec2(x * 1.6, -0.34)) - 0.008);
        }
        put(acc, vec3(0.86, 0.1, 0.16), d);
    } else if (kind == 4) {   // 樱桃耳坠：两颗挂在绿梗上的樱桃
        vec2 b = rot(e, sway);
        float stem = min(sdSeg(b, vec2(0.0, 0.0), vec2(-0.07, -0.2)), sdSeg(b, vec2(0.0, 0.0), vec2(0.07, -0.17))) - 0.01;
        put(acc, vec3(0.25, 0.55, 0.2), stem);
        putO(acc, vec3(0.88, 0.08, 0.16), sdCircle(b - vec2(-0.07, -0.25), 0.065), 0.015);
        putO(acc, vec3(0.88, 0.08, 0.16), sdCircle(b - vec2(0.07, -0.22), 0.065), 0.015);
        put(acc, vec3(1.0, 0.7, 0.75), sdCircle(b - vec2(-0.09, -0.23), 0.017));
        put(acc, vec3(1.0, 0.7, 0.75), sdCircle(b - vec2(0.05, -0.2), 0.017));
        put(acc, gold, sdCircle(b, 0.025));
    }
}

vec4 drawEars(vec2 p, int kind, float earLX, float earRX, float earY, float yaw) {
    vec4 acc = vec4(0.0);
    for (int i = 0; i < 2; ++i) {
        float sx = i == 0 ? -1.0 : 1.0;
        // 转头时朝外转的那只耳朵转到脸后面去了，渐隐
        float hide = clamp((sx * yaw - 0.3) / 0.3, 0.0, 1.0);
        if (hide >= 1.0) {
            continue;
        }
        float x = i == 0 ? earLX : earRX;
        vec2 e = p - vec2(x - sx * 0.02, earY);
        float sway = sin(uTime * 2.2 + sx) * 0.12;
        vec4 one = vec4(0.0);
        earring(one, e, kind, sx, sway);
        acc = one * (1.0 - hide) + acc * (1.0 - one.a * (1.0 - hide));
    }
    return acc;
}

// ---------------------------------------------------------------- 颈部
// 项链绕脖子一圈：前半圈在脖子前面垂成 U 形，后半圈在脖子后面，只在脖子两侧露出一点，往后绕时逐渐看不见。
// a 为绕圈的角度，sin(a) < 0 是前半圈。脖子半宽约 0.78（脸宽的四成左右），项链贴着脖子稍微往外一点。
const float kNeckHalf = 0.78;
vec2 neckLoop(float a, float rx, float drop) {
    float s = sin(a);
    return vec2(cos(a) * rx, -0.22 + s * (s < 0.0 ? drop : 0.1));
}
// 后半圈能看到多少：在脖子轮廓以外才露出来，越往后越暗
float neckBack(float a, float rx) {
    float x = abs(cos(a) * rx);
    return sin(a) < 0.0 ? 1.0 : clamp((x - kNeckHalf + 0.06) / 0.06, 0.0, 1.0);
}

vec4 drawNeck(vec2 p, int kind, float chinY, float yaw) {
    vec4 acc = vec4(0.0);
    // 脖子不跟着转头，抵消一点五官随转头的平移
    vec2 q = p - vec2(-yaw * 0.05, chinY);
    vec3 gold = vec3(1.0, 0.78, 0.25);
    if (kind == 1) {          // 大金链子：一圈粗链节，下面挂一块大金牌
        const float rx = 0.84;
        const int kLinks = 30;
        // 先画后半圈（在脖子后面，偏暗），再画前半圈
        for (int pass = 0; pass < 2; ++pass) {
            for (int k = 0; k < kLinks; ++k) {
                float a = 6.2831853 * (float(k) + 0.5) / float(kLinks);
                bool front = sin(a) < 0.0;
                if (front != (pass == 1)) {
                    continue;
                }
                float vis = neckBack(a, rx);
                if (vis <= 0.0) {
                    continue;
                }
                vec2 c = neckLoop(a, rx, 0.5);
                vec2 tangent = neckLoop(a + 0.01, rx, 0.5) - c;
                vec2 l = rot(q - c, -atan(tangent.y, tangent.x));
                float link = abs(sdEllipse(l, k % 2 == 0 ? vec2(0.085, 0.05) : vec2(0.07, 0.03))) - 0.022;
                float shade = front ? 1.0 : 0.7;
                putA(acc, vec3(0.62, 0.42, 0.1) * shade, link - 0.012, vis);
                putA(acc, gold * shade, link, vis);
                if (front) {
                    putA(acc, vec3(1.0, 0.96, 0.75), max(link + 0.012, -l.y), 0.7);
                }
            }
        }
        vec2 m = q - vec2(0.0, -0.9);
        putO(acc, gold, sdCircle(m, 0.17), 0.025);
        put(acc, vec3(0.85, 0.62, 0.15), abs(sdCircle(m, 0.12)) - 0.012);
        put(acc, vec3(0.85, 0.62, 0.15), sdSeg(m, vec2(0.0, -0.08), vec2(0.0, 0.08)) - 0.018);
        putA(acc, vec3(1.0), sdSeg(m, vec2(-0.08, 0.02), vec2(-0.02, 0.1)) - 0.015, 0.6);
    } else if (kind == 2) {   // 珍珠项链
        const float rx = 0.82;
        const int kPearls = 44;
        for (int pass = 0; pass < 2; ++pass) {
            for (int k = 0; k < kPearls; ++k) {
                float a = 6.2831853 * (float(k) + 0.5) / float(kPearls);
                bool front = sin(a) < 0.0;
                if (front != (pass == 1)) {
                    continue;
                }
                float vis = neckBack(a, rx);
                if (vis <= 0.0) {
                    continue;
                }
                vec2 c = neckLoop(a, rx, 0.4);
                // 正前方的珍珠稍大
                float r = 0.042 + (front ? 0.014 * max(-sin(a), 0.0) : 0.0);
                float shade = front ? 1.0 : 0.75;
                putA(acc, kInk, sdCircle(q - c, r + 0.008), vis);
                putA(acc, vec3(0.96, 0.94, 0.9) * shade, sdCircle(q - c, r), vis);
                if (front) {
                    put(acc, vec3(1.0), sdCircle(q - c - vec2(-r * 0.35, r * 0.35), r * 0.3));
                }
            }
        }
    } else if (kind == 3) {   // 哈达：白色丝巾绕过脖子，两端垂在胸前，末端有流苏
        vec3 silk = vec3(0.97, 0.97, 0.99);
        // 绕过脖子后面的那段在脖子两侧露出来，和项链一样贴着脖子的宽度
        float collar = abs(sdEllipse(q - vec2(0.0, -0.18), vec2(kNeckHalf + 0.06, 0.3))) - 0.09;
        collar = max(collar, q.y + 0.18);
        putA(acc, silk, collar, 0.95);
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 b = q - vec2(sx * 0.55, -0.35);
            float wave = sin(b.y * 7.0 + uTime * 1.5 + sx) * 0.03;
            float x = b.x - wave + b.y * sx * 0.12;
            float band = max(abs(x) - 0.2, max(b.y, -(b.y + 1.5)));
            putA(acc, silk, band, 0.95);
            // 丝巾上的暗纹和折痕
            putA(acc, vec3(0.86, 0.88, 0.94), max(band, abs(fract(b.y * 6.0) - 0.5) - 0.06), 0.5);
            putA(acc, vec3(0.88, 0.9, 0.95), max(band, abs(x) - 0.012), 0.7);
            for (int k = -4; k <= 4; ++k) {
                float fx = float(k) * 0.045;
                put(acc, silk, sdSeg(vec2(x, b.y), vec2(fx, -1.5), vec2(fx, -1.66)) - 0.01);
            }
        }
    } else if (kind == 4) {   // 领结
        vec2 b = q - vec2(0.0, -0.42);
        vec3 red = vec3(0.82, 0.08, 0.14);
        float wings = min(sdTri(b, vec2(0.0, 0.0), vec2(-0.32, 0.17), vec2(-0.32, -0.17)),
                          sdTri(b, vec2(0.0, 0.0), vec2(0.32, 0.17), vec2(0.32, -0.17))) - 0.025;
        putO(acc, red, wings, 0.025);
        put(acc, vec3(0.62, 0.04, 0.1), max(wings, -abs(b.y) + 0.04));
        putO(acc, vec3(0.7, 0.05, 0.12), sdBox(b, vec2(0.065, 0.075), 0.03), 0.025);
    }
    return acc;
}

// ---------------------------------------------------------------- 嘴上叼的
vec4 drawMouth(vec2 p, int kind, float mouthY, float mouthHalf, float open, float yaw) {
    vec4 acc = vec4(0.0);
    // 叼在画面右侧的嘴角，张嘴时跟着下唇往下掉一点
    vec2 corner = vec2(mouthHalf * 0.7 + yaw * 0.2, mouthY - open * 0.12);
    vec2 q = p - corner;
    if (kind == 1 || kind == 2) {   // 雪茄 / 香烟：斜向下伸出，烟头发红，冒烟
        bool cigar = kind == 1;
        float len = cigar ? 0.78 : 0.62;
        float r = cigar ? 0.065 : 0.032;
        vec2 dir = normalize(vec2(1.0, -0.32));
        vec2 l = vec2(dot(q, dir), dot(q, vec2(-dir.y, dir.x)));
        float body = sdBox(l - vec2(len * 0.5, 0.0), vec2(len * 0.5, r), r * 0.6);
        put(acc, kInk, body - 0.02);
        if (cigar) {
            put(acc, vec3(0.45, 0.26, 0.13), body);
            put(acc, vec3(0.34, 0.18, 0.08), max(body, abs(fract(l.x * 9.0) - 0.5) - 0.06));
            putO(acc, vec3(0.85, 0.15, 0.15), sdBox(l - vec2(0.2, 0.0), vec2(0.06, r + 0.008), 0.01), 0.012);
            put(acc, vec3(1.0, 0.85, 0.3), sdBox(l - vec2(0.2, 0.0), vec2(0.02, r + 0.008), 0.005));
        } else {
            put(acc, vec3(0.97, 0.96, 0.94), body);
            put(acc, vec3(0.95, 0.6, 0.28), max(body, l.x - 0.18));
        }
        // 烟灰和红热的烟头，亮度跟着"吸"的节奏起伏
        float glow = 0.75 + 0.25 * sin(uTime * 2.5);
        float tip = max(body, len - r * 0.4 - l.x);
        put(acc, mix(vec3(0.6, 0.12, 0.05), vec3(1.0, 0.45, 0.12), glow), tip);
        putA(acc, vec3(0.62, 0.6, 0.58), max(max(body, len - r * 1.6 - l.x), l.x - (len - r * 0.4)), 0.9);
        // 烟雾：几团从烟头往上飘、逐渐变大变淡
        vec2 tipPos = dir * len;
        for (int k = 0; k < 6; ++k) {
            float t = fract(uTime * 0.25 + float(k) / 6.0);
            vec2 c = tipPos + vec2(sin(t * 6.0 + float(k)) * 0.08 + t * 0.15, t * 1.1 + 0.05);
            float rad = (cigar ? 0.07 : 0.045) + t * (cigar ? 0.18 : 0.1);
            putA(acc, vec3(0.9, 0.9, 0.92), sdCircle(q - c, rad), (1.0 - t) * (cigar ? 0.45 : 0.3));
        }
    } else if (kind == 3) {         // 棒棒糖：白色小棍，彩色螺旋糖
        vec2 dir = normalize(vec2(1.0, -0.15));
        vec2 l = vec2(dot(q, dir), dot(q, vec2(-dir.y, dir.x)));
        float stick = sdBox(l - vec2(0.28, 0.0), vec2(0.28, 0.022), 0.02);
        putO(acc, vec3(0.98), stick, 0.015);
        vec2 c = q - dir * 0.66;
        float candy = sdCircle(c, 0.24);
        putO(acc, vec3(1.0, 0.45, 0.6), candy, 0.025);
        float a = atan(c.y, c.x);
        float spiral = fract((length(c) * 9.0 - a / 6.2831853) + uTime * 0.2);
        put(acc, vec3(1.0, 0.95, 0.97), max(candy + 0.02, abs(spiral - 0.5) - 0.18));
        putA(acc, vec3(1.0), sdCircle(c - vec2(-0.09, 0.09), 0.05), 0.7);
    } else if (kind == 4) {         // 玫瑰：横着咬在嘴里，花朵在画面左侧
        vec2 r = p - vec2(yaw * 0.2, mouthY - open * 0.12);
        float stem = sdSeg(r, vec2(-0.55, 0.02), vec2(0.75, -0.05)) - 0.022;
        putO(acc, vec3(0.2, 0.5, 0.18), stem, 0.012);
        for (int k = 0; k < 4; ++k) {
            float x = mix(-0.2, 0.6, float(k) / 3.0);
            float s = k % 2 == 0 ? 1.0 : -1.0;
            put(acc, vec3(0.18, 0.42, 0.15), sdTri(r, vec2(x - 0.03, 0.0), vec2(x + 0.03, 0.0), vec2(x + 0.04, 0.07 * s)));
        }
        putO(acc, vec3(0.3, 0.62, 0.25), sdEllipse(rot(r - vec2(-0.38, 0.1), 0.6), vec2(0.13, 0.06)), 0.012);
        vec2 b = r - vec2(-0.66, 0.08);
        putO(acc, vec3(0.78, 0.06, 0.14), sdCircle(b, 0.2), 0.02);
        for (int k = 0; k < 3; ++k) {
            float rad = 0.16 - float(k) * 0.05;
            float a = float(k) * 1.3 + 0.4;
            put(acc, vec3(0.55, 0.02, 0.08), abs(sdCircle(b - vec2(cos(a), sin(a)) * 0.02, rad)) - 0.012);
        }
        put(acc, vec3(0.92, 0.2, 0.3), sdCircle(b - vec2(-0.06, 0.07), 0.04));
    }
    return acc;
}

// 叠加一层：上层盖下层
void over(inout vec4 acc, vec4 layer) {
    acc = layer + acc * (1.0 - layer.a);
}

// 头像局部坐标（drawAvatar 用的）换算到脸的局部坐标
const float kAvatarScale = 1.18;
float avatarY(float y) { return (y + 0.3) * kAvatarScale - 0.3; }

void main() {
    vec3 col = texture(uTex, vUV).rgb;
    vec2 pix = vUV * uSize;
    for (int i = 0; i < 2; ++i) {
        if (i >= uFaceCount) {
            break;
        }
        vec2 d = pix - uPose[i].xy;
        vec2 u = uPose[i].zw;
        float s = uPose2[i].x;
        vec2 p = vec2(dot(d, u), -(-d.x * u.y + d.y * u.x)) / s;
        // 哈达垂到胸前，范围要比头部大一些
        if (p.y < -4.0 || length(p) > 4.0) {
            continue;
        }
        AA = 1.4 / s;
        vec4 acc = vec4(0.0);
        if (uAvatar > 0) {
            // 头像整体放大一些，盖住头发和下巴
            vec2 pa = (p - vec2(0.0, -0.3)) / kAvatarScale + vec2(0.0, -0.3);
            AA = 1.4 / (s * kAvatarScale);
            acc = drawAvatar(pa, uAvatar, uPose2[i], uExpr[i], uExpr2[i]);
            AA = 1.4 / s;
        }
        float yaw = uPose2[i].y;
        float open = uExpr[i].z;
        bool av = uAvatar > 0;
        // 戴在头像上时，眼睛、嘴、耳朵、下巴的位置取头像的
        float top = av ? max(uPose2[i].w, 1.05) : uPose2[i].w;
        float eyeHalf = av ? 0.42 * kAvatarScale : uExpr2[i].w;
        float eyeY = av ? avatarY(uAvatar == 6 ? 0.45 : 0.03) : 0.0;
        float mouthY = av ? avatarY(uAvatar == 1 ? -0.9 : -0.68) : uFace3[i].x;
        float mouthHalf = av ? 0.2 : uFace3[i].y;
        float chinY = av ? avatarY(-1.42) : uFace3[i].z;
        float earY = av ? avatarY(-0.5) : uFace3[i].w;
        float earLX = av ? -1.22 * kAvatarScale : uFace4[i].x;
        float earRX = av ? 1.22 * kAvatarScale : uFace4[i].y;
        // 从后往前画：脖子、耳朵、嘴、眼睛、头顶
        if (uAccNeck > 0) over(acc, drawNeck(p, uAccNeck, chinY, yaw));
        if (uAccEars > 0) over(acc, drawEars(p, uAccEars, earLX, earRX, earY, yaw));
        if (uAccMouth > 0) over(acc, drawMouth(p, uAccMouth, mouthY, mouthHalf, open, yaw));
        if (uAccEyes > 0) over(acc, drawEyes(p, uAccEyes, eyeHalf, eyeY, yaw));
        if (uAccHead > 0) over(acc, drawHead(p, uAccHead, top, yaw));
        col = col * (1.0 - acc.a) + acc.rgb;
    }
    oColor = vec4(col, 1.0);
}
)";
