# Lumina Beauty

**中文** | [English](README.en.md)

实时视频美颜：磨皮美白、美型、滤镜、风格妆、虚拟背景、会随表情动的卡通头像和头饰。提供一个小巧的 C 语言 SDK，
以及可以直接加在摄像头上的 [OBS Studio](https://obsproject.com) 滤镜插件。

## 功能

| 类别 | 效果 |
|---|---|
| 美肤 | 磨皮（保边，眼睛、眉毛、嘴唇保持清晰）、美白、锐化、清晰度、美牙、去黑眼圈、去法令纹 |
| 美型 | 大眼、瘦脸、窄脸、颧骨、下巴、鼻翼、额头、小头 |
| 色彩 | 15 款滤镜，11 款风格妆（调色 + 妆容：口红、腮红、眉毛、眼影、眼线、睫毛、修容、粉底），色温 / 色调 / 饱和度 / 亮度 / 对比度 |
| 背景 | 背景虚化或替换，人像掩膜能识别头发，并做边缘细化 |
| 趣味 | 6 款会动的卡通头像（眨眼、张嘴、微笑、挑眉、视线、头部姿态）；配饰分 5 个槽位可自由组合：头顶 8 款、眼部 5 款、耳饰 4 款、颈部 4 款、嘴上叼的 4 款 |

配饰的槽位和选项见 `lumina.h` 里的 `lumina_acc_*`（`LUMINA_ACC_HEAD` 等参数）。不用摄像头也能预览：
`lumina-preview out.ppm head=3 eyes=1 neck=1 mouth=1` 会把它们画在一张示意脸上。

所有效果都在 GPU 上运行，使用离屏的 OpenGL 3.3 上下文。人脸跟踪用的是 MediaPipe 模型，通过 ONNX Runtime 在 CPU
上推理。卡通头像和头饰都由着色器程序化绘制，不需要任何图片素材。

## 目录结构

```
include/lumina/lumina.h   对外的 C API
src/engine/               引擎（渲染管线、着色器、人脸跟踪、人像分割、妆容、头像）
src/platform/win/         Windows 平台相关（WGL 上下文、WIC 图片解码）
src/platform/mac/         macOS 平台相关（CGL 上下文、ImageIO 图片解码）
models/                   转换成 ONNX 的 MediaPipe 模型（Apache 2.0，见 models/NOTICE.md）
looks/                    素材文件：内置滤镜和风格妆的 JSON 版本，格式见 looks/README.md
tools/lumina_cli.cpp      命令行工具，处理原始 I420 帧
tools/lumina_look.cpp     素材工具：导出内置效果、检查素材文件
tools/lumina_preview.cpp  配饰预览：把头像和配饰画在示意脸上出图
plugins/obs/              OBS Studio 滤镜插件（GPL-2.0-or-later）
```

## 构建（Windows）

需要 Visual Studio 2022 或更新版本、CMake 3.20+。配置时会自动下载 ONNX Runtime，也可以用 `ONNXRUNTIME_ROOT`
指向一个已解压的发布包。

```
cmake -S . -B build -A x64
cmake --build build --config Release
build\bin\Release\lumina-cli.exe build\bin\Release\models 1280 720 in.i420 out.i420 smooth=0.6 whiten=0.3 eye=0.3
```

### OBS 插件（Windows）

插件需要目标 OBS 版本的 libobs 头文件和导入库：

```
git clone --depth 1 --branch 32.0.4 https://github.com/obsproject/obs-studio.git ..\obs-studio
.\scripts\make-obs-import-lib.ps1 -ObsDir "C:\Program Files\obs-studio" -OutDir build\obs-lib
cmake -S . -B build -A x64 -DLUMINA_BUILD_OBS_PLUGIN=ON -DOBS_SOURCE_DIR=..\obs-studio -DOBS_LIBRARY=build\obs-lib\obs.lib
cmake --build build --config Release --target lumina-beauty
```

构建完成后，`build\obs-plugin\` 里就是按 OBS 目录结构整理好的插件。一起构建出来的 `lumina-obs-test` 不用打开 OBS，
直接用 I420 / NV12 / YUY2 / UYVY 测试帧调用滤镜回调；在 OBS 的 `bin\64bit` 目录下运行，并把 `LUMINA_MODEL_DIR`
指向 `models\`。安装方法：把 `obs-plugins\64bit\*` 复制到 `C:\ProgramData\obs-studio\plugins\lumina-beauty\bin\64bit\`，
把 `data\obs-plugins\lumina-beauty\*` 复制到 `C:\ProgramData\obs-studio\plugins\lumina-beauty\data\`。
在 OBS 里右键视频采集源，选择 **滤镜**，添加 **Lumina 美颜**。

### OBS 插件（macOS）

插件的架构必须和 OBS 一致：Apple 芯片版 OBS 用 arm64 插件，Intel 版 OBS（包括在 Rosetta 下运行的）用 x86_64 插件。
可以用 `file /Applications/OBS.app/Contents/MacOS/OBS` 查看。头文件要用同一版本 OBS 的 libobs，并链接 OBS.app 里自带的 libobs：

```
brew install simde   # OBS 31 起，Apple 芯片上的 libobs 头文件依赖 SIMDe
git clone --depth 1 --branch 32.2.2 https://github.com/obsproject/obs-studio.git ../obs-studio
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLUMINA_BUILD_OBS_PLUGIN=ON \
  -DOBS_SOURCE_DIR=../obs-studio \
  -DOBS_LIBRARY=/Applications/OBS.app/Contents/Frameworks/libobs.framework/libobs
# Intel 版 OBS：再加上 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DONNXRUNTIME_VERSION=1.23.2（最后一个提供 x86_64 macOS 包的 ONNX Runtime）
cmake --build build -j
```

构建结果是 `build/obs-plugin/lumina-beauty.plugin`，把它复制到 `~/Library/Application Support/obs-studio/plugins/`
后重启 OBS 即可。`build/bin/lumina-obs-test` 会直接加载 OBS.app 里的 libobs 跑滤镜测试
（`LUMINA_MODEL_DIR=models build/bin/lumina-obs-test`）。发版（推送 `v*` 标签）时，CI 也会打包好
`lumina-beauty-macos-arm64.zip`。

如果编译时提示找不到 `<string>` 等 C++ 头文件，是旧版 Command Line Tools 残留了
`/Library/Developer/CommandLineTools/usr/include/c++/v1`。删掉这个目录，或者在配置时加上
`-DCMAKE_CXX_FLAGS="-nostdinc++ -isystem $(xcrun --show-sdk-path)/usr/include/c++/v1"`。

## 素材文件

滤镜和风格妆可以写成 JSON 素材文件加载，不改代码就能加新妆容：调色参数，以及口红、腮红、眉毛、眼影、修容、
眼线、睫毛的颜色和浓度。`looks/` 目录里是 26 款内置效果导出的素材，可以直接复制来改。格式说明见
[looks/README.md](looks/README.md)。

- OBS：滤镜属性「调色」组里的「素材文件」
- SDK：`lumina_set_look_file(engine, path)` / `lumina_set_look_json(engine, json)`
- 检查素材：`lumina-look check my-look.json`

## C API 简介

```c
lumina_engine* e = lumina_create("path/to/models");
lumina_set_param(e, LUMINA_SMOOTH, 0.6f);
lumina_set_param(e, LUMINA_AVATAR, LUMINA_AVATAR_PANDA);
lumina_process_i420(e, width, height, in, out);   /* 或 lumina_process_nv12 / _i420_planes */
lumina_destroy(e);
```

同一个引擎的处理调用必须在同一个线程里进行；参数可以在任意线程设置。

## 现状与计划

- CI 构建 Windows x64 和 macOS（Apple 芯片）。Linux（EGL）在计划中。OBS 插件支持 Windows 和 macOS。
- 支持 BT.601 和 BT.709、全范围和有限范围的帧（`lumina_set_color_space`）；OBS 滤镜会自动读取每一帧的色彩空间。
- OBS 滤镜支持 I420、NV12、YUY2 和 UYVY 帧，其他格式原样透传。

## 许可证

- 核心 SDK：MIT（`LICENSE`）
- OBS 插件（`plugins/obs/`）：GPL-2.0-or-later，因为它链接了 libobs
- 模型：Apache 2.0，来自 Google MediaPipe（`models/NOTICE.md`）
- ONNX Runtime（构建时下载）：MIT
