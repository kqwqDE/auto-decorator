#include "Editor.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

using namespace geode::prelude;

namespace ad::ed {

namespace {

// ----------------------------------------------------------------------------------------------
// Z layers. UNVERIFIED IN-GAME: tune these three constants if the layering looks wrong in the editor.
// Back = behind everything, Mid = the object's own default layer, Front = in front of blocks.
ZLayer zLayerFor(ad::Layer l, GameObject* o) {
    switch (l) {
        case ad::Layer::Back: return ZLayer::B4;
        case ad::Layer::Front: return ZLayer::B1;
        default: return o->m_defaultZLayer;
    }
}

State g_state;

// ----------------------------------------------------------------------------------------------
// Object catalog: resolves abstract roles (Bar, Glow, Crystal ...) to real decoration object IDs.
// IDs are discovered at runtime from the game's own object table, never hard coded:
//   1. frame name keywords give a candidate role,
//   2. the object is created once and must really be GameObjectType::Decoration (so no hazards/portals/triggers),
//   3. its real content size is stored so scaling can be computed exactly.
// Per-role overrides in the mod settings ("ids-<role>") win over auto detection.
struct Entry { int id = 0; float w = 30.f, h = 30.f; std::string frame; };

struct Catalog {
    std::array<std::vector<Entry>, size_t(ad::Role::Count)> e;
    bool built = false;
};
Catalog g_cat;

const char* kRoleKeys[] = {"bar", "tile", "dot", "triangle", "glow", "panel", "circuit", "crystal", "plant", "rock", "spark", "gear"};
const char* kRoleNames[] = {"Bar", "Tile", "Dot", "Triangle", "Glow", "Panel", "Circuit", "Crystal", "Plant", "Rock", "Spark", "Gear"};

std::optional<ad::Role> roleForName(std::string n) {
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    auto has = [&](char const* k) { return n.find(k) != std::string::npos; };
    if (has("glow") || has("gradient") || has("fade")) return ad::Role::Glow;
    if (has("crystal") || has("gem")) return ad::Role::Crystal;
    if (has("plant") || has("leaf") || has("vine") || has("grass") || has("bush") || has("flower")) return ad::Role::Plant;
    if (has("rock") || has("stone")) return ad::Role::Rock;
    if (has("spark") || has("star") || has("twinkle") || has("particle")) return ad::Role::Spark;
    if (has("gear") || has("cog")) return ad::Role::Gear;
    if (has("circuit") || has("chip") || has("wire") || has("pipe")) return ad::Role::Circuit;
    if (has("triangle") || has("slope") || has("corner")) return ad::Role::Triangle;
    if (has("circle") || has("dot") || has("ring")) return ad::Role::Dot;
    if (has("bar") || has("stripe") || has("beam") || has("rod") || has("line")) return ad::Role::Bar;
    if (has("panel") || has("plate") || has("frame")) return ad::Role::Panel;
    if (has("square") || has("pixel") || has("box")) return ad::Role::Tile;
    return std::nullopt;
}

bool measure(int key, Entry& out, bool requireDecoration = true) {
    auto* tb = ObjectToolbox::sharedState();
    char const* frame = tb->intKeyToFrame(key);
    if (!frame || !*frame) return false;
    if (!CCSpriteFrameCache::get()->spriteFrameByName(frame)) return false;
    auto* o = GameObject::createWithKey(key);
    if (!o) return false;
    if (requireDecoration && o->m_objectType != GameObjectType::Decoration) return false;
    if (typeinfo_cast<EffectGameObject*>(o)) return false;        // never use triggers
    auto sz = o->getContentSize();
    out.id = key; out.frame = frame;
    out.w = sz.width > 1.f ? sz.width : 30.f; out.h = sz.height > 1.f ? sz.height : 30.f;
    return true;
}

std::vector<int> parseIds(std::string const& s) {
    std::vector<int> r; std::stringstream ss(s); std::string t;
    while (std::getline(ss, t, ',')) { try { int v = std::stoi(t); if (v > 0) r.push_back(v); } catch (...) {} }
    return r;
}

void buildCatalog() {
    g_cat = Catalog();
    auto* mod = Mod::get();
    // 1) overrides
    for (int r = 0; r < int(ad::Role::Count); r++)
        for (int id : parseIds(mod->getSettingValue<std::string>(std::string("ids-") + kRoleKeys[r]))) {
            Entry en; if (measure(id, en, false)) g_cat.e[size_t(r)].push_back(en);
        }
    // 2) auto detection through frame names
    constexpr int kMaxKey = 5000, kPerRole = 24;
    for (int key = 1; key <= kMaxKey; key++) {
        char const* f = ObjectToolbox::sharedState()->intKeyToFrame(key);
        if (!f || !*f) continue;
        auto role = roleForName(f); if (!role) continue;
        auto& list = g_cat.e[size_t(*role)];
        if (int(list.size()) >= kPerRole) continue;
        Entry en; if (measure(key, en)) list.push_back(en);
    }
    // 3) shape based fallback for the two plain-rectangle roles
    auto& bars = g_cat.e[size_t(ad::Role::Bar)]; auto& tiles = g_cat.e[size_t(ad::Role::Tile)];
    if (bars.empty() || tiles.empty()) {
        for (int key = 1; key <= kMaxKey && (int(bars.size()) < 6 || int(tiles.size()) < 6); key++) {
            Entry en; if (!measure(key, en)) continue;
            float asp = std::max(en.w, en.h) / std::max(1.f, std::min(en.w, en.h));
            if (asp >= 3.f && bars.empty()) bars.push_back(en);
            else if (asp < 1.25f && tiles.size() < 6 && en.w >= 20.f) tiles.push_back(en);
        }
    }
    // 4) fallback chain so every role resolves to *something* real
    using R = ad::Role;
    std::pair<R, R> chain[] = {{R::Bar, R::Tile}, {R::Dot, R::Tile}, {R::Triangle, R::Tile}, {R::Glow, R::Tile}, {R::Panel, R::Tile}, {R::Circuit, R::Bar},
                               {R::Spark, R::Dot}, {R::Crystal, R::Triangle}, {R::Plant, R::Triangle}, {R::Rock, R::Tile}, {R::Gear, R::Dot}};
    for (auto [from, to] : chain) if (g_cat.e[size_t(from)].empty()) g_cat.e[size_t(from)] = g_cat.e[size_t(to)];
    g_cat.built = true;
}

Entry const* pick(ad::Role r, uint64_t key) {
    if (!g_cat.built) buildCatalog();
    auto& v = g_cat.e[size_t(r)]; if (v.empty()) return nullptr;
    uint64_t h = (key + uint64_t(r) * 0x9E3779B97F4A7C15ull) * 0xBF58476D1CE4E5B9ull; h ^= h >> 29;
    return &v[size_t(h % v.size())];
}

// ----------------------------------------------------------------------------------------------
// Palette -> colour channels
std::array<int, 5> channels() {
    int base = int(Mod::get()->getSettingValue<int64_t>("channel-base"));
    return {base, base + 1, base + 2, base + 3, base + 4};
}

void writePalette(LevelEditorLayer* lel, ad::Palette const& p) {
    if (!Mod::get()->getSettingValue<bool>("write-palette") || !lel->m_effectManager) return;
    auto ch = channels();
    for (int i = 0; i < 5; i++) {
        auto* act = lel->m_effectManager->getColorAction(ch[size_t(i)]);
        if (!act) continue;
        ccColor3B c = {p.c[size_t(i)].r, p.c[size_t(i)].g, p.c[size_t(i)].b};
        act->m_color = c; act->m_fromColor = c; act->m_toColor = c;
        act->m_blending = (i == 3);                // glow slot is additive
    }
}

// ----------------------------------------------------------------------------------------------
// Incremental object creation so the editor never freezes on big generations.
class Builder : public CCNode {
public:
    std::vector<ad::Item> items; ad::Palette palette; uint64_t seed = 0; size_t next = 0;
    std::vector<Ref<GameObject>> created; std::function<void(bool)> done; bool cancelled = false;

