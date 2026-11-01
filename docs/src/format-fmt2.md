# fmt 2 / 同人制谱器格式

Milthm 同人制谱器导出的谱面 JSON（根节点 `fmt: 2`）。渲染器解析顺序中排第二（devjson 风格解析失败后进入本格式）。

```jsonc
{
  "fmt": 2,
  "meta": { ... },
  "bpms": [ { "time": [0, 0, 1], "bpm": 180.0 } ],
  "lines": [ { "index": 0, "notes": [ ... ], "animations": [ ... ] } ],
  "storyboard": [ ... ]
}
```

> 官方谱 `.js`（MilizeBeatmap）由渲染器内嵌 quickjs 执行后**直接构造**同语义结构，不经过 JSON 解析；本页字段语义对两者通用。

## meta

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 谱面名称 |
| `music_artist` | string | 曲师 |
| `background_artist` | string | 画师 |
| `charter` | string | 谱师 |
| `difficulty_name` | string | 难度名（如 `"Cloudburst"`） |
| `difficulty` | number / string | 难度；字符串支持 `"13+"` 字面量（渲染器按 `13.5` 计并原样显示） |
| `difficulty_display` | string | 优先级最高的难度显示原文（渲染器扩展） |
| `offset` | number | **秒**。谱面延迟：音乐播放时间减去此值 |
| `background_dim` | number | 背景蒙版透明度 0~1（绘制黑色矩形）；渲染器暂未消费 |

## Beat 与 bpms

- `Beat` = `[主拍, 分子, 分母]`，值为 `主拍 + 分子/分母`（如 `[3,1,4]` = 3.25），**0-based**（第一个 BPM 恒 `[0,0,1]`）
- `bpms[]`：`{ time: Beat, bpm: number }`，时间换算**逐段累加**（段间按 `拍差 × 60/bpm` 累加，再加 `meta.offset`）；缺失时全谱按 120 BPM

## lines[]

| 字段 | 类型 | 说明 |
|------|------|------|
| `index` | int | 线编号，全谱唯一，**事件按此绑定**；缺失时回退数组序 |
| `notes` | list | 音符，见下 |
| `animations` | list | 本线（含线内音符）的动画，见下 |

### notes[]

| 字段 | 类型 | 说明 |
|------|------|------|
| `time` / `endTime` | Beat | `endTime ≥ time`；Hit 且 `endTime > time` = 长条（hold） |
| `type` | int | `0`=Hit、`1`=Drag；渲染器另认 `3`=闪电（Fracture，自制谱扩展） |
| `isFake` | bool | 假音符，不参与判定（闪电的过线特效仍会触发） |
| `isAlwaysPerfect` | bool | 恒 Perfect |
| `index` | int | 规范为全音符唯一；渲染器按「线内 index」绑定音符事件（跨线同名 index 互不干扰） |

### animations[]

| 字段 | 类型 | 说明 |
|------|------|------|
| `startTime` / `endTime` | Beat | `endTime ≥ startTime` |
| `type` | int | 事件类型，编号同 [devjson 事件表](format-devjson.md#事件类型key)（0=PositionX … 23=VisibleArea） |
| `start` / `end` | number | 起止值。Color（22）规范为打包整数 `r<<24 \| g<<16 \| b<<8 \| a`；渲染器另支持 CSS 颜色字符串 |
| `bearer_type` | int | `0`=线、`1`=音符、`2`=故事板（缺省 = 线） |
| `bearer` | int | 归属对象在其类型列表中的 index（音符事件为线内 note.index；缺省 = 所在线） |
| `ease` | Ease 对象 / number | 见下 |

### Ease

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | int | `0`=EaseIn、`1`=EaseOut、`2`=EaseInOut |
| `press` | int | 子类型：`0`=Linear、`1`=Sine、`2`=Quad、`3`=Cubic、`4`=Quart、`5`=Quint、`6`=Expo、`7`=Circ、`8`=Back、`9`=Elastic、`10`=Bounce；`0` = 线性，>10 渲染器忽略 |
| `isValueExp` | bool | 自定义表达式开关；`true` 时 `start`/`end` 无效。渲染器置值域为零（表达式不参与计算） |
| `valueExp` | string | JS ES6 表达式，`with(Math)` 环境、变量 `t`∈0~1，返回值即动画值 |
| `clipLeft` / `clipRight` | number | 缓动曲线左右切割 0~1（渲染器未使用） |

## storyboard[]

`{ type, data, animations }`：`type` `0`=Picture、`1`=Text（规范另有 `2`=Mash）；`data` 为图片路径或文字。动画与线同构，`bearer_type=2`。

> 渲染器按根键 `storyboards`（复数）读取，并额外读取规范的 `layer` 字段（0=背景/1=普通/2=前景）。若谱面使用规范中的单数键 `storyboard`，故事板段会被跳过。

## 坐标系与单位

- 坐标系 1920×1080，**屏幕中心为原点，右上为正**
- 流速单位：规范为 1/10 屏幕高/秒（= 108 px/s × Speed 值）；渲染器按游戏本体对齐实现（speedUnit=120、Multiply 语义、线默认 FlowSpeed=1.0），数值口径以实测对齐为准
- `VisibleArea`：音符相对轨道的距离（不计 x）大于该值则不可见，长条以头部为准；默认无穷

## 动画默认值（无匹配事件时）

**行为**：首个事件前用第一个事件的开始值；最后一个事件后用其结束值。

| 键 | 线 | 音符 | 故事板 |
|----|----|------|--------|
| PositionX | 0 | 0 | 0 |
| PositionY | −350 | 0 | 0 |
| Transparency | 1 | 1 | 0 |
| Size | 1 | 1 | 1 |
| Rotation | 90 | 0 | 0 |
| FlowSpeed | 1 | 1 | 1 |
| RelativeX / RelativeY | 0 | 0 | 0 |
| LineBodyTransparency | 1 | — | — |
| LineHeadTransparency | 1 | — | — |
| Speed | 1 | — | 1 |
| WholeTransparency | 1 | — | — |
| Color | 0xffffffff | 0xffffffff | 0xffffffff |
| VisibleArea | ∞ | — | — |
| StoryBoardWidth / Height | — | — | 1 |

## 谱面压缩包

同人制谱器规范的包内 `meta.json` 用 `chart_path` / `audio_path` / `image_path` 三键 + `res` 资源目录；渲染器**不读取这些键**，zip 谱面包一律按[自动识别规则](chart-standard.md#自动识别规则)按扩展名匹配（识别不了的扩展名会漏，包内请保持谱面/音频/曲绘三件套可按扩展名找到）。
