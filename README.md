# Mil Recorder

基于 [easy-phi](https://github.com/qaqFei/easy-phi) 的 Milthm 谱面渲染与视频录制工具。

## 功能

- 🎮 **实时预览** — OpenGL 窗口播放谱面，含开场演出与收场停留
- 🎬 **录制视频** — 离屏逐帧渲染，导出 h264 + AAC MP4，不依赖屏幕录制
- 📷 **单帧截图** — 指定时间点导出 BMP，可用于逐帧核对
- 🔬 **参数导出** — `--dumpmil` 输出判定线与音符的坐标、旋转、尺寸，便于比对
- 🔄 **格式转换** — 内置 `milchart_convert`

## 支持的输入

| 类型 | 说明 |
|------|------|
| `devjson`（format 2） | 格式化谱面 JSON |
| `REM` | Rain Editor Mobile JSON |
| 谱面包 `zip` | 自动识别谱面、音频、曲绘三件套，投放即播 |

## 使用方法

### 直接运行

双击 `mil-recorder.exe`，拖入谱面或谱面包，选择：
- **1** — OpenGL 预览
- **2** — 导出 MP4 视频（输出到解压目录 `render.mp4`）

### 命令行

```bash
# 录制视频
mil-recorder.exe --record "path/to/chart" out.mp4

# 单帧截图（W H 必填；时间点可选，负值区间为开场演出）
mil-recorder.exe --screenshot "path/to/chart" 1920 1080 frame.bmp -2.5

# 导出判定线/音符参数用于核对
mil-recorder.exe --dumpmil "path/to/chart" 1920 1080 12.5 13.0

# 跳过开场演出
mil-recorder.exe --record "path/to/chart" out.mp4 --no-intro
```

## 构建

### 前置条件

- GCC 13+（推荐 Qt 自带的 `mingw1310_64`）
- Python 3 + [grain](https://github.com/qaqFei/grain) 包管理器
- Windows 10/11 x64

### 编译

双击 `build.bat`：grain 生成源码 → patch → g++ 编译。

输出：`mil-recorder.exe`（约 65 MB）

Linux/macOS 下使用 `build_mil.sh`（需自行准备资源文件）。

### 制作安装包

```bash
pyinstaller --onefile milchart_convert.py
ISCC.exe mil_recorder.iss
```

输出：`dist/MilRecorder_Setup.exe`

## 项目结构

```
Mil-Recorder/
├── files/test/main.cpp        # 程序入口与 CLI
├── lib.hpp                    # easy-phi 渲染库
├── mil_fx_emitter.hpp         # 粒子特效模块
├── milize_js/                 # 谱面解析静态库
├── external/miniz/            # zip 读取
├── build.bat / build_mil.sh   # 一键编译脚本
├── sync_geasy_phi.py          # 上游同步
├── regen_source.py            # 源码再生成
├── patch_gcc13.py             # GCC 13 兼容性补丁
├── apply_mil_patches.py       # Mil Recorder 功能补丁
├── milchart_convert.py        # 谱面格式转换
└── files/resources/           # 资源文件（不入库，见 .gitignore）
```

## 技术栈

| 模块 | 说明 |
|------|------|
| easy-phi | 谱面渲染核心（OpenGL 3.3 + GLFW） |
| grain | C++ 包管理器，合并多个 .hpp/.cpp 为单文件 |
| gvideoenc | 视频编码（libx264 + AAC） |
| gminiaudio | 音频解码与播放 |
| miniz | zip 包读取 |
| quickjs | 谱面脚本解析 |

## 许可

[GPL-3.0](LICENSE)

本项目基于 [easy-phi](https://github.com/qaqFei/easy-phi)（MIT）构建。

`files/resources/` 下的资源文件版权归原作者所有，如有侵权请联系删除。
