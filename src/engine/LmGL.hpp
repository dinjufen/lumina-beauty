#ifndef LUMINA_GL_HPP
#define LUMINA_GL_HPP

/**
 * @brief 本库用到的 OpenGL 3.3 Core 子集：类型、常量与函数指针
 * @remark 着色器按 GLSL ES 3.00 书写，在桌面 OpenGL 上编译时改写版本声明（见 LmPipeline.cpp），
 *         两者在本库用到的范围内语义一致。函数指针由 LmLoadGL 在上下文创建后加载。
 */
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define LM_GL_APIENTRY __stdcall
#else
#define LM_GL_APIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_ONE 1
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_HALF_FLOAT 0x140B
#define GL_RED 0x1903
#define GL_RGBA 0x1908
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_RGBA8 0x8058
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_R8 0x8229
#define GL_TEXTURE0 0x84C0
#define GL_RGBA16F 0x881A
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER 0x8D40

// 名字、返回值、参数表
#define LM_GL_FUNCTIONS(X)                                                                                           \
    X(glActiveTexture, void, (GLenum texture))                                                                        \
    X(glAttachShader, void, (GLuint program, GLuint shader))                                                          \
    X(glBindBuffer, void, (GLenum target, GLuint buffer))                                                             \
    X(glBindFramebuffer, void, (GLenum target, GLuint framebuffer))                                                   \
    X(glBindTexture, void, (GLenum target, GLuint texture))                                                           \
    X(glBindVertexArray, void, (GLuint array))                                                                        \
    X(glBlendFunc, void, (GLenum sfactor, GLenum dfactor))                                                            \
    X(glBufferData, void, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))                           \
    X(glBufferSubData, void, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))                     \
    X(glCheckFramebufferStatus, GLenum, (GLenum target))                                                              \
    X(glClear, void, (GLbitfield mask))                                                                               \
    X(glClearColor, void, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                               \
    X(glCompileShader, void, (GLuint shader))                                                                         \
    X(glCreateProgram, GLuint, (void))                                                                                \
    X(glCreateShader, GLuint, (GLenum type))                                                                          \
    X(glDeleteBuffers, void, (GLsizei n, const GLuint* buffers))                                                      \
    X(glDeleteFramebuffers, void, (GLsizei n, const GLuint* framebuffers))                                            \
    X(glDeleteProgram, void, (GLuint program))                                                                        \
    X(glDeleteShader, void, (GLuint shader))                                                                          \
    X(glDeleteTextures, void, (GLsizei n, const GLuint* textures))                                                    \
    X(glDeleteVertexArrays, void, (GLsizei n, const GLuint* arrays))                                                  \
    X(glDisable, void, (GLenum cap))                                                                                  \
    X(glDrawArrays, void, (GLenum mode, GLint first, GLsizei count))                                                  \
    X(glDrawElements, void, (GLenum mode, GLsizei count, GLenum type, const void* indices))                           \
    X(glEnable, void, (GLenum cap))                                                                                   \
    X(glEnableVertexAttribArray, void, (GLuint index))                                                                \
    X(glFramebufferTexture2D, void, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(glGenBuffers, void, (GLsizei n, GLuint* buffers))                                                               \
    X(glGenFramebuffers, void, (GLsizei n, GLuint* framebuffers))                                                     \
    X(glGenTextures, void, (GLsizei n, GLuint* textures))                                                             \
    X(glGenVertexArrays, void, (GLsizei n, GLuint* arrays))                                                           \
    X(glGetError, GLenum, (void))                                                                                     \
    X(glGetProgramInfoLog, void, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog))                 \
    X(glGetProgramiv, void, (GLuint program, GLenum pname, GLint* params))                                            \
    X(glGetShaderInfoLog, void, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog))                   \
    X(glGetShaderiv, void, (GLuint shader, GLenum pname, GLint* params))                                              \
    X(glGetUniformLocation, GLint, (GLuint program, const GLchar* name))                                              \
    X(glLinkProgram, void, (GLuint program))                                                                          \
    X(glPixelStorei, void, (GLenum pname, GLint param))                                                               \
    X(glReadPixels, void, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* pixels))         \
    X(glShaderSource, void, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length))         \
    X(glTexImage2D, void,                                                                                             \
      (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format,           \
       GLenum type, const void* pixels))                                                                              \
    X(glTexParameteri, void, (GLenum target, GLenum pname, GLint param))                                              \
    X(glTexSubImage2D, void,                                                                                          \
      (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,                \
       const void* pixels))                                                                                           \
    X(glUniform1f, void, (GLint location, GLfloat v0))                                                                \
    X(glUniform1i, void, (GLint location, GLint v0))                                                                  \
    X(glUniform2f, void, (GLint location, GLfloat v0, GLfloat v1))                                                    \
    X(glUniform3f, void, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2))                                        \
    X(glUniform4f, void, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))                            \
    X(glUniform4fv, void, (GLint location, GLsizei count, const GLfloat* value))                                      \
    X(glUseProgram, void, (GLuint program))                                                                           \
    X(glVertexAttribPointer, void,                                                                                    \
      (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer))             \
    X(glViewport, void, (GLint x, GLint y, GLsizei w, GLsizei h))

