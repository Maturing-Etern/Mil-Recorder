# Mil Recorder v1.2.0

> 基于 [easy-phi](https://github.com/qaqFei/easy-phi) 的 Milthm 谱面渲染与视频录制工具。
> 本次更新：**官方 JS 谱面直读** + **命令行 ZIP 播放 / 无头导出** + **hold 判定溶解**。

---

## ✨ 新增

### 🎹 内嵌 Milize JS 引擎（官方 JS 谱面直读）
不再需要外部 Python 转换、DevJson 或临时文件。`loadChart` 自动检测含 `MilizeBeatmap` 的官方 JS 谱面 → 内嵌 **quickjs** 执行 → 直接构造 MilChart 内存模型 → 原生渲染。

- 覆盖官方 API：`t() / n() / a() / m.line() / m.storyboardObject() / p() / withProperty()`
- Color 动画同时支持数字（`r<<24|g<<16|b<<8|a`）与 `oklch()`
- 覆盖率：**373 / 381** 首官谱直接执行（其余 8 首为压缩版/特效配置，预期跳过）

```
官方 JS 谱面 → milize_js (quickjs) → Builder → MilChart（内存）→ easy-phi 渲染（零改动）
```

### 🗜️ 命令行 ZIP 直读播放
内置 **miniz** 解压，直接吃 Milthm 官方谱面 ZIP 包，自动识别并加载 **曲绘 / 音频 / 谱面** 三件套（忽略 `meta.json`）。

```bash
mil-recorder.exe "path/to/chart.zip"
```
- 无 `--` 参数且参数以 `.zip` 结尾时自动进入 ZIP 播放
- 音频按优先级取最大有效文件（自动排除 `hit/drag/tap` 打击音前缀）

### 🎬 无头视频导出 `--record`
命令行直接渲染 MP4，无需 GUI 交互：

```bash
mil-recorder.exe --record <zip | 谱面> <宽> <高> <fps> <out.mp4> [音频文件]
```
- 参数既可以是 **ZIP 包**，也可以是单个 **谱面文件**
- 第 7 个可选参数可手动指定音频

### ✨ hold 判定溶解
hold 判定后 300ms 交叉溶解为激活态贴图（新增资源 `builtin.hold_active.png`）。

- 覆盖 **所有 Hit 系 hold**：标准 hold、EX hold、双押 hold（`simul`）统一生效
- 各 base hold 贴图与激活贴图比例一致（≈3.0），替换不错位

---

## 🐛 修复

- **修复含非标准 hold 的官方 JS 谱面判定瞬间崩溃（`map::at`）**
  此前溶解材质仅对「标准单押 hold」注册，但判定逻辑对**任意** hold 都去查表，导致 `Cloudburst` 等含 EX / 双押 hold 的官谱一判定即崩。现增加可激活性保护，不可激活时只保留基础贴图。

---

## 🧩 其它

- 构建新增外部依赖 `external/miniz/`，`build_mil.sh` 已并入

---

## ⚠️ 已知问题

- 难度为「10+」这类带后缀的数值时，渲染会显示为 `0`（数值解析 bug，暂未修复）

---

## 📦 使用方法

```
===== Mil Recorder =====
1. 预览
2. 渲染视频
请选择:
```
- **预览** — 加载谱面 + 音频 + 曲绘并实时播放（AUTOPLAY）
- **渲染视频** — 导出 1920×1080 60fps MP4

命令行用法见上文「新增」章节。

---

## 附件说明

| 文件 | 说明 |
|------|------|
| `MilRecorder_v1.2.0_GUI.zip` | 可执行程序（交互菜单） |
| `MilRecorder_v1.2.0_Core.zip` | 源码 |
| `MilRecorder_v1.2.0_Patches.zip` | 增量补丁 |
| `MilRecorder_Setup.exe` | 安装程序 |

> `files/resources/` 下的资源文件版权归原作者所有，如有侵权请联系删除。
