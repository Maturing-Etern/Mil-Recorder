# Changelog

## v1.0 (2026-10-28)

- 音符旋转统一为 `90 - lineRotation`：原生链(RPU)与 REM 链(Godot)同域，修复原生分支斜轨音符左右镜像
- `MilLine::init` 幂等化：重置事件游标 + `noteGroups.clear()`，修复 REM 链音符双重提交（半透明叠加）
- 闪电(Fracture)特效不再过滤 `isFake`：REM 导出的闪电全部标 fake，此前粒子从不播放
- REM 速度积分改梯形模型，速度事件恒线性（无视 easing）；重叠速度段规范化 —— milplay 侧积分与 Godot 逐点一致（31 线 558 点残差 <0.003px）
- 难度字符串直通：`difficulty`/`difficultyLevel` 支持 `"13+"`/`"12.5"` 字面量，`difficulty_display` 原样显示；编译开关 `MIL_DIFF_NUMERIC_ONLY` 走旧数值渲染
- 信息页封面两级回退：`meta.json` 的 `image_file` → 谱面 `meta.artworkPath`
