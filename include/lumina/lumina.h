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

    /* fun: LUMINA_AVATAR from lumina_avatar, LUMINA_HEADWEAR from lumina_headwear */
    LUMINA_AVATAR,
    LUMINA_HEADWEAR,

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

/** Background image for LUMINA_BACKGROUND_IMAGE: an image file (png/jpg/bmp), or raw RGBA rows top to bottom. */
LUMINA_API int lumina_set_background_image(lumina_engine* engine, const char* path_utf8);
LUMINA_API int lumina_set_background_rgba(lumina_engine* engine, const uint8_t* rgba, int width, int height);

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
