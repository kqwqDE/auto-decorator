#include "Core.hpp"
#include "Internal.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace ad {
namespace {

constexpr float PI = 3.14159265358979f;
const int DX[4] = {0, 1, 0, -1}, DY[4] = {1, 0, -1, 0};

// ------------------------------------------------------------------ style table
struct StyleDef {
    float border[5];  // Single, Double, Segmented, Offset, Bracketed
    float fill[6];    // PanelGrid, Circuit, Stripes, DotMatrix, Layered, Hex
    float nature, tech, glow;
};
const StyleDef kStyles[int(Style::Count)] = {
    /*Modern     */ {{3, 2, 1, 1, 2}, {3, .5f, 1.5f, 1, 2, .5f}, .1f, .3f, 1.0f},
    /*Minimalist */ {{4, .5f, 0, 0, 2}, {1, 0, 1, 2, .5f, 0}, 0, 0, .6f},
    /*Glow       */ {{3, 3, .5f, .5f, 1}, {1, .5f, 1, 2, 2, .5f}, .1f, .2f, 1.6f},
    /*Cyberpunk  */ {{1.5f, 2, 2, 1, 2.5f}, {1.5f, 3, 1.5f, 1, .5f, 1}, 0, 1.0f, 1.3f},
    /*Futuristic */ {{2, 3, 1, .5f, 2}, {2, 2, 1, .5f, 2, 1.5f}, 0, .8f, 1.2f},
    /*Industrial */ {{2, 2, 1, 2, 2}, {3, .5f, 2, 1, 1, 0}, 0, .2f, .4f},
    /*Tech       */ {{1, 2, 2, 1, 2}, {1, 3, 1.5f, 1.5f, .5f, 1}, 0, 1.0f, 1.0f},
    /*Abstract   */ {{1, 1, 2, 3, 1}, {1, .5f, 2, 2, 2, 2}, .1f, .1f, .9f},
    /*Geometric  */ {{2, 2, 1, 2, 2}, {2, .3f, 2, 1, 3, 1.5f}, 0, .1f, .8f},
    /*Nature     */ {{2, .5f, 3, 1, .5f}, {.5f, 0, .5f, 2, 1, 1.5f}, 1.0f, 0, .6f},
    /*Dark       */ {{2, 2, 1, 1, 2}, {2, 1, 1, 1, 1, .5f}, 0, .2f, .35f},
    /*Detailed   */ {{1, 3, 2, 1, 3}, {2, 2, 2, 2, 2, 2}, .2f, .5f, 1.0f},
    /*Clean      */ {{4, 1, 0, 0, 2}, {2, 0, 1, 1, 1, 0}, 0, 0, .7f},
    /*Space      */ {{1.5f, 2, 1, 1, 2}, {1, 1, 1, 3, 1.5f, 2}, .1f, .4f, 1.3f},
};

int pickW(float const* w, int n, Rng& r, unsigned used = 0) {
    float tot = 0; for (int i = 0; i < n; i++) if (!(used >> i & 1)) tot += w[i];
    if (tot <= 0) { for (int i = 0; i < n; i++) if (!(used >> i & 1)) return i; return 0; }
    float x = r.f() * tot;
    for (int i = 0; i < n; i++) { if (used >> i & 1) continue; x -= w[i]; if (x <= 0) return i; }
    return n - 1;
}

const char* kBorderNames[] = {"Single border", "Double outline", "Segmented", "Stepped", "Bracketed"};
const char* kFillNames[] = {"Panels", "Circuits", "Stripes", "Dot matrix", "Layered", "Hex lattice", "No fill"};

std::vector<VariationProfile> makeProfiles(Settings const& s, int count, Rng& r) {
    StyleDef const& sd = kStyles[int(s.style)];
    std::vector<VariationProfile> out; unsigned ub = 0, uf = 0;
    int bias[] = {-15, 10, 25, 0, -5, 18};
    // shuffle complexity offsets so variations differ in density of detail
    for (int i = 5; i > 0; i--) std::swap(bias[i], bias[r.i(0, i)]);
    for (int i = 0; i < count; i++) {
        VariationProfile p;
        if (ub == 31) ub = 0; if (uf == 63) uf = 0;           // all designs used -> start another round
        int b = pickW(sd.border, 5, r, ub); ub |= 1u << b; p.border = BorderDesign(b);
        int f = pickW(sd.fill, 6, r, uf);  uf |= 1u << f; p.fill = FillPattern(f);
        p.cornerCaps = r.chance(.65f); p.outerGlow = r.chance(.5f + .25f * sd.glow);
        p.complexityBias = bias[i % 6];
        p.label = std::string(kBorderNames[b]) + " + " + kFillNames[f];
        out.push_back(p);
    }
    return out;
}

// ------------------------------------------------------------------ builder context
struct Ctx {
    Analysis const& a; Settings const& s; VariationProfile const& p; Rng& r; std::vector<Item>& out;
    StyleDef const& sd; float t, gap, sc, dens, comp, fill;
    Ctx(Analysis const& a_, Settings const& s_, VariationProfile const& p_, Rng& r_, std::vector<Item>& o)
        : a(a_), s(s_), p(p_), r(r_), out(o), sd(kStyles[int(s_.style)]) {
        sc = std::clamp(s.scalePct, 40, 250) / 100.f;
        t = (0.08f + 0.07f * float(std::clamp(s.borderThickness, 1, 5))) * sc;   // 0.15 .. 0.43 cells
        gap = 0.12f * sc;
        dens = std::clamp(s.density, 0, 100) / 100.f;
        comp = std::clamp(s.complexity + p_.complexityBias, 0, 100) / 100.f;
        fill = std::clamp(s.interiorFill, 0, 100) / 100.f;
    }
    int zOf(Layer l, int k) const { float m = .5f + s.layerDepth / 50.f; return int(std::lround(((l == Layer::Back ? -4 : l == Layer::Front ? 3 : 0) + k) * m)); }
    Item& add(Role role, Layer l, float x, float y, float sx, float sy, float rot, int slot, int zk = 0, int group = 0) {
        Item it; it.role = role; it.layer = l; it.x = x; it.y = y; it.sx = sx; it.sy = sy; it.rot = rot; it.slot = uint8_t(slot); it.z = zOf(l, zk); it.group = uint8_t(group);
        out.push_back(it); return out.back();
    }
    bool solidAt(float x, float y) const { return a.grid.at(int(std::floor(x)), int(std::floor(y))); }
};

float rotFromAngle(float mathDeg) { float r = -mathDeg; while (r < 0) r += 360; while (r >= 360) r -= 360; return r; }
float triRot(int sx, int sy) { return (sx > 0 && sy > 0) ? 0 : (sx > 0 && sy < 0) ? 90 : (sx < 0 && sy < 0) ? 180 : 270; }

// A decorated straight line: centre (cx,cy), unit tangent (tx,ty), unit normal (nx,ny) pointing to the side the
// border sits on, `off` = distance of the border centre line from (cx,cy)-line, len along tangent.
void borderLine(Ctx& c, float cx, float cy, float tx, float ty, float nx, float ny, float len, float off, bool inner, int zk) {
    float rot = rotFromAngle(std::atan2(ty, tx) * 180.f / PI);
    auto bar = [&](float u0, float u1, float o, float th, int slot, int z) {
        float mid = (u0 + u1) * .5f, L = u1 - u0; if (L < .05f) return;
        c.add(Role::Bar, Layer::Mid, cx + tx * mid + nx * o, cy + ty * mid + ny * o, L, th, rot, slot, z, 1);
    };
    float h = len * .5f;
    int slot = inner ? 1 : 0;
    switch (c.p.border) {
        case BorderDesign::Single: bar(-h, h, off, c.t, slot, zk + 1); break;
        case BorderDesign::Double:
            bar(-h, h, off, c.t, slot, zk + 1);
            bar(-h + c.t, h - c.t, off + (inner ? 1 : 1) * (c.t * 1.3f + c.gap) * (inner ? -1.f : 1.f) * 1.f, c.t * .55f, 1, zk + 1);
            break;
        case BorderDesign::Segmented: {
            float segL = 1.6f + 2.4f * c.comp, g = .32f * c.sc;
            int n = std::max(1, int(std::floor((len + g) / (segL + g)))); float seg = (len - (n - 1) * g) / n;
            for (int i = 0; i < n; i++) { float u0 = -h + i * (seg + g); bar(u0, u0 + seg, off, c.t, slot, zk + 1); }
            break; }
        case BorderDesign::Offset: {
            if (len < 4) { bar(-h, h, off, c.t, slot, zk + 1); break; }
            float step = c.t * 1.6f * (inner ? -1.f : 1.f);
            bar(-h, -h * .15f, off, c.t, slot, zk + 1);
            bar(-h * .15f + c.t, h, off + step, c.t, slot, zk + 1);
            bar(-h * .15f - 0.0f, -h * .15f + c.t, off + step * .5f, c.t, 2, zk + 2); // joint accent
            break; }
        case BorderDesign::Bracketed: {
            float bl = std::min(3.f, len * .38f);
            bar(-h, -h + bl, off, c.t, slot, zk + 1); bar(h - bl, h, off, c.t, slot, zk + 1);
            if (len >= 5) bar(-.5f, .5f, off + (inner ? -1.f : 1.f) * c.t * .8f, c.t * .6f, 2, zk + 1);
            break; }
    }
}

bool runEndConvex(Analysis const& a, EdgeRun const& r, bool atStart) {
    int p = atStart ? r.from - 1 : r.to + 1;
    int sx = (r.dir == 0) ? p : (r.dir == 2) ? p : (r.dir == 1 ? r.line - 1 : r.line);
    int sy = (r.dir == 0) ? r.line - 1 : (r.dir == 2) ? r.line : p;
    return !a.grid.at(sx, sy); // neighbour solid-side cell empty -> the wall turns away -> convex
}

struct V2 { float x, y; };
float dist(V2 a, V2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// ------------------------------------------------------------------ shape specific outlines
void triangleVertices(Structure const& s, V2 v[3]) {
    std::vector<int> L, R; rowExtents(s, L, R); int h = s.height();
    int w0 = R[0] - L[0] + 1, w1 = R[h - 1] - L[h - 1] + 1; bool wideBottom = w0 >= w1;
    int wr = wideBottom ? 0 : h - 1, nr = wideBottom ? h - 1 : 0;
    float yw = wideBottom ? float(s.minY) : float(s.maxY + 1), yn = wideBottom ? float(s.maxY + 1) : float(s.minY);
    v[0] = {float(L[wr]), yw}; v[1] = {float(R[wr] + 1), yw};
    float ax;
    if (L[nr] == L[wr]) ax = float(L[wr]); else if (R[nr] == R[wr]) ax = float(R[wr] + 1); else ax = (L[nr] + R[nr] + 1) * .5f;
    v[2] = {ax, yn};
}

void outlineTriangle(Ctx& c, Structure const& s, bool inner) {
    V2 v[3]; triangleVertices(s, v);
    V2 cen = {(v[0].x + v[1].x + v[2].x) / 3, (v[0].y + v[1].y + v[2].y) / 3};
    float a = dist(v[1], v[2]), b = dist(v[0], v[2]), cc = dist(v[0], v[1]), per = a + b + cc;
    V2 inc = {(a * v[0].x + b * v[1].x + cc * v[2].x) / per, (a * v[0].y + b * v[1].y + cc * v[2].y) / per};
    float area = std::fabs((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y)) * .5f, rin = area / (per * .5f);
    float d = c.gap + c.t * .5f;
    float lam = 1.f; if (inner) { if (rin <= d + .8f) return; lam = (rin - d) / rin; }
    V2 w[3]; for (int i = 0; i < 3; i++) w[i] = inner ? V2{inc.x + (v[i].x - inc.x) * lam, inc.y + (v[i].y - inc.y) * lam} : v[i];
    for (int i = 0; i < 3; i++) {
        V2 p = w[i], q = w[(i + 1) % 3]; float L = dist(p, q); if (L < .6f) continue;
        float tx = (q.x - p.x) / L, ty = (q.y - p.y) / L, nx = ty, ny = -tx; V2 mid = {(p.x + q.x) * .5f, (p.y + q.y) * .5f};
        if ((mid.x + nx - cen.x) * nx + (mid.y + ny - cen.y) * ny < (mid.x - cen.x) * nx + (mid.y - cen.y) * ny) { nx = -nx; ny = -ny; }
        // outer: push outward; inner polygon is already inset, so no extra offset
        float off = inner ? 0.f : d;
        borderLine(c, mid.x, mid.y, tx, ty, nx, ny, L + (inner ? -c.t : 2 * d), off, inner, inner ? 0 : 1);
    }
}

void outlineCircle(Ctx& c, Structure const& s, bool inner) {
    float cx = (s.minX + s.maxX + 1) * .5f, cy = (s.minY + s.maxY + 1) * .5f, r0 = (s.width() + s.height()) * .25f;
    float d = c.gap + c.t * .5f, r = inner ? r0 - d - .15f : r0 + d; if (r < 1.5f) return;
    float segTarget = 1.3f + 1.6f * c.comp; int n = std::max(10, int(std::round(2 * PI * r / segTarget))); n += n & 1;
    for (int i = 0; i < n; i++) {
        if (c.p.border == BorderDesign::Bracketed && (i % std::max(2, n / 8)) > std::max(1, n / 16)) continue;
        if (c.p.border == BorderDesign::Segmented && (i & 1) && c.comp < .6f) continue;
        float rr = r + ((c.p.border == BorderDesign::Offset && (i / 3) & 1) ? c.t * 1.5f * (inner ? -1.f : 1.f) : 0.f);
        float th = 2 * PI * i / n;
        float chord = 2 * rr * std::sin(PI / n) * (c.p.border == BorderDesign::Segmented ? .8f : 1.08f);
        c.add(Role::Bar, Layer::Mid, cx + rr * std::cos(th), cy + rr * std::sin(th), chord, c.t, rotFromAngle(th * 180.f / PI + 90.f), inner ? 1 : 0, inner ? 0 : 1, 1);
    }
    if (c.p.border == BorderDesign::Double && !inner) {
        float r2 = r + c.t * 1.8f + c.gap; int n2 = n;
        for (int i = 0; i < n2; i++) { float th = 2 * PI * (i + .5f) / n2; c.add(Role::Bar, Layer::Mid, cx + r2 * std::cos(th), cy + r2 * std::sin(th), 2 * r2 * std::sin(PI / n2) * 1.08f, c.t * .55f, rotFromAngle(th * 180.f / PI + 90.f), 1, 1, 1); }
    }
}

void outlineRuns(Ctx& c, Structure const& s, bool inner) {
    float d = c.gap + c.t * .5f, e = c.gap + c.t;
    for (auto const& r : s.runs) {
        float len = float(r.to - r.from + 1); bool horiz = (r.dir == 0 || r.dir == 2);
        float mid = (r.from + r.to + 1) * .5f; float nx = float(DX[r.dir]) * (inner ? -1.f : 1.f), ny = float(DY[r.dir]) * (inner ? -1.f : 1.f);
        float sgn = inner ? -1.f : 1.f;
        float ext0 = runEndConvex(c.a, r, true) ? e * sgn : -e * sgn, ext1 = runEndConvex(c.a, r, false) ? e * sgn : -e * sgn;
        if (inner && s.thin) continue;
        float L = len + ext0 + ext1; if (L < .5f) continue;
        float shift = (ext1 - ext0) * .5f;
        float cx = horiz ? mid + shift : float(r.line), cy = horiz ? float(r.line) : mid + shift;
        borderLine(c, cx, cy, horiz ? 1.f : 0.f, horiz ? 0.f : 1.f, nx, ny, L, d, inner, inner ? 0 : 1);
    }
}

// ------------------------------------------------------------------ corner details
void cornerDetails(Ctx& c, Structure const& s) {
    float d = c.gap + c.t * .5f;
    bool glowCat = c.s.has(Cat::Glow), extra = c.s.has(Cat::Extra);
    if (s.shape == Shape::Circular) return;
    if (s.shape == Shape::Triangle) {
        V2 v[3]; triangleVertices(s, v);
        for (auto& p : v) {
            c.add(Role::Dot, Layer::Front, p.x, p.y, .3f * c.sc + c.t, .3f * c.sc + c.t, 0, 2, 2, 0);
            if (glowCat) c.add(Role::Glow, Layer::Back, p.x, p.y, 1.4f, 1.4f, 0, 3, 0, 1);
        }
        return;
    }
    for (auto const& cell : s.convex) {
        bool t = !c.a.grid.at(cell.x, cell.y + 1), r = !c.a.grid.at(cell.x + 1, cell.y), b = !c.a.grid.at(cell.x, cell.y - 1), l = !c.a.grid.at(cell.x - 1, cell.y);
        int combos[4][4] = {{t, r, 1, 1}, {r, b, 1, -1}, {b, l, -1, -1}, {l, t, -1, 1}}; (void)combos;
        struct K { bool on; int sx, sy; } ks[4] = {{t && r, 1, 1}, {b && r, 1, -1}, {b && l, -1, -1}, {t && l, -1, 1}};
        for (auto& k : ks) {
            if (!k.on) continue;
            float vx = cell.x + (k.sx > 0 ? 1.f : 0.f), vy = cell.y + (k.sy > 0 ? 1.f : 0.f);
            if (c.p.cornerCaps) c.add(Role::Dot, Layer::Front, vx + k.sx * d, vy + k.sy * d, .26f * c.sc + c.t * .8f, .26f * c.sc + c.t * .8f, 0, 2, 3, 3);
            if (glowCat && c.p.outerGlow) c.add(Role::Glow, Layer::Back, vx + k.sx * d, vy + k.sy * d, 1.3f, 1.3f, 0, 3, 0, 3);
            if (c.p.cornerCaps && c.comp > .35f && s.shape != Shape::Circular) { // inside corner brace
                float sz = std::min(1.4f, 0.7f + c.comp * .8f);
                c.add(Role::Triangle, Layer::Mid, vx - k.sx * (d + sz * .5f + c.t), vy - k.sy * (d + sz * .5f + c.t), sz, sz, triRot(-k.sx, -k.sy), 1, -1, 2);
            }
        }
    }
    if (extra && c.dens > .35f)
        for (auto const& n : s.concave) {   // notch fillers
            if (!c.r.chance(c.dens)) continue;
            // find which solid corner produced the notch: pick direction from solid neighbours
            for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
                int cx = n.x - sx, cy = n.y - sy;
                if (c.a.grid.at(cx, cy) && c.a.grid.at(cx + sx, cy) && c.a.grid.at(cx, cy + sy) && !c.a.grid.at(n.x, n.y)) {
                    float vx = cx + (sx > 0 ? 1.f : 0.f), vy = cy + (sy > 0 ? 1.f : 0.f), sz = .7f;
                    c.add(Role::Triangle, Layer::Mid, vx + sx * sz * .5f, vy + sy * sz * .5f, sz, sz, triRot(sx, sy), 1, 0, 3);
                }
            }
        }
}

// ------------------------------------------------------------------ glow
void glowDecor(Ctx& c, Structure const& s) {
    if (!c.s.has(Cat::Glow) || c.s.glow <= 0) return;
    float g = c.s.glow / 100.f * c.sd.glow;
    float gt = c.t * (2.5f + 3.5f * g);
    if (s.shape == Shape::Circular) {
        float cx = (s.minX + s.maxX + 1) * .5f, cy = (s.minY + s.maxY + 1) * .5f, r = (s.width() + s.height()) * .25f;
        c.add(Role::Glow, Layer::Back, cx, cy, 2 * r + 1.5f + 2 * gt, 2 * r + 1.5f + 2 * gt, 0, 3, 0, 1);
        return;
    }
    if (s.shape == Shape::Triangle) {
        V2 v[3]; triangleVertices(s, v);
        for (int i = 0; i < 3; i++) { V2 p = v[i], q = v[(i + 1) % 3]; float L = dist(p, q); if (L < .8f) continue;
            c.add(Role::Glow, Layer::Back, (p.x + q.x) * .5f, (p.y + q.y) * .5f, L + gt, gt, rotFromAngle(std::atan2(q.y - p.y, q.x - p.x) * 180.f / PI), 3, 0, 1); }
        return;
    }
    for (auto const& r : s.runs) {
        float len = float(r.to - r.from + 1); if (len < 2 && c.dens < .6f) continue;
        bool horiz = (r.dir == 0 || r.dir == 2); float mid = (r.from + r.to + 1) * .5f, off = c.gap + c.t * .5f + gt * .15f;
        float cx = horiz ? mid : r.line + DX[r.dir] * off, cy = horiz ? r.line + DY[r.dir] * off : mid;
        c.add(Role::Glow, Layer::Back, cx, cy, len + gt, gt, horiz ? 0.f : 90.f, 3, 0, 1);
        if (g > .9f && len >= 3) c.add(Role::Glow, Layer::Back, cx, cy, len + gt * 2.2f, gt * 2.2f, horiz ? 0.f : 90.f, 3, -1, 1); // strong glow halo
    }
    if (std::min(s.width(), s.height()) >= 4) { // ambient light
        c.add(Role::Glow, Layer::Back, (s.minX + s.maxX + 1) * .5f, (s.minY + s.maxY + 1) * .5f, s.width() * .55f, s.height() * .55f, 0, 3, -2, 2);
    }
}

// ------------------------------------------------------------------ interior
struct Rect { int x, y, w, h; };

std::vector<Rect> packRegion(Analysis const& a, Structure const& s, int minDepth) {
    int W = s.width(), H = s.height(); std::vector<uint8_t> reg(size_t(W) * H, 0), used(size_t(W) * H, 0);
    for (auto const& c : s.cells) if (a.depthAt(c.x, c.y) >= minDepth) reg[size_t(c.y - s.minY) * W + (c.x - s.minX)] = 1;
    std::vector<Rect> out;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        size_t i = size_t(y) * W + x; if (!reg[i] || used[i]) continue;
        int w = 1; while (x + w < W && reg[i + w] && !used[i + w]) w++;
        int h = 1; for (;; h++) {
            if (y + h >= H) break; bool ok = true;
            for (int k = 0; k < w; k++) { size_t j = size_t(y + h) * W + x + k; if (!reg[j] || used[j]) { ok = false; break; } }
            if (!ok) break;
        }
        for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < w; xx++) used[size_t(y + yy) * W + x + xx] = 1;
        out.push_back({x + s.minX, y + s.minY, w, h});
    }
    return out;
}

