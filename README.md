# Mil Recorder

基于 [easy-phi](https://github.com/qaqFei/easy-phi) 的 Milthm 谱面渲染与视频录制工具。

## 功能

- 🎮 **预览** — OpenGL 窗口实时预览谱面
- 🎬 **渲染视频** — 导出 1920×1080 60fps MP4 视频
- 📦 **ZIP 导入** — 直接拖入 Milthm 官方谱面 ZIP 包，自动解析
- 🔄 **格式转换** — 内置 `milchart_convert`，自动将官谱格式转为 DevJson

## 使用方法

### 直接运行

双击 `mil-recorder.exe`，拖入 ZIP 文件，选择：
- **1** — OpenGL 预览
- **2** — 导出 MP4 视频（输出到解压目录 `render.mp4`）

### 命令行

```bash
mil-recorder.exe "path/to/chart.zip"
```

## 构建

### 前置条件

- GCC 13+ (推荐 Qt 自带的 `mingw1310_64`)
- Python 3 + [grain](https://github.com/qaqFei/grain) 包管理器
- Windows 10/11 x64

### 编译

双击 `build.bat` 即可自动完成：grain 生成源码 → patch → g++ 编译。

输出：`mil-recorder.exe` (~63 MB)

### 制作安装包

```bash
# 编译 Python 转换脚本为独立 exe（可选，需 PyInstaller）
pyinstaller --onefile milchart_convert.py

# 用 Inno Setup 打包
ISCC.exe mil_recorder.iss
```

输出：`dist/MilRecorder_Setup.exe`

## 项目结构

```
easy-phi/
├── files/test/main.cpp   # 入口代码
├── lib.hpp                # easy-phi 渲染库
├── build.bat              # 一键编译脚本
├── patch_gcc13.py         # GCC 13 兼容性补丁
├── milchart_convert.py    # 官谱 → DevJson 转换脚本
├── mil_recorder.iss       # Inno Setup 打包脚本
├── dist/                  # 输出目录
│   ├── MilRecorder_Setup.exe   # 安装包
│   └── easy-phi_src.zip        # 源码包
└── files/
    ├── resources/         # 游戏资源（贴图、音效、字体、shader）
    └── test/info.json     # grain 包配置
```

## 技术栈

| 模块 | 说明 |
|------|------|
| easy-phi | 谱面渲染核心，支持 Phigros / Milthm / Rizline |
| grain | C++ 包管理器，合并多个 .hpp/.cpp 为单文件 |
| gopengl | OpenGL 3.3 封装 |
| gvideoenc | 视频编码（libx264 + AAC） |
| gminiaudio | 音频解码与播放 |
| gglfw3 | GLFW 窗口封装 |

## 许可

[MIT](LICENSE)

`files/resources/` 下的资源文件版权归原作者所有，如有侵权请联系删除。

## 关于官方 JS 谱面

官方 Milthm 谱面（`MilizeBeatmap` JS 格式）需要一个内嵌的 JS 引擎（quickjs）来直接执行。
本版本**未编译该引擎**，加载官方 JS 谱面会直接报错退出。

请先将谱面转换为 DevJson / fmt2 格式再使用。
