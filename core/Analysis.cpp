#include "Core.hpp"
#include "Internal.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>

namespace ad {

Grid Grid::make(int minX, int minY, int maxX, int maxY, int pad) {
    Grid g;
    g.ox = minX - pad; g.oy = minY - pad;
    g.w = (maxX - minX + 1) + pad * 2;
    g.h = (maxY - minY + 1) + pad * 2;
    g.solid.assign(size_t(g.w) * g.h, 0);
    return g;
}

const char* shapeName(Shape s) {
    switch (s) {
        case Shape::Rectangle: return "Rectangle";
        case Shape::Triangle: return "Triangle";
        case Shape::Circular: return "Circular";
        default: return "Complex";
    }
}

static const char* kStyleNames[] = {"Modern", "Minimalist", "Glow", "Cyberpunk", "Futuristic", "Industrial", "Tech", "Abstract", "Geometric", "Nature", "Dark", "Detailed", "Clean", "Space"};
const char* styleName(Style s) { return kStyleNames[int(s)]; }

// ---------- palettes ----------
static Rgb hsv(float h, float s, float v) {
    h = std::fmod(h, 360.f); if (h < 0) h += 360.f;
    float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.f, 2.f) - 1)), m = v - c, r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    return {uint8_t((r + m) * 255), uint8_t((g + m) * 255), uint8_t((b + m) * 255)};
}
static Rgb hex(uint32_t v) { return {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; }

std::vector<Palette> presetPalettes() {
    return {
        {"Neon Night",   {hex(0x14E8FF), hex(0x7A2CFF), hex(0xFF2E93), hex(0x5CF2FF), hex(0x0A0B1E)}},
        {"Sunset",       {hex(0xFF7A3D), hex(0xC2255C), hex(0xFFD166), hex(0xFF9E5E), hex(0x1B0A21)}},
        {"Forest",       {hex(0x4CAF6A), hex(0x2E7D5B), hex(0xE6D36B), hex(0x8CF0A8), hex(0x0B1F17)}},
        {"Ocean",        {hex(0x2EA8FF), hex(0x1B5FD6), hex(0x7CFFE0), hex(0x6FD3FF), hex(0x07142B)}},
        {"Industrial",   {hex(0xB0B7C3), hex(0x6B7280), hex(0xFFB020), hex(0xFFD27A), hex(0x14161B)}},
        {"Royal",        {hex(0x9B5CFF), hex(0x4B2BB5), hex(0xFFD36B), hex(0xC9A6FF), hex(0x120A2B)}},
        {"Mono Ice",     {hex(0xDCEBFF), hex(0x7FA6D6), hex(0xFFFFFF), hex(0xBFE0FF), hex(0x0D1522)}},
        {"Toxic",        {hex(0x9DFF3C), hex(0x2DBE6C), hex(0xFFF23C), hex(0xCFFF7A), hex(0x0B140A)}},
    };
}

Palette makePalette(PaletteMode m, uint64_t seed, int idx, Palette const& custom) {
    Rng r(seed ^ 0xA5A5);
    auto presets = presetPalettes();
    switch (m) {
        case PaletteMode::Custom: return custom;
        case PaletteMode::Random: {
            float h = r.f() * 360.f; Palette p; p.name = "Random";
            // analogous primary/secondary + split-complement accent keeps random palettes harmonious
            p.c = {hsv(h, .75f, 1.f), hsv(h + 35, .8f, .75f), hsv(h + 165, .7f, 1.f), hsv(h, .35f, 1.f), hsv(h + 20, .6f, .12f)};
            return p;
        }
        case PaletteMode::Complementary: {
            float h = r.f() * 360.f; Palette p; p.name = "Complementary";
            p.c = {hsv(h, .8f, 1.f), hsv(h, .6f, .6f), hsv(h + 180, .85f, 1.f), hsv(h, .3f, 1.f), hsv(h, .5f, .1f)};
            return p;
        }
        case PaletteMode::Monochrome: {
            float h = r.f() * 360.f; Palette p; p.name = "Monochrome";
            p.c = {hsv(h, .55f, 1.f), hsv(h, .65f, .65f), hsv(h, .2f, 1.f), hsv(h, .4f, 1.f), hsv(h, .6f, .1f)};
            return p;
        }
        default: return presets[size_t(std::max(0, idx)) % presets.size()];
    }
}

void Settings::randomise(Rng& r) {
    style = Style(r.i(0, int(Style::Count) - 1));
    density = r.i(30, 80); detail = r.i(25, 85); glow = r.i(20, 85);
    borderThickness = r.i(1, 4); complexity = r.i(25, 85); interiorFill = r.i(30, 80);
    background = r.i(10, 60); scalePct = r.i(80, 130); layerDepth = r.i(30, 80);
    spacing = r.i(1, 2); symmetry = r.chance(.3f) ? r.i(30, 100) : 0;
    seed = r.next() & 0xFFFFFF;
    paletteMode = PaletteMode(r.i(0, 3)); paletteIndex = r.i(0, int(presetPalettes().size()) - 1);
}

// ---------- analysis ----------
static const int DX[4] = {0, 1, 0, -1}, DY[4] = {1, 0, -1, 0};

void rowExtents(Structure const& s, std::vector<int>& left, std::vector<int>& right) {
    int h = s.height();
    left.assign(h, 1 << 30); right.assign(h, -(1 << 30));
    for (auto& c : s.cells) { int r = c.y - s.minY; left[r] = std::min(left[r], c.x); right[r] = std::max(right[r], c.x); }
}

static Shape classify(Structure const& s, Analysis const& a) {
    int w = s.width(), h = s.height(), n = int(s.cells.size());
    float fill = float(n) / float(w * h);
    if (fill >= 0.98f) return Shape::Rectangle;
    if (w < 3 || h < 3) return Shape::Complex;
    // contiguous rows? needed for triangle test
    std::vector<int> L, R; rowExtents(s, L, R);
    std::vector<int> wid(h);
    bool solidRows = true;
    for (int r = 0; r < h; r++) {
        if (L[r] > R[r]) { solidRows = false; break; }
        wid[r] = R[r] - L[r] + 1;
        if (wid[r] != 0) for (int x = L[r]; x <= R[r]; x++) if (!a.grid.at(x, s.minY + r)) { solidRows = false; break; }
        if (!solidRows) break;
    }
    if (solidRows && fill > 0.38f && fill < 0.68f && h >= 3) {
        bool inc = true, dec = true;
        for (int r = 1; r < h; r++) { inc &= wid[r] >= wid[r - 1]; dec &= wid[r] <= wid[r - 1]; }
        if ((inc || dec) && std::min(wid[0], wid[h - 1]) <= 2) {   // a true triangle ends in a point, otherwise it is a trapezoid (= Complex)
            // linear fit check on widths
            double maxDev = 0; int w0 = inc ? wid[0] : wid[h - 1], w1 = inc ? wid[h - 1] : wid[0];
            for (int r = 0; r < h; r++) {
                double t = (h == 1) ? 0 : double(r) / (h - 1);
                double expect = inc ? w0 + (w1 - w0) * t : w1 + (w0 - w1) * t;
                maxDev = std::max(maxDev, std::fabs(expect - wid[r]));
            }
            if (maxDev <= std::max(1.5, 0.12 * w)) return Shape::Triangle;
        }
    }
    float aspect = float(w) / float(h);
    if (w >= 5 && h >= 5 && fill > 0.66f && fill < 0.9f && aspect > 0.8f && aspect < 1.25f) {
        int match = 0, tot = 0;
        for (auto& c : s.cells) {
            int mx = s.minX + s.maxX - c.x, my = s.minY + s.maxY - c.y;
            tot += 2; match += (a.grid.at(mx, c.y) ? 1 : 0) + (a.grid.at(c.x, my) ? 1 : 0);
        }
        if (float(match) / float(tot) > 0.92f) return Shape::Circular;
    }
    return Shape::Complex;
}

Analysis analyse(Grid g, int minCells) {
    Analysis A; A.grid = std::move(g);
    auto& G = A.grid; size_t N = size_t(G.w) * G.h;
    A.owner.assign(N, -1); A.depth.assign(N, 0);
    auto idx = [&](int x, int y) { return size_t(y - G.oy) * G.w + (x - G.ox); };

    // 1) connected components (8-connectivity so diagonal blocks join)
    std::vector<int> comp(N, -1); int nComp = 0; std::vector<std::vector<Cell>> comps;
    for (int y = G.oy; y < G.oy + G.h; y++) for (int x = G.ox; x < G.ox + G.w; x++) {
        if (!G.at(x, y) || comp[idx(x, y)] >= 0) continue;
        std::vector<Cell> cells; std::queue<Cell> q; q.push({x, y}); comp[idx(x, y)] = nComp;
        while (!q.empty()) {
            Cell c = q.front(); q.pop(); cells.push_back(c);
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int nx = c.x + dx, ny = c.y + dy;
                if ((dx || dy) && G.at(nx, ny) && comp[idx(nx, ny)] < 0) { comp[idx(nx, ny)] = nComp; q.push({nx, ny}); }
            }
        }
        comps.push_back(std::move(cells)); nComp++;
    }

    // 2) depth layers (distance to nearest empty cell, 4-neighbourhood)
    std::queue<Cell> q;
    for (int y = G.oy; y < G.oy + G.h; y++) for (int x = G.ox; x < G.ox + G.w; x++) {
        if (!G.at(x, y)) continue;
        for (int d = 0; d < 4; d++) if (!G.at(x + DX[d], y + DY[d])) { A.depth[idx(x, y)] = 1; q.push({x, y}); break; }
    }
    while (!q.empty()) {
        Cell c = q.front(); q.pop(); int dv = A.depth[idx(c.x, c.y)];
        for (int d = 0; d < 4; d++) {
            int nx = c.x + DX[d], ny = c.y + DY[d];
            if (G.at(nx, ny) && A.depth[idx(nx, ny)] == 0) { A.depth[idx(nx, ny)] = uint8_t(std::min(250, dv + 1)); q.push({nx, ny}); }
        }
    }

    // 3) structures (noise filter = Smart Detection rule: ignore tiny scatter)
    for (auto& cells : comps) {
        if (int(cells.size()) < minCells) continue;
        Structure s; s.id = int(A.structures.size()); s.cells = std::move(cells);
        s.minX = s.minY = 1 << 30; s.maxX = s.maxY = -(1 << 30);
        for (auto& c : s.cells) {
            s.minX = std::min(s.minX, c.x); s.maxX = std::max(s.maxX, c.x);
            s.minY = std::min(s.minY, c.y); s.maxY = std::max(s.maxY, c.y);
            A.owner[idx(c.x, c.y)] = s.id; s.depthMax = std::max(s.depthMax, int(A.depth[idx(c.x, c.y)]));
        }
        A.structures.push_back(std::move(s));
    }

    // 4) edge runs and corners
    for (auto& s : A.structures) {
        std::map<std::pair<int, int>, std::vector<int>> lines; // (dir,line) -> positions
        for (auto& c : s.cells) {
            for (int d = 0; d < 4; d++) {
                if (G.at(c.x + DX[d], c.y + DY[d])) continue;
                int line = (d == 0) ? c.y + 1 : (d == 2) ? c.y : (d == 1) ? c.x + 1 : c.x;
                int pos = (d == 0 || d == 2) ? c.x : c.y;
                lines[{d, line}].push_back(pos);
            }
            bool t = !G.at(c.x, c.y + 1), r = !G.at(c.x + 1, c.y), b = !G.at(c.x, c.y - 1), l = !G.at(c.x - 1, c.y);
            if ((t && r) || (r && b) || (b && l) || (l && t)) s.convex.push_back(c);
            // concave: both orthogonal neighbours solid but a diagonal is empty -> notch at the diagonal cell
            int sx[4] = {1, 1, -1, -1}, sy[4] = {1, -1, 1, -1};
            for (int k = 0; k < 4; k++)
                if (G.at(c.x + sx[k], c.y) && G.at(c.x, c.y + sy[k]) && !G.at(c.x + sx[k], c.y + sy[k]))
                    s.concave.push_back({c.x + sx[k], c.y + sy[k]});
        }
        for (auto& [key, pos] : lines) {
            std::sort(pos.begin(), pos.end());
            int from = pos[0], prev = pos[0];
            for (size_t i = 1; i <= pos.size(); i++) {
                if (i == pos.size() || pos[i] != prev + 1) { s.runs.push_back({key.first, key.second, from, prev}); if (i < pos.size()) from = pos[i]; }
                if (i < pos.size()) prev = pos[i];
            }
        }
    }

    // 5) enclosed empty regions ("holes") – flood from the padded border
    std::vector<uint8_t> outside(N, 0); std::queue<Cell> fq;
    fq.push({G.ox, G.oy}); outside[idx(G.ox, G.oy)] = 1;
    while (!fq.empty()) {
        Cell c = fq.front(); fq.pop();
        for (int d = 0; d < 4; d++) {
            int nx = c.x + DX[d], ny = c.y + DY[d];
            if (G.in(nx, ny) && !G.at(nx, ny) && !outside[idx(nx, ny)]) { outside[idx(nx, ny)] = 1; fq.push({nx, ny}); }
        }
    }
    std::vector<uint8_t> seen(N, 0);
    for (int y = G.oy; y < G.oy + G.h; y++) for (int x = G.ox; x < G.ox + G.w; x++) {
        if (G.at(x, y) || outside[idx(x, y)] || seen[idx(x, y)]) continue;
        Hole h; h.minX = h.minY = 1 << 30; h.maxX = h.maxY = -(1 << 30); int own = -1;
        std::queue<Cell> hq; hq.push({x, y}); seen[idx(x, y)] = 1;
        while (!hq.empty()) {
            Cell c = hq.front(); hq.pop(); h.cells.push_back(c);
            h.minX = std::min(h.minX, c.x); h.maxX = std::max(h.maxX, c.x); h.minY = std::min(h.minY, c.y); h.maxY = std::max(h.maxY, c.y);
            for (int d = 0; d < 4; d++) {
                int nx = c.x + DX[d], ny = c.y + DY[d];
                if (!G.in(nx, ny)) continue;
                if (G.at(nx, ny)) { if (own < 0) own = A.owner[idx(nx, ny)]; }
                else if (!seen[idx(nx, ny)]) { seen[idx(nx, ny)] = 1; hq.push({nx, ny}); }
            }
        }
        if (own >= 0 && h.cells.size() >= 2) A.structures[size_t(own)].holes.push_back(std::move(h));
    }

    // 6) shape, thickness, score
    for (auto& s : A.structures) {
        s.shape = classify(s, A);
        s.thin = s.depthMax <= 2;
        float n = float(s.cells.size());
        s.score = std::sqrt(n) * (1.f + 0.5f * float(s.holes.size())) * (s.thin ? 0.6f : 1.f) * (1.f + 0.04f * float(s.runs.size()) / std::max(1.f, std::sqrt(n)));
    }
    return A;
}

} // namespace ad
