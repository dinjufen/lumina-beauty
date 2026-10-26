#ifndef LUMINA_LOOK_HPP
#define LUMINA_LOOK_HPP

#include <string>
#include "LmParams.hpp"

/**
 * @brief 一款妆容
 * @remark 颜色是正片叠底色，1 为不变，越小该通道压得越暗；opacity 是滑杆拉满时的浓度。
 */
struct LmMakeupLook {
    float lip[3] = {1.0f, 1.0f, 1.0f};
    float lipOpacity = 0.0f;
    float blush[3] = {1.0f, 1.0f, 1.0f};
    float blushOpacity = 0.0f;
    float blushSize = 0.17f;   ///< 腮红半径，相对脸宽
    float brow[3] = {1.0f, 1.0f, 1.0f};
    float browOpacity = 0.0f;
    float shadow[3] = {1.0f, 1.0f, 1.0f};
    float shadowOpacity = 0.0f;
    float contour[3] = {1.0f, 1.0f, 1.0f};
    float contourOpacity = 0.0f;
    float liner[3] = {0.22f, 0.18f, 0.18f};
    float linerOpacity = 0.0f;
    float lash[3] = {0.12f, 0.10f, 0.10f};
    float lashOpacity = 0.0f;
    float foundation = 0.0f;   ///< 粉底提亮量，只作用于脸部皮肤
};

/**
 * @brief 一个素材：调色和妆容，两部分都可以没有
 * @remark 内置的滤镜只有调色，风格妆两者都有；素材文件用同一结构描述，格式见 looks/README.md
 */
struct LmLook {
    std::string name;
    bool hasGrade = false;
    LmColorGrade grade;
    bool hasMakeup = false;
    LmMakeupLook makeup;
};

/// 内置滤镜（序号见 lumina_filter）与风格妆（序号见 lumina_style），未定义的序号返回 nullptr
const LmLook* LmBuiltinFilter(int filter);
const LmLook* LmBuiltinStyle(int style);

/**
 * @brief 从 JSON 文本读取素材
 * @param error 失败时写入原因
 */
bool LmParseLook(const std::string& json, LmLook& look, std::string& error);

/// 读取素材文件，路径为 UTF-8
bool LmLoadLookFile(const char* path, LmLook& look, std::string& error);

/// 把素材写成 JSON 文本，用于导出内置素材作为模板
std::string LmLookToJson(const LmLook& look);

#endif // LUMINA_LOOK_HPP
