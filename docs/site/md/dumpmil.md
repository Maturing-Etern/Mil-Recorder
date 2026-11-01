# 参数导出格式（dumpmil）

`--dumpmil` 把某一时刻渲染器内部的判定线 / 音符计算结果输出到 **stderr**，用于与游戏画面对拍或排查谱面问题。

```bash
mil-recorder.exe --dumpmil "chart.json" 1280 720 12.5 13.0
```

- 宽高必填：所有坐标都是该分辨率下的屏幕坐标
- 可传任意多个时间点，每个时间点输出一节
- stdout 无内容，重定向时保留 stderr：`... 2> dump.txt`

## 输出结构

每个时间点一节：

```text
========== [dump @ t=32.000s] screen=(1280,720) ==========
[A] line#0 pos=(273,593) rot=85.0 size=1.00 col=(1.00,1.00,1.00,1.00) headA=1.000 bodyA=1.000
[A]   line#0 RotEv tz=[9.200,10.400] v=[85.000,85.000]
[B] #003 LineBody       pos=(273,593) size=(1800,4) rot=85.0 col=(1.00,1.00,1.00,1.00)  <WHITE>
[B] #027 Note           pos=(273,593) size=- rot=265.0 col=(1.00,1.00,1.00,1.00)  <WHITE> <BASE>
...
[A] visible: head>0 = 8, body>0 = 8, whiteVisible = 8 (of 12 lines)
[B] total submitted objects = 86, white candidates = 12
========== [dump end @ t=32.000s] ==========
```

## `[A]` 线级状态

只列 `t` 时刻 head/body 透明度 > 0 的线（真正会画出来的线）。

| 字段 | 含义 |
|------|------|
| `pos` | 线头屏幕坐标 |
| `rot` | 线旋转角（Rotation 事件当前值） |
| `size` | 线缩放 |
| `col` | 线颜色（RGBA，0~1） |
| `headA` / `bodyA` | 线头 / 线体透明度（已乘整线透明度） |

`RotEv` 行列出该线 Rotation 事件在当前时刻的区间与端点值：`tz=[起始,结束] v=[start值,end值]`。

## `[B]` 帧对象

逐个打印本帧实际提交渲染的对象，序号即绘制顺序：

| 类型 | 说明 |
|------|------|
| `Note` | 音符。`pos` 为判定端锚点，`rot` 为贴图旋转角；`<BASE>` / `<ACTIVE>` 标记 hold 材质层；`<WHITE>` 表示接近纯白（可用于白闪检查） |
| `LineHead` / `LineBody` | 判定线头 / 线体矩形。`LineBody` 的 `pos` 是线头坐标，矩形沿线延伸 |
| `HitEffect` | 打击特效（位置 / 尺寸 / 旋转） |
| `Particles` | 粒子批（只给数量） |
| `Text` / `Rect` / `Picture` | storyboard 对象 |

## `[S]` storyboard 汇总

`[S]` 行逐个列出 storyboard 对象（图层、透明度、尺寸、坐标、旋转、data 内容），末行汇总总数 / Picture 数 / 可见数。

## 对拍口径

- 坐标容差 ±0.5px、旋转 ±0.05°、尺寸 ±0.005
- **先比每时刻的行数统计（`visible:` / `total submitted`），再逐行比内容**——重复提交会先表现为计数翻倍
- HitEffect 与打击粒子每跑随机，跨跑对拍需先建噪声基线；Fracture 特效粒子种子固定，可作确定性指纹
- `screen=(W,H)` 与命令行宽高一致；换分辨率对拍时坐标按比例不可直接比，建议固定同一分辨率
