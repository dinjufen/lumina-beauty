# OBS Studio plugin. Needs the libobs headers and import library of the OBS version you target:
#   -DOBS_SOURCE_DIR=<obs-studio source checkout>   (only libobs/ is used)
#   -DOBS_LIBRARY=<obs.lib>                          (scripts/make-obs-import-lib.ps1 creates it from an installed OBS)
set(OBS_SOURCE_DIR "" CACHE PATH "obs-studio source checkout (for libobs headers)")
set(OBS_LIBRARY "" CACHE FILEPATH "libobs import library (obs.lib)")
if(NOT OBS_SOURCE_DIR OR NOT OBS_LIBRARY)
    message(FATAL_ERROR "Set OBS_SOURCE_DIR and OBS_LIBRARY to build the OBS plugin")
endif()

# 插件静态链接美颜核心，只额外带一个 onnxruntime.dll
add_library(lumina_static STATIC ${LUMINA_ENGINE_SOURCES} ${LUMINA_PLATFORM_SOURCES})
target_include_directories(lumina_static PUBLIC ${PROJECT_SOURCE_DIR}/include PRIVATE ${PROJECT_SOURCE_DIR}/src/engine)
target_link_libraries(lumina_static PRIVATE onnxruntime_imported)
if(WIN32)
    target_link_libraries(lumina_static PUBLIC opengl32 windowscodecs ole32)
endif()

add_library(lumina-beauty MODULE lumina-filter.cpp)
target_include_directories(lumina-beauty PRIVATE
    ${OBS_SOURCE_DIR}/libobs
    ${CMAKE_CURRENT_SOURCE_DIR}/include)   # obsconfig.h
target_link_libraries(lumina-beauty PRIVATE lumina_static onnxruntime_imported ${OBS_LIBRARY})
set_target_properties(lumina-beauty PROPERTIES PREFIX "" OUTPUT_NAME "lumina-beauty"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/obs-plugin/obs-plugins/64bit
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/obs-plugin/obs-plugins/64bit)

# 按 OBS 的目录结构整理一份可直接拷贝到 OBS 安装目录的插件包
set(_pkg ${CMAKE_BINARY_DIR}/obs-plugin)
add_custom_command(TARGET lumina-beauty POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:lumina-beauty> ${_pkg}/obs-plugins/64bit/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_DLL} ${_pkg}/obs-plugins/64bit/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data ${_pkg}/data/obs-plugins/lumina-beauty
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/models ${_pkg}/data/obs-plugins/lumina-beauty/models)
