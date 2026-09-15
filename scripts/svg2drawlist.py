#!/usr/bin/env python3
"""svg2drawlist.py — convert (animated) SVG to a C++ header of ImDrawList calls.

Dear ImGui cannot render SVG at runtime. This tool "rasterizes to code": it
parses an SVG subset (circle/ellipse/rect/line/polyline/polygon/path/text and
the common SMIL <animate>/<animateTransform> patterns) and emits one header
with, per input file, a function:

    void DrawSvg_<name>(ImDrawList* dl, ImVec2 origin, ImVec2 size, float t,
                        ImU32 tint = IM_COL32(255,255,255,255));

  origin/size : screen box the viewBox maps into
  t           : seconds — pass ImGui::GetTime() to animate
  tint        : replaces the color given to --tint (for monochrome icons)

SMIL animations become runtime interpolations on top of svg_runtime.h
(SvgKeysLoop / SvgColorKeysLoop / SvgRotatePts ...), incl. keyTimes/keySplines.
Unsupported constructs are emitted as `// WARN:` comments in the output and
echoed on stderr — never silently dropped. Stdlib only; needs no resvg/cairo.

Usage:
    python3 svg2drawlist.py icon.svg [-o out.h] [--tint FFFFFF] [--runtime]
    python3 svg2drawlist.py dir_of_svgs/ -o out_all.h [-r]   # batch
    python3 svg2drawlist.py file_or_dir --check              # lint only
"""
import argparse
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

# --------------------------------------------------------------------- util --

NUM = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")

NAMED_COLORS = {
    "black": "000000", "white": "ffffff", "red": "ff0000", "green": "008000",
    "blue": "0000ff", "yellow": "ffff00", "cyan": "00ffff", "magenta": "ff00ff",
    "gray": "808080", "grey": "808080", "orange": "ffa500", "purple": "800080",
    "lime": "00ff00", "navy": "000080", "teal": "008080", "silver": "c0c0c0",
    "gold": "ffd700", "pink": "ffc0cb", "brown": "a52a2a", "maroon": "800000",
    "olive": "808000", "aqua": "00ffff", "fuchsia": "ff00ff", "limegreen": "32cd32",
}


def local(tag):
    return tag.rsplit('}', 1)[-1]


def nums(s):
    return [float(x) for x in NUM.findall(s or "")]


def num(s, default=0.0):
    v = nums(s)
    return v[0] if v else default


def seconds(s, default=None):
    """'2s' | '1500ms' | '2' | None -> float seconds or default."""
    if s is None:
        return default
    v = nums(s)
    if not v:
        return default
    return v[0] / 1000.0 if "ms" in s else v[0]


def f4(x):
    """C++ float literal from a python number."""
    s = "%.6g" % float(x)
    if "." not in s and "e" not in s and "inf" not in s:
        s += ".0"
    return s + "f"


def parse_color(spec, ctx, where):
    """'#rgb'|'#rrggbb'|name|'none'|'url(#id)' -> (r,g,b) 0..1 | None | ('url',id)"""
    if spec is None:
        return None
    s = spec.strip().lower()
    if s == "none":
        return None
    if s.startswith("#"):
        h = s[1:]
        if len(h) == 3:
            h = "".join(c * 2 for c in h)
        if len(h) != 6:
            ctx.warn("bad color '%s' (%s) -> white" % (spec, where))
            h = "ffffff"
        try:
            return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
        except ValueError:
            ctx.warn("bad color '%s' (%s) -> white" % (spec, where))
            return (1.0, 1.0, 1.0)
    if s in NAMED_COLORS:
        return parse_color("#" + NAMED_COLORS[s], ctx, where)
    if s.startswith("url("):
        return ("url", s[4:].strip().rstrip(")").lstrip("#"))
    ctx.warn("unsupported color '%s' (%s) -> white" % (spec, where))
    return (1.0, 1.0, 1.0)


def style_attrs(el):
    """Element attributes merged with style='a:b;c:d' (style wins)."""
    at = {}
    for k, v in el.attrib.items():
        at[k] = v
    st = at.pop("style", None)
    if st:
        for part in st.split(";"):
            if ":" in part:
                k, v = part.split(":", 1)
                at[k.strip()] = v.strip()
    return at


class Ctx:
    def __init__(self):
        self.code = []
        self.warns = []
        self.gradients = {}
        self.morphs = {}     # sid -> [d string keyframes]
        self.pivot = None    # rotate pivot for the shape being emitted
        self.sid = 0
        self.tint_rgb = None
        self.box = [0, 0, 100, 100]
        self.pivot_hint = None

    def warn(self, msg):
        self.warns.append(msg)
        self.code.append("    // WARN: " + msg)


# -------------------------------------------------------------- transforms --

IDENT = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def matmul(m1, m2):
    a1, b1, c1, d1, x1, y1 = m1
    a2, b2, c2, d2, x2, y2 = m2
    return (a1 * a2 + b1 * c2, a1 * b2 + b1 * d2,
            c1 * a2 + d1 * c2, c1 * b2 + d1 * d2,
            x1 * a2 + y1 * c2 + x2, x1 * b2 + y1 * d2 + y2)


def matapply(m, p):
    a, b, c, d, tx, ty = m
    return (a * p[0] + c * p[1] + tx, b * p[0] + d * p[1] + ty)


def parse_transform(s, ctx):
    m = IDENT
    for fn, arg in re.findall(r"(\w+)\s*\(([^)]*)\)", s or ""):
        a = nums(arg)
        if fn == "translate":
            t = (1, 0, 0, 1, a[0] if a else 0, a[1] if len(a) > 1 else 0)
        elif fn == "scale":
            t = (a[0], 0, 0, (a[1] if len(a) > 1 else a[0]), 0, 0) if a else IDENT
        elif fn == "rotate":
            ang = math.radians(a[0]) if a else 0.0
            cx, cy = (a[1], a[2]) if len(a) > 2 else (0.0, 0.0)
            co, si = math.cos(ang), math.sin(ang)
            t = (co, si, -si, co, cx - co * cx + si * cy, cy - si * cx - co * cy)
        elif fn == "matrix" and len(a) == 6:
            t = tuple(a)
        else:
            ctx.warn("transform '%s(...)' unsupported -> ignored" % fn)
            t = IDENT
        m = matmul(m, t)
    return m


def is_scale_translate(m):
    """True if the matrix cannot shear/rotate -> circle/rect stay primitives."""
    return abs(m[1]) < 1e-9 and abs(m[2]) < 1e-9 and m[0] > 0 and m[3] > 0


# -------------------------------------------------------------------- path --

ARC_W = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "S": 4, "Q": 4, "T": 2, "A": 7}


def tokenize_path(d):
    """-> [(letter, [nums])], implicit command repetition normalized."""
    toks = []
    i, n = 0, len(d or "")
    while i < n:
        ch = d[i]
        if ch.isalpha():
            i += 1
            vals = []
            while i < n:
                if d[i] in " ,\t\r\n":
                    i += 1
                    continue
                if d[i].isalpha():
                    break
                mm = NUM.match(d, i)
                if not mm:
                    i += 1
                    continue
                vals.append(float(mm.group()))
                i = mm.end()
            toks.append((ch, vals))
        else:
            i += 1
    out = []
    for ch, vals in toks:
        up = ch.upper()
        w = ARC_W.get(up, 0)
        if up == "Z" or w == 0:
            out.append((ch, vals))
        elif ch in "Mm":
            # first pair = moveto, subsequent pairs = lineto (per SVG spec)
            for k in range(0, len(vals) - 1, 2):
                out.append((ch if k == 0 else ("l" if ch.islower() else "L"), vals[k:k + 2]))
        elif vals:
            for k in range(0, len(vals) - w + 1, w):
                out.append((ch, vals[k:k + w]))
        else:
            out.append((ch, vals))
    return out


