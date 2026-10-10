#ifndef MIL_FX_EMITTER_HPP
#define MIL_FX_EMITTER_HPP

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

namespace milfx {

using Fx = double;
using u64 = std::uint64_t;

enum FxTexture { TexLightning2 = 0 };

enum FxLayer {
    LayerVerticalStrike = 0,
    LayerLongLightning  = 1,
    LayerExplode        = 2,
    LayerPass           = 3,
    LayerCount          = 4,
};

struct SizeKey  { Fx t, k; };
struct ColorKey { Fx t, r, g, b, a; };

struct Layer {
    FxTexture texture;
    bool additive;
    bool stretch;
    bool omnidirectional;
    Fx lengthScale;
    Fx endLengthScale;
    Fx endWidthScale;
    u64 count;
    Fx lifeMin, lifeMax;
    Fx speedMin, speedMax;
    Fx sizeMin, sizeMax;
    Fx gravity;
    Fx coneDeg;
    Fx emitRadius;
    Fx scale;
    SizeKey const* sizeKeys;
    u64 sizeKeyCount;
};

struct ColorTrack { ColorKey const* keys; u64 count; };

struct Particle {
    Fx life;
    Fx dirX, dirY;
    Fx speed;
    Fx size;
    Fx emitX, emitY;
    Fx gravity;
};

struct Quad {
    Fx x, y;
    Fx halfLen, halfWid;
    Fx rotDeg;
    Fx r, g, b, a;
    FxTexture texture;
    bool additive;
};

inline constexpr SizeKey kPassSizeCurve[] = {
    { 0.0,      0.303226 },
    { 0.495049, 0.984480 },
    { 1.0,      0.0      },
};

inline constexpr Layer kLayers[LayerCount] = {
    { TexLightning2, true,  true,  false, 6.0, 0.0,      1.0,     1,  0.15, 0.15,  0.5,   1.0,  0.77,    0.77,    0.0, 2.0, 0.01,      1.0, nullptr, 0 },
    { TexLightning2, true,  true,  true,  6.0, 0.31915,  0.0,     6,  0.25, 0.35, -0.1,  -0.1,  0.2,     0.4,     0.0, 0.0, 0.01,      1.0, nullptr, 0 },
    { TexLightning2, true,  true,  true,  5.0, 0.0,      0.0,    14,  0.15, 0.35,  4.0,   7.0,  0.13333, 0.16667, 0.0, 0.0, 0.5,       1.0, nullptr, 0 },
    { TexLightning2, true,  false, false, 1.0, 1.0,      1.0,    25,  0.35, 0.90,  1.0,   3.5,  0.06,    0.12,    0.0, 0.0, 0.4263172, 1.5, kPassSizeCurve, 3 },
};

inline constexpr ColorKey kColors0[] = {
    { 0.0,    1.0,      0.168627, 0.184314, 1.0 },
    { 0.3703, 0.921569, 0.266667, 0.313725, 1.0 },
    { 0.4140, 0.043137, 0.843137, 1.0,      0.0 },
    { 0.5772, 0.0,      0.0,      0.0,      1.0 },
    { 1.0,    0.0,      0.0,      0.0,      0.0 },
};
inline constexpr ColorKey kColors1[] = {
    { 0.0,    1.0,      0.168627, 0.184314, 1.0 },
    { 0.5772, 0.921569, 0.266667, 0.313725, 1.0 },
    { 1.0,    0.043137, 0.843137, 1.0,      0.0 },
};
inline constexpr ColorKey kColors2[] = {
    { 0.0,    1.0,      0.168627, 0.184314, 1.0 },
    { 0.5772, 0.921569, 0.266667, 0.313725, 1.0 },
    { 1.0,    0.0,      0.0,      0.0,      0.0 },
};
inline constexpr ColorKey kColors3[] = {
    { 0.0, 0.874510, 0.560784, 1.0,      1.0      },
    { 1.0, 1.0,      0.168627, 0.184314, 0.470588 },
};

inline constexpr ColorTrack kColorTracks[LayerCount] = {
    { kColors0, 5 },
    { kColors1, 3 },
    { kColors2, 3 },
    { kColors3, 2 },
};

inline Fx rand01(u64 seed) noexcept {
    u64 x = seed + 0x9E3779B97F4A7C15ull;
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (Fx)(x >> 11) / (Fx)(1ull << 53);
}

inline void colorAt(ColorTrack const& track, Fx p, Fx out[4]) noexcept {
    auto keys = track.keys;
    auto n = track.count;
    if (n == 0) { out[0] = out[1] = out[2] = out[3] = 1.0; return; }
    auto put = [&](ColorKey const& k) { out[0] = k.r; out[1] = k.g; out[2] = k.b; out[3] = k.a; };
    if (p <= keys[0].t) { put(keys[0]); return; }
    for (u64 i = 1; i < n; i++) {
        if (p <= keys[i].t) {
            auto& a = keys[i - 1];
            auto& b = keys[i];
            auto span = b.t - a.t;
            auto k = span > 1e-9 ? (p - a.t) / span : 0.0;
            out[0] = a.r + (b.r - a.r) * k;
            out[1] = a.g + (b.g - a.g) * k;
            out[2] = a.b + (b.b - a.b) * k;
            out[3] = a.a + (b.a - a.a) * k;
            return;
        }
    }
    put(keys[n - 1]);
}

inline Fx sizeCurveAt(Layer const& spec, Fx p) noexcept {
    auto keys = spec.sizeKeys;
    auto n = spec.sizeKeyCount;
    if (keys == nullptr || n == 0) return 1.0;
    if (p <= keys[0].t) return keys[0].k;
    for (u64 i = 1; i < n; i++) {
        if (p <= keys[i].t) {
            auto span = keys[i].t - keys[i - 1].t;
            auto k = span > 1e-9 ? (p - keys[i - 1].t) / span : 0.0;
            return keys[i - 1].k + (keys[i].k - keys[i - 1].k) * k;
        }
    }
    return keys[n - 1].k;
}

inline void build(std::vector<Particle>& out, Layer const& spec, u64 layerIndex, u64 seedBase) {
    for (u64 k = 0; k < spec.count; k++) {
        auto seed = seedBase ^ (layerIndex * 8191ull) ^ (k * 2654435761ull);

        auto r0 = rand01(seed);
        auto r1 = rand01(seed ^ 0x11111111ull);
        auto r2 = rand01(seed ^ 0x22222222ull);
        auto r3 = rand01(seed ^ 0x33333333ull);
        auto r4 = rand01(seed ^ 0x44444444ull);
        auto r5 = rand01(seed ^ 0x55555555ull);

        auto& p = out.emplace_back();
        p.life = spec.lifeMin + (spec.lifeMax - spec.lifeMin) * r0;
        p.speed = (spec.speedMin + (spec.speedMax - spec.speedMin) * r1) * spec.scale;
        p.size  = (spec.sizeMin  + (spec.sizeMax  - spec.sizeMin)  * r2) * spec.scale;
        p.gravity = spec.gravity;

        Fx angDeg = spec.omnidirectional
            ? r3 * 360.0
            : 90.0 + (r3 * 2.0 - 1.0) * spec.coneDeg;
        Fx rad = angDeg * 3.14159265358979323846 / 180.0;
        p.dirX = std::cos(rad);
        p.dirY = std::sin(rad);

        Fx emitR = spec.emitRadius * spec.scale * std::sqrt(r4);
        Fx emitA = r5 * 2.0 * 3.14159265358979323846;
        p.emitX = std::cos(emitA) * emitR;
        p.emitY = std::sin(emitA) * emitR;
    }
}

inline void emit(std::vector<Quad>& out, Layer const& spec, u64 layerIndex,
                 std::vector<Particle> const& parts, Fx dt,
                 Fx originX, Fx originY, Fx pixelScale) {
    for (auto& p : parts) {
        if (p.life <= 1e-9) continue;
        if (dt < 0.0 || dt > p.life) continue;

        Fx prog = dt / p.life;
        if (prog < 0.0) prog = 0.0;
        if (prog > 1.0) prog = 1.0;

        Fx vx = p.dirX * p.speed;
        Fx vy = p.dirY * p.speed;
        Fx gy = -9.81 * p.gravity;

        Fx wx = p.emitX + vx * dt;
        Fx wy = p.emitY + vy * dt + 0.5 * gy * dt * dt;

        Fx rgba[4];
        colorAt(kColorTracks[layerIndex], prog, rgba);
        if (rgba[3] <= 0.0) continue;

        Fx halfLen = 0.0, halfWid = 0.0, rotDeg = 0.0;

        if (spec.stretch) {
            Fx lenCurve = spec.endLengthScale + (1.0 - spec.endLengthScale) * (1.0 - prog);
            Fx widCurve = spec.endWidthScale + (1.0 - spec.endWidthScale) * (1.0 - prog);

            Fx len = p.size * spec.lengthScale * lenCurve * pixelScale;
            Fx wid = p.size * widCurve * pixelScale;
            if (len <= 0.0 || wid <= 0.0) continue;

            Fx svy = vy + gy * dt;
            rotDeg = std::atan2(-svy, vx) * 180.0 / 3.14159265358979323846;
            halfLen = len / 2.0;
            halfWid = wid / 2.0;
        } else {
            Fx side = p.size * sizeCurveAt(spec, prog) * pixelScale;
            if (side <= 0.0) continue;
            halfLen = halfWid = side / 2.0;
        }

        auto& q = out.emplace_back();
        q.x = originX + wx * pixelScale;
        q.y = originY - wy * pixelScale;
        q.halfLen = halfLen;
        q.halfWid = halfWid;
        q.rotDeg = rotDeg;
        q.r = rgba[0]; q.g = rgba[1]; q.b = rgba[2]; q.a = rgba[3];
        q.texture = spec.texture;
        q.additive = spec.additive;
    }
}

inline void emitEvent(std::vector<Quad>& out, Layer const& spec, u64 layerIndex, u64 seedBase,
                      Fx dt, Fx originX, Fx originY, Fx pixelScale,
                      std::vector<Particle>& scratch) {
    scratch.clear();
    build(scratch, spec, layerIndex, seedBase);
    emit(out, spec, layerIndex, scratch, dt, originX, originY, pixelScale);
}

}

