# OBS Studio plugin. Needs the libobs headers and import library of the OBS version you target:
#   -DOBS_SOURCE_DIR=<obs-studio source checkout>   (only libobs/ is used)
#   -DOBS_LIBRARY=<obs.lib>                          (scripts/make-obs-import-lib.ps1 creates it from an installed OBS)
# On macOS OBS_LIBRARY is the libobs binary inside OBS.app, e.g.
#   /Applications/OBS.app/Contents/Frameworks/libobs.framework/libobs
set(OBS_SOURCE_DIR "" CACHE PATH "obs-studio source checkout (for libobs headers)")
set(OBS_LIBRARY "" CACHE FILEPATH "libobs import library (obs.lib, or libobs inside OBS.app on macOS)")
if(NOT OBS_SOURCE_DIR OR NOT OBS_LIBRARY)
    message(FATAL_ERROR "Set OBS_SOURCE_DIR and OBS_LIBRARY to build the OBS plugin")
endif()

# OBS 31 起 libobs 头文件在非 x86 平台上依赖外部的 SIMDe（Apple 芯片上：brew install simde）
set(_obs_includes ${OBS_SOURCE_DIR}/libobs ${CMAKE_CURRENT_SOURCE_DIR}/include)   # obsconfig.h
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    find_path(SIMDE_INCLUDE_DIR simde/x86/sse2.h HINTS /opt/homebrew/include /usr/local/include)
    if(NOT SIMDE_INCLUDE_DIR)
        message(FATAL_ERROR "SIMDe not found: brew install simde, or set SIMDE_INCLUDE_DIR")
    endif()
    list(APPEND _obs_includes ${SIMDE_INCLUDE_DIR})
endif()

# 插件静态链接美颜核心，只额外带一个 onnxruntime.dll
add_library(lumina_static STATIC ${LUMINA_ENGINE_SOURCES} ${LUMINA_PLATFORM_SOURCES})
target_include_directories(lumina_static PUBLIC ${PROJECT_SOURCE_DIR}/include PRIVATE ${PROJECT_SOURCE_DIR}/src/engine)
target_link_libraries(lumina_static PRIVATE onnxruntime_imported)
lumina_link_platform(lumina_static PUBLIC)

add_library(lumina-beauty MODULE lumina-filter.cpp)
target_include_directories(lumina-beauty PRIVATE ${_obs_includes})
target_link_libraries(lumina-beauty PRIVATE lumina_static onnxruntime_imported ${OBS_LIBRARY})

if(APPLE)
    # macOS 上 OBS 加载 lumina-beauty.plugin 包：二进制在 Contents/MacOS，数据在 Contents/Resources，
    # onnxruntime 放在 Contents/Frameworks
    set(_pkg ${CMAKE_BINARY_DIR}/obs-plugin)
    set_target_properties(lumina-beauty PROPERTIES PREFIX "" SUFFIX "" OUTPUT_NAME "lumina-beauty"
        BUNDLE TRUE BUNDLE_EXTENSION "plugin"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.lumina.obs-beauty"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        LIBRARY_OUTPUT_DIRECTORY ${_pkg}
        BUILD_RPATH "@loader_path/../Frameworks"
        INSTALL_RPATH "@loader_path/../Frameworks")
    set(_bundle ${_pkg}/lumina-beauty.plugin/Contents)
    add_custom_command(TARGET lumina-beauty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_bundle}/Frameworks
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_DLL} ${ONNXRUNTIME_EXTRA_LIBS} ${_bundle}/Frameworks/
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data ${_bundle}/Resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/models ${_bundle}/Resources/models
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/looks ${_bundle}/Resources/looks
        # 本机构建只做 ad-hoc 签名；OBS 关闭了库校验，能直接加载
        COMMAND codesign --force --sign - --deep ${_pkg}/lumina-beauty.plugin)
else()
    set_target_properties(lumina-beauty PROPERTIES PREFIX "" OUTPUT_NAME "lumina-beauty"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/obs-plugin/obs-plugins/64bit
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/obs-plugin/obs-plugins/64bit)

    # 按 OBS 的目录结构整理一份可直接拷贝到 OBS 安装目录的插件包
    set(_pkg ${CMAKE_BINARY_DIR}/obs-plugin)
    add_custom_command(TARGET lumina-beauty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:lumina-beauty> ${_pkg}/obs-plugins/64bit/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_DLL} ${_pkg}/obs-plugins/64bit/
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data ${_pkg}/data/obs-plugins/lumina-beauty
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/models ${_pkg}/data/obs-plugins/lumina-beauty/models
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/looks ${_pkg}/data/obs-plugins/lumina-beauty/looks)
endif()

# 插件逻辑测试：把插件源码编进测试程序，直接调用滤镜回调。在 OBS 的 bin 目录下运行（需要 obs.dll）
add_executable(lumina-obs-test tests/filter_test.cpp)
target_include_directories(lumina-obs-test PRIVATE ${_obs_includes})
target_compile_definitions(lumina-obs-test PRIVATE LUMINA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(lumina-obs-test PRIVATE lumina_static onnxruntime_imported ${OBS_LIBRARY})
add_custom_command(TARGET lumina-obs-test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_DLL} ${ONNXRUNTIME_EXTRA_LIBS} $<TARGET_FILE_DIR:lumina-obs-test>)
set_target_properties(lumina-obs-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(APPLE)
    # 直接从 OBS.app 里加载 libobs，不用拷到 OBS 目录下运行
    get_filename_component(_obs_fw_dir "${OBS_LIBRARY}" DIRECTORY)
    get_filename_component(_obs_fw_dir "${_obs_fw_dir}" DIRECTORY)
    set_target_properties(lumina-obs-test PROPERTIES BUILD_RPATH "@loader_path;${_obs_fw_dir}")
endif()
