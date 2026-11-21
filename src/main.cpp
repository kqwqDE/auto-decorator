#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include "Editor.hpp"
#include "UI.hpp"

using namespace geode::prelude;

// Small button in the editor so the UI is reachable without the hotkey.
class $modify(ADEditorUI, EditorUI) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (auto* menu = this->getChildByID("undo-menu")) {
            auto* spr = ButtonSprite::create("Deco", 40, true, "bigFont.fnt", "GJ_button_04.png", 26.f, .5f);
            auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ADEditorUI::onOpenAD));
            btn->setID("auto-decorator-button"_spr);
            menu->addChild(btn); menu->updateLayout();
        }
        return true;
    }
    void onOpenAD(CCObject*) { ad::ui::ADPopup::toggle(); }
};

$execute {
    // Configurable hotkeys (Geode keybind settings: rebind them in the mod's settings page).
    // Ctrl+Z / Ctrl+Shift+Z are NOT re-bound on purpose: applied batches are one native editor undo step,
    // so the editor's own shortcuts already undo/redo them. A second binding would undo twice.
    auto bind = [](char const* key, void (*fn)()) {
        KeybindSettingPressedEventV3(Mod::get(), key).listen([fn](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || !ad::ed::inEditor()) return false;
            fn(); return true;
        }).leak();
    };
    bind("key-toggle-ui", [] { ad::ui::ADPopup::toggle(); });
    bind("key-decorate-selection", [] { ad::ed::actionDecorateSelection(); });
    bind("key-full-auto", [] { ad::ed::actionFullAuto(); });
    bind("key-next-variation", [] { ad::ed::actionMoreVariations(); });
}
