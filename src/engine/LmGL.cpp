#include "LmGL.hpp"

#include "LmLog.hpp"

#define LM_GL_DEFINE(name, ret, args) PFN_lm_##name lm_##name = nullptr;
LM_GL_FUNCTIONS(LM_GL_DEFINE)
#undef LM_GL_DEFINE

bool LmLoadGL(void* (*getProc)(const char* name)) {
    bool ok = true;
#define LM_GL_LOAD(name, ret, args)                                         \
    lm_##name = reinterpret_cast<PFN_lm_##name>(getProc(#name));            \
    if (!lm_##name) {                                                       \
        LM_LOG_E << "OpenGL function " #name " is not available";           \
        ok = false;                                                         \
    }
    LM_GL_FUNCTIONS(LM_GL_LOAD)
#undef LM_GL_LOAD
    return ok;
}
