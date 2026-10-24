#ifndef LUMINA_AVATAR_HPP
#define LUMINA_AVATAR_HPP

#include "LmFace.hpp"

/// 卡通头像的种类，0 为不使用。与 lumina_avatar 一致
enum LmAvatarKind {
    LM_AVATAR_NONE = 0,
    LM_AVATAR_PIG = 1,     ///< 小猪
    LM_AVATAR_RABBIT = 2,  ///< 小兔
    LM_AVATAR_CAT = 3,     ///< 小猫
    LM_AVATAR_BEAR = 4,    ///< 小熊
    LM_AVATAR_PANDA = 5,   ///< 熊猫
    LM_AVATAR_FROG = 6,    ///< 青蛙
    LM_AVATAR_COUNT
};

/// 头饰的种类，0 为不使用。与 lumina_headwear 一致
enum LmHeadwearKind {
    LM_HEADWEAR_NONE = 0,
    LM_HEADWEAR_CAT_EARS = 1,      ///< 猫耳
    LM_HEADWEAR_BUNNY_EARS = 2,    ///< 兔耳
    LM_HEADWEAR_CROWN = 3,         ///< 皇冠
    LM_HEADWEAR_FLOWERS = 4,       ///< 花环
    LM_HEADWEAR_HALO = 5,          ///< 光环
    LM_HEADWEAR_HORNS = 6,         ///< 小恶魔角
    LM_HEADWEAR_PARTY_HAT = 7,     ///< 派对帽
    LM_HEADWEAR_SUNGLASSES = 8,    ///< 墨镜
    LM_HEADWEAR_COUNT
};

/**
 * @brief 一张脸的姿态与表情，用来驱动卡通头像和头饰
 * @remark 局部坐标系：原点在两眼中点，x 轴沿两眼连线指向画面右侧，y 轴垂直向上（指向额头），
 *         1 个单位为半个脸宽，(ux, uy) 是 x 轴在画面中的单位方向，scale 是 1 个单位对应的像素数。
 */
struct LmAvatarFace {
    float cx = 0.0f;
    float cy = 0.0f;
    float ux = 1.0f;
    float uy = 0.0f;
    float scale = 1.0f;
    float yaw = 0.0f;        ///< 左右转头，约 [-1, 1]，正值为朝画面右侧转
    float pitch = 0.0f;      ///< 抬头低头，约 [-1, 1]，正值为抬头
    float eyeLeft = 1.0f;    ///< 画面左侧眼睛的睁开程度 [0, 1]
    float eyeRight = 1.0f;   ///< 画面右侧眼睛的睁开程度 [0, 1]
    float mouthOpen = 0.0f;  ///< 张嘴程度 [0, 1]
    float smile = 0.0f;      ///< 微笑程度 [0, 1]
    float brow = 0.0f;       ///< 挑眉程度 [0, 1]
    float gazeX = 0.0f;      ///< 视线方向 [-1, 1]
    float gazeY = 0.0f;
    float top = 1.0f;        ///< 头顶（含头发）的 y 坐标
    float eyeHalf = 0.4f;    ///< 两眼间距的一半
    float mouthY = -0.8f;    ///< 嘴的 y 坐标
};

/**
 * @brief 表情的自适应基准
 * @remark 每个人眼睛大小、嘴型不同，睁眼程度等按各自的近期最大值归一化，同一张脸跨帧保留
 */
struct LmAvatarState {
    bool valid = false;
    float eyeMaxLeft = 0.0f;
    float eyeMaxRight = 0.0f;
    float browMean = 0.0f;
    float mouthWidthMean = 0.0f;
    LmAvatarFace last;
};

/**
 * @brief 由关键点计算姿态与表情，并做时域平滑
 */
void LmBuildAvatarFace(const LmFace& face, LmAvatarState& state, LmAvatarFace& out);

#endif // LUMINA_AVATAR_HPP
