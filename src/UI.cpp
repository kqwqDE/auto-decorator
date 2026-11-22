#include "UI.hpp"
#include "Editor.hpp"
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace ad::ui {

namespace {

constexpr float W = 440.f, H = 290.f, CX0 = 124.f;           // popup size, content area x origin
char const* kTabs[] = {"Home", "Outline Decorator", "Smart Detection", "Full Auto Decorate", "Interior Fill", "Decoration Library", "AI Variations", "Styles & Presets", "Advanced Settings"};
char const* kCats[] = {"Structural", "Glow", "Interior", "Nature", "Tech", "Background", "Extra Details"};
char const* kPalModes[] = {"Preset palette", "Random palette", "Complementary", "Monochrome"};
ad::Cat kCatBits[] = {ad::Cat::Structural, ad::Cat::Glow, ad::Cat::Interior, ad::Cat::Nature, ad::Cat::Tech, ad::Cat::Background, ad::Cat::Extra};

enum Act {
    ActFullAuto = 1, ActDecorSel, ActAnalyseSel, ActGenSel, ActScanLevel, ActGenChecked, ActMore, ActPreview, ActApply, ActCancel, ActRemove,
    ActStylePrev, ActStyleNext, ActPalModePrev, ActPalModeNext, ActPalPrev, ActPalNext, ActPalNew, ActSlotNext, ActSave, ActLoad,
    ActRandomise, ActReset, ActSeedMinus, ActSeedPlus, ActCatalog, ActListPrev, ActListNext,
    ActVar1 = 100, ActVar2, ActVar3, ActVar4
};

ad::Settings g_defaults;
int g_slot = 1, g_listPage = 0;

std::filesystem::path presetPath() { return Mod::get()->getSaveDir() / ("preset" + std::to_string(g_slot) + ".json"); }

void savePreset() {
    auto& s = ed::state().settings;
    matjson::Value j = matjson::makeObject({
        {"style", int(s.style)}, {"categories", int(s.categories)}, {"density", s.density}, {"detail", s.detail}, {"glow", s.glow},
        {"borderThickness", s.borderThickness}, {"complexity", s.complexity}, {"interiorFill", s.interiorFill}, {"background", s.background},
        {"scalePct", s.scalePct}, {"layerDepth", s.layerDepth}, {"spacing", s.spacing}, {"symmetry", s.symmetry}, {"seed", int(s.seed & 0x7FFFFFFF)},
        {"paletteMode", int(s.paletteMode)}, {"paletteIndex", s.paletteIndex}});
    auto r = utils::file::writeString(presetPath(), j.dump());
    ed::state().status = r ? "Preset saved to slot " + std::to_string(g_slot) : "Could not save preset.";
}

void loadPreset() {
    auto txt = utils::file::readString(presetPath());
    if (!txt) { ed::state().status = "Preset slot " + std::to_string(g_slot) + " is empty."; return; }
    auto parsed = matjson::parse(txt.unwrap());
    if (!parsed) { ed::state().status = "Preset file is corrupt."; return; }
    auto j = parsed.unwrap(); auto& s = ed::state().settings;
    auto I = [&](char const* k, int d) { return static_cast<int>(j[k].asInt().unwrapOr(d)); };
    s.style = ad::Style(std::clamp(I("style", int(s.style)), 0, int(ad::Style::Count) - 1));
    s.categories = uint32_t(I("categories", int(s.categories))); s.density = I("density", s.density); s.detail = I("detail", s.detail); s.glow = I("glow", s.glow);
    s.borderThickness = I("borderThickness", s.borderThickness); s.complexity = I("complexity", s.complexity); s.interiorFill = I("interiorFill", s.interiorFill);
    s.background = I("background", s.background); s.scalePct = I("scalePct", s.scalePct); s.layerDepth = I("layerDepth", s.layerDepth);
    s.spacing = I("spacing", s.spacing); s.symmetry = I("symmetry", s.symmetry); s.seed = uint64_t(I("seed", int(s.seed)));
    s.paletteMode = ad::PaletteMode(std::clamp(I("paletteMode", int(s.paletteMode)), 0, 3)); s.paletteIndex = I("paletteIndex", s.paletteIndex);
    ed::state().status = "Preset loaded from slot " + std::to_string(g_slot);
}

} // namespace

ADPopup* ADPopup::s_instance = nullptr;

ADPopup* ADPopup::create() {
    auto* r = new ADPopup();
    if (r->init()) { r->autorelease(); return r; }
    delete r; return nullptr;
}

ADPopup::~ADPopup() { if (s_instance == this) s_instance = nullptr; }

void ADPopup::toggle() {
    if (!ed::inEditor()) return;
    if (s_instance) { s_instance->onClose(nullptr); return; }
    s_instance = create(); if (s_instance) s_instance->show();
}

bool ADPopup::init() {
    if (!Popup::init(W, H)) return false;
    this->setTitle("Auto Decorator");
    m_tabMenu = CCMenu::create(); m_tabMenu->setPosition(0, 0); m_mainLayer->addChild(m_tabMenu);
    for (int i = 0; i < 9; i++) {
        auto* spr = ButtonSprite::create(kTabs[i], 110, true, "bigFont.fnt", "GJ_button_04.png", 22.f, .38f);
        auto* b = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ADPopup::onTab));
        b->setTag(i); b->setPosition(62.f, H - 52.f - float(i) * 25.f); m_tabMenu->addChild(b);
    }
    this->scheduleUpdate();
    this->buildPage(0);
    return true;
}

