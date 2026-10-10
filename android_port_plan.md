# Mil-Recorder → Android APK 移植方案

目标：把 Mil-Recorder（C++/easy-phi + quickjs 直读官方 JS 谱面）移植为 Android APK。
保留功能：实时游玩/预览 + 离线导出视频 MP4 + Android 原生界面交互。

日期：2026-09-02。状态：方案评审稿 v1。

---

## 0. 结论摘要

- 核心（quickjs 引擎 + 谱面模型 + 渲染算法 + miniaudio 音频 + stb 系解码）**平台无关，可复用约 60-70%**。
- 必须重写/适配层（~30-40%）：窗口/GL 上下文（GLFW→EGL/GLES3）、UI（gwin32ui→Kotlin）、输入（键鼠→触屏）、MP4 导出（FFmpeg 交叉编译 或 MediaCodec）、着色器（330 core→300 es）、文件对话框（→SAF）。
- 本机已有：Android SDK (platform 34/35 + build-tools 34/35 + cmdline-tools)、JDK 17 (Temurin)、Capacitor 6 工程 `C:\Android\MilLunePlayer-app`（可作 Gradle 壳参考）。
- 本机缺失：**NDK（必须装）**、gradle 发行包（wrapper 会自动下）、FFmpeg arm64 静态库（需交叉编译或换 MediaCodec）。

---

## 1. 现状盘点（已核实的事实）

### 1.1 依赖清单（files/test/info.json externals）
| 包 | 作用 | Windows 专属？ | Android 处理 |
|---|---|---|---|
| geasy_phi (lib.hpp) | 渲染核心 | 否（逻辑层平台无关） | 复用，GL 调用走抽象 |
| milize_js（本仓库内） | quickjs + 官方 JS 谱面执行 | 否（纯 C/C++） | **复用**（libmilize_js 交叉编译） |
| gglfw3 | 窗口/GL 上下文/事件 | 是（桌面 GLFW） | 删，换 EGL/GLES3 |
| gopengl (GL33Context) | GL3.3 loader/封装 | 是（桌面 GL 3.3） | 换 GLES3 loader，或加后端 |
| gminiaudio | 音频引擎/解码 | 否（miniaudio 跨平台） | **复用**（Android 走 AAudio） |
| gvideoenc | FFmpeg H.264+AAC→MP4 | 半（FFmpeg 本体跨平台，当前链接的是 x86_64 静态库） | FFmpeg 交叉编译 arm64，或换 MediaCodec |
| gtext_renderer | 字体渲染 | 否（stb_truetype） | **复用** |
| gimage | 图片解码 | 否（stb_image，.grain/libs 有静态库） | **复用**（交叉编译 stb 单文件即可） |
| gwin32ui | Win32 原生 UI 控件 | **是（HWND/GDI/ShellExecute）** | 删，重写 Kotlin UI |
| gwin_progress_dialog | Windows 进度框 | **是** | 删，换 Android ProgressBar |
| gvorbis / gutf16c2utf8 / gstrutils / gtime / gsp / gnumeric / gframerate_meter / gregapi / gdata / gkeeping_cwd / gthread_safe_queue / gyuv420frame | 工具/数据类型/编解码 | 否 | **复用** |

### 1.2 着色器现状（lib.hpp 实测）
- 行 3653：`#version 100`（vertex，attribute/varying 语法，GLES2 风格）→ 已接近移动端，可改 300 es。
- 行 6129：`#version 330 core`（circHitEffect fragment，in/out + out vec4）→ 需改 300 es（in/out 语法 ES3 兼容，加 precision 即可）。
- 其余运行期 shader 由 `shaderDataLoader` 从资源加载（`shaderString`），这些是 Phigros/Milthm 谱面背景 shader，**需逐段检查版本**（`parseDefaultShaderUniforms` 已做解析，说明是纯 GLSL 文本）。
- lib.hpp 中直接 GL 调用极少（glFlush/glClear/glClearColor），大头都在 gopengl 包封装内——移植面集中在 gopengl。

### 1.3 资源（files/resources/milthm/）
font.ttf + hitsounds(drag.ogg/hit.ogg) + notes(9 张 PNG) + pause.png + line_head.png —— 全部可打进 APK assets。

### 1.4 构建现状
- Windows 构建链：grain 合并源码 → patch_gcc13 → patch_milize_compat → MinGW g++ -O1 -static 链接 libmilize_js.a + 各 .a。
- Android 构建链：NDK clang + CMake（或 ndk-build），同一份源码交叉编译。

---

## 2. 目标 APK 架构