#define LM_GL_DECLARE(name, ret, args)                     \
    typedef ret(LM_GL_APIENTRY* PFN_lm_##name) args; \
    extern PFN_lm_##name lm_##name;
LM_GL_FUNCTIONS(LM_GL_DECLARE)
#undef LM_GL_DECLARE

// 让管线代码照常写 glXxx(...)
#define glActiveTexture lm_glActiveTexture
#define glAttachShader lm_glAttachShader
#define glBindBuffer lm_glBindBuffer
#define glBindFramebuffer lm_glBindFramebuffer
#define glBindTexture lm_glBindTexture
#define glBindVertexArray lm_glBindVertexArray
#define glBlendFunc lm_glBlendFunc
#define glBufferData lm_glBufferData
#define glBufferSubData lm_glBufferSubData
#define glCheckFramebufferStatus lm_glCheckFramebufferStatus
#define glClear lm_glClear
#define glClearColor lm_glClearColor
#define glCompileShader lm_glCompileShader
#define glCreateProgram lm_glCreateProgram
#define glCreateShader lm_glCreateShader
#define glDeleteBuffers lm_glDeleteBuffers
#define glDeleteFramebuffers lm_glDeleteFramebuffers
#define glDeleteProgram lm_glDeleteProgram
#define glDeleteShader lm_glDeleteShader
#define glDeleteTextures lm_glDeleteTextures
#define glDeleteVertexArrays lm_glDeleteVertexArrays
#define glDisable lm_glDisable
#define glDrawArrays lm_glDrawArrays
#define glDrawElements lm_glDrawElements
#define glEnable lm_glEnable
#define glEnableVertexAttribArray lm_glEnableVertexAttribArray
#define glFramebufferTexture2D lm_glFramebufferTexture2D
#define glGenBuffers lm_glGenBuffers
#define glGenFramebuffers lm_glGenFramebuffers
#define glGenTextures lm_glGenTextures
#define glGenVertexArrays lm_glGenVertexArrays
#define glGetError lm_glGetError
#define glGetProgramInfoLog lm_glGetProgramInfoLog
#define glGetProgramiv lm_glGetProgramiv
#define glGetShaderInfoLog lm_glGetShaderInfoLog
#define glGetShaderiv lm_glGetShaderiv
#define glGetUniformLocation lm_glGetUniformLocation
#define glLinkProgram lm_glLinkProgram
#define glPixelStorei lm_glPixelStorei
#define glReadPixels lm_glReadPixels
#define glShaderSource lm_glShaderSource
#define glTexImage2D lm_glTexImage2D
#define glTexParameteri lm_glTexParameteri
#define glTexSubImage2D lm_glTexSubImage2D
#define glUniform1f lm_glUniform1f
#define glUniform1i lm_glUniform1i
#define glUniform2f lm_glUniform2f
#define glUniform3f lm_glUniform3f
#define glUniform4f lm_glUniform4f
#define glUniform4fv lm_glUniform4fv
#define glUseProgram lm_glUseProgram
#define glVertexAttribPointer lm_glVertexAttribPointer
#define glViewport lm_glViewport

/**
 * @brief 加载函数指针，需要在有当前上下文时调用
 * @param[in] getProc 平台的取函数地址方法
 * @return 有函数缺失时返回 false
 */
bool LmLoadGL(void* (*getProc)(const char* name));

#endif // LUMINA_GL_HPP