void ADPopup::onTab(CCObject* s) { this->buildPage(static_cast<CCNode*>(s)->getTag()); }

void ADPopup::update(float) {
    auto& st = ed::state();
    if (m_status) {
        std::string t = st.status;
        if (st.busy) t += " (" + std::to_string(int(st.progress * 100.f)) + "%)";
        m_status->setString(t.c_str());
    }
}

// ---------------------------------------------------------------- small widget helpers
CCLabelBMFont* ADPopup::label(std::string const& t, float x, float y, float sc, CCPoint anchor) {
    auto* l = CCLabelBMFont::create(t.c_str(), "bigFont.fnt"); l->setScale(sc); l->setAnchorPoint(anchor); l->setPosition(x, y);
    m_page->addChild(l); return l;
}

void ADPopup::button(std::string const& t, float x, float y, float w, int act, float sc) {
    auto* spr = ButtonSprite::create(t.c_str(), int(w), true, "bigFont.fnt", "GJ_button_01.png", 26.f, sc);
    auto* b = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ADPopup::onAct));
    b->setTag(act); b->setPosition(x, y); m_menu->addChild(b);
}

void ADPopup::slider(std::string const& t, float x, float y, int lo, int hi, int cur, std::function<void(int)> cb) {
    this->label(t, x, y + 11.f, .32f);
    auto* sl = Slider::create(this, menu_selector(ADPopup::onSlider), .45f);
    sl->setPosition(x + 62.f, y - 4.f); sl->setValue(float(cur - lo) / float(hi - lo));
    sl->m_touchLogic->m_thumb->setTag(int(m_sl.size())); m_page->addChild(sl);
    auto* val = this->label(std::to_string(cur), x + 128.f, y - 4.f, .32f, ccp(1.f, .5f));
    m_sl.push_back([lo, hi, cb, val](float v) { int n = lo + int(std::lround(v * float(hi - lo))); val->setString(std::to_string(n).c_str()); cb(n); });
}

void ADPopup::toggler(std::string const& t, float x, float y, bool cur, std::function<void(bool)> cb) {
    auto* tg = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(ADPopup::onToggle), .5f);
    tg->setPosition(x, y); tg->toggle(cur); tg->setTag(int(m_tg.size())); m_menu->addChild(tg); m_tg.push_back(std::move(cb));
    this->label(t, x + 14.f, y, .34f);
}

void ADPopup::onSlider(CCObject* s) { auto* th = static_cast<SliderThumb*>(s); size_t i = size_t(th->getTag()); if (i < m_sl.size()) m_sl[i](th->getValue()); }
void ADPopup::onToggle(CCObject* s) { auto* t = static_cast<CCMenuItemToggler*>(s); size_t i = size_t(t->getTag()); if (i < m_tg.size()) m_tg[i](!t->isToggled()); }