void fillRect(Ctx& c, Rect const& R, FillPattern pat, int rectIdx) {
    float m = .22f * c.sc;
    float x0 = R.x + m, y0 = R.y + m, w = R.w - 2 * m, h = R.h - 2 * m; if (w < .4f || h < .4f) return;
    float cx = R.x + R.w * .5f, cy = R.y + R.h * .5f;
    bool skipGate = !c.r.chance(.55f + .45f * c.fill);          // negative space: some rects stay empty
    bool tech = c.s.has(Cat::Tech);
    switch (pat) {
        case FillPattern::PanelGrid: {
            if (R.w >= 2 && R.h >= 2 && !skipGate) {
                c.add(Role::Panel, Layer::Mid, cx, cy, w, h, 0, 4, -2, 2);
                if (R.w >= 3 && R.h >= 3 && c.comp > .3f) {            // visual hierarchy: inset plate + highlight
                    c.add(Role::Panel, Layer::Mid, cx, cy, w - 1.0f, h - 1.0f, 0, 1, -1, 2);
                    c.add(Role::Bar, Layer::Mid, cx, y0 + h - c.t * .5f - .1f, w - 1.0f, c.t * .5f, 0, 2, 0, 2);
                }
                if (R.w >= 4 && R.h >= 3 && (tech || c.s.style == Style::Industrial) && c.comp > .45f) {
                    float sd2 = .18f * c.sc + .08f;
                    for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2)
                        c.add(Role::Dot, Layer::Front, cx + sx * (w * .5f - .3f), cy + sy * (h * .5f - .3f), sd2, sd2, 0, 2, 1, 2);
                }
            } else if (!skipGate && c.r.chance(.5f)) c.add(Role::Tile, Layer::Mid, cx, cy, .55f * c.sc, .55f * c.sc, 0, 1, -1, 2);
            break; }
        case FillPattern::Circuit: {
            if (R.w < 2 || R.h < 2 || skipGate) break;
            int n = std::max(2, int(R.w * R.h * (.07f + .10f * c.comp))); std::vector<uint8_t> occ(size_t(R.w) * R.h, 0);
            auto idx = [&](int x, int y) { return size_t(y - R.y) * R.w + (x - R.x); };
            for (int k = 0; k < n; k++) {
                int px = c.r.i(R.x, R.x + R.w - 1), py = c.r.i(R.y, R.y + R.h - 1); int dir = c.r.i(0, 3);
                if (occ[idx(px, py)]) continue;
                float lx = px + .5f, ly = py + .5f; occ[idx(px, py)] = 1; bool moved = false;
                for (int seg = 0; seg < 3; seg++) {
                    int len = c.r.i(1, 3), step = 0;
                    for (int attempt = 0; attempt < 4 && step == 0; attempt++, dir = (dir + 1) & 3) {   // try all directions before giving up
                        for (step = 0; step < len; step++) {
                            int nx = px + DX[dir], ny = py + DY[dir];
                            if (nx < R.x || ny < R.y || nx >= R.x + R.w || ny >= R.y + R.h || occ[idx(nx, ny)]) break;
                            px = nx; py = ny; occ[idx(px, py)] = 1;
                        }
                        if (step > 0) { dir = (dir + 3) & 3; break; }   // undo the loop increment so `dir` is the direction actually walked
                    }
                    if (step == 0) break;
                    moved = true;
                    float ex = px + .5f, ey = py + .5f;
                    c.add(Role::Circuit, Layer::Mid, (lx + ex) * .5f, (ly + ey) * .5f, std::fabs(ex - lx) + std::fabs(ey - ly) + .12f, .1f * c.sc + .04f, DX[dir] ? 0.f : 90.f, 1, -1, 2);
                    lx = ex; ly = ey; dir = (dir + (c.r.chance(.5f) ? 1 : 3)) & 3;
                }
                if (moved) c.add(Role::Dot, Layer::Front, lx, ly, .24f * c.sc, .24f * c.sc, 0, 2, 0, 2);
            }
            break; }
        case FillPattern::Stripes: {
            if (R.w < 2 && R.h < 2) break; bool alongX = R.w >= R.h; float L = (alongX ? w : h), span = alongX ? h : w;
            int lines = std::max(1, int(span / (1.f + (1.f - c.fill) * .8f)));
            for (int i = 0; i < lines; i++) {
                if (skipGate && c.r.chance(.5f)) continue; if (c.r.chance(.18f * (1.f - c.fill))) continue;
                float o = -span * .5f + (i + .5f) * span / lines; float th = (i % 3 == 0) ? .2f * c.sc : .09f * c.sc;
                float len = L; float shift = 0; if (L > 6 && c.r.chance(.4f)) { len = L * c.r.f() * .4f + L * .55f; shift = (L - len) * .5f * (c.r.chance(.5f) ? 1.f : -1.f); }
                c.add(Role::Bar, Layer::Mid, alongX ? cx + shift : cx + o, alongX ? cy + o : cy + shift, len, th, alongX ? 0.f : 90.f, (i % 3 == 0) ? 1 : 4, -1, 2);
            }
            break; }
        case FillPattern::DotMatrix: case FillPattern::Hex: {
            float pitch = c.comp > .6f ? 1.f : 1.5f; bool hex = pat == FillPattern::Hex; int row = 0;
            for (float yy = y0 + pitch * .5f; yy < y0 + h; yy += hex ? pitch * .866f : pitch, row++)
                for (float xx = x0 + pitch * .5f + ((hex && (row & 1)) ? pitch * .5f : 0.f); xx < x0 + w - .05f; xx += pitch) {
                    float dn = std::hypot((xx - cx) / std::max(1.f, w * .5f), (yy - cy) / std::max(1.f, h * .5f));
                    if (c.r.chance(.45f - .4f * c.fill) || dn > 1.2f) continue;
                    float sz = (hex ? .24f : .2f) * c.sc * (1.f - .45f * std::min(1.f, dn));
                    if (hex) c.add(Role::Tile, Layer::Mid, xx, yy, sz * 1.4f, sz * 1.4f, 45, 1, -1, 2); else c.add(Role::Dot, Layer::Mid, xx, yy, sz, sz, 0, (row % 4 == 0) ? 2 : 1, -1, 2);
                }
            break; }
        case FillPattern::Layered: {
            int k = 0; float inset = 0;
            while (w - 2 * inset > 1.2f && h - 2 * inset > 1.2f && k < 4) {
                float ww = w - 2 * inset, hh = h - 2 * inset, th = std::max(.07f, c.t * (1.f - .22f * k)); int slot = (k & 1) ? 4 : 1;
                if (!skipGate || k == 0) {
                    c.add(Role::Bar, Layer::Mid, cx, y0 + hh + inset - th * .5f, ww, th, 0, slot, -2, 2);
                    c.add(Role::Bar, Layer::Mid, cx, y0 + inset + th * .5f, ww, th, 0, slot, -2, 2);
                    c.add(Role::Bar, Layer::Mid, x0 + inset + th * .5f, cy, hh - 2 * th, th, 90, slot, -2, 2);
                    c.add(Role::Bar, Layer::Mid, x0 + ww + inset - th * .5f, cy, hh - 2 * th, th, 90, slot, -2, 2);
                }
                inset += 1.0f; k++;
            }
            break; }
        case FillPattern::None: break;
    }
    (void)rectIdx;
}