def path_segments(d, ctx, where):
    """Normalize 'd' to absolute segments: ('M',p) ('L',p) ('C',c1,c2,p)
       ('Q',c,p) ('Z',). Arcs are converted to cubics (exact enough, 1/4 turn).
       Unsupported letters are warned and skipped."""
    segs = []
    cur = (0.0, 0.0)
    start = (0.0, 0.0)
    prev = None
    for ch, v in tokenize_path(d):
        up = ch.upper()
        rel = ch.islower()

        def pt(x, y):
            return (cur[0] + x, cur[1] + y) if rel else (x, y)

        if up == "Z":
            segs.append(("Z",))
            cur = start
        elif up == "M":
            if len(v) < 2:
                continue
            cur = start = pt(v[0], v[1])
            segs.append(("M", cur))
        elif up == "L":
            if len(v) < 2:
                continue
            cur = pt(v[0], v[1])
            segs.append(("L", cur))
        elif up == "H":
            cur = (cur[0] + v[0] if rel else v[0], cur[1])
            segs.append(("L", cur))
        elif up == "V":
            cur = (cur[0], cur[1] + v[0] if rel else v[0])
            segs.append(("L", cur))
        elif up == "C":
            c1, c2, e = pt(*v[0:2]), pt(*v[2:4]), pt(*v[4:6])
            segs.append(("C", c1, c2, e))
            cur = e
        elif up == "S":
            c2, e = pt(*v[0:2]), pt(*v[2:4])
            c1 = (2 * cur[0] - prev[3][0], 2 * cur[1] - prev[3][1]) \
                if prev and prev[0] == "C" else cur
            segs.append(("C", c1, c2, e))
            cur = e
        elif up == "Q":
            c1, e = pt(*v[0:2]), pt(*v[2:4])
            segs.append(("Q", c1, e))
            cur = e
        elif up == "T":
            e = pt(*v[0:2])
            c1 = (2 * cur[0] - prev[1][0], 2 * cur[1] - prev[1][1]) \
                if prev and prev[0] == "Q" else cur
            segs.append(("Q", c1, e))
            cur = e
        elif up == "A":
            rx, ry, xrot, laf, sf, x, y = v
            e = pt(x, y)
            for c1, c2, e2 in arc_to_cubics(cur, (abs(rx), abs(ry)), xrot, laf, sf, e):
                segs.append(("C", c1, c2, e2))
            cur = e
        else:
            ctx.warn("path command '%s' unsupported (%s) -> skipped" % (ch, where))
            continue
        prev = segs[-1] if segs else None
    return segs


def arc_to_cubics(p0, r, phi_deg, large, sweep, p1):
    """SVG endpoint arc -> list of cubic ((c1,c2,end)) — spec F.6.5."""
    rx, ry = r
    if p0 == p1 or rx == 0 or ry == 0:
        return [] if p0 == p1 else [(p0, p1, p1)]
    phi = math.radians(phi_deg)
    dx, dy = (p0[0] - p1[0]) / 2.0, (p0[1] - p1[1]) / 2.0
    x1 = math.cos(phi) * dx + math.sin(phi) * dy
    y1 = -math.sin(phi) * dx + math.cos(phi) * dy
    lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry)
    if lam > 1:
        rad = math.sqrt(lam)
        rx *= rad
        ry *= rad
    num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    den = rx * rx * y1 * y1 + ry * ry * x1 * x1
    co = (math.sqrt(max(num, 0.0) / den) if den else 0.0)
    if bool(large) == bool(sweep):
        co = -co
    cxp = co * rx * y1 / ry
    cyp = -co * ry * x1 / rx
    ccx = math.cos(phi) * cxp - math.sin(phi) * cyp + (p0[0] + p1[0]) / 2.0
    ccy = math.sin(phi) * cxp + math.cos(phi) * cyp + (p0[1] + p1[1]) / 2.0

    uvec = lambda ux, uy: math.atan2(uy, ux)
    th1 = uvec((x1 - cxp) / rx, (y1 - cyp) / ry)
    th2 = uvec((-x1 - cxp) / rx, (-y1 - cyp) / ry)
    dth = th2 - th1
    if not sweep and dth > 0:
        dth -= 2 * math.pi
    if sweep and dth < 0:
        dth += 2 * math.pi
    seg = max(1, int(math.ceil(abs(dth) / (math.pi / 2.0))))
    dseg = dth / seg
    t = 4.0 / 3.0 * math.tan(dseg / 4.0)

    def eval_pt(th):
        cs, sn = math.cos(th), math.sin(th)
        return (ccx + rx * cs * math.cos(phi) - ry * sn * math.sin(phi),
                ccy + rx * cs * math.sin(phi) + ry * sn * math.cos(phi))

    def eval_der(th):
        cs, sn = math.cos(th), math.sin(th)
        return (-rx * sn * math.cos(phi) - ry * cs * math.sin(phi),
                -rx * sn * math.sin(phi) + ry * cs * math.cos(phi))

    out = []
    th = th1
    for _ in range(seg):
        thn = th + dseg
        p1s, d1 = eval_pt(th), eval_der(th)
        p2s, d2 = eval_pt(thn), eval_der(thn)
        out.append(((p1s[0] + t * d1[0], p1s[1] + t * d1[1]),
                    (p2s[0] - t * d2[0], p2s[1] - t * d2[1]),
                    p2s))
        th = thn
    return out


def cubic_at(p0, c1, c2, p1, u):
    mu = 1.0 - u
    return tuple(mu * mu * mu * a + 3 * mu * mu * u * b + 3 * mu * u * u * c + u * u * u * d
                 for a, b, c, d in ((p0[0], c1[0], c2[0], p1[0]), (p0[1], c1[1], c2[1], p1[1])))


def quad_at(p0, c1, p1, u):
    mu = 1.0 - u
    return tuple(mu * mu * a + 2 * mu * u * b + u * u * c
                 for a, b, c in ((p0[0], c1[0], p1[0]), (p0[1], c1[1], p1[1])))


def flatten(segs, per_curve=16):
    """segments -> list of subpath point lists (polylines). Corner points kept."""
    subs, cur, start = [], [], None
    for s in segs:
        if s[0] == "M":
            if cur:
                subs.append(cur)
            cur, start = [s[1]], s[1]
        elif s[0] == "L":
            cur.append(s[1])
        elif s[0] == "C":
            p0 = cur[-1] if cur else start
            for k in range(1, per_curve + 1):
                cur.append(cubic_at(p0, s[1], s[2], s[3], k / per_curve))
        elif s[0] == "Q":
            p0 = cur[-1] if cur else start
            for k in range(1, per_curve + 1):
                cur.append(quad_at(p0, s[1], s[2], k / per_curve))
        elif s[0] == "Z":
            if cur and start is not None:
                cur.append(start)
            subs.append(cur)
            cur = []
    if cur:
        subs.append(cur)
    return subs


