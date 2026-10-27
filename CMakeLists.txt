cmake_minimum_required(VERSION 3.20)
project(lumina_beauty VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LUMINA_BUILD_OBS_PLUGIN "Build the OBS Studio filter plugin" OFF)
option(LUMINA_BUILD_TOOLS "Build the command line tools" ON)
set(ONNXRUNTIME_ROOT "" CACHE PATH "Extracted ONNX Runtime release (include/ and lib/). Downloaded when empty.")
set(ONNXRUNTIME_VERSION "1.31.0" CACHE STRING "ONNX Runtime release to download")

if(MSVC)
    add_compile_options(/utf-8 /W3)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------- ONNX Runtime
include(cmake/OnnxRuntime.cmake)

# ---------------------------------------------------------------- core library
file(GLOB LUMINA_ENGINE_SOURCES CONFIGURE_DEPENDS src/engine/*.cpp)
if(WIN32)
    file(GLOB LUMINA_PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/win/*.cpp)
elseif(APPLE)
    file(GLOB LUMINA_PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/mac/*.cpp)
else()
    message(FATAL_ERROR "Only Windows and macOS are supported for now; a Linux (EGL) backend is planned.")
endif()

# 各平台的系统库
function(lumina_link_platform target scope)
    if(WIN32)
        target_link_libraries(${target} ${scope} opengl32 windowscodecs ole32)
    elseif(APPLE)
        target_link_libraries(${target} ${scope} "-framework OpenGL" "-framework CoreFoundation"
                              "-framework ApplicationServices" "-framework ImageIO")
    endif()
endfunction()

add_library(lumina SHARED ${LUMINA_ENGINE_SOURCES} ${LUMINA_PLATFORM_SOURCES})
target_include_directories(lumina
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE src/engine)
target_compile_definitions(lumina PRIVATE LUMINA_BUILD_SHARED INTERFACE LUMINA_USE_SHARED)
target_link_libraries(lumina PRIVATE onnxruntime_imported)
lumina_link_platform(lumina PRIVATE)
set_target_properties(lumina PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    BUILD_RPATH "@loader_path"
    INSTALL_RPATH "@loader_path")

# 运行目录里放好 onnxruntime 与模型，便于直接运行工具和调试
add_custom_command(TARGET lumina POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_DLL} ${ONNXRUNTIME_EXTRA_LIBS} $<TARGET_FILE_DIR:lumina>
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/models $<TARGET_FILE_DIR:lumina>/models
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/looks $<TARGET_FILE_DIR:lumina>/looks)

install(TARGETS lumina RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(FILES include/lumina/lumina.h DESTINATION include/lumina)
install(FILES ${ONNXRUNTIME_DLL} ${ONNXRUNTIME_EXTRA_LIBS} DESTINATION bin)
install(DIRECTORY models/ DESTINATION models FILES_MATCHING PATTERN "*.onnx" PATTERN "*.txt" PATTERN "*.md")
install(DIRECTORY looks/ DESTINATION looks)

# 静态版核心：OBS 插件和需要直接用引擎内部类的工具链接它
if(LUMINA_BUILD_TOOLS OR LUMINA_BUILD_OBS_PLUGIN)
    add_library(lumina_static STATIC ${LUMINA_ENGINE_SOURCES} ${LUMINA_PLATFORM_SOURCES})
    target_include_directories(lumina_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                               PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/engine)
    target_link_libraries(lumina_static PRIVATE onnxruntime_imported)
    lumina_link_platform(lumina_static PUBLIC)
endif()

# ---------------------------------------------------------------- tools
if(LUMINA_BUILD_TOOLS)
    add_executable(lumina-cli tools/lumina_cli.cpp)
    target_link_libraries(lumina-cli PRIVATE lumina)
    set_target_properties(lumina-cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")

    # 素材工具：导出内置效果为素材文件、检查素材文件。只用到素材读写，直接编进引擎里的这几个源文件
    add_executable(lumina-look tools/lumina_look.cpp src/engine/LmLook.cpp src/engine/LmJson.cpp)
    target_include_directories(lumina-look PRIVATE src/engine include)
    set_target_properties(lumina-look PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # 配饰预览：不用摄像头，把头像和配饰画在一张示意脸上出图
    add_executable(lumina-preview tools/lumina_preview.cpp)
    target_include_directories(lumina-preview PRIVATE src/engine)
    target_link_libraries(lumina-preview PRIVATE lumina_static onnxruntime_imported)
    set_target_properties(lumina-preview PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
endif()

# ---------------------------------------------------------------- OBS plugin
if(LUMINA_BUILD_OBS_PLUGIN)
    add_subdirectory(plugins/obs)
endif()
