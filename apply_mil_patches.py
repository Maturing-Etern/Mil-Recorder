import sys, io

# Mil-Recorder grain 单文件 3 处修复补丁（2026-09-04 调试定稿，可重放/幂等）
# 用法: python apply_mil_patches.py [src.cpp]  -> 就地修补
# grain 重建 source.cpp 后执行本脚本即可恢复全部修复。

PATCHES = [
    # 1) 删 init 卡死元凶: std::ios::sync_with_stdio(false);
    ("drop sync_with_stdio line",
     "std::ios::sync_with_stdio(false);",
     None),
    # 2) dumpMilObjects 裸 render -> 走 mainloopFrame (viewport/帧状态)
    ("dumpMilObjects use mainloopFrame",
     "renderer->render({ .base = { .time = t, .disableHitsound = true } });",
     "win.mainloopFrame({ .base = { .time = t, .isRenderingVideo = true } });"),
    # 3) MilTakeOverer::loadResources() 尾: illustionTexture 空指针兜底
    ("illustionTexture fallback in loadResources",
     "                    circHitEffectTexs.push_back(tex);",
     "                    circHitEffectTexs.push_back(tex);\n"
     "            if (!sharedComp.illustionTexture) {\n"
     "                sharedComp.illustionTexture = backgroundMask;\n"
     "            }"),
    # 4) GL33Canvas::drawRoundedRect (开场信息页卡片圆角, 2026-09-13)
    ("canvas drawRoundedRect",
     "                mesh.addRect(config.position, config.size, config.uvPosition, config.uvSize);\n"
     "                drawMesh(mesh);\n"
     "            }\n",
     "                mesh.addRect(config.position, config.size, config.uvPosition, config.uvSize);\n"
     "                drawMesh(mesh);\n"
     "            }\n"
     "\n"
     "            // 圆角矩形: 几何三角化(中央矩形 + 4 边条 + 4 角扇形), 复用默认纹理程序, 不改共享 shader\n"
     "            void drawRoundedRect(const DrawRectConfig& config, float64 radiusPx) noexcept {\n"
     "                if (config.size.x <= 0 || config.size.y <= 0) return;\n"
     "                if (radiusPx <= 0.5) { drawRect(config); return; }\n"
     "                constexpr double MPI = 3.141592653589793;\n"
     "                const float64 W = config.size.x, H = config.size.y;\n"
     "                const float64 r = std::min(radiusPx, std::min(W, H) * 0.5);\n"
     "                const int seg = 12;\n"
     "                const uint64 vcount = 6u + 24u + 4u * (uint64)seg * 3u;\n"
     "                auto mesh = glCtx->requestMesh(vcount);\n"
     "                mesh.color = config.color;\n"
     "                mesh.texture = config.texture;\n"
     "                auto addTri = [&](GLvec2 p, GLvec2 uv) { *mesh.vertices.next() = { p, uv, config.color }; };\n"
     "                auto uvOf = [&](float64 lx, float64 ly) {\n"
     "                    return GLvec2{ config.uvPosition.x + (lx / W) * config.uvSize.x,\n"
     "                                   config.uvPosition.y + (ly / H) * config.uvSize.y };\n"
     "                };\n"
     "                auto addQuad = [&](float64 ax, float64 ay, float64 bx, float64 by,\n"
     "                                   float64 cx, float64 cy, float64 dx, float64 dy) {\n"
     "                    GLvec2 A = config.position + GLvec2{ ax, ay };\n"
     "                    GLvec2 B = config.position + GLvec2{ bx, by };\n"
     "                    GLvec2 C = config.position + GLvec2{ cx, cy };\n"
     "                    GLvec2 D = config.position + GLvec2{ dx, dy };\n"
     "                    GLvec2 uA = uvOf(ax, ay), uB = uvOf(bx, by), uC = uvOf(cx, cy), uD = uvOf(dx, dy);\n"
     "                    addTri(A, uA); addTri(B, uB); addTri(C, uC);\n"
     "                    addTri(A, uA); addTri(C, uC); addTri(D, uD);\n"
     "                };\n"
     "                // 中央 + 4 边条(覆盖全矩形, 仅留 4 角方格给扇形)\n"
     "                addQuad(r, r, W - r, r, W - r, H - r, r, H - r);\n"
     "                addQuad(r, 0, W - r, 0, W - r, r, r, r);\n"
     "                addQuad(r, H - r, W - r, H - r, W - r, H, r, H);\n"
     "                addQuad(0, r, r, r, r, H - r, 0, H - r);\n"
     "                addQuad(W - r, r, W, r, W, H - r, W - r, H - r);\n"
     "                // 4 角扇形(屏幕 y 向下: 点 = (cx + r cosθ, cy + r sinθ))\n"
     "                auto fan = [&](float64 cx, float64 cy, float64 a0, float64 a1) {\n"
     "                    GLvec2 Cc = config.position + GLvec2{ cx, cy };\n"
     "                    GLvec2 uvC = uvOf(cx, cy);\n"
     "                    for (int i = 0; i < seg; i++) {\n"
     "                        double t0 = a0 + (a1 - a0) * ((double)i / seg);\n"
     "                        double t1 = a0 + (a1 - a0) * ((double)(i + 1) / seg);\n"
     "                        GLvec2 p0 = config.position + GLvec2{ cx + r * std::cos(t0), cy + r * std::sin(t0) };\n"
     "                        GLvec2 p1 = config.position + GLvec2{ cx + r * std::cos(t1), cy + r * std::sin(t1) };\n"
     "                        GLvec2 uv0 = uvOf(cx + r * std::cos(t0), cy + r * std::sin(t0));\n"
     "                        GLvec2 uv1 = uvOf(cx + r * std::cos(t1), cy + r * std::sin(t1));\n"
     "                        addTri(Cc, uvC); addTri(p0, uv0); addTri(p1, uv1);\n"
     "                    }\n"
     "                };\n"
     "                fan(r, r, MPI, MPI * 1.5);\n"
     "                fan(W - r, r, MPI * 1.5, MPI * 2.0);\n"
     "                fan(W - r, H - r, 0.0, MPI * 0.5);\n"
     "                fan(r, H - r, MPI * 0.5, MPI);\n"
     "                drawMesh(mesh);\n"
     "            }\n"),
    # 5) ggeometry::lineIsIntersectRect: 包源码用复合字面量 (Vec2[2]){...} 取地址,
    #    GCC 报 "taking address of temporary array" -> 改回具名数组 (2026-09-16)
    ("ggeometry lineIsIntersectRect named arrays",
     "    bool lineIsIntersectRect(const Vec2& linePoint, float64 lineDeg, const Rect& r) noexcept {\n"
     "        return (\n"
     "            lineIsIntersectLineSeg(linePoint, lineDeg, (Vec2[2]) { Vec2 { r.x, r.y }, Vec2 { r.x + r.w, r.y } }) ||\n"
     "            lineIsIntersectLineSeg(linePoint, lineDeg, (Vec2[2]) { Vec2 { r.x + r.w, r.y }, Vec2 { r.x + r.w, r.y + r.h } }) ||\n"
     "            lineIsIntersectLineSeg(linePoint, lineDeg, (Vec2[2]) { Vec2 { r.x + r.w, r.y + r.h }, Vec2 { r.x, r.y + r.h } }) ||\n"
     "            lineIsIntersectLineSeg(linePoint, lineDeg, (Vec2[2]) { Vec2 { r.x, r.y + r.h }, Vec2 { r.x, r.y } })\n"
     "        );\n"
     "    }\n",
     "    bool lineIsIntersectRect(const Vec2& linePoint, float64 lineDeg, const Rect& r) noexcept {\n"
     "        Vec2 _a1[2]={Vec2{r.x,r.y},Vec2{r.x+r.w,r.y}};\n"
     "        Vec2 _a2[2]={Vec2{r.x+r.w,r.y},Vec2{r.x+r.w,r.y+r.h}};\n"
     "        Vec2 _a3[2]={Vec2{r.x+r.w,r.y+r.h},Vec2{r.x,r.y+r.h}};\n"
     "        Vec2 _a4[2]={Vec2{r.x,r.y+r.h},Vec2{r.x,r.y}};\n"
     "        return (\n"
     "            lineIsIntersectLineSeg(linePoint, lineDeg, _a1) ||\n"
     "            lineIsIntersectLineSeg(linePoint, lineDeg, _a2) ||\n"
     "            lineIsIntersectLineSeg(linePoint, lineDeg, _a3) ||\n"
     "            lineIsIntersectLineSeg(linePoint, lineDeg, _a4)\n"
     "        );\n"
     "    }\n"),
]

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "source.cpp.patched.cpp"
    s = open(src, encoding="utf-8").read()
    for name, old, new in PATCHES:
        n = s.count(old)
        if new is not None and new in s:
            print("[skip] %s (already applied)" % name)
        elif n == 0:
            print("[skip] %s (not found)" % name)
        elif new is None:
            s = s.replace(old, "")
            print("[ok]   %s (removed)" % name)
        else:
            s = s.replace(old, new)
            print("[ok]   %s (%d site)" % (name, n))
    open(src, "w", encoding="utf-8").write(s)
    print("done ->", src)

if __name__ == "__main__":
    main()
