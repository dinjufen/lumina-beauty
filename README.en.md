# Lumina Beauty

[中文](README.md) | **English**

Real-time face beauty for video: skin smoothing and whitening, face reshaping, color filters, style
makeup, virtual background, animated cartoon avatars and headwear. Ships as a small C SDK and as an
[OBS Studio](https://obsproject.com) video filter.

## Features

| Area | Effects |
|---|---|
| Skin | smoothing (edge-aware, keeps eyes/brows/lips sharp), whitening, sharpen, clarity, whiter teeth, dark circles, smile lines |
| Face shape | bigger eyes, slim face, narrow face, cheekbones, jaw, nose width, forehead, smaller head |
| Color | 15 filters, 11 style looks (color grade + makeup: lips, blush, brows, eye shadow, eyeliner, lashes, contour, foundation), temperature / tint / saturation / brightness / contrast |
| Background | blur or replace, with a hair-aware person mask and edge refinement |
| Fun | 6 animated cartoon avatars (blink, mouth, smile, brows, gaze, head pose) and 8 headwear items |

All effects run on the GPU through an offscreen OpenGL 3.3 context. Faces are tracked with MediaPipe
models through ONNX Runtime on the CPU. Avatars and headwear are drawn procedurally in shaders, so no
image assets are needed.

## Repository layout

```
include/lumina/lumina.h   public C API
src/engine/               engine (pipeline, shaders, face tracking, segmentation, makeup, avatars)
src/platform/win/         Windows specifics (WGL context, WIC image decoding)
src/platform/mac/         macOS specifics (CGL context, ImageIO image decoding)
models/                   MediaPipe models converted to ONNX (Apache 2.0, see models/NOTICE.md)
tools/lumina_cli.cpp      process raw I420 frames from the command line
plugins/obs/              OBS Studio filter plugin (GPL-2.0-or-later)
```

## Build (Windows)

Requirements: Visual Studio 2022 or later, CMake 3.20+. ONNX Runtime is downloaded automatically at
configure time (or point `ONNXRUNTIME_ROOT` at an extracted release).

```
cmake -S . -B build -A x64
cmake --build build --config Release
build\bin\Release\lumina-cli.exe build\bin\Release\models 1280 720 in.i420 out.i420 smooth=0.6 whiten=0.3 eye=0.3
```

### OBS plugin (Windows)

The plugin needs the libobs headers and an import library for the OBS version you target:

```
git clone --depth 1 --branch 32.0.4 https://github.com/obsproject/obs-studio.git ..\obs-studio
.\scripts\make-obs-import-lib.ps1 -ObsDir "C:\Program Files\obs-studio" -OutDir build\obs-lib
cmake -S . -B build -A x64 -DLUMINA_BUILD_OBS_PLUGIN=ON -DOBS_SOURCE_DIR=..\obs-studio -DOBS_LIBRARY=build\obs-lib\obs.lib
cmake --build build --config Release --target lumina-beauty
```

`build\obs-plugin\` then contains the plugin in OBS's layout. `lumina-obs-test` (built alongside) runs the
filter callbacks on I420 / NV12 / YUY2 / UYVY test frames without opening OBS; run it from OBS's
`bin\64bit` directory with `LUMINA_MODEL_DIR` pointing at `models\`. Install it by copying
`obs-plugins\64bit\*` to `C:\ProgramData\obs-studio\plugins\lumina-beauty\bin\64bit\` and
`data\obs-plugins\lumina-beauty\*` to `C:\ProgramData\obs-studio\plugins\lumina-beauty\data\`.
In OBS, right-click a video capture source, choose **Filters**, add **Lumina Beauty** (shown as **Lumina 美颜** in Chinese OBS).

### OBS plugin (macOS)

The plugin's architecture must match OBS: the Apple silicon OBS needs an arm64 plugin, the Intel OBS
(which also runs under Rosetta) needs x86_64. Check with `file /Applications/OBS.app/Contents/MacOS/OBS`.
Use libobs headers from the same OBS version and link the libobs inside OBS.app:

```
brew install simde   # libobs headers from OBS 31+ need SIMDe on Apple silicon
git clone --depth 1 --branch 32.2.2 https://github.com/obsproject/obs-studio.git ../obs-studio
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLUMINA_BUILD_OBS_PLUGIN=ON \
  -DOBS_SOURCE_DIR=../obs-studio \
  -DOBS_LIBRARY=/Applications/OBS.app/Contents/Frameworks/libobs.framework/libobs
# Intel OBS: add -DCMAKE_OSX_ARCHITECTURES=x86_64 -DONNXRUNTIME_VERSION=1.23.2 (last ONNX Runtime with an x86_64 macOS build)
cmake --build build -j
```

This produces `build/obs-plugin/lumina-beauty.plugin`. Copy it to
`~/Library/Application Support/obs-studio/plugins/` and restart OBS. `build/bin/lumina-obs-test` runs the
filter test against the libobs in OBS.app (`LUMINA_MODEL_DIR=models build/bin/lumina-obs-test`). Release
builds (pushing a `v*` tag) also package `lumina-beauty-macos-arm64.zip` in CI.

If the compiler can't find `<string>` or other C++ headers, an old Command Line Tools install has left
`/Library/Developer/CommandLineTools/usr/include/c++/v1` behind. Remove that folder, or add
`-DCMAKE_CXX_FLAGS="-nostdinc++ -isystem $(xcrun --show-sdk-path)/usr/include/c++/v1"`.

## C API in brief

```c
lumina_engine* e = lumina_create("path/to/models");
lumina_set_param(e, LUMINA_SMOOTH, 0.6f);
lumina_set_param(e, LUMINA_AVATAR, LUMINA_AVATAR_PANDA);
lumina_process_i420(e, width, height, in, out);   /* or lumina_process_nv12 / _i420_planes */
lumina_destroy(e);
```

Process calls of one engine must come from one thread. Parameters can be set from any thread.

## Status and roadmap

- Windows x64 and macOS (Apple silicon) build in CI. Linux (EGL) is planned. The OBS plugin builds on Windows and macOS.
- Frames can be BT.601 or BT.709, full or limited range (`lumina_set_color_space`); the OBS filter
  picks this up from each frame automatically.
- The OBS filter handles I420, NV12, YUY2 and UYVY frames; other formats pass through unchanged.

## License

- Core SDK: MIT (`LICENSE`)
- OBS plugin (`plugins/obs/`): GPL-2.0-or-later, because it links libobs
- Models: Apache 2.0, from Google MediaPipe (`models/NOTICE.md`)
- ONNX Runtime (downloaded at build time): MIT
