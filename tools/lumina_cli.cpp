// Command line tool: apply effects to raw I420 frames, useful for testing and batch evaluation.
//
// usage: lumina-cli <models dir> <width> <height> <in.i420> <out.i420> [name=value ...] [--repeat N]
//   names: smooth whiten sharpen clarity eye slim narrow cheekbone jaw nose forehead shrink_head teeth pouch folds
//          temperature tint saturation brightness contrast filter filter_intensity style style_color style_makeup
//          background avatar headwear, plus background_image=<path>, matrix=601|709, full_range=0|1
//   The input file may contain several frames back to back; each is processed in turn.
//   --repeat N processes every frame N times (lets temporal smoothing settle) and reports the average time.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "lumina/lumina.h"

namespace {

struct Name {
    const char* name;
    lumina_param param;
};

const Name kNames[] = {
    {"smooth", LUMINA_SMOOTH}, {"whiten", LUMINA_WHITEN}, {"sharpen", LUMINA_SHARPEN}, {"clarity", LUMINA_CLARITY},
    {"eye", LUMINA_EYE_SIZE}, {"slim", LUMINA_FACE_SLIM}, {"narrow", LUMINA_FACE_NARROW},
    {"cheekbone", LUMINA_CHEEKBONE}, {"jaw", LUMINA_JAW}, {"nose", LUMINA_NOSE}, {"forehead", LUMINA_FOREHEAD},
    {"shrink_head", LUMINA_SHRINK_HEAD}, {"teeth", LUMINA_WHITEN_TEETH}, {"pouch", LUMINA_REMOVE_POUCH},
    {"folds", LUMINA_REMOVE_SMILE_FOLDS}, {"temperature", LUMINA_TEMPERATURE}, {"tint", LUMINA_TINT},
    {"saturation", LUMINA_SATURATION}, {"brightness", LUMINA_BRIGHTNESS}, {"contrast", LUMINA_CONTRAST},
    {"filter", LUMINA_FILTER}, {"filter_intensity", LUMINA_FILTER_INTENSITY}, {"style", LUMINA_STYLE},
    {"style_color", LUMINA_STYLE_COLOR}, {"style_makeup", LUMINA_STYLE_MAKEUP}, {"background", LUMINA_BACKGROUND},
    {"avatar", LUMINA_AVATAR}, {"headwear", LUMINA_HEADWEAR},
};

void Log(lumina_log_level level, const char* message, void*) {
    fprintf(stderr, "%s %s\n", level == LUMINA_LOG_ERROR ? "E" : (level == LUMINA_LOG_WARN ? "W" : "I"), message);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s <models dir> <width> <height> <in.i420> <out.i420> [name=value ...] [--repeat N]\n",
                argv[0]);
        return 2;
    }
    lumina_set_log_callback(Log, nullptr);
    const int w = atoi(argv[2]);
    const int h = atoi(argv[3]);
    const size_t frameSize = static_cast<size_t>(w) * h * 3 / 2;

    lumina_engine* engine = lumina_create(argv[1]);
    if (!engine) {
        return 1;
    }
    int repeat = 1;
    for (int i = 6; i < argc; ++i) {
        if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
            repeat = atoi(argv[++i]);
            continue;
        }
        const char* eq = strchr(argv[i], '=');
        if (!eq) {
            continue;
        }
        const std::string key(argv[i], eq - argv[i]);
        if (key == "matrix" || key == "full_range") {
            static int matrix = 601;
            static int full = 1;
            (key == "matrix" ? matrix : full) = atoi(eq + 1);
            lumina_set_color_space(engine, matrix, full);
            continue;
        }
        if (key == "background_image") {
            lumina_set_background_image(engine, eq + 1);
            continue;
        }
        bool found = false;
        for (const Name& n : kNames) {
            if (key == n.name) {
                lumina_set_param(engine, n.param, static_cast<float>(atof(eq + 1)));
                found = true;
            }
        }
        if (!found) {
            fprintf(stderr, "unknown parameter %s\n", key.c_str());
        }
    }

    FILE* in = fopen(argv[4], "rb");
    FILE* out = fopen(argv[5], "wb");
    if (!in || !out) {
        fprintf(stderr, "cannot open input or output file\n");
        return 1;
    }
    std::vector<uint8_t> src(frameSize);
    std::vector<uint8_t> dst(frameSize);
    int frames = 0;
    double totalMs = 0.0;
    while (fread(src.data(), 1, frameSize, in) == frameSize) {
        for (int r = 0; r < repeat; ++r) {
            const auto t0 = std::chrono::steady_clock::now();
            if (!lumina_process_i420(engine, w, h, src.data(), dst.data())) {
                fprintf(stderr, "processing failed\n");
                return 1;
            }
            totalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        fwrite(dst.data(), 1, frameSize, out);
        ++frames;
    }
    fclose(in);
    fclose(out);
    printf("%d frames, %.2f ms per call\n", frames, frames ? totalMs / (frames * repeat) : 0.0);
    lumina_destroy(engine);
    return 0;
}
