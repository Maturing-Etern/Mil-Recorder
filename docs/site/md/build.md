# 构建指南

## 前置条件

| 依赖 | 要求 |
|------|------|
| 系统 | Windows 10/11 x64 |
| 编译器 | GCC 13+（推荐 Qt 自带的 `mingw1310_64`） |
| Python | 3.x + [grain](https://github.com/qaqFei/grain) 包管理器 |

## 一键编译

双击 `build.bat`（或命令行运行）：

1. 定位 grain 与 g++
2. grain 生成合并源码 `source.cpp`
3. 打 GCC 13 兼容补丁与 Mil 功能补丁
4. g++ 静态编译

产物：仓库根目录 `mil-recorder.exe`（约 74 MB，静态链接，含 fdk-aac）。

> 编译前先关闭正在运行的 `mil-recorder.exe`，否则链接阶段报 `Permission denied`（exe 被占用）。也不要并发跑两个构建实例。

## grain 全链（与 build.bat 等价的分步命令）

源码真值是仓库里的 `lib.hpp`（LF）与 `files/test/main.cpp`（CRLF），构建源由以下链路生成：

```bash
cd Mil-Recorder

# 1. 同步仓库 lib.hpp -> geasy_phi 包缓存（漏跑 = 改动不进 exe）
python sync_geasy_phi.py

# 2. grain 重新生成合并源（资源文件编译期嵌入此步完成）
PYTHONPATH=<grain_src目录> python regen_v2.py

# 3. GCC 13 兼容补丁
python patch_gcc13.py <grain_build>/source.cpp

# 4. Mil Recorder 功能补丁
python apply_mil_patches.py <grain_build>/source.cpp.patched.cpp

# 5. 编译
bash build_mil.sh        # 或 build.bat 内的 g++ 命令
```

注意：

- 第 1 步**不能漏**。合并源由包缓存生成，跳过同步时改动不会进入 exe
- 资源文件在 `files/resources/`，由 `grain_embed_N[]` 在第 2 步编译期嵌入，不入 git（见 `.gitignore`）
- Linux / macOS 可用 `build_mil.sh`，需自行准备资源文件

## 制作安装包

```bash
pyinstaller --onefile milchart_convert.py
ISCC.exe mil_recorder.iss
```

产物：`dist/MilRecorder_Setup.exe`（Inno Setup，含 `mil-recorder.exe` 与 `milchart_convert.exe` 格式转换器）。

## 发版

- 推送 `v*` tag 后 GitHub Actions 自动创建 Release，正文取 `CHANGELOG.md`
- 更新版本时在 `CHANGELOG.md` 顶部追加条目即可

## 文档站部署（Cloudflare Pages）

文档为 HTML 壳 + Markdown 内容源（`docs/site/md/`），页面运行时拉取渲染，部署在 Cloudflare Pages，无构建依赖：

| 项 | 值 |
|------|------|
| Production branch | `main` |
| Build command | 留空 |
| Build output directory | `docs/site` |

推送到 `main` 的 `docs/**` 变更会自动触发重新部署。

本地预览需经 HTTP 服务（`fetch` 在 `file://` 下被浏览器拦截）：

```bash
cd docs/site && python -m http.server 8080
# 打开 http://localhost:8080
```

改内容直接编辑 `docs/site/md/*.md`，刷新页面即生效，无构建步骤。

## 项目结构

```text
Mil-Recorder/
├── files/test/main.cpp        # 程序入口与 CLI
├── lib.hpp                    # easy-phi 渲染库（含 Milthm 渲染链）
├── mil_fx_emitter.hpp         # 粒子特效模块
├── milize_js/                 # 谱面解析静态库
├── external/miniz/            # zip 读取
├── build.bat / build_mil.sh   # 一键编译脚本
├── sync_geasy_phi.py          # 上游同步
├── regen_v2.py                # 合并源码再生成
├── patch_gcc13.py             # GCC 13 兼容性补丁
├── apply_mil_patches.py       # Mil Recorder 功能补丁
├── milchart_convert.py        # 谱面格式转换
└── files/resources/           # 资源文件（编译期嵌入，不入库）
```

## 技术栈

| 模块 | 说明 |
|------|------|
| easy-phi | 谱面渲染核心（OpenGL 3.3 + GLFW） |
| grain | C++ 包管理器，合并多个 .hpp/.cpp 为单文件 |
| gvideoenc | 视频编码（libx264 + AAC），内含 h264 解码与 fdk-aac 音频解码 |
| gminiaudio | 音频解码与播放 |
| miniz | zip 包读取 |
| quickjs | 官方 JS 谱面解析 |
