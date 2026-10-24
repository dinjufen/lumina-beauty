#include "LmTuning.hpp"

#include <cstdlib>
#include <fstream>
#include <map>
#include "LmLog.hpp"

namespace {

LmTuning g_tuning;

std::map<std::string, float*> Fields() {
    return {
        {"smooth_edge", &g_tuning.smoothEdge},
        {"smooth_keep", &g_tuning.smoothKeep},
        {"smooth_protect", &g_tuning.smoothProtect},
        {"smooth_grain", &g_tuning.smoothGrain},
        {"smooth_curve", &g_tuning.smoothCurve},
        {"smooth_threshold", &g_tuning.smoothThreshold},
        {"smooth_lift", &g_tuning.smoothLift},
        {"whiten_gain", &g_tuning.whitenGain},
        {"whiten_beta", &g_tuning.whitenBeta},
        {"whiten_hue_keep", &g_tuning.whitenHueKeep},
        {"whiten_rosy", &g_tuning.whitenRosy},
        {"whiten_global", &g_tuning.whitenGlobal},
        {"sharpen_gain", &g_tuning.sharpenGain},
        {"clarity_gain", &g_tuning.clarityGain},
        {"region_blur_mix", &g_tuning.regionBlurMix},
        {"pouch_lift", &g_tuning.pouchLift},
        {"folds_lift", &g_tuning.foldsLift},
        {"quality_temperature", &g_tuning.qualityTemperature},
        {"quality_tint", &g_tuning.qualityTint},
        {"quality_saturation", &g_tuning.qualitySaturation},
        {"quality_light", &g_tuning.qualityLight},
        {"quality_light_down", &g_tuning.qualityLightDown},
        {"quality_contrast", &g_tuning.qualityContrast},
        {"preset_gain", &g_tuning.presetGain},
        {"reshape_overall", &g_tuning.reshapeOverall},
        {"reshape_face_cut", &g_tuning.reshapeFaceCut},
        {"reshape_cheek", &g_tuning.reshapeCheek},
        {"reshape_jaw", &g_tuning.reshapeJaw},
        {"reshape_nose", &g_tuning.reshapeNose},
        {"reshape_forehead", &g_tuning.reshapeForehead},
        {"reshape_eye", &g_tuning.reshapeEye},
        {"reshape_shrink_head", &g_tuning.reshapeShrinkHead},
        {"mask_guide_radius", &g_tuning.maskGuideRadius},
        {"mask_guide_eps", &g_tuning.maskGuideEps},
        {"background_blur_step", &g_tuning.backgroundBlurStep},
    };
}

std::string Trim(const std::string& s) {
    const size_t begin = s.find_first_not_of(" \t\r");
    const size_t end = s.find_last_not_of(" \t\r");
    return begin == std::string::npos ? std::string() : s.substr(begin, end - begin + 1);
}

} // namespace

const LmTuning& LmGetTuning() {
    return g_tuning;
}

int LmLoadTuning(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        LM_LOG_E << "Failed to open tuning file " << path;
        return -1;
    }

    const auto fields = Fields();
    int count = 0;
    std::string line;
    while (std::getline(file, line)) {
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) {
            continue;
        }
        const auto field = fields.find(Trim(line.substr(0, eq)));
        if (field == fields.end()) {
            LM_LOG_E << "Unknown tuning key: " << Trim(line.substr(0, eq));
            continue;
        }
        *field->second = static_cast<float>(atof(Trim(line.substr(eq + 1)).c_str()));
        ++count;
    }
    LM_LOG_I << "loaded " << count << " tuning values from " << path;
    return count;
}
