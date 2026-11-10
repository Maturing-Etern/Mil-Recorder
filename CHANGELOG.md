# Changelog

## v1.2 (2026-11-10)

- ↔️ **故事板 X/Y 拉伸事件**：图片精灵最终尺寸接入 `SBWidth`/`SBHeight`（= 贴图自然尺寸 × Size × SBWidth/SBHeight），此二键仅故事板有效，线/音符不消费
- 🎨 **颜色事件数字值兜底**：谱面以数字（uint32 RGBA）而非字符串传颜色时按位拆分解析（r<<24|g<<16|b<<8|a），修复颜色淡入失效（黑幕变白幕）
- 🔁 **旋转重映射限定线级**：只翻转线级倾角语义，音符/故事板的 rotation 保持原值直用，修复雨滴类音符倒立
- 🧱 构建去 `-pipe`：8GB 内存机器编译稳定性

## v1.1 (2026-11-09)

- 🎯 **线体旋转口径修正**：部分谱面线体方向反/斜线消失的根因修复（谱面旋转语义与渲染口径不一致，统一到内部倾角）
- 🖤 **Storyboard 默认 Transparency 1→0 + 绘制层序调整**：无事件精灵不再全曲可见；Background 层画在黑 mask 之下、Normal 层画在线/音符之下 —— 故事板全屏黑带根因
- ✂️ **VisibleArea 裁剪修正**：取 PositionY 覆盖后的 floor position（RPU 对齐），逐 note 编排（phinity Target=noteId）不再被整批误裁 a=0
- 🖼 **裸谱面曲绘自动发现**：无 zip/meta.json 时在谱面目录及上级 `artwork/` 自动找最大图片，曲库目录布局散谱不再黑背景
- 📏 线体长度可调：环境变量 `MIL_LINE_BODY_LEN`（默认 5.0 = 2× 原 2.5 屏高）
- 📚 文档站：html2canvas 一键导出当前页 PNG + 轻提示 toast

## v1.0 (2026-10-28)

- 音符旋转统一为 `90 - lineRotation`：原生链(RPU)与 REM 链(Godot)同域，修复原生分支斜轨音符左右镜像
- `MilLine::init` 幂等化：重置事件游标 + `noteGroups.clear()`，修复 REM 链音符双重提交（半透明叠加）
- 闪电(Fracture)特效不再过滤 `isFake`：REM 导出的闪电全部标 fake，此前粒子从不播放
- REM 速度积分改梯形模型，速度事件恒线性（无视 easing）；重叠速度段规范化 —— milplay 侧积分与 Godot 逐点一致（31 线 558 点残差 <0.003px）
- 难度字符串直通：`difficulty`/`difficultyLevel` 支持 `"13+"`/`"12.5"` 字面量，`difficulty_display` 原样显示；编译开关 `MIL_DIFF_NUMERIC_ONLY` 走旧数值渲染
- 信息页封面两级回退：`meta.json` 的 `image_file` → 谱面 `meta.artworkPath`