```
┌──────────────────────────────────────────────┐
│ Kotlin Activity (Android 原生界面层)          │
│  - SAF 文件选择/拖入谱面 zip                   │
│  - 按钮: 预览 / 导出视频 / 设置                 │
│  - ProgressBar 渲染进度                        │
├──────────────────────────────────────────────┤
│ GLSurfaceView / SurfaceView (渲染宿主)         │
├──────────────────────────────────────────────┤
│ JNI 桥 (jni_bridge.cpp)                       │
│  - loadChartFromZip / renderFrame / exportVideo│
│  - 触屏事件 → 键鼠事件映射                      │
├──────────────────────────────────────────────┤
│ 复用层: lib.hpp (easy-phi 核心)               │
│        milize_js (quickjs 引擎, libmilize_js) │
│        gminiaudio / gtext_renderer / gimage   │
│ 适配层: gopengl→gles3 后端 / mainloop→Android  │
│        gvideoenc→MediaCodec 或 arm64 FFmpeg   │
└──────────────────────────────────────────────┘
```

### 关键决策点（D1-D6）
- **D1 GL 后端**：gopengl 是否已有 GLES 后端？若无 → 新增 `GLES3Context` 实现同一接口（函数指针经 `eglGetProcAddress` 注入），接口一致则 lib.hpp 零改动。
- **D2 MP4 导出**：
  - 路线 X：NDK 交叉编译 FFmpeg（libx264/fdk-aac）arm64-v8a → gvideoenc 换 .a 路径，改动最小但包体 +30MB 且构建慢。
  - 路线 Y：弃 FFmpeg，写 `MediaCodecEncoder`（H.264 + AAC 硬编，Surface 直录），性能最好、免 GPL，但 gvideoenc 调用点要重写。
  - 建议：**实时游玩用 GLSurfaceView 直渲；导出视频先做路线 Y（MediaCodec offscreen Surface 录制）**，FFmpeg 仅当需要逐帧离屏渲染保真时才交叉编译。
- **D3 UI 保留度**：gwin32ui 全部重写。原桌面 GUI 布局（路径输入+按钮）在 Android 上改为：打开 zip（SAF）→ 列表显示歌曲 → 点歌进预览/导出。不再逐像素复刻原窗口。
- **D4 主循环**：Windows `mainloopFrame`（GLFW 事件轮询 + busyWait vsync）→ Android GLSurfaceView.Renderer.onDrawFrame（vsync 由系统驱动），视频导出走离屏 Surface + 固定步长 time。
- **D5 音频**：gminiaudio 在 Android 用 AAudio 后端（需确认其包是否启用 AAudio；miniaudio 默认支持，CMake 加 `-DMA_ENABLE_AAUDIO`）。
- **D6 谱面 zip 来源**：官方谱面以 zip 形式由用户导入（SAF 读入 app 私有目录再解压），与桌面"拖 zip"行为一致。

---

## 3. 分阶段实施计划（里程碑）

### M0 环境准备（半天）
1. 安装 NDK：`sdkmanager --install "ndk;26.3.11579264"`（或 r27），确认 `C:\Android\sdk\ndk\<ver>\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe`。
2. 验证 CMake：NDK 自带 cmake；或装 `cmake;3.22.1` + `ndk;...`。
3. 建工程骨架：`Mil-Recorder/android/`（Gradle 8.2.1 wrapper 参考现有 Capacitor 工程），minSdk 26 / target 34。
4. 冒烟：空 Activity + `logcat` 通。

### M1 核心引擎交叉编译（1 天）
1. 用 NDK clang 编 quickjs + milize_js → `libmilize_js.a`(arm64)。
2. 用 NDK 编 stb 系（gimage/gvorbis/gtext_renderer/gminiaudio）单文件 → .a。
3. CMake 加 `externalNativeBuild`，JNI 导出：`Java_..._executeChartJs(assets路径)` → 返回 Builder 统计（bpms/lines/notes/animations 数），先在纯 JNI 层验证 373 首谱面覆盖率 = Windows 同款结果。
4. 验收：logcat 打印 Aleph-0 = 155bpm/82线/765音符/9483动画。

### M2 渲染上屏（1-2 天）
1. 写 `gles3` 后端：`GLES3Context` 实现 lib.hpp 依赖的 GL33Context 接口（VAO/VBO/shader/纹理/FBO/清屏），函数指针走 eglGetProcAddress。
2. shader 版本迁移：`#version 100`→300 es（attribute→in/varying→out 保持也可，100 在 ES2 能跑但混 ES3 上下文需统一）；330 core→300 es（加 `precision highp float;`）。
3. GLSurfaceView 接入：onSurfaceCreated 建上下文，onDrawFrame 调 `renderer->render()`；资源从 assets 加载（AAssetManager → std::string）。
4. 触屏：onTouchEvent → 映射为 tap 坐标（1920x1080 逻辑坐标→屏幕归一化），驱动 HitObject 判定/暂停。
5. 验收：真机/模拟器跑通 Aleph-0 实时游玩，音频 hitsound 正常。

