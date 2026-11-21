// Auto Decorator – SDK-independent decoration engine.
// Plain C++17 so it can be unit-tested without Geometry Dash.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ad {

constexpr float kUnit = 30.f; // one GD grid cell in editor units

struct Cell { int x = 0, y = 0; };

// ---------- RNG (deterministic, seedable) ----------
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 1) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float f() { return float(next() >> 40) * (1.0f / 16777216.0f); }   // [0,1)
    int i(int lo, int hi) { return lo + int(f() * float(hi - lo + 1)); } // inclusive
    bool chance(float p) { return f() < p; }
};

// ---------- Level geometry ----------
struct Grid {
    int ox = 0, oy = 0, w = 0, h = 0;
    std::vector<uint8_t> solid; // 1 = gameplay solid
    bool in(int x, int y) const { return x >= ox && y >= oy && x < ox + w && y < oy + h; }
    bool at(int x, int y) const { return in(x, y) && solid[size_t(y - oy) * w + (x - ox)]; }
    void set(int x, int y) { if (in(x, y)) solid[size_t(y - oy) * w + (x - ox)] = 1; }
    static Grid make(int minX, int minY, int maxX, int maxY, int pad = 3);
};

enum class Shape { Rectangle, Triangle, Circular, Complex };
const char* shapeName(Shape s);

struct EdgeRun {  // maximal straight stretch of exposed block edge
    int dir;      // 0 faces +y (top), 1 faces +x, 2 faces -y, 3 faces -x
    int line;     // grid line the edge lies on (y for dir 0/2, x for dir 1/3), in cell-edge coordinates
    int from, to; // inclusive cell range along the edge
};

struct Hole { std::vector<Cell> cells; int minX = 0, minY = 0, maxX = 0, maxY = 0; };

struct Structure {
    int id = 0;
    std::vector<Cell> cells;
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    Shape shape = Shape::Complex;
    std::vector<EdgeRun> runs;
    std::vector<Cell> convex, concave;
    std::vector<Hole> holes;
    float score = 0;
    bool thin = false;
    int depthMax = 0;
    int width() const { return maxX - minX + 1; }
    int height() const { return maxY - minY + 1; }
};

struct Analysis {
    Grid grid;
    std::vector<uint8_t> depth;      // 0 empty, 1 outline layer, 2+ deeper
    std::vector<int> owner;          // structure id per cell, -1 none
    std::vector<Structure> structures;
    int depthAt(int x, int y) const { return grid.in(x, y) ? depth[size_t(y - grid.oy) * grid.w + (x - grid.ox)] : 0; }
    int ownerAt(int x, int y) const { return grid.in(x, y) ? owner[size_t(y - grid.oy) * grid.w + (x - grid.ox)] : -1; }
};

Analysis analyse(Grid g, int minCells = 4);

// ---------- Styles, categories, palettes ----------
enum class Role : uint8_t { Bar, Tile, Dot, Triangle, Glow, Panel, Circuit, Crystal, Plant, Rock, Spark, Gear, Count };
enum class Layer : uint8_t { Back, Mid, Front };

enum class Style : uint8_t { Modern, Minimalist, Glow, Cyberpunk, Futuristic, Industrial, Tech, Abstract, Geometric, Nature, Dark, Detailed, Clean, Space, Count };
const char* styleName(Style s);

enum class Cat : uint32_t { Structural = 1, Glow = 2, Interior = 4, Nature = 8, Tech = 16, Background = 32, Extra = 64, All = 127 };

struct Rgb { uint8_t r = 255, g = 255, b = 255; };
struct Palette { std::string name; std::array<Rgb, 5> c; }; // 0 primary,1 secondary,2 accent,3 glow,4 dark
enum class PaletteMode { Preset, Random, Complementary, Monochrome, Custom };
std::vector<Palette> presetPalettes();
Palette makePalette(PaletteMode m, uint64_t seed, int presetIndex, Palette const& custom);

struct Settings {
    Style style = Style::Cyberpunk;
    uint32_t categories = (uint32_t)Cat::All;
    int density = 55, detail = 50, glow = 55;
    int borderThickness = 2;   // 1-5, in quarter units
    int complexity = 50, interiorFill = 55, background = 35;
    int scalePct = 100;        // 50-200
    int layerDepth = 50;
    int spacing = 1;           // min gap (cells) between decor pieces of the same layer
    int symmetry = 0;          // 0-100
    uint64_t seed = 1;
    PaletteMode paletteMode = PaletteMode::Preset;
    int paletteIndex = 0;
    Palette custom;
    bool has(Cat c) const { return (categories & (uint32_t)c) != 0; }
    void reset() { *this = Settings(); }
    void randomise(Rng& r);
};

// ---------- Generator output ----------
struct Item {
    Role role = Role::Tile;
    Layer layer = Layer::Mid;
    float x = 0, y = 0;   // centre in cell units (cell k spans [k, k+1))
    float rot = 0;        // degrees clockwise (GD convention)
    float sx = 1, sy = 1; // length / thickness in cell units
    uint8_t slot = 0;     // palette slot
    int z = 0;
    bool flip = false;
    uint8_t group = 0;    // 0 normal, 1 border/glow run (exempt from overlap), 2 inside-only, 3 outside-only
};

enum class BorderDesign { Single, Double, Segmented, Offset, Bracketed };
enum class FillPattern { PanelGrid, Circuit, Stripes, DotMatrix, Layered, Hex, None };

struct VariationProfile {
    BorderDesign border = BorderDesign::Single;
    FillPattern fill = FillPattern::PanelGrid;
    bool cornerCaps = true;
    bool outerGlow = true;
    int complexityBias = 0;
    std::string label;
};

struct Variation {
    VariationProfile profile;
    std::vector<Item> items;
    Palette palette;
    uint64_t seed = 0;
    int objectCount() const { return int(items.size()); }
};

std::vector<Variation> generate(Analysis const& a, std::vector<int> const& structureIds, Settings const& s, int count, uint64_t salt = 0);
std::vector<Variation> generateFull(Analysis const& a, Settings const& s, int count, uint64_t salt = 0);
int optimise(std::vector<Item>& items, Analysis const& a, Settings const& s);

} // namespace ad
