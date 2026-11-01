# Mil Recorder 文档

基于 [easy-phi](https://github.com/qaqFei/easy-phi) 的 Milthm 谱面渲染与视频录制工具。本节是文档总入口，按主题分册：

| 分册 | 内容 |
|------|------|
| [使用方式](usage.md) | 获取方式、GUI 界面说明、命令行参数全表 |
| [谱面包规范](chart-standard.md) | zip 谱面包结构、meta.json 字段、谱面文件 meta 与难度显示 |
| [谱面格式分析](format-devjson.md) | devjson / REM / 官方 JS 三种谱面的逐字段分析（坐标系、事件表、时间换算） |
| [解锁演出格式](reveal-format.md) | `meta.json` 的 `reveal` 段：自制谱进谱前 PV 演出（mil-reveal-1） |
| [参数导出格式](dumpmil.md) | `--dumpmil` 输出行逐字段说明与对拍口径 |
| [构建指南](build.md) | 前置依赖、一键编译、grain 全链构建、安装包与发版 |

## 快速开始

1. 获取程序：提交申请，审核通过后获取程序文件；或按[构建指南](build.md)自行编译
2. 双击运行，选择谱面（`.json` / `.js` / 谱面包 `.zip`），点 **预览** 或 **渲染视频**
3. 需要批量出片时用命令行：

```bash
mil-recorder.exe --record "谱面包.zip" 1920 1080 60 out.mp4
```

## 支持的输入

| 类型 | 说明 |
|------|------|
| devjson（format 2） | Milthm 格式化谱面 JSON |
| REM | Rain Editor Mobile JSON，含 v1.5 新字段（`bpmChanges` 三元组节拍、`parentId` 父子轨道） |
| 官方 JS 谱面 | MilizeBeatmap，经内置 quickjs 执行 |
| 谱面包 zip | 自动识别谱面 / 音频 / 曲绘三件套，详见[谱面包规范](chart-standard.md) |
