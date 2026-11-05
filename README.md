# Mil Recorder

基于 [easy-phi](https://github.com/qaqFei/easy-phi) 的 Milthm 谱面渲染与视频录制工具：实时预览、离屏逐帧录制、解锁演出还原与谱面参数导出。

- 📖 **完整文档**：[docs/](docs/site/)（使用方式 / 谱面包规范 / 参数导出 / 构建指南）
- 🔑 **获取**：申请审核通过后发放，或按[构建指南](docs/site/#/build)自行编译

## 功能

- 🎮 **实时预览** — OpenGL 窗口播放谱面，含载入信息页与收场停留
- 🎬 **录制视频** — 离屏逐帧渲染导出 H.264 + AAC MP4，不依赖屏幕录制，60/30fps 均稳定
- 🎞️ **解锁演出（reveal PV）** — 谱面包 `meta.json` 带 `reveal` 键时自动在打歌前播放 PV，时序与本家一致（内置 fdk-aac 解码）
- 📷 **单帧截图** — 指定时间点导出 BMP，可用于逐帧核对
- 🔬 **参数导出** — `--dumpmil` 输出判定线与音符的坐标、旋转、尺寸及 Rotation 事件明细，便于比对
- 🧬 **谱面格式** — devjson（format 2）、REM（含 v1.5）、官方 JS 谱面（MilizeBeatmap）
- 🔄 **格式转换** — 内置 `milchart_convert`

## 快速开始

```bash
# GUI：双击 mil-recorder.exe，选择谱面后点「预览」或「渲染视频」

# 命令行渲染谱面包
mil-recorder.exe --record "谱面包.zip" 1920 1080 60 out.mp4

# 单帧截图
mil-recorder.exe --screenshot "chart.json" 1920 1080 frame.bmp 12.5
```

更多用法见[使用方式](docs/site/#/usage)。

## 文档

| 分册 | 内容 |
|------|------|
| [简介与导航](docs/site/) | 文档总览 |
| [使用方式](docs/site/#/usage) | 获取方式、GUI 界面、命令行参数全表、FAQ |
| [谱面包规范](docs/site/#/chart-standard) | zip 三件套识别规则、meta.json、谱面 meta 与难度显示 |
| [解锁演出格式](docs/site/#/reveal-format) | `reveal` 段格式（mil-reveal-1） |
| [参数导出格式](docs/site/#/dumpmil) | `--dumpmil` 输出行逐字段说明与对拍口径 |
| [构建指南](docs/site/#/build) | 前置依赖、一键编译、grain 全链、安装包与发版 |

## 许可

[GPL-3.0](LICENSE)

本项目基于 [easy-phi](https://github.com/qaqFei/easy-phi)（MIT）构建。

`files/resources/` 下的资源文件版权归原作者所有，如有侵权请联系删除

本项目仅语法检查使用AI，请尊重他人的劳动成果