void interiorFill(Ctx& c, Structure const& s) {
    if (!c.s.has(Cat::Interior) || c.fill <= 0.01f || c.p.fill == FillPattern::None || s.depthMax < 3) return;
    if (s.shape == Shape::Circular) {      // concentric rings follow the curve
        float cx = (s.minX + s.maxX + 1) * .5f, cy = (s.minY + s.maxY + 1) * .5f, r0 = (s.width() + s.height()) * .25f;
        for (int k = 0; k < 3; k++) {
            float r = r0 - 1.3f - k * 1.2f; if (r < 1.f) break; int n = std::max(8, int(2 * PI * r / (1.1f + c.comp))); n += n & 1;
            for (int i = 0; i < n; i++) {
                if ((k == 1) && (i % 4 == 3)) continue; float th = 2 * PI * i / n;
                c.add(Role::Bar, Layer::Mid, cx + r * std::cos(th), cy + r * std::sin(th), 2 * r * std::sin(PI / n) * 1.05f, std::max(.07f, c.t * (1.f - .25f * k)), rotFromAngle(th * 180.f / PI + 90.f), k & 1 ? 4 : 1, -2, 2);
            }
        }
        c.add(Role::Dot, Layer::Front, cx, cy, .5f * c.sc, .5f * c.sc, 0, 2, 1, 2);
        return;
    }
    if (s.shape == Shape::Triangle) {      // nested triangles
        V2 v[3]; triangleVertices(s, v);
        V2 cen = {(v[0].x + v[1].x + v[2].x) / 3, (v[0].y + v[1].y + v[2].y) / 3};
        for (int k = 1; k <= 3; k++) {
            float lam = 1.f - .26f * k; if (lam < .25f) break;
            V2 w[3]; for (int i = 0; i < 3; i++) w[i] = {cen.x + (v[i].x - cen.x) * lam, cen.y + (v[i].y - cen.y) * lam};
            for (int i = 0; i < 3; i++) { V2 p = w[i], q = w[(i + 1) % 3]; float L = dist(p, q); if (L < .6f) continue;
                c.add(Role::Bar, Layer::Mid, (p.x + q.x) * .5f, (p.y + q.y) * .5f, L, std::max(.07f, c.t * (1.f - .2f * k)), rotFromAngle(std::atan2(q.y - p.y, q.x - p.x) * 180.f / PI), (k & 1) ? 4 : 1, -2, 2); }
        }
        c.add(Role::Dot, Layer::Front, cen.x, cen.y, .4f * c.sc, .4f * c.sc, 0, 2, 1, 2);
        return;
    }
    auto rects = packRegion(c.a, s, 2);
    // split very long rects so that patterns keep rhythm and variety
    std::vector<Rect> split;
    for (auto& R : rects) {
        if (R.w > 12) { int cut = c.r.i(5, R.w - 5); split.push_back({R.x, R.y, cut, R.h}); split.push_back({R.x + cut, R.y, R.w - cut, R.h}); }
        else split.push_back(R);
    }
    int i = 0;
    for (auto& R : split) {
        FillPattern pat = c.p.fill;
        // secondary pattern for variety: complexity decides how often a rect deviates from the main pattern
        if (c.r.chance(.2f * c.comp) && R.w >= 3 && R.h >= 3) pat = (pat == FillPattern::PanelGrid) ? FillPattern::Layered : FillPattern::PanelGrid;
        fillRect(c, R, pat, i++);
    }
}

