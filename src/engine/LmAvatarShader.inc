// 卡通头像与头饰的片段着色器，由 LmPipeline.cpp 包含。
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
uniform int uAvatar;
uniform int uHeadwear;
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

vec4 drawHeadwear(vec2 p, int kind, float top, float eyeHalf, float yaw) {
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
    } else if (kind == 8) {   // 墨镜
        vec2 g = p - vec2(yaw * 0.04, 0.0);
        float lens = min(sdBox(g - vec2(-eyeHalf, 0.0), vec2(0.27, 0.16), 0.1),
                         sdBox(g - vec2(eyeHalf, 0.0), vec2(0.27, 0.16), 0.1));
        float frame = min(sdSeg(g, vec2(-eyeHalf + 0.25, 0.06), vec2(eyeHalf - 0.25, 0.06)) - 0.025,
                          min(sdSeg(g, vec2(-eyeHalf - 0.27, 0.05), vec2(-0.98, 0.1)) - 0.025,
                              sdSeg(g, vec2(eyeHalf + 0.27, 0.05), vec2(0.98, 0.1)) - 0.025));
        put(acc, kInk, min(frame, lens - 0.03));
        putA(acc, vec3(0.08, 0.09, 0.12), lens, 0.95);
        // 镜片上的反光
        for (int i = 0; i < 2; ++i) {
            float sx = i == 0 ? -1.0 : 1.0;
            vec2 r = g - vec2(sx * eyeHalf, 0.0);
            putA(acc, vec3(1.0), max(sdSeg(r, vec2(-0.12, 0.02), vec2(0.0, 0.12)) - 0.025, lens), 0.6);
        }
    }
    return acc;
}

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
        if (length(p - vec2(0.0, 0.0)) > 3.2) {
            continue;
        }
        AA = 1.4 / s;
        vec4 acc = vec4(0.0);
        if (uAvatar > 0) {
            // 头像整体放大一些，盖住头发和下巴
            const float kAvatarScale = 1.18;
            vec2 pa = (p - vec2(0.0, -0.3)) / kAvatarScale + vec2(0.0, -0.3);
            AA = 1.4 / (s * kAvatarScale);
            acc = drawAvatar(pa, uAvatar, uPose2[i], uExpr[i], uExpr2[i]);
            AA = 1.4 / s;
        }
        if (uHeadwear > 0) {
            // 有头像时头饰戴在头像上，头顶位置取头像的
            float top = uAvatar > 0 ? max(uPose2[i].w, 1.05) : uPose2[i].w;
            vec4 h = drawHeadwear(p, uHeadwear, top, uAvatar > 0 ? 0.42 : uExpr2[i].w, uPose2[i].y);
            acc = h + acc * (1.0 - h.a);
        }
        col = col * (1.0 - acc.a) + acc.rgb;
    }
    oColor = vec4(col, 1.0);
}
)";
