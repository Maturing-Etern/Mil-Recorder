# REM 格式字段分析

REM Editor（v1.3+，Rain Editor Mobile）导出包内的 `chart.json`。根结构为 `bpm` + `meta` + `chartStructure`，语义以编辑器配套的 Godot 播放器（chart_loader / track_player / note_player）为权威。

```jsonc
{
  "bpm": 120.0,
  "editorVersion": "REM-Editor-v1.5.0",
  "meta": { ... },
  "chartStructure": {
    "subBeats": 4,
    "bpmChanges": [ ... ],
    "tracks": [ ... ]
  }
}
```

## meta

| 字段 | 类型 | 说明 |
|------|------|------|
| `musicTitle` | string | 曲名 |
| `musicAuthor` | string | 曲师 |
| `artworkAuthor` | string | 曲绘作者 |
| `chartAuthor` | string | 谱师 |
| `difficultyCode` | string | 难度名 |
| `difficultyLevel` | number / string | 难度；字符串支持字面量（如 `"13+"`，按 `13.5` 计） |
| `musicFilePath` | string | 音频相对路径 |
| `artworkPath` | string | 曲绘相对路径 |
| `offset` | number | **毫秒**。时间轴对齐：谱面时间 = 音频时间 + offset（如 `-430` 表示谱面轴比音频慢 430ms） |

## 拍与时间

- 拍的统一写法：**`[主拍, 分子, 分母]`，主拍 1-based**（`[1,0,1]` = 第 0 拍 = 时间 0）；事件时间也接受单数字简写（== 主拍）
- **bpmChanges（v1.5，可选）**：`[ { "beat": [1,0,1], "bpm": 120.0 }, ... ]`，拍轴分段常数。缺失时回退顶层 `bpm`（默认 120）
- 拍 ↔ 毫秒换算必须**分段累加前缀和**（每段记 `ms_at_start`），不能每段独立用 `60000/bpm × 拍`
- 音频对齐由 `meta.offset` 单独负责，**不要**混进 BPM 换算；`beat < 1`（负时间区）合法，配合负 offset 可延伸到 0ms 前
- `chartStructure.subBeats` 为编辑器细分设置，播放器不消费

## chartStructure.tracks[]

轨道 = 判定线。`id` / `parentId` 兼容数字与字符串（数字会整数字符串化以配对）。

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `id` | int / string | — | 轨道标识，事件与父子轨按 id 引用 |
| `parentId` | int / string | `0` | **v1.5 父子轨**。父轨 id，`0`/缺失 = 顶层 |
| `name` | string | — | 编辑器显示名，播放器不消费 |
| `image` | string | — | 线贴图相对路径 |
| `bindNoteSize` | bool | `false` | 音符尺寸绑定线缩放 |
| `notes` | array | — | 音符表，见下 |
| `events` | object | — | 事件表，见下 |

### 父子轨语义（v1.5）

- **只有 x / y 参与继承**：子轨的 x/y 事件值是**相对父轨的偏移（local）**，世界坐标 = 自身 local + 父链所有祖先的 local（同一时刻 t 的动画求值）
- `rotation` / `scale` / `noteAlpha` / `trackAlpha` / `lineAlpha` / `color` **不继承**
- 约束：不可成环、父轨必须存在、不可自引——违规时降级为顶层，不应崩溃或死循环

### notes[]

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `type` | number | `0` | `0`=tap、`1`=drag、`2`=hold、`3`=闪电（Fracture） |
| `startTime` / `endTime` | array | — | 拍三元组；hold 用 endTime 表尾，tap 时二者相等 |
| `speed` | number | `1.0` | 音符流速倍率（作用于无 speedEvent 时的下落速度） |
| `positionX` | number | `0` | 音符横向偏移（REM 参考系单位，见坐标换算） |
| `size` | number | `1.0` | 音符缩放 |
| `lane` | number | `0` | 编辑器元数据，播放器不消费 |
| `isFake` | bool | `false` | 不参与判定（仅视觉；闪电的过线特效仍会触发） |
| `isAlwaysPerfect` | bool | `false` | 恒 Perfect |

### events{}

数值事件为 `{ startTime, endTime, startValue, endValue, easing }` 数组；颜色事件值是 `[r,g,b]`（0~255）。

| 键 | 含义 | 值域/单位 |
|----|------|-----------|
| `x` / `y` | 轨道位置（REM 参考系） | 见坐标换算 |
| `rotation` | 轨道旋转（度） | 见坐标换算 |
| `scale` | 轨道缩放 | 倍率 |
| `noteAlpha` | 音符透明度 | 0~1 |
| `trackAlpha` | 线头透明度 | 0~255 |
| `lineAlpha` | 线体透明度 | 0~255 |
| `speedEvent` | 流速 | 倍率（× base_speed，见下） |
| `color` | 线颜色 | `[r,g,b]` 0~255 |

`easing`：1~28 的 id（对应缓动曲线族 + press 参数；缺失默认 1 = 线性）。

## 坐标与单位换算（REM 参考系 → Milthm 世界系）

REM 参考空间 **1500×900**，中心 (750, 450)；Milthm 世界 1920×1080，原点屏幕中心、y 向上。等比缩放系数 = `1080/900 = 1.2`：

| 量 | 换算 |
|----|------|
| PositionX | `remX × 1.2 − 900` |
| PositionY | `540 − remY × 1.2`（y 轴翻转） |
| Rotation | `rem + 90`（Milthm 线角 = REM 角 + 90） |
| Size / noteAlpha | 直传 |
| trackAlpha / lineAlpha | `÷ 255` |

**流速**：REM 播放器 `base_speed = 800`（参考系像素/秒），实际滚动 = `speedEvent × 800`；换算到 Milthm 世界单位（×1.2 纵向比），对应 `speedUnit = 960 / 1.66 ≈ 578.3`。**Speed 事件值 = REM 倍数原样直传**。

**时间轴**：渲染器时间 = REM 谱面时间 − `offset/1000`。
