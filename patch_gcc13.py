import re, sys

src = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\Lenovo\.grain_data\build\source.cpp'

with open(src, 'r', encoding='utf-8') as f:
    c = f.read()


old = '''    bool lineIsIntersectRect(const Vec2& linePoint, float64 lineDeg, const Rect& r) noexcept {
        return (
            lineIsIntersectLineSeg(linePoint, lineDeg, (Vec2[2]) { Vec2 { r.x, r.y }, Vec2 { r.x + r.w, r.y } }) ||
            lineIsIntersectLineSeg(linePoint, lineDeg, (Vec2[2]) { Vec2 { r.x + r.w, r.y }, Vec2 { r.x + r.w, r.y + r.h } }) ||
            lineIsIntersectLineSeg(linePoint, lineDeg, (Vec2[2]) { Vec2 { r.x + r.w, r.y + r.h }, Vec2 { r.x, r.y + r.h } }) ||
            lineIsIntersectLineSeg(linePoint, lineDeg, (Vec2[2]) { Vec2 { r.x, r.y + r.h }, Vec2 { r.x, r.y } })
        );
    }'''

new = '''    bool lineIsIntersectRect(const Vec2& linePoint, float64 lineDeg, const Rect& r) noexcept {
        Vec2 _a1[2]={Vec2{r.x,r.y},Vec2{r.x+r.w,r.y}};
        Vec2 _a2[2]={Vec2{r.x+r.w,r.y},Vec2{r.x+r.w,r.y+r.h}};
        Vec2 _a3[2]={Vec2{r.x+r.w,r.y+r.h},Vec2{r.x,r.y+r.h}};
        Vec2 _a4[2]={Vec2{r.x,r.y+r.h},Vec2{r.x,r.y}};
        return (
            lineIsIntersectLineSeg(linePoint, lineDeg, _a1) ||
            lineIsIntersectLineSeg(linePoint, lineDeg, _a2) ||
            lineIsIntersectLineSeg(linePoint, lineDeg, _a3) ||
            lineIsIntersectLineSeg(linePoint, lineDeg, _a4)
        );
    }'''

assert old in c, 'ERROR: function pattern not found!'
c = c.replace(old, new)
print('  [OK] Fixed lineIsIntersectRect')


old2 = '''                if (!quadStrictlyIntersectRect((Vec2[4]) {
                    poly.p1, poly.p2, poly.p3, poly.p4
                }, screenRect)) objects.pop_back();'''

new2 = '''                {
                    Vec2 _q[4] = { poly.p1, poly.p2, poly.p3, poly.p4 };
                    if (!quadStrictlyIntersectRect(_q, screenRect)) objects.pop_back();
                }'''

count = c.count(old2)
c = c.replace(old2, new2)
print(f'  [OK] Fixed sharedCulling Vec2[4] ({count} occurrence(s))')


rem = c.count('(Vec2[2])') + c.count('(Vec2[4])')
if rem > 0:
    print(f'  [WARN] {rem} C-casts remaining!')
else:
    print('  [OK] No remaining C-casts')

out_path = src + '.patched.cpp'
with open(out_path, 'w', encoding='utf-8') as f:
    f.write(c)

print(f'  [OK] Output: {out_path}')