// ------------------------------------------------------------------ nature / tech / extras
void natureDecor(Ctx& c, Structure const& s) {
    bool natStyle = c.sd.nature > .5f;
    if (!c.s.has(Cat::Nature) && !natStyle) return; if (!c.s.has(Cat::Nature)) return;
    float amount = c.dens * (natStyle ? 1.f : .5f) + c.sd.nature * .3f;
    for (auto const& r : s.runs) {
        float len = float(r.to - r.from + 1);
        if (s.shape == Shape::Circular) break;
        if (r.dir == 0) {                          // top: crystals / rocks
            int n = int(len * amount * .35f + (c.r.chance(amount) ? 1 : 0)); float x = r.from + c.r.f();
            for (int k = 0; k < n; k++) {
                float h = .7f + c.r.f() * (1.2f + c.comp), w = h * .45f, px = r.from + .6f + c.r.f() * std::max(0.f, len - 1.2f);
                (void)x; bool crystal = c.r.chance(.65f);
                c.add(crystal ? Role::Crystal : Role::Rock, Layer::Front, px, r.line + h * .5f - .05f, w, h, crystal ? (c.r.f() - .5f) * 28.f + 0.f : 0.f, crystal ? 2 : 1, 2, 3);
                if (crystal && c.s.has(Cat::Glow)) c.add(Role::Glow, Layer::Back, px, r.line + h * .5f, w * 2.6f, h * 1.4f, 0, 3, -1, 3);
            }
        } else if (r.dir == 2 && len >= 2) {       // bottom: hanging vines
            int n = int(len * amount * .4f);
            for (int k = 0; k < n; k++) {
                float h = 1.f + c.r.f() * (1.5f + 2.f * c.comp), px = r.from + .5f + c.r.f() * (len - 1.f);
                c.add(Role::Plant, Layer::Front, px, r.line - h * .5f + .05f, .5f * c.sc + .1f, h, 180.f, 0, 2, 3);
            }
        }
    }
}

