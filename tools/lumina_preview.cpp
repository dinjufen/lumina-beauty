// Accessory preview: draws the cartoon avatars and accessories on a mannequin face without a camera, so
// placement and looks can be checked. Writes a binary PPM image.
//
// usage: lumina-preview <out.ppm> [head=N] [eyes=N] [ears=N] [neck=N] [mouth=N] [avatar=N]
//                       [yaw=F] [open=F] [time=F] [size=W]
// The mannequin is a plain head drawn from the same pose numbers the engine gets from face landmarks.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "LmAvatar.hpp"
#include "LmGLContext.hpp"
#include "LmPipeline.hpp"

namespace {

struct Rgb {
    float r, g, b;
};

/// 画一个示意用的人头：脸、头发、耳朵、脖子、眼睛、嘴，位置和 LmAvatarFace 一致
std::vector<Rgb> DrawMannequin(int w, int h, const LmAvatarFace& f) {
    std::vector<Rgb> img(static_cast<size_t>(w) * h);
    const Rgb bg{0.80f, 0.84f, 0.88f};
    const Rgb skin{0.96f, 0.80f, 0.70f};
    const Rgb shade{0.88f, 0.70f, 0.60f};
    const Rgb hair{0.20f, 0.15f, 0.13f};
    const Rgb shirt{0.30f, 0.42f, 0.60f};
    const Rgb ink{0.15f, 0.10f, 0.10f};
    const Rgb lip{0.82f, 0.45f, 0.48f};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // 画面坐标到局部坐标（y 向上，1 个单位为半个脸宽）
            const float dx = x - f.cx;
            const float dy = y - f.cy;
            const float px = (dx * f.ux + dy * f.uy) / f.scale;
            const float py = -(-dx * f.uy + dy * f.ux) / f.scale;
            Rgb c = bg;
            auto ellipse = [&](float cx, float cy, float rx, float ry) {
                const float ex = (px - cx) / rx;
                const float ey = (py - cy) / ry;
                return ex * ex + ey * ey < 1.0f;
            };
            if (py < f.chinY - 0.55f && std::fabs(px) < 1.6f + (f.chinY - 0.55f - py) * 0.8f) c = shirt;
            // 脖子半宽约为半个脸宽的 0.78，与着色器里项链贴合的宽度一致
            if (std::fabs(px) < 0.78f && py < f.chinY + 0.5f && py > f.chinY - 1.0f) c = shade;
            if (ellipse(f.earLeftX - 0.02f, f.earY + 0.2f, 0.13f, 0.25f) ||
                ellipse(f.earRightX + 0.02f, f.earY + 0.2f, 0.13f, 0.25f)) c = shade;
            if (ellipse(0.0f, 0.15f, 1.12f, 1.2f)) c = hair;
            if (ellipse(0.0f, -0.3f, 1.0f, f.chinY < -1.0f ? -f.chinY - 0.3f + 0.05f : 1.15f) && py < 0.65f) c = skin;
            if (ellipse(-f.eyeHalf, 0.0f, 0.14f, 0.06f) || ellipse(f.eyeHalf, 0.0f, 0.14f, 0.06f)) c = Rgb{1, 1, 1};
            if (ellipse(-f.eyeHalf, 0.0f, 0.05f, 0.05f) || ellipse(f.eyeHalf, 0.0f, 0.05f, 0.05f)) c = ink;
            if (std::fabs(py - 0.2f) < 0.025f && std::fabs(std::fabs(px) - f.eyeHalf) < 0.15f) c = hair;
            if (ellipse(0.0f, f.mouthY, f.mouthHalf, 0.05f + f.mouthOpen * 0.2f)) c = lip;
            if (std::fabs(px) < 0.03f && py < -0.05f && py > -0.42f) c = shade;
            img[static_cast<size_t>(y) * w + x] = c;
        }
    }
    return img;
}

