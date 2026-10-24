#ifndef LUMINA_GL_CONTEXT_HPP
#define LUMINA_GL_CONTEXT_HPP

/**
 * @brief 离屏 OpenGL 3.3 Core 上下文
 * @remark Windows 下用 WGL，挂在一个不显示的窗口上。同一时刻只能在一个线程上设为当前。
 */
class LmGLContext {
public:
    LmGLContext();
    ~LmGLContext();

    bool Init();
    void Release();

    bool MakeCurrent();
    void DoneCurrent();

private:
    struct Env;
    Env* m_env = nullptr;
};

#endif // LUMINA_GL_CONTEXT_HPP