void techDecor(Ctx& c, Structure const& s) {
    if (!c.s.has(Cat::Tech) || s.shape == Shape::Circular || s.shape == Shape::Triangle) return;
    float tw = c.sd.tech * .6f + .3f;
    for (auto const& r : s.runs) {
        float len = float(r.to - r.from + 1); if (len < 5 || !c.r.chance(tw * c.dens + .15f)) continue;
        bool horiz = (r.dir == 0 || r.dir == 2); float mid = (r.from + r.to + 1) * .5f;
        float off = 1.15f + c.r.f() * .5f;      // digital line dashes riding behind the border, outside
        int dashes = std::max(2, int(len / 2.5f)); float u = r.from + .8f;
        for (int k = 0; k < dashes; k++) {
            float dl = .4f + c.r.f() * 1.2f; if (u + dl > r.to + 1 - .5f) break;
            float cx = horiz ? u + dl * .5f : r.line + DX[r.dir] * off, cy = horiz ? r.line + DY[r.dir] * off : u + dl * .5f;
            c.add(Role::Circuit, Layer::Back, cx, cy, dl, .09f * c.sc + .03f, horiz ? 0.f : 90.f, (k % 3 == 0) ? 2 : 3, -1, 3);
            u += dl + .5f + c.r.f() * .9f;
        }
        (void)mid;
    }
}

