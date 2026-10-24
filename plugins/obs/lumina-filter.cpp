// OBS Studio video filter: face beauty, color filters, style makeup, virtual background, cartoon avatars
// and headwear for any asynchronous video source (webcams, capture cards, media sources).
//
// Copyright (c) 2026 Lumina Beauty contributors.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <util/platform.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "lumina/lumina.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("lumina-beauty", "en-US")

namespace {

struct Slider {
    const char* key;       // settings key, also the locale key
    lumina_param param;
    int defaultValue;      // 0..100
};

// 0..100 的滑杆，写入时除以 100
const Slider kSkin[] = {
    {"smooth", LUMINA_SMOOTH, 50},       {"whiten", LUMINA_WHITEN, 30},  {"sharpen", LUMINA_SHARPEN, 0},
    {"clarity", LUMINA_CLARITY, 0},      {"teeth", LUMINA_WHITEN_TEETH, 0},
    {"pouch", LUMINA_REMOVE_POUCH, 0},   {"folds", LUMINA_REMOVE_SMILE_FOLDS, 0},
};
const Slider kShape[] = {
    {"eye", LUMINA_EYE_SIZE, 0},         {"slim", LUMINA_FACE_SLIM, 0},     {"narrow", LUMINA_FACE_NARROW, 0},
    {"cheekbone", LUMINA_CHEEKBONE, 0},  {"jaw", LUMINA_JAW, 0},            {"nose", LUMINA_NOSE, 50},
    {"forehead", LUMINA_FOREHEAD, 50},   {"shrink_head", LUMINA_SHRINK_HEAD, 0},
};
const Slider kQuality[] = {
    {"temperature", LUMINA_TEMPERATURE, 50}, {"tint", LUMINA_TINT, 50},         {"saturation", LUMINA_SATURATION, 50},
    {"brightness", LUMINA_BRIGHTNESS, 50},   {"contrast", LUMINA_CONTRAST, 50},
};

const char* kFilterNames[] = {"Filter.None",  "Filter.Sunny",  "Filter.Film",       "Filter.Oxygen",
                              "Filter.Bellflower", "Filter.Fair", "Filter.Gentle", "Filter.FirstSight",
                              "Filter.Gray",  "Filter.Island", "Filter.Summer",     "Filter.Clear",
                              "Filter.Icy",   "Filter.Natural", "Filter.Glow",      "Filter.Porcelain"};
const char* kStyleNames[] = {"Style.None",    "Style.Fair",    "Style.Korean",   "Style.Energetic",
                             "Style.Warm",    "Style.Western", "Style.Elegant",  "Style.Sweet",
                             "Style.Deep",    "Style.BareFace", "Style.Senior",  "Style.Junior"};
const char* kAvatarNames[] = {"Avatar.None", "Avatar.Pig", "Avatar.Bunny", "Avatar.Cat",
                              "Avatar.Bear", "Avatar.Panda", "Avatar.Frog"};
const char* kHeadwearNames[] = {"Headwear.None",  "Headwear.CatEars", "Headwear.BunnyEars", "Headwear.Crown",
                                "Headwear.Flowers", "Headwear.Halo",  "Headwear.Horns",     "Headwear.PartyHat",
                                "Headwear.Sunglasses"};
const char* kBackgroundNames[] = {"Background.None", "Background.Blur", "Background.Image"};

struct Filter {
    obs_source_t* source = nullptr;
    lumina_engine* engine = nullptr;
    std::string backgroundPath;
    bool warnedFormat = false;
    // YUY2 等打包格式转成 I420 处理
    std::vector<uint8_t> planar;
};

void LogFromEngine(lumina_log_level level, const char* message, void*) {
    blog(level == LUMINA_LOG_ERROR ? LOG_ERROR : (level == LUMINA_LOG_WARN ? LOG_WARNING : LOG_INFO), "%s", message);
}

void AddSliders(obs_properties_t* group, const Slider* sliders, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        obs_properties_add_int_slider(group, sliders[i].key, obs_module_text(sliders[i].key), 0, 100, 1);
    }
}

