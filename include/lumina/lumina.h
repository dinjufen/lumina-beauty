/*
 * Lumina Beauty - real-time face beauty, virtual background and fun effects for video frames.
 *
 * The engine works on CPU-side frames: you hand it a video frame, it renders the effects with
 * the GPU (OpenGL 3.3, offscreen) and writes the result back. It owns its own GL context, so it
 * can be called from any single thread that does not already have an OpenGL context current.
 *
 * Copyright (c) 2026 Lumina Beauty contributors. MIT License.
 */
#ifndef LUMINA_BEAUTY_H
#define LUMINA_BEAUTY_H

#include <stdint.h>

#if defined(_WIN32)
#if defined(LUMINA_BUILD_SHARED)
#define LUMINA_API __declspec(dllexport)
#elif defined(LUMINA_USE_SHARED)
#define LUMINA_API __declspec(dllimport)
#else
#define LUMINA_API
#endif
#else
#define LUMINA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lumina_engine lumina_engine;

/** Effect parameters. Unless noted otherwise values are in [0, 1], 0 = off. */
typedef enum lumina_param {
    /* skin */
    LUMINA_SMOOTH = 0,           /**< skin smoothing */
    LUMINA_WHITEN,               /**< skin whitening */
    LUMINA_SHARPEN,              /**< detail sharpening */
    LUMINA_CLARITY,              /**< local contrast */

    /* face reshaping (needs face landmarks) */
    LUMINA_EYE_SIZE,             /**< bigger eyes */
    LUMINA_FACE_SLIM,            /**< slimmer face */
    LUMINA_FACE_NARROW,          /**< narrower face */
    LUMINA_CHEEKBONE,            /**< slimmer cheekbones */
    LUMINA_JAW,                  /**< slimmer jaw */
    LUMINA_NOSE,                 /**< nose width, 0.5 = unchanged, lower = slimmer */
    LUMINA_FOREHEAD,             /**< forehead height, 0.5 = unchanged */
    LUMINA_SHRINK_HEAD,          /**< smaller head */
    LUMINA_WHITEN_TEETH,         /**< whiter teeth */
    LUMINA_REMOVE_POUCH,         /**< soften dark circles */
    LUMINA_REMOVE_SMILE_FOLDS,   /**< soften smile lines */

    /* picture quality, 0.5 = unchanged */
    LUMINA_TEMPERATURE,
    LUMINA_TINT,
    LUMINA_SATURATION,
    LUMINA_BRIGHTNESS,
    LUMINA_CONTRAST,

    /* color filter: LUMINA_FILTER is an index from lumina_filter, LUMINA_FILTER_INTENSITY in [0, 1] */
    LUMINA_FILTER,
    LUMINA_FILTER_INTENSITY,

    /* style makeup: LUMINA_STYLE is an index from lumina_style; color and makeup strengths are separate */
    LUMINA_STYLE,
    LUMINA_STYLE_COLOR,
    LUMINA_STYLE_MAKEUP,

    /* background: LUMINA_BACKGROUND is a value from lumina_background */
    LUMINA_BACKGROUND,

    /* fun: LUMINA_AVATAR from lumina_avatar.
       LUMINA_HEADWEAR is the older single accessory list (lumina_headwear); setting it fills LUMINA_ACC_HEAD,
       or LUMINA_ACC_EYES for the sunglasses. New code should use the LUMINA_ACC_* slots below. */
    LUMINA_AVATAR,
    LUMINA_HEADWEAR,

    /* accessories, one item per slot, all slots combine freely; 0 = none */
    LUMINA_ACC_HEAD,    /**< lumina_acc_head */
    LUMINA_ACC_EYES,    /**< lumina_acc_eyes */
    LUMINA_ACC_EARS,    /**< lumina_acc_ears */
    LUMINA_ACC_NECK,    /**< lumina_acc_neck */
    LUMINA_ACC_MOUTH,   /**< lumina_acc_mouth */

    LUMINA_PARAM_COUNT
} lumina_param;