void extraDecor(Ctx& c, Structure const& s) {
    if (!c.s.has(Cat::Extra) || s.shape == Shape::Circular || s.shape == Shape::Triangle) return;
    for (auto const& r : s.runs) {
        float len = float(r.to - r.from + 1); if (len < 3) continue; bool horiz = (r.dir == 0 || r.dir == 2);
        if (c.r.chance(c.dens * .7f)) {     // accent highlight near one end of a run
            float u = r.from + .7f + (c.r.chance(.5f) ? 0.f : len - 1.4f), off = c.gap + c.t * 1.8f;
            c.add(Role::Bar, Layer::Front, horiz ? u + .5f : r.line + DX[r.dir] * off, horiz ? r.line + DY[r.dir] * off : u + .5f, 1.f, c.t * .55f, horiz ? 0.f : 90.f, 2, 3, 3);
        }
        if (c.r.chance(c.dens * .5f)) {     // decorative dot trio
            float u = r.from + len * (.3f + .4f * c.r.f()); float off = 1.0f;
            for (int k = 0; k < 3; k++) { float uu = u + k * .45f; float cx = horiz ? uu : r.line + DX[r.dir] * off, cy = horiz ? r.line + DY[r.dir] * off : uu;
                c.add(Role::Dot, Layer::Front, cx, cy, (k == 1 ? .26f : .16f) * c.sc, (k == 1 ? .26f : .16f) * c.sc, 0, k == 1 ? 2 : 3, 2, 3); }
        }
    }
    for (auto const& cell : s.convex) if (c.r.chance(c.dens * .15f)) {   // sparks near corners
        float a = c.r.f() * 2 * PI, d = 1.2f + c.r.f();
        c.add(Role::Spark, Layer::Front, cell.x + .5f + std::cos(a) * d, cell.y + .5f + std::sin(a) * d, .25f * c.sc, .25f * c.sc, c.r.f() * 90.f, 2, 3, 3);
    }
}