void AddList(obs_properties_t* props, const char* key, const char* const* names, size_t count) {
    obs_property_t* list = obs_properties_add_list(props, key, obs_module_text(key), OBS_COMBO_TYPE_LIST,
                                                   OBS_COMBO_FORMAT_INT);
    for (size_t i = 0; i < count; ++i) {
        obs_property_list_add_int(list, obs_module_text(names[i]), static_cast<long long>(i));
    }
}

void ApplySliders(Filter* f, obs_data_t* settings, const Slider* sliders, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        lumina_set_param(f->engine, sliders[i].param,
                         static_cast<float>(obs_data_get_int(settings, sliders[i].key)) / 100.0f);
    }
}

const char* GetName(void*) {
    return obs_module_text("LuminaBeauty");
}

void Update(void* data, obs_data_t* settings) {
    Filter* f = static_cast<Filter*>(data);
    if (!f->engine) {
        return;
    }
    ApplySliders(f, settings, kSkin, sizeof(kSkin) / sizeof(kSkin[0]));
    ApplySliders(f, settings, kShape, sizeof(kShape) / sizeof(kShape[0]));
    ApplySliders(f, settings, kQuality, sizeof(kQuality) / sizeof(kQuality[0]));
    lumina_set_param(f->engine, LUMINA_FILTER, static_cast<float>(obs_data_get_int(settings, "filter")));
    lumina_set_param(f->engine, LUMINA_FILTER_INTENSITY, obs_data_get_int(settings, "filter_intensity") / 100.0f);
    lumina_set_param(f->engine, LUMINA_STYLE, static_cast<float>(obs_data_get_int(settings, "style")));
    lumina_set_param(f->engine, LUMINA_STYLE_COLOR, obs_data_get_int(settings, "style_color") / 100.0f);
    lumina_set_param(f->engine, LUMINA_STYLE_MAKEUP, obs_data_get_int(settings, "style_makeup") / 100.0f);
    lumina_set_param(f->engine, LUMINA_AVATAR, static_cast<float>(obs_data_get_int(settings, "avatar")));
    lumina_set_param(f->engine, LUMINA_HEADWEAR, static_cast<float>(obs_data_get_int(settings, "headwear")));
    lumina_set_param(f->engine, LUMINA_BACKGROUND, static_cast<float>(obs_data_get_int(settings, "background")));

    const char* path = obs_data_get_string(settings, "background_image");
    const std::string newPath = path ? path : "";
    if (newPath != f->backgroundPath) {
        f->backgroundPath = newPath;
        lumina_set_background_image(f->engine, newPath.c_str());
    }
}

void* Create(obs_data_t* settings, obs_source_t* source) {
    Filter* f = new Filter();
    f->source = source;
    // LUMINA_MODEL_DIR 可以覆盖模型目录，便于开发和测试
    const char* overrideDir = getenv("LUMINA_MODEL_DIR");
    char* models = overrideDir ? nullptr : obs_module_file("models");
    f->engine = lumina_create(overrideDir ? overrideDir : (models ? models : ""));
    bfree(models);
    if (!f->engine) {
        blog(LOG_ERROR, "[lumina] failed to create the engine");
    }
    Update(f, settings);
    return f;
}

void Destroy(void* data) {
    Filter* f = static_cast<Filter*>(data);
    lumina_destroy(f->engine);
    delete f;
}

void Defaults(obs_data_t* settings) {
    for (const Slider& s : kSkin) obs_data_set_default_int(settings, s.key, s.defaultValue);
    for (const Slider& s : kShape) obs_data_set_default_int(settings, s.key, s.defaultValue);
    for (const Slider& s : kQuality) obs_data_set_default_int(settings, s.key, s.defaultValue);
    obs_data_set_default_int(settings, "filter", 0);
    obs_data_set_default_int(settings, "filter_intensity", 80);
    obs_data_set_default_int(settings, "style", 0);
    obs_data_set_default_int(settings, "style_color", 80);
    obs_data_set_default_int(settings, "style_makeup", 80);
    obs_data_set_default_int(settings, "background", 0);
    obs_data_set_default_int(settings, "avatar", 0);
    obs_data_set_default_int(settings, "headwear", 0);
}