typedef enum lumina_filter {
    LUMINA_FILTER_NONE = 0,
    LUMINA_FILTER_SUNNY, LUMINA_FILTER_FILM, LUMINA_FILTER_OXYGEN, LUMINA_FILTER_BELLFLOWER,
    LUMINA_FILTER_FAIR, LUMINA_FILTER_GENTLE, LUMINA_FILTER_FIRST_SIGHT, LUMINA_FILTER_GRAY,
    LUMINA_FILTER_ISLAND, LUMINA_FILTER_SUMMER, LUMINA_FILTER_CLEAR, LUMINA_FILTER_ICY,
    LUMINA_FILTER_NATURAL, LUMINA_FILTER_GLOW, LUMINA_FILTER_PORCELAIN,
    LUMINA_FILTER_COUNT
} lumina_filter;

typedef enum lumina_style {
    LUMINA_STYLE_NONE = 0,
    LUMINA_STYLE_FAIR, LUMINA_STYLE_KOREAN, LUMINA_STYLE_ENERGETIC, LUMINA_STYLE_WARM,
    LUMINA_STYLE_WESTERN, LUMINA_STYLE_ELEGANT, LUMINA_STYLE_SWEET, LUMINA_STYLE_DEEP,
    LUMINA_STYLE_BARE_FACE, LUMINA_STYLE_SENIOR, LUMINA_STYLE_JUNIOR,
    LUMINA_STYLE_COUNT
} lumina_style;

typedef enum lumina_background {
    LUMINA_BACKGROUND_NONE = 0,
    LUMINA_BACKGROUND_BLUR = 1,
    LUMINA_BACKGROUND_IMAGE = 2   /**< set the image with lumina_set_background_image* */
} lumina_background;

typedef enum lumina_avatar {
    LUMINA_AVATAR_NONE = 0,
    LUMINA_AVATAR_PIG, LUMINA_AVATAR_BUNNY, LUMINA_AVATAR_CAT, LUMINA_AVATAR_BEAR,
    LUMINA_AVATAR_PANDA, LUMINA_AVATAR_FROG,
    LUMINA_AVATAR_COUNT
} lumina_avatar;

typedef enum lumina_headwear {
    LUMINA_HEADWEAR_NONE = 0,
    LUMINA_HEADWEAR_CAT_EARS, LUMINA_HEADWEAR_BUNNY_EARS, LUMINA_HEADWEAR_CROWN, LUMINA_HEADWEAR_FLOWERS,
    LUMINA_HEADWEAR_HALO, LUMINA_HEADWEAR_HORNS, LUMINA_HEADWEAR_PARTY_HAT, LUMINA_HEADWEAR_SUNGLASSES,
    LUMINA_HEADWEAR_COUNT
} lumina_headwear;

typedef enum lumina_acc_head {
    LUMINA_ACC_HEAD_NONE = 0,
    LUMINA_ACC_HEAD_CAT_EARS, LUMINA_ACC_HEAD_BUNNY_EARS, LUMINA_ACC_HEAD_CROWN, LUMINA_ACC_HEAD_FLOWERS,
    LUMINA_ACC_HEAD_HALO, LUMINA_ACC_HEAD_HORNS, LUMINA_ACC_HEAD_PARTY_HAT, LUMINA_ACC_HEAD_SANTA_HAT,
    LUMINA_ACC_HEAD_COUNT
} lumina_acc_head;

typedef enum lumina_acc_eyes {
    LUMINA_ACC_EYES_NONE = 0,
    LUMINA_ACC_EYES_SUNGLASSES, LUMINA_ACC_EYES_ROUND_GLASSES, LUMINA_ACC_EYES_THICK_FRAMES,
    LUMINA_ACC_EYES_HEART_GLASSES, LUMINA_ACC_EYES_EYE_PATCH,
    LUMINA_ACC_EYES_COUNT
} lumina_acc_eyes;

typedef enum lumina_acc_ears {
    LUMINA_ACC_EARS_NONE = 0,
    LUMINA_ACC_EARS_GOLD_HOOPS, LUMINA_ACC_EARS_PEARL_STUDS, LUMINA_ACC_EARS_TASSELS, LUMINA_ACC_EARS_CHERRIES,
    LUMINA_ACC_EARS_COUNT
} lumina_acc_ears;

typedef enum lumina_acc_neck {
    LUMINA_ACC_NECK_NONE = 0,
    LUMINA_ACC_NECK_GOLD_CHAIN, LUMINA_ACC_NECK_PEARLS, LUMINA_ACC_NECK_KHATA, LUMINA_ACC_NECK_BOW_TIE,
    LUMINA_ACC_NECK_COUNT
} lumina_acc_neck;

