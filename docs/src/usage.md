# 使用方式

## 下载

前往 [Releases](https://github.com/Maturing-Etern/Mil-Recorder/releases) 页面：

- **正式版**：下载 `mil-recorder.exe`（绿色单文件）或 `MilRecorder_Setup.exe`（安装包）。每个版本附带更新说明。
- **开发版**：仓库 Actions 构建产物，功能最新但未经稳定验证。

系统要求：Windows 10/11 x64。

## GUI

双击 `mil-recorder.exe` 打开主界面（窗口标题 Open RPE Recorder）。

### 谱面选择

| 控件 | 说明 |
|------|------|
| 谱面根目录 | 选择谱面所在目录（storyboard 贴图也从此目录寻找） |
| 谱面文件 | `.json`（devjson / REM）或 `.js`（官方 MilizeBeatmap） |
| 曲绘文件 | `.png` / `.jpg` / `.jpeg` |
| 音频文件 | `.mp3` / `.wav` / `.ogg` / `.flac` / `.m4a` / `.aac` |
| 从 info.txt ... | 一次性导入 Rainy Playground（RPE）工程的 `info.txt`，自动填三件套路径 |

> 谱面包 `.zip` 无需逐项填写，见下文命令行或直接拖入后按提示操作；规范见[谱面包规范](chart-standard.md)。

### 设置

- **通用**：音乐音量、音效音量、音符缩放（写入注册表，下次启动保留）
- **视频参数**：分辨率（宽 × 高）、帧率、打击音随机抖动（勾选后渲染视频时打击音效带随机相位，模拟真人手感）

### 按钮

- **预览**：弹出演奏窗口实时播放（载入信息页 → 打歌；带 `reveal` 键的谱面包先播解锁演出）。`--fullscreen` 可全屏启动，`--disable-vsync` 关闭垂直同步
- **渲染视频**：弹出保存对话框，离屏逐帧渲染导出 H.264 + AAC 的 MP4，**不依赖屏幕录制**，60/30fps 均稳定

## 命令行

所有子命令第一个参数是命令名；`W`、`H` 为输出分辨率。

| 命令 | 说明 |
|------|------|
| `--record <chart\|zip> <W> <H> <fps> <out.mp4> [audio]` | 渲染视频。zip 自动匹配音频；裸谱面必须追加第 7 参音频路径，否则报 `缺少音频` |
| `--screenshot <chart> <W> <H> <out.bmp> [t]` | 单帧截图 BMP。`t` 缺省为 `1.983`，负值落在载入信息页 / 解锁演出段 |
| `--dumpmil <chart> <W> <H> [t...]` | 导出判定线 / 音符参数到 stderr，可传多个时间点，格式见[参数导出格式](dumpmil.md) |
| `<xxx.zip>`（裸路径） | 解压并直接播放谱面包 |

全局标志（放在命令任意位置）：

| 标志 | 说明 |
|------|------|
| `--no-intro` | 跳过载入信息页 |
| `--no-reveal` | 跳过解锁演出段 |
| `--fullscreen` | GUI 预览全屏 |
| `--disable-vsync` | GUI 预览关闭垂直同步 |

示例：

```bash
# 渲染谱面包（自动配音频曲绘）
mil-recorder.exe --record "LiFE Garden.zip" 1920 1080 60 out.mp4

# 渲染裸谱面（手动指定音频）
mil-recorder.exe --record "chart.json" 1920 1080 60 out.mp4 "song.mp3"

# 跳过解锁演出直接渲打歌段
mil-recorder.exe --no-reveal --record "LiFE Garden.zip" 1920 1080 60 out.mp4

# 截取载入信息页（t = -10）
mil-recorder.exe --screenshot "chart.json" 1920 1080 frame.bmp -10

# 参数核对：一次导出多个时间点
mil-recorder.exe --dumpmil "chart.json" 1280 720 12.5 13.0 32
```

## 时间轴约定

- `t < 0`：载入信息页（约 2s，曲名 / 曲师 / 曲绘 / 难度）
- 谱面包带 `reveal` 键时：信息页前先播解锁演出 PV（时长取决于视频）
- `t ≥ 0`：打歌段，从谱面 0s 起算

## 常见问题

**裸谱面渲染报「缺少音频」**
裸谱面不会自动找音频，`--record` 第 7 参传入音频路径，或改用 zip 谱面包。

**渲染视频分辨率有限制吗**
没有硬性限制，宽高为命令行 / GUI 视频参数里填的值，渲染器按此分辨率离屏逐帧输出。

**GUI 双击闪退**
确认系统为 Windows 10/11 x64；首次运行写注册表设置失败（权限受限账户）时尝试以管理员运行。

**视频里没有解锁演出**
meta.json 的 `reveal` 段格式不对会被静默跳过，对照[解锁演出格式](reveal-format.md)检查 `fmt` 键是否为 `mil-reveal-1`。