def ring_of(segs, ctx, where, per_curve=16):
    """Flatten to a single point list; warn if multiple subpaths (joined)."""
    subs = flatten(segs, per_curve)
    if not subs:
        return []
    if len(subs) > 1:
        ctx.warn("<%s>: %d subpaths merged into one outline for morph/rotation"
                 % (where, len(subs)))
    out = []
    for sp in subs:
        out.extend(sp)
    return out


def arc_lengths(ring):
    cum = [0.0]
    for a, b in zip(ring, ring[1:]):
        cum.append(cum[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    return cum


def resample(ring, n, closed):
    """Uniform arc-length resample to exactly n points (n+1 if closed)."""
    if len(ring) < 2 or n < 2:
        return ring
    lens = arc_lengths(ring)
    total = lens[-1]
    if total <= 0:
        return [ring[0]] * n + ([ring[0]] if closed else [])
    out, j = [], 0
    for k in range(n):
        target = total * (k / (n - 1.0) if not closed else k / n)
        while j < len(ring) - 2 and lens[j + 1] < target:
            j += 1
        span = lens[j + 1] - lens[j]
        u = 0.0 if span <= 0 else (target - lens[j]) / span
        a, b = ring[j], ring[j + 1]
        out.append((a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u))
    if closed:
        out.append(out[0])
    return out


def circle_ring(c, r, n=48):
    return [(c[0] + r * math.cos(2 * math.pi * i / n),
             c[1] + r * math.sin(2 * math.pi * i / n)) for i in range(n + 1)]


def P(xe, ye):
    return "SVG_P(%s, %s)" % (xe, ye)


def pts_lit(ring):
    return ", ".join("ImVec2(%s, %s)" % (f4(x), f4(y)) for x, y in ring)


# ---------------------------------------------------------------- animation --

class Anim:
    """One <animate>/<animateTransform>, normalized to keyframe rows."""

    def __init__(self, el, ctx):
        self.el = el
        self.kind = local(el.tag)
        self.attr = el.get("attributeName", "")
        self.type = el.get("type")  # animateTransform only
        self.begin = seconds(el.get("begin"), 0.0) or 0.0
        self.dur = seconds(el.get("dur"))
        self.repeat = el.get("repeatCount", "1")
        self.raw_values = el.get("values")
        self.keytimes = nums(el.get("keyTimes")) or None
        self.splines = None
        ks = el.get("keySplines")
        if ks:
            sp = []
            for grp in ks.split(";"):
                v = nums(grp)
                if len(v) == 4:
                    sp.extend(v)
            if sp:
                self.splines = sp
        if self.raw_values:
            self.rows = [nums(s) for s in self.raw_values.split(";")]
        elif el.get("from") is not None and el.get("to") is not None:
            self.rows = [nums(el.get("from")), nums(el.get("to"))]
            self.raw_values = None
        else:
            self.rows = None
            ctx.warn("animation on '%s' has no values/from-to -> static" % self.attr)

    def usable(self):
        return (self.rows is not None and len(self.rows) >= 2
                and self.dur and self.dur > 0)

    def numeric(self, pick=0):
        return self.usable() and all(len(r) > pick for r in self.rows)

    def loop_call(self, ctx, name, vals, extra_indent="    "):
        """Emit a keys array + return the SvgKeysLoop(t,...) expression."""
        kn = "%s_k" % name
        ctx.code.append(extra_indent + "static const float %s[] = {%s};"
                        % (kn, ", ".join(f4(v) for v in vals)))
        kt = "NULL"
        if self.keytimes and len(self.keytimes) == len(vals):
            tn = "%s_t" % name
            ctx.code.append(extra_indent + "static const float %s[] = {%s};"
                            % (tn, ", ".join(f4(v) for v in self.keytimes)))
            kt = tn
        sp = "NULL"
        if self.splines:
            sn = "%s_s" % name
            ctx.code.append(extra_indent + "static const float %s[] = {%s};"
                            % (sn, ", ".join(f4(v) for v in self.splines)))
            sp = sn
        if self.repeat != "indefinite":
            ctx.warn("animation on '%s' repeatCount=%s -> generated code loops forever"
                     % (self.attr, self.repeat))
        return "SvgKeysLoop(t, %s, %s, %s, %d, %s, %s)" % (
            f4(self.begin)[:-1], f4(self.dur)[:-1], kn, len(vals), kt, sp)

    def num_expr(self, ctx, name, pick=0, indent="    "):
        return self.loop_call(ctx, name, [r[pick] for r in self.rows], indent)


# ----------------------------------------------------------------- gradients --

def collect_defs(root, ctx):
    grads = {}
    for el in root.iter():
        t = local(el.tag)
        if t in ("linearGradient", "radialGradient"):
            stops = []
            for st in el:
                if local(st.tag) == "stop":
                    sa = style_attrs(st)
                    col = parse_color(sa.get("stop-color", st.get("stop-color", "#000000")),
                                      ctx, "stop") or (0.0, 0.0, 0.0)
                    an = [a for a in st if local(a.tag) == "animate"
                          and a.get("attributeName") == "stop-color"]
                    stops.append((sa.get("offset", "0%"), col, an))
            grads[el.get("id")] = {
                "kind": t, "stops": stops,
                "x1": el.get("x1", "0%"), "y1": el.get("y1", "0%"),
                "x2": el.get("x2", "100%"), "y2": el.get("y2", "0%"),
            }
    return grads


def pct(v):
    s = str(v).strip()
    try:
        return float(s.rstrip("%")) / (100.0 if s.endswith("%") else 1.0)
    except ValueError:
        return 0.0


# --------------------------------------------------------------- generation --

class Gen:
    """Accumulates the C++ function body for one SVG."""

    def __init__(self, ctx, box, fname):
        self.ctx = ctx
        self.box = box
        self.fn = "DrawSvg_" + re.sub(r"[^0-9a-zA-Z_]", "_", fname)
        self.ctx.tint_rgb = None

    def L(self, s=""):
        self.ctx.code.append(s)


def color_lit(rgb, alpha_expr, tintable):
    """ImU32 C++ expr. rgb=(r,g,b) 0..1."""
    r, g, b = (int(round(v * 255)) for v in rgb)
    if tintable:
        if not alpha_expr:
            return "tint"
        return "(tint & ~IM_COL32_A_MASK) | ((ImU32)(int)(255.0f * (%s)) << IM_COL32_A_SHIFT)" % alpha_expr
    if not alpha_expr:
        return "IM_COL32(%d,%d,%d,255)" % (r, g, b)
    return "IM_COL32(%d,%d,%d,(int)(255.0f * (%s)))" % (r, g, b, alpha_expr)


def emit_shape(ctx, el, m_parent, alpha_parent):
    """Emit one graphics element. Returns False if it could not be converted."""
    tag = local(el.tag)
    at = style_attrs(el)
    sid = "s%d" % ctx.sid
    ctx.sid += 1

    anims = [Anim(a, ctx) for a in el if local(a.tag) in ("animate", "animateTransform")]

    def anim_for(attr):
        for a in anims:
            if a.attr == attr and a.usable():
                return a
        return None

    anim_transform = next((a for a in anims if a.attr == "transform" and a.usable()), None)

    # --- paint ---
    if "fill" in at:
        fill = parse_color(at["fill"], ctx, tag + "@fill")
    else:
        fill = None if tag in ("line", "polyline") else (0.0, 0.0, 0.0)
    stroke = parse_color(at.get("stroke"), ctx, tag + "@stroke")
    sw = seconds(at.get("stroke-width", "1"), 1.0) or 1.0

    alpha = 1.0
    try:
        alpha = float(at.get("opacity", "1"))
    except ValueError:
        pass
    alpha *= alpha_parent
    alpha_expr = None if abs(alpha - 1.0) < 1e-6 else f4(alpha)
    a_op = anim_for("opacity")
    if a_op and a_op.numeric():
        e = a_op.num_expr(ctx, sid + "_op")
        alpha_expr = "(%s) * %s" % (e, alpha_expr) if alpha_expr else "(%s)" % e

    # If a paint is animated but absent statically (SVG initial 'none'), seed it
    # from the animation's first keyframe so the element renders + animates.
    for paint_attr in ("fill", "stroke"):
        base = fill if paint_attr == "fill" else stroke
        a_paint = anim_for(paint_attr)
        if base is None and a_paint is not None and a_paint.raw_values:
            hexes = re.findall(r"#[0-9a-fA-F]{3,8}", a_paint.raw_values)
            if hexes:
                c = parse_color(hexes[0], ctx, paint_attr + " (from animate)")
                if c and not (isinstance(c, tuple) and c and c[0] == "url"):
                    if paint_attr == "fill":
                        fill = c
                    else:
                        stroke = c

    grad_ref = None
    if isinstance(fill, tuple) and fill and fill[0] == "url":
        grad_ref = fill[1]
        fill = None  # replaced by gradient emitter below (or dropped)

    def col_of(rgb, attr="fill"):
        if rgb is None:
            return None
        ca = anim_for(attr)
        if ca is not None and ca.raw_values:
            hexes = re.findall(r"#[0-9a-fA-F]{3,8}", ca.raw_values)
            if len(hexes) >= 2:
                arr = sid + "_c"
                items = []
                for hx in hexes:
                    c = parse_color(hx, ctx, "animate color") or (1, 1, 1)
                    items.append("IM_COL32(%d,%d,%d,255)"
                                 % tuple(int(round(v * 255)) for v in c))
                ctx.code.append("    static const ImU32 %s[] = {%s};" % (arr, ", ".join(items)))
                kt = "NULL"
                if ca.keytimes and len(ca.keytimes) == len(hexes):
                    tn = arr + "_t"
                    ctx.code.append("    static const float %s[] = {%s};"
                                    % (tn, ", ".join(f4(v) for v in ca.keytimes)))
                    kt = tn
                e = "SvgColorKeysLoop(t, %s, %s, %s, %d, %s)" % (
                    f4(ca.begin)[:-1], f4(ca.dur)[:-1], arr, len(hexes), kt)
                if alpha_expr:
                    e = "SvgMulAlpha(%s, %s)" % (e, alpha_expr)
                return e
        tintable = (ctx.tint_rgb is not None and rgb == ctx.tint_rgb)
        return color_lit(rgb, alpha_expr, tintable)

    # --- transform + rotation animation ---
    m = matmul(m_parent, parse_transform(at.get("transform"), ctx))

    def apply_p(p):
        return matapply(m, p)

    rot_deg = None      # expression for animated rotation angle
    rot_pivot = None    # pivot in user space
    ts_anim = None      # animate attributeName=transform w/ translate/scale
    if anim_transform:
        a = anim_transform
        if a.type == "rotate" or (a.raw_values and re.search(r"rotate", a.raw_values or "")):
            pivots = [(r[1], r[2]) for r in (a.rows or []) if len(r) >= 3]
            rot_pivot = pivots[0] if pivots else None
            if pivots and any(p != pivots[0] for p in pivots):
                ctx.warn("rotate anim changes pivot mid-flight -> first pivot used")
            vals = [r[0] for r in (a.rows or []) if r]
            if len(vals) >= 2:
                rot_deg = a.loop_call(ctx, sid + "_rot", vals)
        elif a.type in ("scale", "translate") or (a.raw_values and re.search(r"(scale|translate)", a.raw_values or "")):
            ts_anim = a
        elif a.type not in (None, "rotate", "scale", "translate"):
            ctx.warn("animateTransform type=%s unsupported -> static geometry" % a.type)
    simple = is_scale_translate(m)

    # geometry-parameter animations kept on primitives
    a_cx, a_cy, a_r = anim_for("cx"), anim_for("cy"), anim_for("r")
    a_x, a_y, a_w, a_h = anim_for("x"), anim_for("y"), anim_for("width"), anim_for("height")
    a_rx, a_ry = anim_for("rx"), anim_for("ry")
    a_d = anim_for("d")
    a_dash = anim_for("stroke-dasharray")
    a_doff = anim_for("stroke-dashoffset")

    no_geom_anim = not any((rot_deg, ts_anim, a_cx, a_cy, a_r, a_x, a_y, a_w, a_h,
                            a_rx, a_ry, a_d, a_dash, a_doff))

    def num_or(anim, base, name):
        """user-space numeric: animated expr string or literal."""
        if anim is not None and anim.numeric():
            return anim.num_expr(ctx, sid + "_" + name)
        return f4(base)

    def stroke_dash_cut(ring, col_expr, closed):
        """Static or animated dash run from the path start. Returns True."""
        lens = arc_lengths(ring)
        total = lens[-1]
        off = seconds(at.get("stroke-dashoffset"), 0.0) or 0.0
        if a_dash is not None and a_dash.raw_values:
            runs = []
            for grp in a_dash.raw_values.split(";"):
                v = nums(grp)
                runs.append(v[0] if v else 0.0)
            e = a_dash.loop_call(ctx, sid + "_dash", runs) if len(runs) >= 2 else f4(runs[0] if runs else total)
        else:
            pat = nums(at.get("stroke-dasharray"))
            e = f4(pat[0] if pat else total)
        if a_doff and a_doff.numeric():
            o = a_doff.num_expr(ctx, sid + "_doff")
        elif a_dash is None:
            o = f4(off)
        else:
            o = f4(off)
        ctx.code.append("    {")
        ctx.code.append("        static const ImVec2 kp[] = {%s};" % pts_lit(ring))
        ctx.code.append("        static const float cl[] = {%s};"
                        % ", ".join(f4(v) for v in lens))
        ctx.code.append("        float d0 = %s, d1 = d0 + %s;" % (o, e))
        ctx.code.append("        dl->PathClear();")
        ctx.code.append("        for (int i = 0; i < IM_COUNTOF(kp) - 1; i++)")
        ctx.code.append("        {")
        ctx.code.append("            const float s0 = fmodf(cl[i],     cl[IM_COUNTOF(cl) - 1]),")
        ctx.code.append("                        s1 = fmodf(cl[i + 1], cl[IM_COUNTOF(cl) - 1]);")
        ctx.code.append("            if (d0 <= s0 && s1 <= d1) dl->PathLineTo(SVG_P(kp[i].x, kp[i].y));")
        ctx.code.append("        }")
        ctx.code.append("        dl->PathStroke(%s, %s * ks, 0);" % (col_expr, f4(sw * math.sqrt(m[0] * m[0] + m[2] * m[2]))))
        ctx.code.append("    }")
        return True

    def rotated_ring(ring, col_expr, fill_expr, closed, dash_win=None):
        piv = rot_pivot or getattr(ctx, "pivot_hint", None)
        if piv is None:
            ctx.warn("rotate anim without pivot coords -> rotating around viewBox center")
            piv = (ctx.box[0] + ctx.box[2] / 2.0, ctx.box[1] + ctx.box[3] / 2.0)
        ctx.code.append("    {")
        ctx.code.append("        static const ImVec2 kp[] = {%s};" % pts_lit(ring))
        ctx.code.append("        ImVec2 rp[IM_COUNTOF(kp)];")
        ctx.code.append("        SvgRotatePts(rp, kp, IM_COUNTOF(kp), %s, %s, %s);"
                        % (f4(piv[0]), f4(piv[1]), rot_deg))
        ctx.code.append("        dl->PathClear();")
        if dash_win:
            lens = arc_lengths(ring)
            ctx.code.append("        static const float cl[] = {%s};" % ", ".join(f4(v) for v in lens))
            ctx.code.append("        for (int i = 0; i < IM_COUNTOF(kp); i++)")
            ctx.code.append("            if (cl[i] >= %sf && cl[i] <= %sf) dl->PathLineTo(SVG_P(rp[i].x, rp[i].y));"
                            % (f4(dash_win[0])[:-1], f4(dash_win[1])[:-1]))
        else:
            ctx.code.append("        for (int i = 0; i < IM_COUNTOF(kp); i++) dl->PathLineTo(SVG_P(rp[i].x, rp[i].y));")
        if fill_expr:
            ctx.code.append("        dl->PathFillConcave(%s);" % fill_expr)
        if col_expr and (not fill_expr or stroke):
            ctx.code.append("        dl->PathStroke(%s, %s * ks, 0);"
                            % (col_expr, f4(sw * math.sqrt(m[0] * m[0] + m[2] * m[2]))))
        ctx.code.append("    }")
        return True

    def ts_ring(ring, fill_expr, col_expr):
        a = ts_anim
        assert a is not None
        raws = [s.strip() for s in (a.raw_values or "").split(";")]
        tx, ty, sx = [], [], []
        for s in raws:
            tr = re.search(r"translate\(([^)]*)\)", s)
            sc = re.search(r"scale\(([^)]*)\)", s)
            tv = nums(tr.group(1)) if tr else []
            sv = nums(sc.group(1)) if sc else []
            tx.append(tv[0] if tv else 0.0)
            ty.append(tv[1] if len(tv) > 1 else 0.0)
            sx.append(sv[0] if sv else 1.0)
        sx = sx or [1.0] * len(tx)
        if len(sx) == 1 and len(tx) > 1:
            sx = sx * len(tx)
        ctx.code.append("    {")
        ctx.code.append("        const float tx_ = %s;" % a.loop_call(ctx, sid + "_tx", tx, "        "))
        ctx.code.append("        const float ty_ = %s;" % a.loop_call(ctx, sid + "_ty", ty, "        "))
        ctx.code.append("        const float s_  = %s;" % a.loop_call(ctx, sid + "_ts", sx, "        "))
        ctx.code.append("        static const ImVec2 kp[] = {%s};" % pts_lit(ring))
        ctx.code.append("        dl->PathClear();")
        ctx.code.append("        for (int i = 0; i < IM_COUNTOF(kp); i++)")
        ctx.code.append("            dl->PathLineTo(SVG_P(tx_ + kp[i].x * s_, ty_ + kp[i].y * s_));")
        if fill_expr:
            ctx.code.append("        dl->PathFillConcave(%s);" % fill_expr)
        if stroke and col_expr:
            ctx.code.append("        dl->PathStroke(%s, %s * ks, 0);" % (col_expr, f4(sw)))
        ctx.code.append("    }")
        return True

    # ------------------------------------------------------------------ circle
    if tag == "circle":
        cx, cy, r = num(at.get("cx")), num(at.get("cy")), num(at.get("r"))
        ring = circle_ring((cx, cy), r)
        f_expr, s_expr = col_of(fill), col_of(stroke, 'stroke')
        if grad_ref and not f_expr:
            return emit_gradient(ctx, at, grad_ref, rect=(cx - r, cy - r, 2 * r, 2 * r))
        if rot_deg:
            win = None
            if at.get("stroke-dasharray") and stroke and not fill:
                pat = nums(at.get("stroke-dasharray"))
                dash_len = pat[0] if pat else 0.0
                off = seconds(at.get("stroke-dashoffset"), 0.0) or 0.0
                win = (off, off + dash_len)
            return rotated_ring([apply_p(p) for p in ring], s_expr, f_expr, True, dash_win=win)
        if ts_anim:
            return ts_ring([apply_p(p) for p in ring], f_expr, s_expr)
        if at.get("stroke-dasharray") and stroke and not fill:
            return stroke_dash_cut([apply_p(p) for p in ring], s_expr, True)
        cen = P(num_or(a_cx, apply_p((cx, cy))[0], "cx"),
                num_or(a_cy, apply_p((cx, cy))[1], "cy")) \
            if (a_cx or a_cy) else P(f4(apply_p((cx, cy))[0]), f4(apply_p((cx, cy))[1]))
        if (a_cx or a_cy) and m != m_parent:
            ctx.warn("cx/cy animation applied in user space, transform ignored for it")
        rad = "(%s) * (%s * ks)" % (num_or(a_r, r, "r"),
                             f4(math.sqrt((m[0] * m[0] + m[2] * m[2]) / 2.0 + (m[1] * m[1] + m[3] * m[3]) / 2.0)))
        if stroke and not fill:
            ctx.code.append("    dl->AddCircle(%s, %s, %s, 0, %s * ks);"
                            % (cen, rad, s_expr, f4(sw * math.sqrt(m[0] * m[0] + m[2] * m[2]))))
        elif fill and not stroke:
            ctx.code.append("    dl->AddCircleFilled(%s, %s, %s);" % (cen, rad, f_expr))
        elif fill and stroke:
            ctx.code.append("    dl->AddCircleFilled(%s, %s, %s);" % (cen, rad, f_expr))
            ctx.code.append("    dl->AddCircle(%s, %s, %s, 0, %s * ks);"
                            % (cen, rad, s_expr, f4(sw * math.sqrt(m[0] * m[0] + m[2] * m[2]))))
        else:
            ctx.warn("<circle> has neither fill nor stroke -> skipped")
        return True

    # ----------------------------------------------------------------- ellipse
    if tag == "ellipse":
        cx, cy = num(at.get("cx")), num(at.get("cy"))
        rx, ry = num(at.get("rx")), num(at.get("ry"))
        col = col_of(stroke, 'stroke') or col_of(fill)
        if rot_deg:
            ring = [(cx + rx * math.cos(2 * math.pi * i / 32),
                     cy + ry * math.sin(2 * math.pi * i / 32)) for i in range(33)]
            return rotated_ring([apply_p(p) for p in ring], col, col_of(fill), True)
        ctx.code.append("    dl->AddEllipse(%s, ImVec2(%s * kx, %s * ky), %s, 0.0f, 0, %s * ks);"
                        % (P(f4(apply_p((cx, cy))[0]), f4(apply_p((cx, cy))[1])),
                           num_or(a_rx, rx, "rx"), num_or(a_ry, ry, "ry"), col,
                           f4(sw * math.sqrt(m[0] * m[0] + m[2] * m[2]))))
        return True

    # -------------------------------------------------------------------- line
    if tag == "line":
        p1 = (num(at.get("x1")), num(at.get("y1")))
        p2 = (num(at.get("x2")), num(at.get("y2")))
        col = col_of(stroke, 'stroke') or col_of(fill)
        if rot_deg:
            return rotated_ring([apply_p(p1), apply_p(p2)], col, None, False)
        ctx.code.append("    dl->AddLine(%s, %s, %s, %s * ks);"
                        % (P(*[f4(v) for v in apply_p(p1)]), P(*[f4(v) for v in apply_p(p2)]),
                           col, f4(sw * math.sqrt(m[0] * m[0] + m[2] * m[2]))))
        if at.get("stroke-linecap") == "round":
            ctx.warn("stroke-linecap=round ignored — add AddCircleFilled(p, thickness/2) at "
                     "endpoints if the cap matters (ImDrawList has no cap flags)")
        return True

    # -------------------------------------------------------------------- rect
    if tag == "rect":
        x, y = num(at.get("x")), num(at.get("y"))
        w, h = num(at.get("width")), num(at.get("height"))
        rnd = num(at.get("rx"))
        corners = [(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)]
        f_expr, s_expr = col_of(fill), col_of(stroke, 'stroke')
        if grad_ref:
            return emit_gradient(ctx, at, grad_ref, rect=(x, y, w, h))
        if rot_deg:
            return rotated_ring([apply_p(p) for p in corners], s_expr, f_expr, True)
        if ts_anim:
            return ts_ring([apply_p(p) for p in corners], f_expr, s_expr)
        x_u = num_or(a_x, x, "x")
        y_u = num_or(a_y, y, "y")
        w_u = num_or(a_w, w, "w")
        h_u = num_or(a_h, h, "h")
        if (a_x or a_y or a_w or a_h) and m != m_parent:
            ctx.warn("rect x/y/width/height animation ignores non-trivial transform")
        r_e = "%s * ks" % f4(rnd)
        if fill and stroke:
            ctx.code.append("    dl->AddRectFilled(SVG_P(%s, %s), SVG_P((%s) + (%s), (%s) + (%s)), %s, %s);"
                            % (x_u, y_u, x_u, w_u, y_u, h_u, f_expr, r_e))
            ctx.code.append("    dl->AddRect(SVG_P(%s, %s), SVG_P((%s) + (%s), (%s) + (%s)), %s, %s, %s * ks);"
                            % (x_u, y_u, x_u, w_u, y_u, h_u, s_expr, r_e, f4(sw)))
        elif fill:
            ctx.code.append("    dl->AddRectFilled(SVG_P(%s, %s), SVG_P((%s) + (%s), (%s) + (%s)), %s, %s);"
                            % (x_u, y_u, x_u, w_u, y_u, h_u, f_expr, r_e))
        elif stroke:
            ctx.code.append("    dl->AddRect(SVG_P(%s, %s), SVG_P((%s) + (%s), (%s) + (%s)), %s, %s, %s * ks);"
                            % (x_u, y_u, x_u, w_u, y_u, h_u, s_expr, r_e, f4(sw)))
        else:
            ctx.warn("<rect> has neither fill nor stroke -> skipped")
        return True

    # --------------------------------------------------- polygon / polyline
    if tag in ("polygon", "polyline"):
        pts = nums(at.get("points", ""))
        ring = list(zip(pts[0::2], pts[1::2]))
        if len(ring) < 2:
            return False
        ring = [apply_p(p) for p in ring]
        closed = tag == "polygon"
        f_expr = col_of(fill) if closed else None
        s_expr = col_of(stroke, 'stroke')
        if rot_deg:
            return rotated_ring(ring, s_expr or f_expr, f_expr, closed)
        if closed and f_expr and not s_expr:
            ctx.code.append("    {")
            ctx.code.append("        static const ImVec2 kp[] = {%s};" % pts_lit(ring))
            ctx.code.append("        dl->PathClear();")
            ctx.code.append("        for (int i = 0; i < IM_COUNTOF(kp); i++) dl->PathLineTo(SVG_P(kp[i].x, kp[i].y));")
            ctx.code.append("        dl->PathFillConcave(%s);" % f_expr)
            ctx.code.append("    }")
            return True
        ctx.code.append("    {")
        ctx.code.append("        static const ImVec2 kp[] = {%s};" % pts_lit(ring))
        ctx.code.append("        dl->PathClear();")
        ctx.code.append("        for (int i = 0; i < IM_COUNTOF(kp); i++) dl->PathLineTo(SVG_P(kp[i].x, kp[i].y));")
        if f_expr:
            ctx.code.append("        dl->PathFillConcave(%s);" % f_expr)
        if s_expr:
            ctx.code.append("        dl->PathStroke(%s, %s * ks, %s);"
                            % (s_expr, f4(sw), "ImDrawFlags_Closed" if closed else "0"))
        ctx.code.append("    }")
        return True

    # -------------------------------------------------------------------- path
    if tag == "path":
        segs = path_segments(at.get("d", ""), ctx, "path")
        closed = any(s[0] == "Z" for s in segs)
        f_expr, s_expr = col_of(fill), col_of(stroke, 'stroke')

        if a_d is not None and a_d.raw_values and len(a_d.rows or []) >= 2:
            keys = [s.strip() for s in a_d.raw_values.split(";") if s.strip()]
            rings = []
            for i, kv in enumerate(keys):
                kg = path_segments(kv, ctx, "morph-kf%d" % i)
                rg = flatten_and_warn(kg, ctx)
                rings.append(resample([apply_p(p) for p in rg], 48, closed and any(s[0] == "Z" for s in kg)))
            if all(len(r) == len(rings[0]) and len(r) >= 2 for r in rings):
                ctx.code.append("    {")
                ctx.code.append("        const float u_ = SvgLoopT(t, %s, %s) * %d;"
                                % (f4(a_d.begin)[:-1], f4(a_d.dur)[:-1], len(rings) - 1))
                ctx.code.append("        int k0_ = (int)u_; if (k0_ > %d) k0_ = %d;"
                                % (len(rings) - 2, len(rings) - 2))
                ctx.code.append("        const float v_ = u_ - k0_;")
                for i, rg in enumerate(rings):
                    ctx.code.append("        static const ImVec2 kf%d[] = {%s};" % (i, pts_lit(rg)))
                ctx.code.append("        static const ImVec2* kfs[] = {%s};"
                                % ", ".join("kf%d" % i for i in range(len(rings))))
                ctx.code.append("        dl->PathClear();")
                ctx.code.append("        for (int i = 0; i < IM_COUNTOF(kf0); i++)")
                ctx.code.append("            dl->PathLineTo(SVG_P(kfs[k0_][i].x + (kfs[k0_ + 1][i].x - kfs[k0_][i].x) * v_,"
                                )
                ctx.code.append("                                  kfs[k0_][i].y + (kfs[k0_ + 1][i].y - kfs[k0_][i].y) * v_));")
                if f_expr and not s_expr:
                    ctx.code.append("        dl->PathFillConcave(%s);" % f_expr)
                else:
                    ctx.code.append("        dl->PathStroke(%s, %s * ks, %s);"
                                    % (s_expr or f_expr, f4(sw),
                                       "ImDrawFlags_Closed" if closed else "0"))
                ctx.code.append("    }")
                ctx.warn("d-morph: keyframes arc-length resampled to 48 pts (SMIL would require "
                         "matching structure; resampling always works)")
                return True
            ctx.warn("<animate d> keyframes failed to flatten consistently -> first keyframe static")

        if rot_deg:
            rg = flatten_and_warn(segs, ctx)
            return rotated_ring([apply_p(p) for p in rg], s_expr, f_expr, closed)
        if at.get("stroke-dasharray") and stroke and not fill:
            rg = flatten_and_warn(segs, ctx)
            return stroke_dash_cut([apply_p(p) for p in rg], s_expr, closed)
        if ts_anim is not None:
            rg = flatten_and_warn(segs, ctx)
            return ts_ring([apply_p(p) for p in rg], f_expr, s_expr)
        if stroke:
            ctx.code.append("    dl->PathClear();")
            for s in segs:
                if s[0] == "M":
                    ctx.code.append("    dl->PathLineTo(%s);" % P(*[f4(v) for v in apply_p(s[1])]))
                elif s[0] == "L":
                    ctx.code.append("    dl->PathLineTo(%s);" % P(*[f4(v) for v in apply_p(s[1])]))
                elif s[0] == "C":
                    a, b, e = (apply_p(p) for p in s[1:4])
                    ctx.code.append("    dl->PathBezierCubicCurveTo(%s, %s, %s);"
                                    % (P(*[f4(v) for v in a]), P(*[f4(v) for v in b]), P(*[f4(v) for v in e])))
                elif s[0] == "Q":
                    a, e = (apply_p(p) for p in s[1:3])
                    ctx.code.append("    dl->PathBezierQuadraticCurveTo(%s, %s);"
                                    % (P(*[f4(v) for v in a]), P(*[f4(v) for v in e])))
            ctx.code.append("    dl->PathStroke(%s, %s * ks, %s);"
                            % (s_expr, f4(sw), "ImDrawFlags_Closed" if closed else "0"))
        if fill:
            for sp in flatten(segs):
                if len(sp) >= 3:
                    rg = [apply_p(p) for p in sp]
                    ctx.code.append("    {")
                    ctx.code.append("        static const ImVec2 kp[] = {%s};" % pts_lit(rg))
                    ctx.code.append("        dl->PathClear();")
                    ctx.code.append("        for (int i = 0; i < IM_COUNTOF(kp); i++) dl->PathLineTo(SVG_P(kp[i].x, kp[i].y));")
                    ctx.code.append("        dl->PathFillConcave(%s);" % f_expr)
                    ctx.code.append("    }")
        if not stroke and not fill:
            ctx.warn("<path> has neither fill nor stroke -> skipped")
        return True

    # -------------------------------------------------------------------- text
    if tag == "text":
        txt = "".join(el.itertext()).strip()
        if not txt:
            return False
        size = seconds(at.get("font-size"), 16.0) or 16.0
        x, y = num(at.get("x")), num(at.get("y"))
        anchor = at.get("text-anchor", "start")
        col = col_of(fill if fill else (0.0, 0.0, 0.0))
        ctx.warn("<text>: rendered with ImGui's current font (font-family ignored); "
                 "SVG filters (feGaussianBlur glow) unsupported — pulse is kept, fake the glow "
                 "with 2 offset AddText passes in a dark color if needed")
        off = "0.0f" if anchor == "start" else ("ts_.x * 0.5f" if anchor == "middle" else "ts_.x")
        px, py = apply_p((x, y))
        ctx.code.append("    {")
        ctx.code.append("        const float fs_ = %s * ky;" % f4(size))
        ctx.code.append("        ImGui::PushFont(NULL, fs_);  // bake-on-demand: renders from the 2nd frame")
        ctx.code.append('        const ImVec2 ts_ = ImGui::CalcTextSize("%s");' % txt)
        ctx.code.append("        const ImVec2 tp_ = SVG_P(%s, %s);" % (f4(px), f4(py)))
        ctx.code.append("        dl->AddText(ImVec2(tp_.x - (%s), tp_.y - ts_.y * 0.8f), %s, \"%s\");"
                        % (off, col, txt))
        ctx.code.append("        ImGui::PopFont();")
        ctx.code.append("    }")
        return True

    return False


def flatten_and_warn(segs, ctx):
    subs = [sp for sp in flatten(segs) if len(sp) >= 2]
    if len(subs) > 1:
        ctx.warn("path has %d subpaths — merged into one outline (fills may overlap wrongly)"
                 % len(subs))
    out = []
    for sp in subs:
        out.extend(sp)
    return out


def emit_gradient(ctx, at, grad_id, rect):
    gd = ctx.gradients.get(grad_id)
    if not gd or len(gd["stops"]) < 2:
        ctx.warn("fill=url(#%s): gradient not found/has <2 stops -> skipped" % grad_id)
        return False
    if gd["kind"] == "radialGradient":
        ctx.warn("radialGradient approximated as linear")
    x, y, w, h = rect
    c0 = gd["stops"][0][1]
    c1 = gd["stops"][-1][1]

    def stop_expr(idx, base, tagname):
        an = gd["stops"][idx][2]
        if an:
            hexes = re.findall(r"#[0-9a-fA-F]{3,8}", an[0].get("values", ""))
            if len(hexes) >= 2:
                arr = "g%d_%s" % (ctx.sid, tagname)
                items = []
                for hx in hexes:
                    c = parse_color(hx, ctx, "stop anim") or (1, 1, 1)
                    items.append("IM_COL32(%d,%d,%d,255)" % tuple(int(round(v * 255)) for v in c))
                ctx.code.append("    static const ImU32 %s[] = {%s};" % (arr, ", ".join(items)))
                a = Anim(an[0], ctx)
                return "SvgColorKeysLoop(t, %s, %s, %s, %d, NULL)" % (
                    f4(a.begin)[:-1], f4(a.dur)[:-1], arr, len(hexes))
        return color_lit(base, None, False)

    e0, e1 = stop_expr(0, c0, 'a'), stop_expr(-1, c1, 'b')
    gx = pct(gd["x2"]) - pct(gd["x1"])
    gy = pct(gd["y2"]) - pct(gd["y1"])
    if abs(gx) >= abs(gy) and gy == 0:
        cs = (e0, e1, e1, e0)
    elif abs(gy) >= abs(gx) and gx == 0:
        cs = (e0, e0, e1, e1)
    else:
        mid = "SvgLerpU32(%s, %s, 0.5f)" % (e0, e1)
        cs = (e0, mid, e1, mid)
    ctx.code.append("    dl->AddRectFilledMultiColor(%s, %s, %s, %s, %s, %s);"
                    % (P(f4(x), f4(y)), P(f4(x + w), f4(y + h)), *cs))
    return True


# --------------------------------------------------------------------- walk --

SUPPORTED = ("circle", "ellipse", "rect", "line", "polygon", "polyline", "path", "text")


def walk(ctx, el, m, alpha):
    tag = local(el.tag)
    if tag in ("defs", "linearGradient", "radialGradient", "stop", "animate",
               "animateTransform", "animateMotion", "clipPath", "title", "desc"):
        return
    if tag == "g":
        at = style_attrs(el)
        mm = matmul(m, parse_transform(at.get("transform"), ctx))
        try:
            aa = alpha * float(at.get("opacity", "1"))
        except ValueError:
            aa = alpha
        for ch in el:
            walk(ctx, ch, mm, aa)
        return
    if tag in ("filter", "mask", "image", "use", "symbol", "switch", "pattern"):
        ctx.warn("<%s> unsupported -> children may render unmasked/missing" % tag)
        for ch in el:
            walk(ctx, ch, m, alpha)
        return
    if tag in SUPPORTED:
        ctx.pivot_hint = None
        for a in el:
            if local(a.tag) == "animateTransform" and a.get("type") == "rotate":
                r0 = (nums(a.get("from", "")) or nums((a.get("values") or "").split(";")[0])) 
                if len(r0) >= 3:
                    ctx.pivot_hint = (r0[1], r0[2])
        if not emit_shape(ctx, el, m, alpha):
            ctx.warn("<%s> not converted -> skipped" % tag)
        return
    for ch in el:
        walk(ctx, ch, m, alpha)


def load_svg(path):
    tree = ET.parse(path)
    root = tree.getroot()
    if local(root.tag) != "svg":
        raise ValueError("root element is <%s>, not <svg>" % local(root.tag))
    vb = nums(root.get("viewBox", ""))
    w = num(root.get("width"), 0)   # 'px'-suffixed values: nums() strips suffix
    h = num(root.get("height"), 0)
    if len(vb) == 4:
        box = vb
    elif w > 0 and h > 0:
        box = [0, 0, w, h]
    else:
        raise ValueError("no viewBox and no width/height")
    return root, box


def convert(path, tint_hex=None, name=None, emit_include=True):
    ctx = Ctx()
    root, box = load_svg(path)
    ctx.box = box
    ctx.gradients = collect_defs(root, ctx)
    if tint_hex:
        t = tint_hex.lstrip("#")
        if len(t) == 3:
            t = "".join(c * 2 for c in t)
        ctx.tint_rgb = tuple(int(t[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    fname = name or os.path.splitext(os.path.basename(path))[0]
    fn = "DrawSvg_" + re.sub(r"[^0-9a-zA-Z_]", "_", fname)
    bw, bh = box[2] - box[0], box[3] - box[1]
    ctx.code.append("// Generated by scripts/svg2drawlist.py from %s — do not edit by hand."
                    % os.path.basename(path))
    ctx.code.append("// Maps viewBox (%.6g,%.6g,%.6g,%.6g) into [origin, origin+size]."
                    % tuple(box))
    ctx.code.append("// t = seconds (ImGui::GetTime()); tint replaces --tint color if given.")
    ctx.code.append("inline void %s(ImDrawList* dl, const ImVec2& origin, const ImVec2& size, float t," % fn)
    ctx.code.append("                       ImU32 tint = IM_COL32(255, 255, 255, 255))")
    ctx.code.append("{")
    ctx.code.append("    (void)t; (void)tint;")
    ctx.code.append("    const float kx = size.x / %s, ky = size.y / %s, ks = (kx + ky) * 0.5f;"
                    % (f4(bw), f4(bh)))
    ctx.code.append("    (void)kx; (void)ky; (void)ks;")
    ctx.code.append("#define SVG_P(x_, y_) ImVec2(origin.x + float(x_) * kx, origin.y + float(y_) * ky)")
    ctx.sid = 0
    for ch in root:
        walk(ctx, ch, IDENT, 1.0)
    ctx.code.append("#undef SVG_P")
    ctx.code.append("}")
    return fn, ctx


# --------------------------------------------------------------------- lint --

def lint_file(path):
    issues = []
    try:
        root, box = load_svg(path)
    except ET.ParseError as e:
        return [("error", "XML parse error: %s" % e)]
    except Exception as e:
        return [("error", str(e))]
    for el in root.iter():
        t = local(el.tag)
        if t == "script":
            issues.append(("error", "<script> present — refuse to convert"))
        if t in ("use", "mask", "pattern", "foreignObject"):
            issues.append(("warn", "<%s> unsupported — expect dropped/warned output" % t))
        if t == "filter":
            issues.append(("warn", "<filter> (glow/blur) unsupported — approximated or dropped"))
        if t == "style":
            issues.append(("warn", "<style> block unsupported — use inline style attrs"))
    for el in root.iter():
        if local(el.tag) == "animateTransform" and el.get("type") not in (None, "rotate", "scale", "translate"):
            issues.append(("error", "animateTransform type=%s unsupported" % el.get("type")))
    vb = nums(root.get("viewBox", "") or "")
    if len(vb) != 4:
        issues.append(("warn", "no viewBox — width/height used; icons should set viewBox"))
    else:
        if vb[0] != 0 or vb[1] != 0:
            issues.append(("warn", "viewBox min is not 0,0 — offsets map through, but design is unusual"))
    return issues


# --------------------------------------------------------------------- main --

def main():
    ap = argparse.ArgumentParser(prog="svg2drawlist.py",
                                 description="Convert (animated) SVG to ImDrawList C++.")
    ap.add_argument("input", help=".svg file or a directory of them")
    ap.add_argument("-o", "--output", help="output header (default: <input>_imgui.h)")
    ap.add_argument("--tint", metavar="RRGGBB",
                    help="hex color in the SVG that the runtime `tint` argument replaces")
    ap.add_argument("--check", action="store_true", help="lint only, exit 1 on errors")
    ap.add_argument("--runtime-include", action="store_true",
                    help="add #include \"svg_runtime.h\" to the output")
    args = ap.parse_args()

    if os.path.isdir(args.input):
        files = sorted(os.path.join(args.input, f) for f in os.listdir(args.input)
                       if f.lower().endswith(".svg"))
    else:
        files = [args.input]
    if not files:
        print("no .svg files found", file=sys.stderr)
        sys.exit(2)

    if args.check:
        errors = 0
        for f in files:
            for level, msg in lint_file(f):
                print("%-6s%s: %s" % (level.upper() + " ", os.path.basename(f), msg))
                errors += level == "error"
        print("%s: %d file(s), %d error(s)" % ("FAIL" if errors else "OK", len(files), errors))
        sys.exit(1 if errors else 0)

    if args.output:
        out_path = args.output
    elif len(files) == 1:
        # default lands in CWD, never next to the input (keeps source repos clean)
        out_path = os.path.splitext(os.path.basename(files[0]))[0] + "_imgui.h"
    else:
        print("batch mode requires -o OUT.h", file=sys.stderr)
        sys.exit(2)

    lines = ["// Generated by scripts/svg2drawlist.py — do not edit by hand.",
             "// Requires imgui.h and the helpers from svg_runtime.h.",
             "#ifndef %s_H" % re.sub(r"[^0-9a-zA-Z_]", "_", os.path.splitext(os.path.basename(out_path))[0]).upper(),
             "#define %s_H" % re.sub(r"[^0-9a-zA-Z_]", "_", os.path.splitext(os.path.basename(out_path))[0]).upper(),
             "",
             "#include <imgui.h>",
             "#include <math.h>   // fmodf (dash windows)"]
    if args.runtime_include:
        lines.append('#include "svg_runtime.h"')
    total_warns = 0
    fns = []
    for f in files:
        try:
            fn, ctx = convert(f, args.tint)
        except Exception as e:
            print("error converting %s: %s" % (f, e), file=sys.stderr)
            sys.exit(1)
        total_warns += len(ctx.warns)
        lines.append("")
        lines.extend(ctx.code)
        fns.append(fn)
        for w in ctx.warns:
            print("  warn %s: %s" % (os.path.basename(f), w), file=sys.stderr)
    lines += ["", "#endif // guard", ""]
    with open(out_path, "w") as fh:
        fh.write("\n".join(lines))
    print("wrote %s: %d function(s), %d warning(s)" % (out_path, len(fns), total_warns))
    print("  fns: %s" % ", ".join(fns))


if __name__ == "__main__":
    main()