### M3 离线导出 MP4（1-2 天，路线 Y 优先）
1. 新建离屏 Surface（EGL PbufferSurface 或 SurfaceTexture）跑同一条 render 管线，逐帧固定步长。
2. MediaCodec 硬编 H.264 + AAC（miniaudio 解码 ogg 音源 → PCM → 硬编 AAC），mux 成 mp4（MediaMuxer）。
3. 帧率 60fps，进度回调 → Kotlin ProgressBar。
4. 若路线 Y 保真度不足（特效 shader 差异），评估回退路线 X 交叉编译 FFmpeg arm64。
5. 验收：导出 Aleph-0 60fps mp4，音画同步 ±1 帧。

### M4 Android 原生 UI 完善（1 天）
1. 主界面：导入 zip（SAF）→ 谱面列表（读 zip 内 info.json：标题/难度/曲师）→ 预览 / 导出按钮。
2. 播放中 UI：暂停/继续/返回；进度。
3. 错误处理：坏 zip/不支持的谱面 → toast + 日志文件。

### M5 打磨与打包（0.5-1 天）
1. 权限：无特殊权限（全本地文件）；`android:screenOrientation` 锁定横屏。
2. 触屏手感/延迟调优；hitsound 延迟校准。
3. 包体瘦身：只打包 milthm 资源，去掉 phigros/rizline 资源（files/resources 下三套 → 只留 milthm，-XX MB）。
4. 签名 debug/release；产出 `MilRecorder.apk`。

**总工期预估：5-8 个工作日**（含排错余量），大头在 M2 GLES 适配 + M3 视频导出。

---

## 4. Windows → Android 替换对照表

| Windows 侧 | Android 侧 |
|---|---|
| gglfw3::Window / Monitor / pollEvents | GLSurfaceView / Choreographer vsync |
| GL33Context::Make(gglfw3::getProcAddress) | GLES3Context + eglGetProcAddress |
| `#version 330 core` / `100` | `#version 300 es` + precision |
| gwin32ui 窗口/按钮/输入框/拖拽 | Kotlin Compose/View + SAF |
| ShellExecute 打开链接 | Intent.ACTION_VIEW |
| gwin_progress_dialog | ProgressBar + Handler/JNI 回调 |
| 键盘/鼠标事件 | onTouchEvent / onKeyDown |
| 文件对话框(路径字符串) | SAF Uri → contentResolver 拷入私有目录 |
| main.cpp WinMain/main 主流程 | Activity 生命周期 + GLSurfaceView.Renderer |
| gvideoenc(FFmpeg x86_64 .a) | MediaCodec 硬编 / arm64 FFmpeg |
| 控制台 cout | __android_log_print (logcat) |
| 宽字符 wstring/Win32Utils | UTF-8 string + assets 读取 |
| build.bat (grain+MinGW) | gradle + CMake + NDK clang |

---

## 5. 风险清单

| # | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| R1 | gopengl GL33Context 接口与 GLES3 差异大（如 VAO/instancing/整数纹理） | 中 | 高 | M2 先做接口差异清单；lib.hpp 中 GL 直呼极少（已核实），大头在封装内 |
| R2 | 运行期 shader（谱面背景滤镜）部分用了桌面 GL 专属写法 | 中 | 中 | shaderDataLoader 层做版本归一化预处理 |
| R3 | gminiaudio Android 后端未启用/编译失败 | 低 | 中 | CMake 显式 AAudio；备选 OpenSL |
| R4 | quickjs 在 ARM64 架构差异（已有 2026-06-04 新版，Windows 全谱面验证过） | 低 | 低 | 先在 M1 跑覆盖率回归 |
| R5 | MediaCodec 硬编画面与实时渲染不完全一致 | 中 | 中 | 同一管线双跑（Surface 直渲 vs 离屏），必要时 FFmpeg 回退 |
| R6 | 谱面 ZIP 由手机 SAF 导入流程复杂（微信/QQ 收到 zip → SAF 打开） | 中 | 中 | 增加"打开方式"intent-filter，接收外部 zip |
| R7 | C 盘空间/NDK 下载网络（ghproxy 可用） | 低 | 低 | NDK 装到 C:\Android\sdk |

---

## 6. 需要你确认的决策

1. **D2 MP4 导出路线**：先 MediaCodec（推荐，免 GPL、快）还是直接交叉编译 FFmpeg arm64？
2. **D5 音频**：hitsound + BGM 是否都要低延迟（AAudio 原生路径即可，无需额外确认）？
3. **UI 语言**：界面文案中文即可？
4. **目标机型**：有没有指定真机（决定 arm64 还是也要 armv7/x86 模拟器）？默认只出 arm64-v8a。
5. **APK 用途**：自用/朋友用 → 只需 debug 签名；上架/分发 → 需要 release keystore。

确认后从 M0 开工。