    static Builder* create(ad::Variation const& v, std::function<void(bool)> cb) {
        auto* b = new Builder(); b->init(); b->autorelease();
        b->items = v.items; b->palette = v.palette; b->seed = v.seed; b->done = std::move(cb);
        return b;
    }
    void start(LevelEditorLayer* lel) {
        lel->addChild(this); writePalette(lel, palette);
        this->schedule(schedule_selector(Builder::step), 0.f);
    }
    void step(float) {
        auto* lel = LevelEditorLayer::get();
        if (!lel || cancelled) { finish(false); return; }
        auto ch = channels(); size_t budget = 150;
        while (budget-- && next < items.size()) {
            auto const& it = items[next]; size_t idx = next++;
            uint64_t key = (it.role == ad::Role::Crystal || it.role == ad::Role::Plant || it.role == ad::Role::Rock || it.role == ad::Role::Spark) ? seed + idx % 3 : seed;
            auto const* en = pick(it.role, key); if (!en) continue;
            auto* o = lel->createObject(en->id, ccp(it.x * ad::kUnit, it.y * ad::kUnit), true);
            if (!o) continue;
            bool twoD = it.role != ad::Role::Bar && it.role != ad::Role::Circuit;
            float baseRot = 0.f, sx, sy;
            if (twoD) { sx = it.sx * ad::kUnit / en->w; sy = it.sy * ad::kUnit / en->h; }
            else {     // thin sprites: long side must run along the item's length
                bool wide = en->w >= en->h; baseRot = wide ? 0.f : 90.f;
                float lo = std::max(1.f, std::min(en->w, en->h)), hi = std::max(en->w, en->h);
                sx = it.sx * ad::kUnit / hi; sy = it.sy * ad::kUnit / lo;
            }
            o->setRotation(it.rot + baseRot);
            o->updateCustomScaleX(sx); o->updateCustomScaleY(sy);
            if (it.flip) o->setFlipX(true);
            int c = ch[it.slot % 5];
            if (o->m_baseColor) o->m_baseColor->m_colorID = c;
            if (o->m_detailColor) o->m_detailColor->m_colorID = c;
            o->m_zLayer = zLayerFor(it.layer, o);
            o->m_zOrder = it.z;
            created.push_back(o);
        }
        g_state.progress = items.empty() ? 1.f : float(next) / float(items.size());
        if (next >= items.size()) finish(true);
    }
    void finish(bool ok) {
        this->unschedule(schedule_selector(Builder::step));
        auto cb = std::move(done); auto self = Ref<Builder>(this);
        this->removeFromParentAndCleanup(true);
        if (cb) cb(ok);
    }
};

Ref<Builder> g_builder;

void removeObjects(std::vector<Ref<GameObject>>& objs) {
    auto* lel = LevelEditorLayer::get(); if (!lel) { objs.clear(); return; }
    for (auto& o : objs) if (o && lel->m_objects->containsObject(o)) lel->removeObject(o, true);
    objs.clear();
}

Ref<UndoObject> g_lastUndo;

} // namespace

State& state() { return g_state; }
bool inEditor() { return LevelEditorLayer::get() != nullptr && EditorUI::get() != nullptr; }
void rebuildCatalog() { buildCatalog(); }

std::string catalogReport() {
    if (!g_cat.built) buildCatalog();
    std::string s;
    for (int r = 0; r < int(ad::Role::Count); r++) {
        s += std::string(kRoleNames[r]) + ": ";
        auto& v = g_cat.e[size_t(r)];
        if (v.empty()) s += "none";
        for (size_t i = 0; i < v.size() && i < 4; i++) s += std::to_string(v[i].id) + (i + 1 < std::min<size_t>(4, v.size()) ? ", " : "");
        if (v.size() > 4) s += " (+" + std::to_string(v.size() - 4) + ")";
        s += "\n";
    }
    return s;
}

bool scan(bool selectionOnly, std::string& err) {
    auto* lel = LevelEditorLayer::get(); auto* ui = EditorUI::get();
    if (!lel || !ui) { err = "Open a level in the editor first."; return false; }
        auto isStructure = [](GameObject* o) {
        return o->m_objectType == GameObjectType::Solid || o->m_objectType == GameObjectType::Slope;
    };
    CCArray* pool = nullptr;
    if (selectionOnly) {
        pool = ui->getSelectedObjects();
        if (!pool || pool->count() == 0) { err = "Nothing selected. Select the blocks/outlines you want to decorate."; return false; }
    } else {
        pool = lel->m_objects;
    }
    std::vector<GameObject*> src, any;
    for (auto* o : CCArrayExt<GameObject*>(pool)) {
        if (typeinfo_cast<EffectGameObject*>(o)) continue;
        if (isStructure(o)) src.push_back(o);
        else if (o->m_objectType != GameObjectType::Hazard) any.push_back(o);
    }
    if (src.empty()) src = any;   // layouts made of decoration-type outlines
    // rasterise object rectangles into 30-unit cells
    std::vector<std::pair<int, int>> cells; int minX = 1 << 30, minY = 1 << 30, maxX = -(1 << 30), maxY = -(1 << 30);
    for (auto* o : src) {
        if (typeinfo_cast<EffectGameObject*>(o)) continue;
        auto r = o->getObjectRect();
        int x0 = int(std::floor((r.getMinX() + 1.f) / ad::kUnit)), x1 = int(std::floor((r.getMaxX() - 1.f) / ad::kUnit));
        int y0 = int(std::floor((r.getMinY() + 1.f) / ad::kUnit)), y1 = int(std::floor((r.getMaxY() - 1.f) / ad::kUnit));
        if (x1 - x0 > 64 || y1 - y0 > 64) continue;       // huge scaled blocks would explode the grid
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            cells.emplace_back(x, y); minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
    }
    if (cells.empty()) { err = "No solid blocks found to decorate."; return false; }
    if (int64_t(maxX - minX + 7) * int64_t(maxY - minY + 7) > 30'000'000) { err = "Level area is too large to analyse."; return false; }
    auto grid = ad::Grid::make(minX, minY, maxX, maxY, 4);
    for (auto& c : cells) grid.set(c.first, c.second);
    g_state.analysis = ad::analyse(std::move(grid), selectionOnly ? 1 : 4);
    g_state.targets.clear();
    for (auto& s : g_state.analysis.structures) g_state.targets.push_back(s.id);
    if (g_state.analysis.structures.empty()) { err = "No structures found (tiny scattered blocks are ignored)."; return false; }
    return true;
}

bool generate(bool full, int count, std::string& err) {
    auto& st = g_state;
    if (st.analysis.structures.empty()) { err = "Scan first."; return false; }
    if (!full && st.targets.empty()) { err = "No structures selected."; return false; }
    st.variations = full ? ad::generateFull(st.analysis, st.settings, count, st.salt) : ad::generate(st.analysis, st.targets, st.settings, count, st.salt);
    st.fullMode = full; st.selected = 0;
    size_t maxObj = size_t(Mod::get()->getSettingValue<int64_t>("max-objects"));
    for (auto& v : st.variations) if (v.items.size() > maxObj) v.items.resize(maxObj);   // hard safety cap
    return true;
}

void cancelPreview() {
    if (g_builder) { g_builder->cancelled = true; g_builder->finish(false); g_builder = nullptr; }
    removeObjects(g_state.preview);
    g_state.previewing = false; g_state.busy = false; g_state.progress = 0.f;
}

void showPreview(int idx) {
    auto& st = g_state; auto* lel = LevelEditorLayer::get();
    if (!lel || idx < 0 || idx >= int(st.variations.size())) return;
    cancelPreview(); st.selected = idx; st.busy = true; st.progress = 0.f;
    st.status = "Creating preview of variation " + std::to_string(idx + 1) + "...";
    auto* b = Builder::create(st.variations[size_t(idx)], [idx](bool ok) {
        auto& s = g_state; s.busy = false;
        if (ok && g_builder) { s.preview = std::move(g_builder->created); s.previewing = true; s.status = "Preview " + std::to_string(idx + 1) + ": " + std::to_string(s.preview.size()) + " objects. Apply or Cancel."; }
        else { if (g_builder) removeObjects(g_builder->created); s.previewing = false; s.status = "Preview cancelled."; }
        g_builder = nullptr;
    });
    g_builder = b; b->start(lel);
}

bool applyPreview() {
    auto& st = g_state; auto* lel = LevelEditorLayer::get(); auto* ui = EditorUI::get();
    if (!lel || !ui || st.busy || !st.previewing || st.preview.empty()) return false;
    auto* arr = CCArray::create();
    for (auto& o : st.preview) arr->addObject(o);
    // One undo step for the whole batch: editor Ctrl+Z removes everything again, Ctrl+Shift+Z restores it.
    auto* undo = UndoObject::createWithArray(arr, UndoCommand::Paste);
    lel->addToUndoList(undo, false); g_lastUndo = undo;
    ui->selectObjects(arr, true);
    st.history.push_back(std::move(st.preview)); st.preview.clear(); st.previewing = false;
    st.status = "Applied " + std::to_string(st.history.back().size()) + " objects.";
    return true;
}

bool removeLastBatch() {
    auto& st = g_state; auto* lel = LevelEditorLayer::get(); auto* ui = EditorUI::get();
    if (!lel || !ui || st.history.empty() || st.busy) return false;
    // Only safe through the editor's own undo, and only while our batch is still the newest entry.
    if (!g_lastUndo || lel->m_undoObjects->lastObject() != g_lastUndo) { st.status = "Newer edits exist - use editor undo (Ctrl+Z) step by step."; return false; }
    ui->undoLastAction(nullptr);
    st.history.pop_back(); g_lastUndo = nullptr; st.status = "Last decoration batch removed.";
    return true;
}

// ----------------------------------------------------------------------------------------------
void actionDecorateSelection() {
    if (g_state.busy) return;
    std::string err;
    if (!scan(true, err) || !generate(false, 4, err)) { g_state.status = err; Notification::create(err, NotificationIcon::Error)->show(); return; }
    showPreview(0);
}

void actionFullAuto() {
    if (g_state.busy) return;
    std::string err;
    if (!scan(false, err) || !generate(true, 4, err)) { g_state.status = err; Notification::create(err, NotificationIcon::Error)->show(); return; }
    showPreview(0);
}

void actionMoreVariations() {
    if (g_state.busy || g_state.analysis.structures.empty()) return;
    g_state.salt++; std::string err;
    if (!generate(g_state.fullMode, 4, err)) { g_state.status = err; return; }
    showPreview(0);
}

} // namespace ad::ed
