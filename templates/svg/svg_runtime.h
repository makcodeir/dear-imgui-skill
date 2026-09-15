// svg_runtime.h — runtime helpers used by code generated with scripts/svg2drawlist.py
//
// Header-only, no deps beyond imgui.h + math.h. Include AFTER imgui.h.
// All animation functions take wall-clock seconds (ImGui::GetTime()) and loop
// like SMIL repeatCount="indefinite": the timeline (t - begin) wraps into
// [0, dur), keyframes are piecewise-linear (optionally eased per-segment via
// keySplines cubic-beziers), matching the SVG semantics for our subset.
#ifndef IMGUI_SVG_RUNTIME_H
#define IMGUI_SVG_RUNTIME_H

#include <imgui.h>
#include <math.h>

// Clamp/Lerp without imgui_internal.h (public API only).
static inline float  SvgClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int    SvgClampI(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }
static inline float  SvgLerpF(float a, float b, float t)    { return a + (b - a) * t; }

// Normalized progress in [0,1] of one looping cycle. begin/dur in seconds.
static inline float SvgLoopT(double t, double begin, double dur)
{
    double x = t - begin;
    if (dur <= 0.0)
        return 1.0f;
    x = fmod(fmod(x, dur) + dur, dur); // correct wrap for begin > t too
    return (float)(x / dur);
}

// cubic-bezier(p1x,p1y,p2x,p2y) easing — y at progress x via bisection.
// SVG keySplines "0.5 0 0.5 1" == ease-in-out-ish; matches browser accuracy
// to ~1e-4 with 18 steps, which is sub-pixel at any UI scale.
static inline float SvgEaseSpline(float u, float x1, float y1, float x2, float y2)
{
    if (u <= 0.0f) return 0.0f;
    if (u >= 1.0f) return 1.0f;
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 18; i++)
    {
        const float s = (lo + hi) * 0.5f, ms = 1.0f - s;
        const float sx = 3.0f * ms * ms * s * x1 + 3.0f * ms * s * s * x2 + s * s * s;
        if (sx < u) lo = s; else hi = s;
    }
    const float s = (lo + hi) * 0.5f, ms = 1.0f - s;
    return 3.0f * ms * ms * s * y1 + 3.0f * ms * s * s * y2 + s * s * s;
}

// Piecewise-linear loop over n keyframes.
//   keytimes: n ascending values in [0,1] or NULL for uniform spacing.
//   splines : (n-1)*4 keySpline control numbers or NULL for linear segments.
static inline float SvgKeysLoop(double t, double begin, double dur,
                                const float* keys, int n,
                                const float* keytimes, const float* splines)
{
    if (n <= 1)
        return keys[0];
    const float u = SvgLoopT(t, begin, dur);
    int seg = n - 2;
    if (keytimes)
    {
        for (int i = 0; i < n - 1; i++)
            if (u <= keytimes[i + 1]) { seg = i; break; }
        const float t0 = keytimes[seg], t1 = keytimes[seg + 1];
        float v = (t1 > t0) ? (u - t0) / (t1 - t0) : 1.0f;
        v = SvgClampF(v, 0.0f, 1.0f);
        if (splines)
            v = SvgEaseSpline(v, splines[seg * 4], splines[seg * 4 + 1],
                              splines[seg * 4 + 2], splines[seg * 4 + 3]);
        return SvgLerpF(keys[seg], keys[seg + 1], v);
    }
    const float uu = u * (float)(n - 1);
    int s0 = (int)uu;
    if (s0 > n - 2) s0 = n - 2;
    float v = uu - (float)s0;
    if (splines)
        v = SvgEaseSpline(v, splines[s0 * 4], splines[s0 * 4 + 1],
                          splines[s0 * 4 + 2], splines[s0 * 4 + 3]);
    return SvgLerpF(keys[s0], keys[s0 + 1], v);
}

// Channel-wise color interpolation helpers (straight alpha multiply).
static inline ImU32 SvgLerpU32(ImU32 a, ImU32 b, float v)
{
    const int r = (int)(float)((a >> IM_COL32_R_SHIFT) & 0xFF) + (int)(((float)((b >> IM_COL32_R_SHIFT) & 0xFF) - (float)((a >> IM_COL32_R_SHIFT) & 0xFF)) * v);
    const int g = (int)(float)((a >> IM_COL32_G_SHIFT) & 0xFF) + (int)(((float)((b >> IM_COL32_G_SHIFT) & 0xFF) - (float)((a >> IM_COL32_G_SHIFT) & 0xFF)) * v);
    const int bl = (int)(float)((a >> IM_COL32_B_SHIFT) & 0xFF) + (int)(((float)((b >> IM_COL32_B_SHIFT) & 0xFF) - (float)((a >> IM_COL32_B_SHIFT) & 0xFF)) * v);
    const int al = (int)(float)((a >> IM_COL32_A_SHIFT) & 0xFF) + (int)(((float)((b >> IM_COL32_A_SHIFT) & 0xFF) - (float)((a >> IM_COL32_A_SHIFT) & 0xFF)) * v);
    return IM_COL32(SvgClampI(r, 0, 255), SvgClampI(g, 0, 255), SvgClampI(bl, 0, 255), SvgClampI(al, 0, 255));
}

static inline ImU32 SvgMulAlpha(ImU32 c, float a)
{
    return (c & ~IM_COL32_A_MASK) | (((ImU32)SvgClampI((int)(255.0f * a), 0, 255)) << IM_COL32_A_SHIFT);
}

// Color keyframe loop; keytimes may be NULL (uniform).
static inline ImU32 SvgColorKeysLoop(double t, double begin, double dur,
                                     const ImU32* keys, int n, const float* keytimes)
{
    if (n <= 1)
        return keys[0];
    const float u = SvgLoopT(t, begin, dur);
    if (keytimes)
    {
        for (int i = 0; i < n - 1; i++)
            if (u <= keytimes[i + 1])
            {
                const float t0 = keytimes[i], t1 = keytimes[i + 1];
                float v = (t1 > t0) ? (u - t0) / (t1 - t0) : 1.0f;
                return SvgLerpU32(keys[i], keys[i + 1], SvgClampF(v, 0.0f, 1.0f));
            }
        return SvgLerpU32(keys[n - 2], keys[n - 1], 1.0f);
    }
    const float uu = u * (float)(n - 1);
    int s0 = (int)uu;
    if (s0 > n - 2) s0 = n - 2;
    return SvgLerpU32(keys[s0], keys[s0 + 1], uu - (float)s0);
}

// Rotate n points (2D user space) around (cx,cy) by deg degrees (SVG: +deg = CW
// because Y is down). dst and src may alias with dst==src being the SAME buffer
// only if you pass distinct pointers otherwise; here dst != src required.
static inline void SvgRotatePts(ImVec2* dst, const ImVec2* src, int n,
                                float cx, float cy, float deg)
{
    const float a = deg * 0.017453292519943295f; // PI/180
    const float co = cosf(a), si = sinf(a);
    for (int i = 0; i < n; i++)
    {
        const float dx = src[i].x - cx, dy = src[i].y - cy;
        dst[i].x = cx + dx * co - dy * si;
        dst[i].y = cy + dx * si + dy * co;
    }
}

#endif // IMGUI_SVG_RUNTIME_H