void holeDecor(Ctx& c, Structure const& s) {
    for (auto const& h : s.holes) {
        float cx = (h.minX + h.maxX + 1) * .5f, cy = (h.minY + h.maxY + 1) * .5f, w = float(h.maxX - h.minX + 1), hh = float(h.maxY - h.minY + 1);
        if (c.s.has(Cat::Glow)) c.add(Role::Glow, Layer::Back, cx, cy, w * .9f, hh * .9f, 0, 3, -3, 0);
        if (c.s.has(Cat::Background) && w * hh >= 9) {
            int n = std::min(8, int(w * hh * .12f * c.dens) + 1);
            for (int k = 0; k < n; k++) { float px = h.minX + .5f + c.r.f() * (w - 1), py = h.minY + .5f + c.r.f() * (hh - 1);
                if (c.solidAt(px, py)) continue; c.add(Role::Spark, Layer::Back, px, py, .22f * c.sc, .22f * c.sc, 0, 3, -2, 0); }
        }
    }
}

void backgroundScatter(Ctx& c, float x0, float y0, float x1, float y1, int count, float marginAvoid) {
    if (!c.s.has(Cat::Background) || count <= 0) return;
    std::vector<V2> placed;
    for (int tries = 0, got = 0; tries < count * 12 && got < count; tries++) {
        float px = x0 + c.r.f() * (x1 - x0), py = y0 + c.r.f() * (y1 - y0);
        if (c.solidAt(px, py)) continue;
        bool ok = true; for (auto& p : placed) if (dist(p, {px, py}) < 2.2f + c.s.spacing) { ok = false; break; }
        if (ok) for (int dy = -1; dy <= 1 && ok; dy++) for (int dx = -1; dx <= 1; dx++) if (c.solidAt(px + dx * marginAvoid, py + dy * marginAvoid)) { ok = false; break; }
        if (!ok) continue; placed.push_back({px, py}); got++;
        float k = c.r.f();
        if (k < .5f) c.add(Role::Spark, Layer::Back, px, py, (.18f + .2f * c.r.f()) * c.sc, (.18f + .2f * c.r.f()) * c.sc, c.r.f() * 90.f, 3, -3, 3);
        else if (k < .82f) { float sz = (1.2f + 2.f * c.r.f()) * c.sc; c.add(Role::Triangle, Layer::Back, px, py, sz, sz, 90.f * c.r.i(0, 3), 4, -5, 3); }
        else { float sz = (1.6f + 2.5f * c.r.f()) * c.sc; c.add(Role::Tile, Layer::Back, px, py, sz, sz, 45, 4, -5, 3); }
    }
}

void decorateStructure(Ctx& c, Structure const& s) {
    bool structural = c.s.has(Cat::Structural);
    if (structural) {
        auto outline = [&](bool inner) {
            if (s.shape == Shape::Triangle) outlineTriangle(c, s, inner);
            else if (s.shape == Shape::Circular) outlineCircle(c, s, inner);
            else outlineRuns(c, s, inner);
        };
        outline(false);
        if (!s.thin && std::min(s.width(), s.height()) >= 3 && c.r.chance(.55f + .4f * c.comp)) outline(true);
        cornerDetails(c, s);
    }
    glowDecor(c, s);
    interiorFill(c, s);
    natureDecor(c, s);
    techDecor(c, s);
    extraDecor(c, s);
    holeDecor(c, s);
    int bg = int(((s.maxX - s.minX + 1) + (s.maxY - s.minY + 1)) * 0.5f * c.s.background / 100.f * (.5f + c.dens));
    backgroundScatter(c, s.minX - 5.f, s.minY - 4.f, s.maxX + 6.f, s.maxY + 5.f, bg, 1.0f);
}

// ------------------------------------------------------------------ mirror for symmetry
void symmetrise(std::vector<Item>& items, float axisX, int pct, Rng& r, size_t from) {
    if (pct <= 0) return; size_t n = items.size();
    for (size_t i = from; i < n; i++) {
        Item it = items[i]; if (it.x > axisX - .01f) continue;           // keep right half, mirror it to the left
        if (!r.chance(pct / 100.f)) continue;
        Item m = it; m.x = 2 * axisX - it.x; m.rot = std::fmod(360.f - it.rot, 360.f); m.flip = !it.flip; items.push_back(m);
    }
}

} // namespace