void RgbToI420(const std::vector<Rgb>& img, int w, int h, std::vector<uint8_t>& out) {
    out.assign(static_cast<size_t>(w) * h * 3 / 2, 128);
    auto clamp8 = [](float v) { return static_cast<uint8_t>(std::fmin(std::fmax(v, 0.0f), 255.0f) + 0.5f); };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Rgb& c = img[static_cast<size_t>(y) * w + x];
            out[static_cast<size_t>(y) * w + x] = clamp8((0.299f * c.r + 0.587f * c.g + 0.114f * c.b) * 255.0f);
        }
    }
    uint8_t* u = out.data() + static_cast<size_t>(w) * h;
    uint8_t* v = u + static_cast<size_t>(w / 2) * (h / 2);
    for (int y = 0; y < h / 2; ++y) {
        for (int x = 0; x < w / 2; ++x) {
            const Rgb& c = img[static_cast<size_t>(y * 2) * w + x * 2];
            const float yy = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            u[y * (w / 2) + x] = clamp8(((c.b - yy) * 0.564f + 0.5f) * 255.0f);
            v[y * (w / 2) + x] = clamp8(((c.r - yy) * 0.713f + 0.5f) * 255.0f);
        }
    }
}

bool WritePpm(const char* path, const std::vector<uint8_t>& i420, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    const uint8_t* yp = i420.data();
    const uint8_t* up = yp + static_cast<size_t>(w) * h;
    const uint8_t* vp = up + static_cast<size_t>(w / 2) * (h / 2);
    std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float Y = yp[y * w + x] / 255.0f;
            const float U = up[(y / 2) * (w / 2) + x / 2] / 255.0f - 0.5f;
            const float V = vp[(y / 2) * (w / 2) + x / 2] / 255.0f - 0.5f;
            const float rgb[3] = {Y + 1.402f * V, Y - 0.344f * U - 0.714f * V, Y + 1.772f * U};
            for (int k = 0; k < 3; ++k) {
                row[x * 3 + k] = static_cast<uint8_t>(std::fmin(std::fmax(rgb[k], 0.0f), 1.0f) * 255.0f + 0.5f);
            }
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <out.ppm> [head=N] [eyes=N] [ears=N] [neck=N] [mouth=N] [avatar=N] [yaw=F] [open=F] "
                "[size=W]\n",
                argv[0]);
        return 2;
    }
    LmParams params;
    int size = 640;
    float yaw = 0.0f;
    float open = 0.0f;
    for (int i = 2; i < argc; ++i) {
        const char* eq = strchr(argv[i], '=');
        if (!eq) {
            continue;
        }
        const std::string key(argv[i], eq - argv[i]);
        const float v = static_cast<float>(atof(eq + 1));
        if (key == "head") params.accHead = static_cast<int>(v);
        else if (key == "eyes") params.accEyes = static_cast<int>(v);
        else if (key == "ears") params.accEars = static_cast<int>(v);
        else if (key == "neck") params.accNeck = static_cast<int>(v);
        else if (key == "mouth") params.accMouth = static_cast<int>(v);
        else if (key == "avatar") params.avatar = static_cast<int>(v);
        else if (key == "yaw") yaw = v;
        else if (key == "open") open = v;
        else if (key == "size") size = static_cast<int>(v) & ~1;
        else fprintf(stderr, "unknown option %s\n", key.c_str());
    }
    const int w = size;
    const int h = size;

    // 一张正对镜头、略偏上的脸，数值与引擎从真实关键点算出的量级一致
    LmAvatarFace face;
    face.cx = w * 0.5f;
    face.cy = h * 0.40f;
    face.scale = w * 0.14f;
    face.yaw = yaw;
    face.mouthOpen = open;
    face.top = 1.15f;
    face.eyeHalf = 0.4f;
    face.mouthY = -0.88f;
    face.mouthHalf = 0.33f;
    face.chinY = -1.42f;
    face.earLeftX = -1.0f;
    face.earRightX = 1.0f;
    face.earY = -0.3f;

    std::vector<uint8_t> in;
    RgbToI420(DrawMannequin(w, h, face), w, h, in);
    std::vector<uint8_t> out(in.size());

    LmGLContext context;
    LmPipeline pipeline;
    if (!context.Init() || !context.MakeCurrent() || !pipeline.Init()) {
        fprintf(stderr, "cannot create the OpenGL pipeline\n");
        return 1;
    }
    const std::vector<LmAvatarFace> avatars = {face};
    const bool ok = pipeline.Render(w, h, in.data(), out.data(), params, {}, {}, {}, avatars, nullptr);
    pipeline.Release();
    context.DoneCurrent();
    context.Release();
    if (!ok || !WritePpm(argv[1], out, w, h)) {
        fprintf(stderr, "render or write failed\n");
        return 1;
    }
    return 0;
}