obs_properties_t* Properties(void*) {
    obs_properties_t* props = obs_properties_create();

    obs_properties_t* skin = obs_properties_create();
    AddSliders(skin, kSkin, sizeof(kSkin) / sizeof(kSkin[0]));
    obs_properties_add_group(props, "group_skin", obs_module_text("Group.Skin"), OBS_GROUP_NORMAL, skin);

    obs_properties_t* shape = obs_properties_create();
    AddSliders(shape, kShape, sizeof(kShape) / sizeof(kShape[0]));
    obs_properties_add_group(props, "group_shape", obs_module_text("Group.Shape"), OBS_GROUP_NORMAL, shape);

    obs_properties_t* color = obs_properties_create();
    AddList(color, "filter", kFilterNames, sizeof(kFilterNames) / sizeof(kFilterNames[0]));
    obs_properties_add_int_slider(color, "filter_intensity", obs_module_text("filter_intensity"), 0, 100, 1);
    AddList(color, "style", kStyleNames, sizeof(kStyleNames) / sizeof(kStyleNames[0]));
    obs_properties_add_int_slider(color, "style_color", obs_module_text("style_color"), 0, 100, 1);
    obs_properties_add_int_slider(color, "style_makeup", obs_module_text("style_makeup"), 0, 100, 1);
    AddSliders(color, kQuality, sizeof(kQuality) / sizeof(kQuality[0]));
    obs_properties_add_group(props, "group_color", obs_module_text("Group.Color"), OBS_GROUP_NORMAL, color);

    obs_properties_t* fun = obs_properties_create();
    AddList(fun, "background", kBackgroundNames, sizeof(kBackgroundNames) / sizeof(kBackgroundNames[0]));
    obs_properties_add_path(fun, "background_image", obs_module_text("background_image"), OBS_PATH_FILE,
                            "Images (*.png *.jpg *.jpeg *.bmp)", nullptr);
    AddList(fun, "avatar", kAvatarNames, sizeof(kAvatarNames) / sizeof(kAvatarNames[0]));
    AddList(fun, "headwear", kHeadwearNames, sizeof(kHeadwearNames) / sizeof(kHeadwearNames[0]));
    obs_properties_add_group(props, "group_fun", obs_module_text("Group.Fun"), OBS_GROUP_NORMAL, fun);
    return props;
}

// 打包的 4:2:2 格式（YUY2 / UYVY）与 I420 之间转换，色度在竖直方向取两行平均
void PackedToI420(const obs_source_frame* frame, bool uyvy, uint8_t* y, uint8_t* u, uint8_t* v) {
    const int w = static_cast<int>(frame->width);
    const int h = static_cast<int>(frame->height);
    const int yo = uyvy ? 1 : 0;
    const int co = uyvy ? 0 : 1;
    for (int r = 0; r < h; ++r) {
        const uint8_t* row = frame->data[0] + static_cast<size_t>(r) * frame->linesize[0];
        for (int c = 0; c < w; ++c) {
            y[static_cast<size_t>(r) * w + c] = row[c * 2 + yo];
        }
    }
    for (int r = 0; r < h / 2; ++r) {
        const uint8_t* a = frame->data[0] + static_cast<size_t>(r * 2) * frame->linesize[0];
        const uint8_t* b = a + frame->linesize[0];
        for (int c = 0; c < w / 2; ++c) {
            u[static_cast<size_t>(r) * (w / 2) + c] = static_cast<uint8_t>((a[c * 4 + co] + b[c * 4 + co] + 1) / 2);
            v[static_cast<size_t>(r) * (w / 2) + c] = static_cast<uint8_t>((a[c * 4 + co + 2] + b[c * 4 + co + 2] + 1) / 2);
        }
    }
}