// ------------------------------------------------------------------ optimiser
int optimise(std::vector<Item>& items, Analysis const& a, Settings const& s) {
    auto prio = [](Item const& it) { return it.group == 1 ? 4 : (it.role == Role::Panel || it.role == Role::Bar || it.role == Role::Glow) ? 3 : (it.role == Role::Crystal || it.role == Role::Plant || it.role == Role::Rock) ? 2 : 1; };
    std::vector<size_t> order(items.size()); std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return prio(items[x]) > prio(items[y]); });
    std::vector<uint8_t> keep(items.size(), 0);
    std::unordered_map<long long, std::vector<size_t>> buckets[3];
    auto aabb = [](Item const& it, float& hx, float& hy) {
        float rr = it.rot * PI / 180.f, cs = std::fabs(std::cos(rr)), sn = std::fabs(std::sin(rr));
        hx = (cs * it.sx + sn * it.sy) * .5f; hy = (sn * it.sx + cs * it.sy) * .5f; };
    int removed = 0;
    for (size_t i : order) {
        Item const& it = items[i]; float hx, hy; aabb(it, hx, hy);
        int cx = int(std::floor(it.x)), cy = int(std::floor(it.y));
        if (it.group == 3 && a.grid.at(cx, cy)) { removed++; continue; }
        if (it.group == 2 && a.ownerAt(cx, cy) < 0) { removed++; continue; }
        bool overlap = false;
        if (it.group != 1 && it.role != Role::Glow) {
            auto& B = buckets[int(it.layer)];
            float minGap = (it.role == Role::Dot || it.role == Role::Spark) ? .05f : 0.f;
            for (int gx = int(std::floor(it.x - hx)) - 1; gx <= int(std::floor(it.x + hx)) + 1 && !overlap; gx++)
                for (int gy = int(std::floor(it.y - hy)) - 1; gy <= int(std::floor(it.y + hy)) + 1 && !overlap; gy++) {
                    auto f = B.find((long long)gx * 1000003LL + gy); if (f == B.end()) continue;
                    for (size_t j : f->second) {
                        Item const& o = items[j]; float ox, oy; aabb(o, ox, oy);
                        float iw = std::min(it.x + hx, o.x + ox) - std::max(it.x - hx, o.x - ox) + minGap, ih = std::min(it.y + hy, o.y + oy) - std::max(it.y - hy, o.y - oy) + minGap;
                        if (iw > 0 && ih > 0) { float ia = iw * ih, sm = std::min(4 * hx * hy, 4 * ox * oy); if (ia > .3f * sm) { overlap = true; break; } }
                    }
                }
        }
        if (overlap) { removed++; continue; }
        keep[i] = 1;
        if (it.group != 1 && it.role != Role::Glow) {
            auto& B = buckets[int(it.layer)];
            for (int gx = int(std::floor(it.x - hx)); gx <= int(std::floor(it.x + hx)); gx++)
                for (int gy = int(std::floor(it.y - hy)); gy <= int(std::floor(it.y + hy)); gy++) B[(long long)gx * 1000003LL + gy].push_back(i);
        }
    }
    // object budget: drop lowest priority first
    size_t cells = 0; for (auto& st : a.structures) cells += st.cells.size();
    size_t cap = size_t(40 + 3.5f * std::sqrt(float(cells)) * (.4f + s.density / 100.f) * std::max<size_t>(1, a.structures.size() > 8 ? 4 : a.structures.size()) * 1.1f);
    size_t alive = std::count(keep.begin(), keep.end(), 1);
    for (int p = 1; p <= 4 && alive > cap; p++)
        for (size_t k = order.size(); k-- > 0 && alive > cap;) { size_t i = order[k]; if (keep[i] && prio(items[i]) == p && p < 4) { keep[i] = 0; alive--; removed++; } }
    std::vector<Item> out; out.reserve(alive);
    for (size_t i = 0; i < items.size(); i++) if (keep[i]) out.push_back(items[i]);
    items = std::move(out);
    return removed;
}

// ------------------------------------------------------------------ entry points
static std::vector<Variation> run(Analysis const& a, std::vector<int> const& ids, Settings const& s, int count, uint64_t salt, bool full) {
    Rng profRng(s.seed * 7919ull + salt * 104729ull + 17);
    auto profiles = makeProfiles(s, count, profRng);
    Palette pal = makePalette(s.paletteMode, s.seed ^ (salt * 0x9E37ull), s.paletteIndex, s.custom);
    std::vector<Variation> res;
    for (int vi = 0; vi < count; vi++) {
        Variation v; v.profile = profiles[size_t(vi)]; v.seed = s.seed * 1315423911ull + uint64_t(vi) * 7919ull + salt * 104729ull; v.palette = pal;
        Rng r(v.seed);
        Ctx c(a, s, v.profile, r, v.items);
        for (int id : ids) {
            if (id < 0 || id >= int(a.structures.size())) continue;
            auto const& st = a.structures[size_t(id)];
            size_t from = v.items.size(); decorateStructure(c, st);
            if (s.symmetry > 0 && (st.shape == Shape::Rectangle || st.shape == Shape::Circular)) symmetrise(v.items, (st.minX + st.maxX + 1) * .5f, s.symmetry, r, from);
        }
        if (full && s.has(Cat::Background) && !a.structures.empty()) {
            // level-wide depth layer: sparse large shapes + particles over the whole extent
            int minX = 1 << 30, minY = 1 << 30, maxX = -(1 << 30), maxY = -(1 << 30);
            for (auto& st : a.structures) { minX = std::min(minX, st.minX); maxX = std::max(maxX, st.maxX); minY = std::min(minY, st.minY); maxY = std::max(maxY, st.maxY); }
            int n = std::clamp(int((maxX - minX) / 14.f * s.background / 50.f), 0, 400);
            backgroundScatter(c, minX - 6.f, minY - 6.f, maxX + 6.f, maxY + 10.f, n, 2.0f);
        }
        optimise(v.items, a, s);
        res.push_back(std::move(v));
    }
    return res;
}

std::vector<Variation> generate(Analysis const& a, std::vector<int> const& ids, Settings const& s, int count, uint64_t salt) { return run(a, ids, s, count, salt, false); }

std::vector<Variation> generateFull(Analysis const& a, Settings const& s, int count, uint64_t salt) {
    std::vector<int> ids; for (auto& st : a.structures) ids.push_back(st.id);
    return run(a, ids, s, count, salt, true);
}

} // namespace ad