#ifdef MILFX_SELFTEST
#include <cstdio>
int main() {
    using namespace milfx;
    char const* names[LayerCount] = { "VerticalStrike", "LongLightning", "Explode", "Pass" };
    for (u64 L = 0; L < LayerCount; L++) {
        auto const& spec = kLayers[L];
        std::vector<Particle> parts;
        build(parts, spec, L, 0x1234ull);
        for (Fx dt : { 0.0, 0.05, 0.15, 0.3, 0.6 }) {
            std::vector<Quad> qs;
            emit(qs, spec, L, parts, dt, 960.0, 540.0, 48.0);
            if (qs.empty()) { std::printf("%-14s %5.2f  %3zu  none\n", names[L], dt, qs.size()); continue; }
            auto& q = qs[0];
            std::printf("%-14s %5.2f  %3zu  (%7.2f,%7.2f) (%6.2f,%6.2f) %.2f,%.2f,%.2f,%.2f\n",
                names[L], dt, qs.size(), q.x, q.y, q.halfLen, q.halfWid, q.r, q.g, q.b, q.a);
        }
    }
    std::vector<Particle> a, b;
    build(a, kLayers[LayerExplode], LayerExplode, 42ull);
    build(b, kLayers[LayerExplode], LayerExplode, 42ull);
    bool same = a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); i++)
        same = (a[i].life == b[i].life) && (a[i].dirX == b[i].dirX) && (a[i].speed == b[i].speed);
    std::printf("\ndeterministic: %s\n", same ? "OK" : "FAIL");
    return same ? 0 : 1;
}
#endif

#endif
