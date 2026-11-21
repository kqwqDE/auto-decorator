#pragma once
#include <Geode/Geode.hpp>
#include <ctime>
#include <functional>

namespace ad::ui {

class ADPopup : public geode::Popup {
public:
    static ADPopup* create();
    static void toggle();          // F6
    ~ADPopup() override;

protected:
    static ADPopup* s_instance;
    cocos2d::CCMenu* m_tabMenu = nullptr;
    cocos2d::CCNode* m_page = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_info = nullptr;
    int m_tab = 0;
    std::vector<std::function<void(float)>> m_sl;
    std::vector<std::function<void(bool)>> m_tg;

    bool init();
    void update(float dt) override;
    void buildPage(int tab);
    cocos2d::CCLabelBMFont* label(std::string const& t, float x, float y, float sc, cocos2d::CCPoint anchor = {0.f, .5f});
    void button(std::string const& t, float x, float y, float w, int act, float sc);
    void slider(std::string const& t, float x, float y, int lo, int hi, int cur, std::function<void(int)> cb);
    void toggler(std::string const& t, float x, float y, bool cur, std::function<void(bool)> cb);
    void onTab(cocos2d::CCObject*);
    void onAct(cocos2d::CCObject*);
    void onSlider(cocos2d::CCObject*);
    void onToggle(cocos2d::CCObject*);
};

} // namespace ad::ui
