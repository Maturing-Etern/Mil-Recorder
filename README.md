# Mil Recorder

基于 [easy-phi](https://github.com/qaqFei/easy-phi) 的 Milthm 谱面渲染与视频录制工具。

## 功能

- 🎮 **实时预览** — OpenGL 窗口播放谱面，含载入信息页与收场停留
- 🎬 **录制视频** — 离屏逐帧渲染，导出 h264 + AAC MP4，不依赖屏幕录制，60/30fps 均稳定
- 🎞️ **解锁演出（reveal PV）** — 谱面包 `meta.json` 带 `reveal` 键时自动在打歌前播放 PV：视频与 PV 音频同帧起播 → 视频停在末帧 → StartGame 音效 → 黑场过渡切入打歌；meta 未提供独立音频时自动从 PV 视频音轨解出音乐（内置 fdk-aac 解码）
- 📷 **单帧截图** — 指定时间点导出 BMP，可用于逐帧核对
- 🔬 **参数导出** — `--dumpmil` 输出判定线与音符的坐标、旋转、尺寸及 Rotation 事件明细，便于比对
- 🧬 **谱面格式** — devjson（format 2）、REM（含 v1.5：`bpmChanges` 三元组节拍、`parentId` 父子轨道）、官方 JS 谱面（MilizeBeatmap）
- 🔄 **格式转换** — 内置 `milchart_convert`

## 支持的输入

| 类型 | 说明 |
|------|------|
| `devjson`（format 2） | 格式化谱面 JSON |
| `REM` | Rain Editor Mobile JSON，v1.5 新字段（`bpmChanges` / `parentId`）已支持 |
| 官方 JS 谱面 | MilizeBeatmap，经内置 quickjs 执行 |
| 谱面包 `zip` | 自动识别谱面、音频、曲绘三件套；含 `meta.json` 的 `reveal` 段时播放解锁演出 |

## 解锁演出格式

谱面包 `meta.json` 顶层加 `reveal` 键：

```json
{
  "reveal": {
    "fmt": 1,
    "video": "reveal.mp4",
    "audio": "reveal_audio.wav"
  }
}
```

- `video`：PV 视频（h264，yuv420p）；`audio` 可省略，省略时自动解视频音轨
- 播放时序与本家一致：PV 音画同帧起播 → 视频完停末帧 → StartGame 音效（bundle 内 `sweet_dream_opening`）→ 1s 黑场淡入/淡出 → 进打歌
- 详细格式见 [docs/reveal-format.md](docs/reveal-format.md)

## 使用方法

### 直接运行

双击 `mil-recorder.exe`，拖入谱面或谱面包，选择：
- **1** — OpenGL 预览
- **2** — 导出 MP4 视频（输出到解压目录 `render.mp4`）

### 命令行

```bash
# 录制视频（W H fps 必填；zip 自动配音频，裸谱面可追加第 7 参音频路径）
mil-recorder.exe --record "path/to/chart" 1920 1080 60 out.mp4
mil-recorder.exe --record "path/to/chart" 1920 1080 60 out.mp4 "path/to/audio.wav"

# 单帧截图（W H 必填；时间点可选，负值区间为载入信息页/解锁演出段）
mil-recorder.exe --screenshot "path/to/chart" 1920 1080 frame.bmp -20

# 导出判定线/音符参数用于核对（可传多个时间点）
mil-recorder.exe --dumpmil "path/to/chart" 1920 1080 12.5 13.0

# 跳过载入信息页 / 跳过解锁演出
mil-recorder.exe --no-intro --record "path/to/chart" 1920 1080 60 out.mp4
mil-recorder.exe --no-reveal --record "path/to/chart" 1920 1080 60 out.mp4

# 谱面 zip 直接播放（无 --zipplay 参数，裸 zip 路径即可）
mil-recorder.exe "path/to/chart.zip"
```

## 功能演示

**解锁演出录制**（带 `reveal` 键的谱面包，PV 段含视频、音乐与 StartGame 音效）：

```bash
$ mil-recorder.exe --record "LiFE Garden.zip" 1920 1080 60 out.mp4
[rv] setup ok 1920x1080 codec=h264
...  # PV 段 (~31s) → StartGame SE → 黑场过渡 → 打歌段
# 成片：h264 + aac 双轨，PV 音乐 ~-13.5dB，无黑屏/无静音
```

**参数导出核对**（`[A]` 行为判定线，含 Rotation 事件；`[B]` 行为音符级基准）：

```bash
$ mil-recorder.exe --dumpmil "chart.json" 1920 1080 10.0
[A] line#0 pos=(960,540) rot=100.0 size=1.00 col=#FFFFFF headA=1.000 bodyA=1.000
[A]   line#0 RotEv tz=[9.200,10.400] v=[100.000,100.000]
[B] #001 tap           pos=(1342.5,612.0) size=(180.0,180.0) rot=0.0 col=#FFFFFF
[B] total submitted objects = 86, white candidates = 12
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
| gvideoenc | 视频编码（libx264 + AAC），内含 h264 解码与 fdk-aac 音频解码 |
| gminiaudio | 音频解码与播放 |
| miniz | zip 包读取 |
| quickjs | 谱面脚本解析 |

## 许可

[GPL-3.0](LICENSE)

本项目基于 [easy-phi](https://github.com/qaqFei/easy-phi)（MIT）构建。

`files/resources/` 下的资源文件版权归原作者所有，如有侵权请联系删除。