// ---------------------------------------------------------------- pages
void ADPopup::buildPage(int tab) {
    if (m_page) m_page->removeFromParentAndCleanup(true);
    m_sl.clear(); m_tg.clear(); m_status = nullptr; m_info = nullptr; m_tab = tab;
    m_page = CCNode::create(); m_page->setPosition(0, 0); m_mainLayer->addChild(m_page);
    m_menu = CCMenu::create(); m_menu->setPosition(0, 0); m_page->addChild(m_menu, 5);
    auto& st = ed::state(); auto& S = st.settings;
    this->label(kTabs[tab], CX0, H - 40.f, .55f);
    float top = H - 70.f;
    auto statusLine = [&]() { m_status = this->label("", CX0, 18.f, .3f); };

    switch (tab) {
    case 0: { // Home
        this->label(std::string("Style: ") + ad::styleName(S.style), CX0, top, .42f);
        this->button("<", CX0 + 190.f, top, 26.f, ActStylePrev, .5f); this->button(">", CX0 + 236.f, top, 26.f, ActStyleNext, .5f);
        this->button("FULL AUTO DECORATE", CX0 + 150.f, top - 55.f, 290.f, ActFullAuto, .85f);
        this->button("Decorate selected outlines", CX0 + 150.f, top - 100.f, 220.f, ActDecorSel, .55f);
        this->label("Preview opens as real objects. Apply = one editor undo step.", CX0, top - 135.f, .28f);
        this->label("Hotkeys: F6 UI  F7 selection  F8 full  F9 more variations", CX0, top - 150.f, .28f);
        statusLine(); break; }
    case 1: { // Outline Decorator
        this->label("1. Select outlines/blocks in the editor.", CX0, top, .33f);
        this->button("Analyse selection", CX0 + 80.f, top - 28.f, 150.f, ActAnalyseSel, .5f);
        this->button("Generate 4 variations", CX0 + 235.f, top - 28.f, 150.f, ActGenSel, .5f);
        std::string info;
        for (size_t i = 0; i < st.analysis.structures.size() && i < 7; i++) { auto& s = st.analysis.structures[i];
            info += "#" + std::to_string(s.id) + "  " + ad::shapeName(s.shape) + "  " + std::to_string(s.width()) + "x" + std::to_string(s.height()) + "  holes " + std::to_string(s.holes.size()) + "\n"; }
        if (info.empty()) info = "No analysis yet.";
        m_info = this->label(info, CX0, top - 60.f, .3f, ccp(0.f, 1.f)); statusLine(); break; }
    case 2: { // Smart Detection
        this->button("Scan level", CX0 + 50.f, top, 90.f, ActScanLevel, .5f);
        this->button("Decorate checked", CX0 + 180.f, top, 130.f, ActGenChecked, .5f);
        auto& A = st.analysis; std::vector<int> order(A.structures.size()); for (size_t i = 0; i < order.size(); i++) order[i] = int(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return A.structures[size_t(a)].score > A.structures[size_t(b)].score; });
        int per = 6, pages = std::max(1, (int(order.size()) + per - 1) / per); g_listPage = std::clamp(g_listPage, 0, pages - 1);
        for (int k = 0; k < per; k++) { int oi = g_listPage * per + k; if (oi >= int(order.size())) break;
            auto& s = A.structures[size_t(order[size_t(oi)])]; int id = s.id;
            bool on = std::find(st.targets.begin(), st.targets.end(), id) != st.targets.end();
            this->toggler("#" + std::to_string(id) + " " + ad::shapeName(s.shape) + " " + std::to_string(s.width()) + "x" + std::to_string(s.height()) + " score " + std::to_string(int(s.score)),
                CX0 + 10.f, top - 32.f - float(k) * 24.f, on, [id](bool v) { auto& t = ed::state().targets; t.erase(std::remove(t.begin(), t.end(), id), t.end()); if (v) t.push_back(id); }); }
        this->button("<", CX0 + 230.f, 40.f, 24.f, ActListPrev, .45f); this->button(">", CX0 + 262.f, 40.f, 24.f, ActListNext, .45f);
        this->label("Page " + std::to_string(g_listPage + 1) + "/" + std::to_string(pages), CX0 + 160.f, 40.f, .3f);
        statusLine(); break; }
    case 3: { // Full Auto
        this->button("FULL AUTO DECORATE", CX0 + 150.f, top - 20.f, 290.f, ActFullAuto, .85f);
        this->button("Decorate selection only", CX0 + 150.f, top - 65.f, 200.f, ActDecorSel, .55f);
        this->label("Pipeline: analyse > structures > interiors > borders > glow >", CX0, top - 105.f, .28f);
        this->label("background > palette > details > optimise. Only decoration", CX0, top - 118.f, .28f);
        this->label("objects are created; gameplay objects are never modified.", CX0, top - 131.f, .28f);
        statusLine(); break; }
    case 4: { // Interior Fill
        this->slider("Interior fill", CX0, top, 0, 100, S.interiorFill, [&S](int v) { S.interiorFill = v; });
        this->slider("Pattern complexity", CX0, top - 36.f, 0, 100, S.complexity, [&S](int v) { S.complexity = v; });
        this->slider("Density", CX0, top - 72.f, 0, 100, S.density, [&S](int v) { S.density = v; });
        this->toggler("Interior only (no borders/glow)", CX0 + 8.f, top - 108.f, S.categories == uint32_t(ad::Cat::Interior), [&S](bool v) { S.categories = v ? uint32_t(ad::Cat::Interior) : uint32_t(ad::Cat::All); });
        this->label("Patterns per variation: panels, circuits, stripes, dot matrix,", CX0, top - 135.f, .28f);
        this->label("layered frames, hex lattice. Circles/triangles get rings/triangles.", CX0, top - 148.f, .28f);
        statusLine(); break; }
    case 5: { // Library
        for (int i = 0; i < 7; i++) { auto bit = kCatBits[i];
            this->toggler(kCats[i], CX0 + 10.f + float(i / 4) * 90.f, top - float(i % 4) * 24.f, S.has(bit), [&S, bit](bool v) { if (v) S.categories |= uint32_t(bit); else S.categories &= ~uint32_t(bit); }); }
        this->button("Rebuild catalog", CX0 + 240.f, top - 10.f, 110.f, ActCatalog, .45f);
        this->label("Resolved object IDs per role:", CX0, top - 100.f, .3f);
        m_info = this->label(ed::catalogReport(), CX0, top - 112.f, .22f, ccp(0.f, 1.f)); statusLine(); break; }
    case 6: { // Variations
        for (int i = 0; i < 4; i++) {
            std::string t = "Variation " + std::to_string(i + 1);
            if (i < int(st.variations.size())) t += " (" + std::to_string(st.variations[size_t(i)].objectCount()) + ")";
            this->button(t, CX0 + 60.f + float(i % 2) * 125.f, top - float(i / 2) * 30.f, 115.f, ActVar1 + i, .42f); }
        if (st.selected < int(st.variations.size())) this->label(st.variations[size_t(st.selected)].profile.label, CX0, top - 68.f, .3f);
        this->button("Preview", CX0 + 40.f, top - 100.f, 70.f, ActPreview, .42f); this->button("Apply Selected", CX0 + 125.f, top - 100.f, 90.f, ActApply, .42f);
        this->button("Generate More", CX0 + 215.f, top - 100.f, 90.f, ActMore, .42f); this->button("Cancel", CX0 + 285.f, top - 100.f, 60.f, ActCancel, .42f);
        this->button("Remove last batch", CX0 + 80.f, top - 135.f, 130.f, ActRemove, .42f);
        statusLine(); break; }
    case 7: { // Styles & presets
        this->label(std::string("Style: ") + ad::styleName(S.style), CX0, top, .42f);
        this->button("<", CX0 + 190.f, top, 26.f, ActStylePrev, .5f); this->button(">", CX0 + 236.f, top, 26.f, ActStyleNext, .5f);
        this->label(std::string("Colours: ") + kPalModes[std::min(3, int(S.paletteMode))], CX0, top - 30.f, .34f);
        this->button("<", CX0 + 190.f, top - 30.f, 26.f, ActPalModePrev, .5f); this->button(">", CX0 + 236.f, top - 30.f, 26.f, ActPalModeNext, .5f);
        auto pals = ad::presetPalettes();
        this->label(std::string("Preset: ") + pals[size_t(S.paletteIndex) % pals.size()].name, CX0, top - 58.f, .34f);
        this->button("<", CX0 + 190.f, top - 58.f, 26.f, ActPalPrev, .5f); this->button(">", CX0 + 236.f, top - 58.f, 26.f, ActPalNext, .5f);
        this->button("New random palette", CX0 + 80.f, top - 88.f, 140.f, ActPalNew, .42f);
        this->label("Preset slot " + std::to_string(g_slot), CX0, top - 120.f, .34f);
        this->button("Slot", CX0 + 120.f, top - 120.f, 40.f, ActSlotNext, .42f); this->button("Save", CX0 + 170.f, top - 120.f, 45.f, ActSave, .42f); this->button("Load", CX0 + 236.f, top - 120.f, 45.f, ActLoad, .42f);
        statusLine(); break; }
    case 8: { // Advanced
        struct Row { char const* n; int lo, hi; int* v; };
        Row rows[] = {{"Density", 0, 100, &S.density}, {"Detail", 0, 100, &S.detail}, {"Glow", 0, 100, &S.glow}, {"Border thickness", 1, 5, &S.borderThickness},
                      {"Pattern complexity", 0, 100, &S.complexity}, {"Interior fill", 0, 100, &S.interiorFill}, {"Background", 0, 100, &S.background},
                      {"Scale %", 50, 200, &S.scalePct}, {"Layer depth", 0, 100, &S.layerDepth}, {"Object spacing", 0, 4, &S.spacing}, {"Symmetry", 0, 100, &S.symmetry}};
        for (int i = 0; i < 11; i++) { int* p = rows[i].v; this->slider(rows[i].n, CX0 + float(i / 6) * 160.f, top + 4.f - float(i % 6) * 33.f, rows[i].lo, rows[i].hi, *p, [p](int v) { *p = v; }); }
        this->label("Seed " + std::to_string(S.seed), CX0 + 165.f, top - 175.f, .34f);
        this->button("-", CX0 + 255.f, top - 175.f, 22.f, ActSeedMinus, .45f); this->button("+", CX0 + 282.f, top - 175.f, 22.f, ActSeedPlus, .45f);
        this->button("Randomize", CX0 + 40.f, 38.f, 76.f, ActRandomise, .42f); this->button("Reset", CX0 + 105.f, 38.f, 50.f, ActReset, .42f);
        statusLine(); break; }
    }
}

