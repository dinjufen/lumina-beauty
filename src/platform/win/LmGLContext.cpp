// Windows：用 WGL 创建 OpenGL 3.3 Core 上下文，挂在一个不显示的窗口上，所有渲染都走 FBO
#include "LmGLContext.hpp"

#include <Windows.h>
#include <mutex>
#include "LmGL.hpp"
#include "LmLog.hpp"

namespace {

const wchar_t* kWindowClass = L"LuminaBeautyGLWindow";

// WGL_ARB_create_context
const int kContextMajorVersion = 0x2091;
const int kContextMinorVersion = 0x2092;
const int kContextProfileMask = 0x9126;
const int kContextCoreProfileBit = 0x00000001;
typedef HGLRC(WINAPI* PFNCreateContextAttribs)(HDC, HGLRC, const int*);

HMODULE g_opengl32 = nullptr;

void* GetGLProc(const char* name) {
    void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
    // 1.1 的函数不能用 wglGetProcAddress 取，部分驱动对未知函数返回 1、2、3 或 -1
    const intptr_t v = reinterpret_cast<intptr_t>(p);
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) {
        p = g_opengl32 ? reinterpret_cast<void*>(GetProcAddress(g_opengl32, name)) : nullptr;
    }
    return p;
}

bool RegisterWindowClass() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kWindowClass;
        ok = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        g_opengl32 = LoadLibraryW(L"opengl32.dll");
    });
    return ok;
}

} // namespace

struct LmGLContext::Env {
    HWND window = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
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
    if (!RegisterWindowClass()) {
        LM_LOG_E << "RegisterClassEx failed, err = " << GetLastError();
        return false;
    }
    m_env->window = CreateWindowExW(0, kWindowClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
    if (!m_env->window) {
        LM_LOG_E << "CreateWindowEx failed, err = " << GetLastError();
        return false;
    }
    m_env->dc = GetDC(m_env->window);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(m_env->dc, &pfd);
    if (!format || !SetPixelFormat(m_env->dc, format, &pfd)) {
        LM_LOG_E << "SetPixelFormat failed, err = " << GetLastError();
        Release();
        return false;
    }

    // 先建一个旧式上下文，才能取到 wglCreateContextAttribsARB
    HGLRC legacy = wglCreateContext(m_env->dc);
    if (!legacy || !wglMakeCurrent(m_env->dc, legacy)) {
        LM_LOG_E << "wglCreateContext failed, err = " << GetLastError();
        if (legacy) {
            wglDeleteContext(legacy);
        }
        Release();
        return false;
    }
    auto createAttribs = reinterpret_cast<PFNCreateContextAttribs>(wglGetProcAddress("wglCreateContextAttribsARB"));
    if (createAttribs) {
        const int attribs[] = {kContextMajorVersion, 3, kContextMinorVersion, 3, kContextProfileMask,
                               kContextCoreProfileBit, 0};
        m_env->context = createAttribs(m_env->dc, nullptr, attribs);
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    if (!m_env->context) {
        LM_LOG_E << "OpenGL 3.3 core context is not supported by the graphics driver";
        Release();
        return false;
    }

    if (!wglMakeCurrent(m_env->dc, m_env->context) || !LmLoadGL(GetGLProc)) {
        LM_LOG_E << "Failed to load OpenGL functions";
        Release();
        return false;
    }
    wglMakeCurrent(nullptr, nullptr);
    LM_LOG_I << "OpenGL 3.3 core context created";
    return true;
}

void LmGLContext::Release() {
    if (!m_env) {
        return;
    }
    if (m_env->context) {
        if (wglGetCurrentContext() == m_env->context) {
            wglMakeCurrent(nullptr, nullptr);
        }
        wglDeleteContext(m_env->context);
        m_env->context = nullptr;
    }
    if (m_env->dc) {
        ReleaseDC(m_env->window, m_env->dc);
        m_env->dc = nullptr;
    }
    if (m_env->window) {
        DestroyWindow(m_env->window);
        m_env->window = nullptr;
    }
}

bool LmGLContext::MakeCurrent() {
    return m_env && m_env->context && wglMakeCurrent(m_env->dc, m_env->context) == TRUE;
}

void LmGLContext::DoneCurrent() {
    if (m_env && m_env->context && wglGetCurrentContext() == m_env->context) {
        wglMakeCurrent(nullptr, nullptr);
    }
}
