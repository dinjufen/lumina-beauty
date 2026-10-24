// macOS：用 CGL 创建离屏 OpenGL Core 上下文（系统提供 4.1 Core，兼容本库的 GLSL 3.30），所有渲染都走 FBO
#define GL_SILENCE_DEPRECATION
#include "LmGLContext.hpp"

#include <OpenGL/OpenGL.h>
#include <dlfcn.h>
#include "LmGL.hpp"
#include "LmLog.hpp"

namespace {

void* g_opengl = nullptr;

void* GetGLProc(const char* name) {
    return g_opengl ? dlsym(g_opengl, name) : nullptr;
}

} // namespace

struct LmGLContext::Env {
    CGLContextObj context = nullptr;
};

LmGLContext::LmGLContext() {
    m_env = new Env();
}

LmGLContext::~LmGLContext() {
    Release();
    delete m_env;
    m_env = nullptr;
}

bool LmGLContext::Init() {
    if (m_env->context) {
        return true;
    }
    if (!g_opengl) {
        g_opengl = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
    }

    // 允许离线显卡，笔记本上不强制切到独显
    const CGLPixelFormatAttribute attribs[] = {kCGLPFAOpenGLProfile,
                                               static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),
                                               kCGLPFAColorSize,
                                               static_cast<CGLPixelFormatAttribute>(24),
                                               kCGLPFAAlphaSize,
                                               static_cast<CGLPixelFormatAttribute>(8),
                                               kCGLPFAAllowOfflineRenderers,
                                               static_cast<CGLPixelFormatAttribute>(0)};
    CGLPixelFormatObj format = nullptr;
    GLint count = 0;
    if (CGLChoosePixelFormat(attribs, &format, &count) != kCGLNoError || !format) {
        LM_LOG_E << "CGLChoosePixelFormat failed";
        return false;
    }
    const CGLError err = CGLCreateContext(format, nullptr, &m_env->context);
    CGLReleasePixelFormat(format);
    if (err != kCGLNoError || !m_env->context) {
        LM_LOG_E << "CGLCreateContext failed, err = " << static_cast<int>(err);
        m_env->context = nullptr;
        return false;
    }
    if (CGLSetCurrentContext(m_env->context) != kCGLNoError || !LmLoadGL(GetGLProc)) {
        LM_LOG_E << "Failed to load OpenGL functions";
        Release();
        return false;
    }
    CGLSetCurrentContext(nullptr);
    LM_LOG_I << "OpenGL core context created";
    return true;
}

void LmGLContext::Release() {
    if (m_env && m_env->context) {
        if (CGLGetCurrentContext() == m_env->context) {
            CGLSetCurrentContext(nullptr);
        }
        CGLDestroyContext(m_env->context);
        m_env->context = nullptr;
    }
}

bool LmGLContext::MakeCurrent() {
    return m_env && m_env->context && CGLSetCurrentContext(m_env->context) == kCGLNoError;
}

void LmGLContext::DoneCurrent() {
    if (m_env && m_env->context && CGLGetCurrentContext() == m_env->context) {
        CGLSetCurrentContext(nullptr);
    }
}