typedef enum lumina_acc_mouth {
    LUMINA_ACC_MOUTH_NONE = 0,
    LUMINA_ACC_MOUTH_CIGAR, LUMINA_ACC_MOUTH_CIGARETTE, LUMINA_ACC_MOUTH_LOLLIPOP, LUMINA_ACC_MOUTH_ROSE,
    LUMINA_ACC_MOUTH_COUNT
} lumina_acc_mouth;

typedef enum lumina_log_level { LUMINA_LOG_INFO = 0, LUMINA_LOG_WARN = 1, LUMINA_LOG_ERROR = 2 } lumina_log_level;

typedef void (*lumina_log_callback)(lumina_log_level level, const char* message, void* user);

/** Route the engine's log messages. Without a callback, warnings and errors go to stderr. */
LUMINA_API void lumina_set_log_callback(lumina_log_callback callback, void* user);

/**
 * Create an engine.
 * @param model_dir directory holding the .onnx models (with trailing separator or not).
 *                  Face effects and virtual background are disabled when models cannot be loaded.
 * @return NULL when the GPU context cannot be created.
 */
LUMINA_API lumina_engine* lumina_create(const char* model_dir);
LUMINA_API void lumina_destroy(lumina_engine* engine);

/** Set / get a parameter. Safe to call from any thread. */
LUMINA_API void lumina_set_param(lumina_engine* engine, lumina_param param, float value);
LUMINA_API float lumina_get_param(lumina_engine* engine, lumina_param param);

/**
 * Color space of the frames passed to the process functions; output uses the same.
 * @param matrix     601 (BT.601, the default) or 709 (BT.709, typical for HD webcams)
 * @param full_range 1 for full range (0-255, the default), 0 for limited range (Y 16-235, UV 16-240)
 */
LUMINA_API void lumina_set_color_space(lumina_engine* engine, int matrix, int full_range);

/** Background image for LUMINA_BACKGROUND_IMAGE: an image file (png/jpg/bmp), or raw RGBA rows top to bottom. */
LUMINA_API int lumina_set_background_image(lumina_engine* engine, const char* path_utf8);
LUMINA_API int lumina_set_background_rgba(lumina_engine* engine, const uint8_t* rgba, int width, int height);

/**
 * Use a look file (a style: color grade and/or makeup, see looks/README.md) instead of the built-in style
 * chosen with LUMINA_STYLE. LUMINA_STYLE_COLOR and LUMINA_STYLE_MAKEUP still control how strongly it applies.
 * Pass NULL or "" to go back to the built-in style.
 * @return 1 on success (or when cleared); 0 when the file cannot be read or parsed, the reason is logged and
 *         the previous look stays active.
 */
LUMINA_API int lumina_set_look_file(lumina_engine* engine, const char* path_utf8);
/** Same as lumina_set_look_file with the JSON text itself, for apps that ship looks in their own resources. */
LUMINA_API int lumina_set_look_json(lumina_engine* engine, const char* json_utf8);

/**
 * Process one I420 frame (Y plane, then U and V at half resolution, tightly packed).
 * Width and height must be even. in and out may point to the same buffer.
 * Call all process functions of one engine from the same thread.
 * @return 1 on success, 0 on failure (out is then left untouched).
 */
LUMINA_API int lumina_process_i420(lumina_engine* engine, int width, int height, const uint8_t* in, uint8_t* out);

/** Same for planes with arbitrary strides. Output planes may be the input planes. */
LUMINA_API int lumina_process_i420_planes(lumina_engine* engine, int width, int height,
                                          const uint8_t* y, int y_stride, const uint8_t* u, int u_stride,
                                          const uint8_t* v, int v_stride,
                                          uint8_t* out_y, int out_y_stride, uint8_t* out_u, int out_u_stride,
                                          uint8_t* out_v, int out_v_stride);

/** NV12: Y plane then interleaved UV plane. */
LUMINA_API int lumina_process_nv12(lumina_engine* engine, int width, int height,
                                   const uint8_t* y, int y_stride, const uint8_t* uv, int uv_stride,
                                   uint8_t* out_y, int out_y_stride, uint8_t* out_uv, int out_uv_stride);

#ifdef __cplusplus
}
#endif

#endif /* LUMINA_BEAUTY_H */
