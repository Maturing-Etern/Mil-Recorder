# 谱面包规范

Mil Recorder 支持「散文件」与「谱面包 zip」两种输入。谱面包把谱面、音频、曲绘打进一个 zip，程序自动识别，无需手动指定路径。

## zip 结构

```text
MyChart.zip
├── chart.json        # 谱面（.json / .js，chart.json 优先）
├── song.mp3          # 音频
├── cover.png         # 曲绘
└── meta.json         # 包信息（可选，见下文）
```

子目录不影响识别，程序会递归遍历整个 zip。

### 自动识别规则

| 类型 | 规则 |
|------|------|
| 谱面 | 第一个 `.js` / `.json`；若存在 `chart.json` 则**优先**；`meta.json`、`shaders.json` 永远不会被当成谱面 |
| 音频 | `.wav` / `.mp3` / `.ogg` / `.flac` / `.m4a` / `.aac` 中**取最大**的文件；`hit*` / `drag*` / `tap*` 开头的打击音效文件会被排除 |
| 曲绘 | `.png` / `.jpg` / `.jpeg` 中取最大的文件 |

识别失败的报错格式：`zip 中找不到谱面文件(.js/.json)`、`zip 中找不到音频文件(.wav/.mp3/.ogg/...)`。

## meta.json

谱面包根目录（或谱面所在目录）的 `meta.json` 是**可选**的包信息文件，目前支持：

| 字段 | 类型 | 说明 |
|------|------|------|
| `image_file` | string | 信息页封面，包内相对路径；未填时回退到谱面 `meta.artworkPath`，再回退到自动识别的曲绘 |
| `reveal` | object | 解锁演出配置，独立格式见[解锁演出格式（mil-reveal-1）](reveal-format.md) |

## 谱面文件 meta

谱面文件内部的 `meta` 对象用于载入信息页与难度显示（devjson / REM 通用）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `Title` | string | 曲名 |
| `Composer` | string | 曲师 |
| `Illustrator` | string | 曲绘作者 |
| `Beatmapper` | string | 谱师 |
| `Difficulty` | string / number | 难度。字符串支持字面量如 `"13+"`、`"12.5"`，按原文显示 |
| `DifficultyValue` | number | 难度数值 |
| `artworkPath` | string | 封面回退路径（无 `meta.json` 的 `image_file` 时使用） |

`difficulty_display`（REM）优先级最高：存在时信息页难度一栏原样显示该字符串。

## 支持的谱面格式

| 格式 | 识别 | 字段级分析 |
|------|------|------|
| devjson（format 2） | `.json` | [devjson 字段分析](format-devjson.md) |
| REM | `.json` | [REM 字段分析](format-rem.md) |
| 同人制谱器 fmt 2 | `.json`（根键 `fmt: 2`） | [fmt 2 字段分析](format-fmt2.md) |
| 官方 JS 谱面 | `.js` | 同上（MilizeBeatmap，内嵌 quickjs 执行后直接构造） |

三种格式共享同一条渲染链路；事件与判定线行为均以 Milthm 游戏本体为对齐基准。
