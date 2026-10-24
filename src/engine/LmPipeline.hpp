#ifndef LUMINA_PIPELINE_HPP
#define LUMINA_PIPELINE_HPP

#include <cstdint>
#include <vector>
#include "LmAvatar.hpp"
#include "LmFace.hpp"
#include "LmMakeup.hpp"
#include "LmParams.hpp"
#include "LmSegmenter.hpp"

/**
 * @brief 美颜渲染管线
 * @remark I420 进、I420 出。所有方法必须在持有 GL 上下文的线程调用。
 *
 * 渲染顺序：
 *   1. I420 转 RGB
 *   2. 四分之一分辨率高斯模糊（清晰用，也给磨皮做保边判断）
 *   3. 半分辨率保边模糊（磨皮用）
 *   4. 合成：磨皮、白牙与黑眼圈法令纹的局部处理、美白、锐化、清晰、画质、滤镜
 *   5. 美型变形（有变形操作时），只作用在人像上
 *   6. 虚拟背景合成（有人像掩膜时），掩膜按同样的变形对齐人像，背景不变形
 *   7. RGB 转回 Y 平面与 UV 平面并回读
 */
class LmPipeline {
public:
    LmPipeline();
    ~LmPipeline();

    bool Init();
    void Release();

    /**
     * @brief 处理一帧
     * @param[in] mask 人像掩膜，LM_MASK_WIDTH x LM_MASK_HEIGHT，255 为人像；为空时不做虚拟背景
     */
    bool Render(int w, int h, const uint8_t* in, uint8_t* out, const LmParams& params,
                const std::vector<LmWarpOp>& warpOps, const std::vector<LmRegion>& regions,
                const std::vector<LmMakeupVertex>& makeup, const std::vector<LmAvatarFace>& avatars,
                const uint8_t* mask);

    /**
     * @brief 设置背景图片
     * @param[in] rgba 像素数据，逐行从上到下；传空指针清除
     */
    void SetBackgroundImage(const uint8_t* rgba, int w, int h);
    bool HasBackgroundImage() const { return m_texBackground != 0; }

private:
    struct Target {
        unsigned int tex = 0;
        unsigned int fbo = 0;
        int w = 0;
        int h = 0;
    };

    bool Resize(int w, int h);
    void DestroyTargets();
    bool CreateTarget(Target& target, int w, int h, bool singleChannel);
    bool CreateTarget(Target& target, int w, int h, unsigned int internalFormat, unsigned int format, unsigned int type);
    void DestroyTarget(Target& target);
    void DrawTo(const Target& target);
    void BindTexture(int unit, unsigned int tex);
    void DrawMesh(unsigned int source, const Target& target);
    /// 用画面引导细化人像掩膜，系数结果在 m_guideB；返回 false 时用原掩膜
    bool RefineMask(unsigned int imageTex, unsigned int maskTex);

private:
    bool m_bInit = false;
    int m_width = 0;
    int m_height = 0;

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;

    // 美型变形用的网格
    unsigned int m_meshVao = 0;
    unsigned int m_meshVbo = 0;
    unsigned int m_meshIbo = 0;
    unsigned int m_makeupVao = 0;
    unsigned int m_makeupVbo = 0;
    int m_meshIndexCount = 0;
    std::vector<float> m_meshVertices;

    // 输入的三个平面纹理
    unsigned int m_texY = 0;
    unsigned int m_texU = 0;
    unsigned int m_texV = 0;

    Target m_rgb;        ///< 原图 RGB，全分辨率
    Target m_halfA;      ///< 保边模糊中间结果，半分辨率
    Target m_halfB;      ///< 保边模糊结果，半分辨率
    Target m_makeupA;    ///< 美妆图层，全分辨率，预乘 alpha
    Target m_makeupB;    ///< 美妆图层模糊的中间结果
    Target m_quarterA;   ///< 高斯模糊中间结果，四分之一分辨率
    Target m_quarterB;   ///< 高斯模糊结果，四分之一分辨率
    Target m_final;      ///< 合成结果，全分辨率
    Target m_bgBlurA;    ///< 背景虚化中间结果，四分之一分辨率
    Target m_bgBlurB;    ///< 背景虚化结果，四分之一分辨率
    Target m_composited; ///< 虚拟背景合成结果，全分辨率
    Target m_warped;     ///< 美型变形结果，全分辨率
    Target m_avatarOut;  ///< 叠加卡通头像与头饰后的结果，全分辨率
    Target m_maskWarped; ///< 按美型变形后的人像掩膜，掩膜分辨率
    Target m_outY;       ///< 输出 Y 平面
    Target m_outUV;      ///< 输出 U、V 平面，上半 U 下半 V
    Target m_guideA;     ///< 掩膜细化的中间结果，四分之一分辨率，浮点
    Target m_guideB;     ///< 掩膜细化的系数结果
    Target m_guideC;     ///< 掩膜细化的中间结果
    bool m_guideFailed = false; ///< 不支持浮点渲染目标，掩膜细化不可用

    unsigned int m_progYuv2Rgb = 0;
    unsigned int m_progSkinBlur = 0;
    unsigned int m_progGauss = 0;
    unsigned int m_progCompose = 0;
    unsigned int m_progComposite = 0;
    unsigned int m_progWarp = 0;
    unsigned int m_progGuidePack = 0;
    unsigned int m_progBox = 0;
    unsigned int m_progGuideCoef = 0;
    unsigned int m_progMakeup = 0;
    unsigned int m_progAvatar = 0;

    unsigned int m_texMask = 0;        ///< 人像掩膜
    unsigned int m_texBackground = 0;  ///< 背景图片
    unsigned int m_texEmpty = 0;       ///< 1x1 黑图，占位未使用的采样器
    int m_backgroundW = 0;
    int m_backgroundH = 0;
    unsigned int m_progRgb2Y = 0;
    unsigned int m_progRgb2UV = 0;
};

#endif // LUMINA_PIPELINE_HPP