void I420ToPacked(const uint8_t* y, const uint8_t* u, const uint8_t* v, bool uyvy, obs_source_frame* frame) {
    const int w = static_cast<int>(frame->width);
    const int h = static_cast<int>(frame->height);
    const int yo = uyvy ? 1 : 0;
    const int co = uyvy ? 0 : 1;
    for (int r = 0; r < h; ++r) {
        uint8_t* row = frame->data[0] + static_cast<size_t>(r) * frame->linesize[0];
        const size_t cr = static_cast<size_t>(r / 2) * (w / 2);
        for (int c = 0; c < w; ++c) {
            row[c * 2 + yo] = y[static_cast<size_t>(r) * w + c];
        }
        for (int c = 0; c < w / 2; ++c) {
            row[c * 4 + co] = u[cr + c];
            row[c * 4 + co + 2] = v[cr + c];
        }
    }
}

// obs_source_frame 只带 YUV 到 RGB 的矩阵。Cr 到 R 的系数除以亮度系数，消去范围的影响后：
// BT.709 约为 1.57（有限范围 1.54），BT.601 约为 1.40（有限范围 1.37）
bool IsBt709(const obs_source_frame* frame) {
    const float yScale = frame->color_matrix[0];
    return yScale > 0.0f && frame->color_matrix[2] / yScale > 1.47f;
}

obs_source_frame* FilterVideo(void* data, obs_source_frame* frame) {
    Filter* f = static_cast<Filter*>(data);
    if (!f->engine || !frame || (frame->width & 1) || (frame->height & 1)) {
        return frame;
    }
    const int w = static_cast<int>(frame->width);
    const int h = static_cast<int>(frame->height);
    // 按画面实际的色彩空间转换，摄像头常见的是 BT.709 有限范围
    lumina_set_color_space(f->engine, IsBt709(frame) ? 709 : 601, frame->full_range ? 1 : 0);
    switch (frame->format) {
        case VIDEO_FORMAT_I420:
            lumina_process_i420_planes(f->engine, w, h, frame->data[0], frame->linesize[0], frame->data[1],
                                       frame->linesize[1], frame->data[2], frame->linesize[2], frame->data[0],
                                       frame->linesize[0], frame->data[1], frame->linesize[1], frame->data[2],
                                       frame->linesize[2]);
            break;
        case VIDEO_FORMAT_NV12:
            lumina_process_nv12(f->engine, w, h, frame->data[0], frame->linesize[0], frame->data[1],
                                frame->linesize[1], frame->data[0], frame->linesize[0], frame->data[1],
                                frame->linesize[1]);
            break;
        case VIDEO_FORMAT_YUY2:
        case VIDEO_FORMAT_UYVY: {
            const bool uyvy = frame->format == VIDEO_FORMAT_UYVY;
            const size_t ySize = static_cast<size_t>(w) * h;
            const size_t cSize = static_cast<size_t>(w / 2) * (h / 2);
            f->planar.resize(ySize + cSize * 2);
            uint8_t* y = f->planar.data();
            PackedToI420(frame, uyvy, y, y + ySize, y + ySize + cSize);
            if (lumina_process_i420(f->engine, w, h, y, y)) {
                I420ToPacked(y, y + ySize, y + ySize + cSize, uyvy, frame);
            }
            break;
        }
        default:
            if (!f->warnedFormat) {
                blog(LOG_WARNING, "[lumina] video format %d is not supported yet, frames pass through unchanged",
                     static_cast<int>(frame->format));
                f->warnedFormat = true;
            }
            break;
    }
    return frame;
}

} // namespace

extern "C" bool obs_module_load(void) {
    lumina_set_log_callback(LogFromEngine, nullptr);

    static obs_source_info info = {};
    info.id = "lumina_beauty_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC;
    info.get_name = GetName;
    info.create = Create;
    info.destroy = Destroy;
    info.update = Update;
    info.get_defaults = Defaults;
    info.get_properties = Properties;
    info.filter_video = FilterVideo;
    obs_register_source(&info);
    blog(LOG_INFO, "[lumina] plugin loaded");
    return true;
}
