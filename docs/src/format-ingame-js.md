# 官方 JS 谱面字段分析

Milthm 官方谱（`.js`，MilizeBeatmap）。文件是 JavaScript，渲染器经内置 quickjs 执行得到谱面 JSON 后按本页结构解析。根结构为 `meta` + `bpms` + `lines`（音符与事件内嵌线内）+ `storyboards`。

```jsonc
{
  "meta": { ... },
  "bpms": [ { "time": [1, 1, 4], "bpm": 120.0 } ],
  "lines": [ { "index": 0, "notes": [ ... ], "animations": [ ... ] } ],
  "storyboards": [ ... ]
}
```

## meta

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 曲名 |
| `music_artist` | string | 曲师 |
| `background_artist` | string | 曲绘作者 |
| `charter` | string | 谱师 |
| `difficulty_name` | string | 难度名 |
| `difficulty` | number / string | 难度；字符串支持 `"13+"` 字面量（按 `13.5` 计） |
| `difficulty_display` | string | 优先级最高的难度显示原文 |
| `offset` | number | 秒，整体时间轴平移（拍时间 0 处即落在 offset） |

## bpms

`[ { "time": [主拍, 分子, 分母] 或 number, "bpm": value } ]`。拍 **0-based**（与 devjson / REM 的 1-based 不同）。时间换算为**逐段累加**：从拍 0 起按段累加 `(拍差) × 60/bpm`，最后加 `meta.offset`；`bpms` 缺失时按 120 BPM 全谱处理。

## lines[]

| 字段 | 类型 | 说明 |
|------|------|------|
| `index` | number | 线编号，**事件按此绑定**；缺失时回退数组序 |
| `notes` | array | 音符，见下 |
| `animations` | array | 本线（含线内音符）的动画事件，见下 |

### notes[]

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `time` / `endTime` | array / number | endTime=time | 拍三元组（0-based）；hold = `endTime > time` |
| `type` | number | ✅必填 | `0`=tap、`1`=drag、`2`=hold、`3`=闪电（Fracture） |
| `isFake` | bool | `false` | 不参与判定（仅视觉） |
| `isAlwaysPerfect` | bool | `false` | 恒 Perfect |
| `index` | number | 数组序 | 线内编号，音符事件按此绑定 |

### animations[]

| 字段 | 类型 | 说明 |
|------|------|------|
| `startTime` / `endTime` | array / number | 拍三元组（0-based） |
| `type` | number | 事件类型，编号同 [devjson 事件表](format-devjson.md#事件类型key)（0=PositionX … 23=VisibleArea） |
| `start` / `end` | number / string | 起止值；`type=22`（Color）时为 CSS 颜色字符串 |
| `bearer_type` | number | 作用对象类型：`0`=线、`1`=音符、`2`=故事板（缺省 = 线） |
| `bearer` | number | 作用对象编号：音符事件为**线内** note.index；线 / 故事板为全局 index（缺省 = 所在线） |
| `ease` | number / object | 数字 = 缓动 id；对象形式 `{ "type": n, "press": n, "isValueExp": bool }` |
| — `press` | number | Milthm 按压感缓动参数，`0` = 线性，>10 忽略 |
| — `isValueExp` | bool | `true` 时值域强制清零（自定义表达式保留语法、不参与计算） |

## storyboards[]

同 [devjson 的 storyboardObjects](format-devjson.md#storyboardobjects)：`type`（0=图片/1=文本）、`data`、`layer`（0=背景/1=普通/2=前景）。

## 与 devjson 的主要差异速查

| 项 | 官方 JS | devjson |
|----|---------|---------|
| 文件 | `.js`（quickjs 执行） | `.json` |
| 事件位置 | 内嵌 `lines[].animations` | 根级 `animations` + `data`/`i1` 指回对象 |
| 事件值键 | `start` / `end` | `fv` / `tv` |
| 对象引用 | `bearer_type` + `bearer`（音符为线内 index） | `data` + `i1`（全局 index） |
| BPM 拍基 | 0-based | 秒 + bpm 数组下标 |
| 难度字段 | `difficulty` / `difficulty_display` | `Difficulty` / `DifficultyValue` |
| speedUnit | 120（Multiply 语义，线默认 FlowSpeed = 1.0） | 108（Override 语义） |
