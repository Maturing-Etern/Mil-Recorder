# devjson（format 2）字段分析

Milthm 格式化谱面 JSON。根结构为 `meta` + `bpms` + `lines` + `storyboardObjects` + `animations` 五段，事件全部集中在根级 `animations`，通过 `data` + `i1` 指回作用对象。

```jsonc
{
  "meta": { ... },
  "bpms": [ { "start": 0.0, "bpm": 145.0 } ],
  "lines": [ { "notes": [ ... ] } ],
  "storyboardObjects": [ ... ],
  "animations": [ ... ]
}
```

> 识别：`.json` 扩展名，先按 devjson 解析（失败再试 ingame JS 导出、REM）。RE 编辑器导出的 devjson 带根节点 `_rem` 键，渲染器按 RPU 角度口径处理，字段结构不变。

## meta

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `Title` | string | ✅ | 曲名（信息页显示） |
| `Composer` | string | ✅ | 曲师 |
| `Illustrator` | string | ✅ | 曲绘作者 |
| `Beatmapper` | string | ✅ | 谱师 |
| `Difficulty` | string | ✅ | 难度名，信息页原样显示（如 `"13+"`） |
| `DifficultyValue` | number | ✅ | 难度数值 |

## bpms

| 字段 | 类型 | 说明 |
|------|------|------|
| `start` | number | 该 BPM 的起始时刻（秒，绝对时间） |
| `bpm` | number | BPM 值 |

devjson 的时间基准是**秒**而非拍——`bpms[i].start` 直接是秒，音符/事件通过下面的时间三元组换算。

## 时间表示

音符与事件的时间字段接受两种写法：

- **number**：直接是秒
- **array**：`[beat, 分子, 分母]` 或 `[beat, 分子, 分母, bpmIndex]`
  - 拍值 = `beat + 分子 / 分母`
  - 换算：`秒 = bpms[bpmIndex].start + 拍值 × 60 / bpms[bpmIndex].bpm`
  - 4 元素时 `bpmIndex` 显式指定 BPM 段；3 元素时取所属对象的 `bpm` 字段

**注意**：音符与动画对象上的 `bpm` / `bpmId` 字段是 **bpms 数组的下标**，不是 BPM 数值。

## lines[].notes[]

音符挂在判定线下。devjson 无独立 hold 类型——**hold = Hit 且 `endTime > startTime`**。

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `bpm` | number | ✅ | bpms 下标（本音符时间换算基准） |
| `startTime` | number / array | ✅ | 判定开始时刻（hold 为头部） |
| `endTime` | number / array | ✅ | 判定结束时刻；tap 时等于 startTime |
| `type` | number | ✅ | `0`=Hit(tap/hold)、`1`=Drag、`2`=Fracture（闪电） |
| `isFake` | bool | ✅ | `true` = 不参与判定（仅视觉）。注意：闪电音符即使 isFake 也会触发过线特效 |
| `isAlwaysPerfect` | bool | ✅ | 恒 Perfect 判定 |
| `index` | number | ✅ | 音符编号，**事件按此绑定** |

## storyboardObjects[]

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `type` | number | ✅ | `0`=图片、`1`=文本 |
| `data` | string | ✅ | 图片为相对路径；文本为内容 |
| `layer` | number | ✅ | `0`=背景、`1`=普通、`2`=前景 |

## animations[]

所有属性动画（判定线 / 音符 / 故事板对象共用），逐条声明：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `bpmId` | number | ✅ | bpms 下标 |
| `fromBeat` / `toBeat` | number / array | ✅ | 事件起止（写法同时间三元组） |
| `key` | number | ✅ | 事件类型，见下表 |
| `fv` / `tv` | number 或 string | ✅ | 起止值。`key=22`（Color）时为 CSS 颜色字符串（如 `"#ff0000"`）；其余为数值 |
| `data` | number | ✅ | 作用对象类型：`0`=判定线、`1`=音符、`2`=故事板对象 |
| `i1` | number | ✅ | 作用对象的 `index` |
| `ease` | number | ✅ | 缓动类型 id |
| `press` | number | ✅ | Milthm 特有的「按压感」缓动参数，`0` = 线性 |
| `isCustomEase` | bool | ❌ | `true` 时启用自定义缓动 |
| `customEaseArr` | number[] | 条件 | 自定义缓动曲线采样点 |

### 事件类型（key）

| 值 | 含义 | 值域/单位 |
|----|------|-----------|
| 0 | PositionX 横坐标 | 世界单位，原点在屏幕中心，x 向右 |
| 1 | PositionY 纵坐标 | 世界单位，y 向上（世界系 y 向下取负，见下） |
| 2 | Transparency 音符透明度 | 0~1 |
| 3 | Size 判定线缩放 | 倍率 |
| 4 | Rotation 线旋转 | 度 |
| 5 | FlowSpeed 线流速（音符随线的下落速度） | 倍率 |
| 6 / 7 | RelativeX / RelativeY | 相对偏移 |
| 8 | LineBodyTransparency 线体透明度 | 0~1 |
| 9 | LineHeadTransparency 线头透明度 | 0~1 |
| 10 / 11 | 故事板宽 / 高 | — |
| 12 | Speed 音符独立流速 | 倍率 |
| 13 | WholeTransparency 整线透明（线体+线头+音符） | 0~1 |
| 14~21 | 故事板四角坐标（左下/右下/左上/右上 × x/y） | — |
| 22 | Color 线颜色 | CSS 颜色字符串 |
| 23 | VisibleArea 可见区域 | — |

### 坐标系

世界系 1920×1080：原点在**屏幕中心**，x 向右为正，y **向上**为正（世界原点取 (-960, 540)，视口 1920×-1080）。

### 流速语义

devjson 的音符滚动为 **Override（覆盖）语义**：音符最终流速由 `Speed`（key 12）事件直接决定，`speedUnit` 取 108（fmt 2 / 同人制谱器格式为 Multiply 语义、speedUnit=120，见 [fmt 2 格式](format-fmt2.md)）。速度事件段重叠 / 不连续时的规范化以后段优先。