// ---------------------------------------------------------------- actions
void ADPopup::onAct(CCObject* sender) {
    int act = static_cast<CCNode*>(sender)->getTag(); auto& st = ed::state(); auto& S = st.settings; std::string err; bool rebuild = true;
    int nStyles = int(ad::Style::Count);
    switch (act) {
        case ActFullAuto: ed::actionFullAuto(); rebuild = false; break;
        case ActDecorSel: ed::actionDecorateSelection(); rebuild = false; break;
        case ActAnalyseSel: if (!ed::scan(true, err)) st.status = err; else st.status = "Found " + std::to_string(st.analysis.structures.size()) + " structure(s)."; break;
        case ActGenSel: if (!ed::scan(true, err) || !ed::generate(false, 4, err)) st.status = err; else { ed::showPreview(0); this->buildPage(6); return; } break;
        case ActScanLevel: g_listPage = 0; if (!ed::scan(false, err)) st.status = err; else st.status = "Level scan: " + std::to_string(st.analysis.structures.size()) + " structure(s)."; break;
        case ActGenChecked: if (st.targets.empty()) st.status = "Check at least one structure."; else if (!ed::generate(false, 4, err)) st.status = err; else { ed::showPreview(0); this->buildPage(6); return; } break;
        case ActMore: ed::actionMoreVariations(); break;
        case ActPreview: ed::showPreview(st.selected); rebuild = false; break;
        case ActApply: if (!ed::applyPreview()) st.status = "Nothing to apply - preview a variation first."; rebuild = false; break;
        case ActCancel: ed::cancelPreview(); st.status = "Preview removed."; rebuild = false; break;
        case ActRemove: if (!ed::removeLastBatch() && st.status.empty()) st.status = "Nothing to remove."; rebuild = false; break;
        case ActStylePrev: S.style = ad::Style((int(S.style) + nStyles - 1) % nStyles); break;
        case ActStyleNext: S.style = ad::Style((int(S.style) + 1) % nStyles); break;
        case ActPalModePrev: S.paletteMode = ad::PaletteMode((int(S.paletteMode) + 3) % 4); break;
        case ActPalModeNext: S.paletteMode = ad::PaletteMode((int(S.paletteMode) + 1) % 4); break;
        case ActPalPrev: S.paletteIndex = (S.paletteIndex + 7) % 8; S.paletteMode = ad::PaletteMode::Preset; break;
        case ActPalNext: S.paletteIndex = (S.paletteIndex + 1) % 8; S.paletteMode = ad::PaletteMode::Preset; break;
        case ActPalNew: st.salt++; if (S.paletteMode == ad::PaletteMode::Preset) S.paletteMode = ad::PaletteMode::Random; break;
        case ActSlotNext: g_slot = g_slot % 3 + 1; break;
        case ActSave: savePreset(); break;
        case ActLoad: loadPreset(); break;
        case ActRandomise: { ad::Rng r(uint64_t(std::time(nullptr))); S.randomise(r); st.status = "Settings randomised."; break; }
        case ActReset: S.reset(); st.status = "Settings reset."; break;
        case ActSeedMinus: S.seed = S.seed > 0 ? S.seed - 1 : 0; break;
        case ActSeedPlus: S.seed++; break;
        case ActCatalog: ed::rebuildCatalog(); break;
        case ActListPrev: g_listPage--; break;
        case ActListNext: g_listPage++; break;
        default:
            if (act >= ActVar1 && act <= ActVar4) { if (act - ActVar1 < int(st.variations.size())) ed::showPreview(act - ActVar1); else st.status = "Generate variations first."; }
            break;
    }
    if (rebuild) this->buildPage(m_tab);
}

} // namespace ad::ui
