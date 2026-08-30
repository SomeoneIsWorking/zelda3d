#pragma once

// Shared GLSL implementation of Grezzo/PICA200's generic texture-environment combiner. Both the
// established CMB shader and the unified CMB variant inject these exact bytes into their templates;
// keeping a second hand-copied evaluator would let PREVBUF/constant/alpha semantics drift.
namespace Fast::Zelda3DTev {

inline constexpr const char* kGenericFunctions = R"GLSL(
bool alphaPass(float a, float ref, int f) {
    if (f == 0) return false;
    if (f == 1) return a < ref;
    if (f == 2) return a == ref;
    if (f == 3) return a <= ref;
    if (f == 4) return a > ref;
    if (f == 5) return a != ref;
    if (f == 6) return a >= ref;
    return true;
}
vec4 tevSrc(uint code, vec4 prim, vec4 fragPrimary, vec4 fragSecondary, vec4 t0, vec4 t1, vec4 t2,
            vec4 prev, vec4 pbuf, uint kidx) {
    if (code == 0u) return prim;
    if (code == 1u) return fragPrimary;
    if (code == 2u) return fragSecondary;
    if (code == 3u || code == 6u) return t0;
    if (code == 4u) return t1;
    if (code == 5u) return t2;
    if (code == 14u) return unpackUnorm4x8(ubo.uTevConst[kidx >> 2][kidx & 3u]);
    if (code == 15u) return prev;
    if (code == 13u) return pbuf;
    return vec4(0.0);
}
vec3 tevColorMod(uint m, vec4 s) {
    if (m == 0u) return s.rgb;
    if (m == 1u) return 1.0 - s.rgb;
    if (m == 2u) return vec3(s.a);
    if (m == 3u) return vec3(1.0 - s.a);
    if (m == 4u) return vec3(s.r);
    if (m == 5u) return vec3(1.0 - s.r);
    if (m == 8u) return vec3(s.g);
    if (m == 9u) return vec3(1.0 - s.g);
    if (m == 12u) return vec3(s.b);
    if (m == 13u) return vec3(1.0 - s.b);
    return s.rgb;
}
float tevAlphaMod(uint m, vec4 s) {
    if (m == 0u) return s.a;
    if (m == 1u) return 1.0 - s.a;
    if (m == 2u) return s.r;
    if (m == 3u) return 1.0 - s.r;
    if (m == 4u) return s.g;
    if (m == 5u) return 1.0 - s.g;
    if (m == 6u) return s.b;
    if (m == 7u) return 1.0 - s.b;
    return s.a;
}
vec3 tevColorOp(uint op, vec3 a, vec3 b, vec3 c) {
    if (op == 0u) return a;
    if (op == 1u) return a * b;
    if (op == 2u) return a + b;
    if (op == 3u) return a + b - 0.5;
    if (op == 4u) return mix(b, a, c);
    if (op == 5u) return a - b;
    if (op == 6u || op == 7u) return vec3(4.0 * dot(a - 0.5, b - 0.5));
    if (op == 8u) return a * b + c;
    return clamp(a + b, 0.0, 1.0) * c;
}
float tevAlphaOp(uint op, float a, float b, float c) {
    if (op == 0u) return a;
    if (op == 1u) return a * b;
    if (op == 2u) return a + b;
    if (op == 3u) return a + b - 0.5;
    if (op == 4u) return mix(b, a, c);
    if (op == 5u) return a - b;
    if (op == 8u) return a * b + c;
    if (op == 9u) return clamp(a + b, 0.0, 1.0) * c;
    return a;
}
vec4 tevRun(vec4 prim, vec4 fragPrimary, vec4 fragSecondary, vec4 t0, vec4 t1, vec4 t2) {
    vec4 prev = vec4(0.0);
    vec4 buf = vec4(0.0);
    vec4 nextbuf = vec4(0.0);
    int n = int(ubo.uTevCtl.x + 0.5);
    for (int s = 0; s < n; s++) {
        uvec4 w = ubo.uTevStages[s];
        uint kidx = (w.y >> 24) & 7u;
        vec4 sa = tevSrc(w.x & 15u, prim, fragPrimary, fragSecondary, t0, t1, t2, prev, buf, kidx);
        vec4 sb = tevSrc((w.x >> 4) & 15u, prim, fragPrimary, fragSecondary, t0, t1, t2, prev, buf, kidx);
        vec4 sc = tevSrc((w.x >> 8) & 15u, prim, fragPrimary, fragSecondary, t0, t1, t2, prev, buf, kidx);
        vec3 ca = tevColorMod(w.y & 15u, sa);
        vec3 cb = tevColorMod((w.y >> 4) & 15u, sb);
        vec3 cc = tevColorMod((w.y >> 8) & 15u, sc);
        vec3 rgb = tevColorOp(w.z & 15u, ca, cb, cc);
        rgb = clamp(rgb * float(1u << ((w.z >> 8) & 3u)), 0.0, 1.0);
        float alpha;
        if (((w.z >> 4) & 15u) == 7u) {
            alpha = rgb.r;
        } else {
            // PICA's source word has RGB at bits 0/4/8, then alpha at 16/20/24. The
            // four-bit gap is real; modifiers use a different layout and do start at bit 12.
            vec4 aa = tevSrc((w.x >> 16) & 15u, prim, fragPrimary, fragSecondary, t0, t1, t2, prev, buf, kidx);
            vec4 ab = tevSrc((w.x >> 20) & 15u, prim, fragPrimary, fragSecondary, t0, t1, t2, prev, buf, kidx);
            vec4 ac = tevSrc((w.x >> 24) & 15u, prim, fragPrimary, fragSecondary, t0, t1, t2, prev, buf, kidx);
            float fa = tevAlphaMod((w.y >> 12) & 15u, aa);
            float fb = tevAlphaMod((w.y >> 16) & 15u, ab);
            float fc = tevAlphaMod((w.y >> 20) & 15u, ac);
            alpha = tevAlphaOp((w.z >> 4) & 15u, fa, fb, fc);
        }
        alpha = clamp(alpha * float(1u << ((w.z >> 10) & 3u)), 0.0, 1.0);
        prev = vec4(rgb, alpha);
        buf = nextbuf;
        uint latch = (s + 1 < n) ? ubo.uTevStages[s + 1].z : 0u;
        if ((latch & 4096u) != 0u) nextbuf.rgb = prev.rgb;
        if ((latch & 8192u) != 0u) nextbuf.a = prev.a;
    }
    return prev;
}
)GLSL";

} // namespace Fast::Zelda3DTev
