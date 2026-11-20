#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <vector>

using namespace geode::prelude;

struct GMState {
    bool noclip = false;
    float speed = 1.0f;
    int targetFPS = 60;
    bool fpsBypass = false;

    bool showHitboxes = false;
    bool hitboxTrail = false;
    int maxTrailLength = 60;
    ccColor3B trailColor = { 0, 255, 128 };

    bool smoothOptimize = false;

    bool showLabels = false;
    float hudOpacity = 0.85f;

    int totalFrames = 0;
    int noclipHitFrames = 0;
    int noclipDeaths = 0;
    bool wasCollidingThisTick = false;
    std::deque<double> clickTimestamps;
    std::deque<CCRect> playerTrailHistory;
};

static GMState g_state;

static void applyFPSAndSpeed() {
    auto dir = CCDirector::sharedDirector();
    if (g_state.fpsBypass) {
        int clampedFPS = std::clamp(g_state.targetFPS, 30, 240);
        CCApplication::sharedApplication()->setAnimationInterval(1.0 / static_cast<double>(clampedFPS));
    } else {
        CCApplication::sharedApplication()->setAnimationInterval(1.0 / 60.0);
    }
    dir->getScheduler()->setTimeScale(g_state.speed);
}

static void syncFromGeodeSettings() {
    auto mod = Mod::get();
    g_state.noclip = mod->getSettingValue<bool>("noclip");
    g_state.speed = static_cast<float>(mod->getSettingValue<double>("speed"));
    g_state.fpsBypass = mod->getSettingValue<bool>("fps-bypass");
    g_state.targetFPS = static_cast<int>(mod->getSettingValue<int64_t>("target-fps"));
    g_state.showHitboxes = mod->getSettingValue<bool>("show-hitboxes");
    g_state.hitboxTrail = mod->getSettingValue<bool>("hitbox-trail");
    g_state.maxTrailLength = static_cast<int>(mod->getSettingValue<int64_t>("trail-length"));
    g_state.trailColor = mod->getSettingValue<ccColor3B>("trail-color");
    g_state.smoothOptimize = mod->getSettingValue<bool>("smooth-optimize");
    g_state.showLabels = mod->getSettingValue<bool>("show-labels");
    g_state.hudOpacity = static_cast<float>(mod->getSettingValue<double>("hud-opacity"));
    applyFPSAndSpeed();
}

static void saveToGeodeSettings() {
    auto mod = Mod::get();
    mod->setSettingValue<bool>("noclip", g_state.noclip);
    mod->setSettingValue<double>("speed", g_state.speed);
    mod->setSettingValue<bool>("fps-bypass", g_state.fpsBypass);
    mod->setSettingValue<int64_t>("target-fps", g_state.targetFPS);
    mod->setSettingValue<bool>("show-hitboxes", g_state.showHitboxes);
    mod->setSettingValue<bool>("hitbox-trail", g_state.hitboxTrail);
    mod->setSettingValue<int64_t>("trail-length", g_state.maxTrailLength);
    mod->setSettingValue<ccColor3B>("trail-color", g_state.trailColor);
    mod->setSettingValue<bool>("smooth-optimize", g_state.smoothOptimize);
    mod->setSettingValue<bool>("show-labels", g_state.showLabels);
    mod->setSettingValue<double>("hud-opacity", g_state.hudOpacity);
}

$on_mod(Loaded) {
    syncFromGeodeSettings();
    listenForAllSettingChanges([](std::string_view, std::shared_ptr<SettingV3>) {
        syncFromGeodeSettings();
    });
}

static bool isCheatingActive() {
    return g_state.noclip ||
           std::abs(g_state.speed - 1.0f) > 0.01f ||
           g_state.showHitboxes ||
           g_state.hitboxTrail;
}

class GMMenuPopup : public geode::Popup<> {
protected:
    CCLabelBMFont* m_speedLabel = nullptr;
    CCLabelBMFont* m_fpsLabel = nullptr;
    CCLabelBMFont* m_trailLenLabel = nullptr;
    CCLabelBMFont* m_opacityLabel = nullptr;

    bool setup() override {
        syncFromGeodeSettings();
        this->setTitle("Geometry Dash Menu (GM)");
        auto winSize = m_mainLayer->getContentSize();

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(menu);

        float leftX = 32.f;
        float rightX = winSize.width - 42.f;
        float startY = winSize.height - 48.f;
        float rowGap = 27.f;
        int row = 0;

        auto addToggleRow = [&](const char* title, bool* targetVar, SEL_MenuHandler cb) {
            float y = startY - (row * rowGap);
            auto lbl = CCLabelBMFont::create(title, "bigFont.fnt");
            lbl->setScale(0.40f);
            lbl->setAnchorPoint({ 0.f, 0.5f });
            lbl->setPosition({ leftX, y });
            m_mainLayer->addChild(lbl);

            auto toggler = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.62f);
            toggler->toggle(*targetVar);
            toggler->setPosition({ rightX, y });
            menu->addChild(toggler);
            row++;
        };

        auto addStepperRow = [&](const char* title, float titleScale, CCLabelBMFont** outValLabel,
                               const std::string& initialText, SEL_MenuHandler downCb, SEL_MenuHandler upCb) {
            float y = startY - (row * rowGap);
            auto lbl = CCLabelBMFont::create(title, "bigFont.fnt");
            lbl->setScale(titleScale);
            lbl->setAnchorPoint({ 0.f, 0.5f });
            lbl->setPosition({ leftX, y });
            m_mainLayer->addChild(lbl);

            auto valLbl = CCLabelBMFont::create(initialText.c_str(), "goldFont.fnt");
            valLbl->setScale(0.42f);
            valLbl->setPosition({ rightX - 44.f, y });
            m_mainLayer->addChild(valLbl);
            *outValLabel = valLbl;

            auto minusBtn = CCMenuItemSpriteExtra::create(
                CCLabelBMFont::create("-", "bigFont.fnt"), this, downCb
            );
            minusBtn->setScale(0.62f);
            minusBtn->setPosition({ rightX - 84.f, y });
            menu->addChild(minusBtn);

            auto plusBtn = CCMenuItemSpriteExtra::create(
                CCLabelBMFont::create("+", "bigFont.fnt"), this, upCb
            );
            plusBtn->setScale(0.62f);
            plusBtn->setPosition({ rightX - 4.f, y });
            menu->addChild(plusBtn);
            row++;
        };

        addToggleRow("Noclip", &g_state.noclip, menu_selector(GMMenuPopup::onToggleNoclip));
        addStepperRow("Speed:", 0.40f, &m_speedLabel, fmt::format("{:.2f}x", g_state.speed),
                      menu_selector(GMMenuPopup::onSpeedDown), menu_selector(GMMenuPopup::onSpeedUp));
        addStepperRow("FPS (Max 240):", 0.40f, &m_fpsLabel, fmt::format("{} FPS", g_state.targetFPS),
                      menu_selector(GMMenuPopup::onFPSDown), menu_selector(GMMenuPopup::onFPSUp));
        addToggleRow("Show Hitboxes", &g_state.showHitboxes, menu_selector(GMMenuPopup::onToggleHitboxes));
        addToggleRow("Hitbox Trail", &g_state.hitboxTrail, menu_selector(GMMenuPopup::onToggleHitboxTrail));
        addStepperRow("Trail Max 240:", 0.37f, &m_trailLenLabel, fmt::format("{}", g_state.maxTrailLength),
                      menu_selector(GMMenuPopup::onTrailDown), menu_selector(GMMenuPopup::onTrailUp));

        {
            float y = startY - (row * rowGap);
            auto lbl = CCLabelBMFont::create("Trail Color:", "bigFont.fnt");
            lbl->setScale(0.40f);
            lbl->setAnchorPoint({ 0.f, 0.5f });
            lbl->setPosition({ leftX, y });
            m_mainLayer->addChild(lbl);

            auto colorBtnSprite = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
            colorBtnSprite->setScale(0.62f);
            colorBtnSprite->setColor(g_state.trailColor);

            auto colorBtn = CCMenuItemSpriteExtra::create(
                colorBtnSprite, this, menu_selector(GMMenuPopup::onCycleTrailColor)
            );
            colorBtn->setPosition({ rightX, y });
            menu->addChild(colorBtn);
            row++;
        }

        addToggleRow("Smooth & Optimize", &g_state.smoothOptimize, menu_selector(GMMenuPopup::onToggleOptimize));
        addToggleRow("HUD Labels & Dot", &g_state.showLabels, menu_selector(GMMenuPopup::onToggleLabels));
        addStepperRow("HUD Opacity:", 0.40f, &m_opacityLabel,
                      fmt::format("{}%", static_cast<int>(g_state.hudOpacity * 100)),
                      menu_selector(GMMenuPopup::onOpacityDown), menu_selector(GMMenuPopup::onOpacityUp));

        return true;
    }

    void onToggleNoclip(CCObject*) { g_state.noclip = !g_state.noclip; saveToGeodeSettings(); }
    void onToggleHitboxes(CCObject*) { g_state.showHitboxes = !g_state.showHitboxes; saveToGeodeSettings(); }
    void onToggleHitboxTrail(CCObject*) {
        g_state.hitboxTrail = !g_state.hitboxTrail;
        if (!g_state.hitboxTrail) g_state.playerTrailHistory.clear();
        saveToGeodeSettings();
    }
    void onToggleOptimize(CCObject*) {
        g_state.smoothOptimize = !g_state.smoothOptimize;
        if (auto gm = GameManager::sharedState()) {
            gm->setGameVariable("0066", g_state.smoothOptimize);
            gm->setGameVariable("0072", g_state.smoothOptimize);
            gm->setGameVariable("0082", g_state.smoothOptimize);
        }
        saveToGeodeSettings();
    }
    void onToggleLabels(CCObject*) { g_state.showLabels = !g_state.showLabels; saveToGeodeSettings(); }

    void onSpeedDown(CCObject*) {
        g_state.speed = std::max(0.10f, g_state.speed - 0.10f);
        m_speedLabel->setString(fmt::format("{:.2f}x", g_state.speed).c_str());
        applyFPSAndSpeed();
        saveToGeodeSettings();
    }
    void onSpeedUp(CCObject*) {
        g_state.speed = std::min(3.00f, g_state.speed + 0.10f);
        m_speedLabel->setString(fmt::format("{:.2f}x", g_state.speed).c_str());
        applyFPSAndSpeed();
        saveToGeodeSettings();
    }

    void onFPSDown(CCObject*) {
        g_state.fpsBypass = true;
        g_state.targetFPS = std::max(30, g_state.targetFPS - 30);
        m_fpsLabel->setString(fmt::format("{} FPS", g_state.targetFPS).c_str());
        applyFPSAndSpeed();
        saveToGeodeSettings();
    }
    void onFPSUp(CCObject*) {
        g_state.fpsBypass = true;
        g_state.targetFPS = std::min(240, g_state.targetFPS + 30);
        m_fpsLabel->setString(fmt::format("{} FPS", g_state.targetFPS).c_str());
        applyFPSAndSpeed();
        saveToGeodeSettings();
    }

    void onTrailDown(CCObject*) {
        g_state.maxTrailLength = std::max(10, g_state.maxTrailLength - 20);
        m_trailLenLabel->setString(fmt::format("{}", g_state.maxTrailLength).c_str());
        saveToGeodeSettings();
    }
    void onTrailUp(CCObject*) {
        g_state.maxTrailLength = std::min(240, g_state.maxTrailLength + 20);
        m_trailLenLabel->setString(fmt::format("{}", g_state.maxTrailLength).c_str());
        saveToGeodeSettings();
    }

    void onCycleTrailColor(CCObject* sender) {
        static const std::vector<ccColor3B> palette = {
            { 0, 255, 128 },
            { 0, 200, 255 },
            { 255, 60, 60 },
            { 255, 220, 0 },
            { 200, 80, 255 },
            { 255, 255, 255 }
        };
        static size_t idx = 0;
        idx = (idx + 1) % palette.size();
        g_state.trailColor = palette[idx];
        if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
            if (auto spr = typeinfo_cast<CCSprite*>(btn->getNormalImage())) {
                spr->setColor(g_state.trailColor);
            }
        }
        saveToGeodeSettings();
    }

    void onOpacityDown(CCObject*) {
        g_state.hudOpacity = std::max(0.10f, g_state.hudOpacity - 0.15f);
        m_opacityLabel->setString(fmt::format("{}%", static_cast<int>(g_state.hudOpacity * 100)).c_str());
        saveToGeodeSettings();
    }
    void onOpacityUp(CCObject*) {
        g_state.hudOpacity = std::min(1.00f, g_state.hudOpacity + 0.15f);
        m_opacityLabel->setString(fmt::format("{}%", static_cast<int>(g_state.hudOpacity * 100)).c_str());
        saveToGeodeSettings();
    }

public:
    static GMMenuPopup* create() {
        auto ret = new GMMenuPopup();
        if (ret && ret->initAnchored(380.f, 320.f)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

class GMButtonNode : public CCNode {
public:
    void onOpenGM(CCObject*) {
        if (auto popup = GMMenuPopup::create()) {
            popup->show();
        }
    }

    static GMButtonNode* create() {
        auto ret = new GMButtonNode();
        if (ret && ret->init()) {
            ret->autorelease();
            ret->setID("gm-floating-button"_spr);
            ret->setZOrder(99999);

            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto menu = CCMenu::create();
            menu->setPosition({ 34.f, winSize.height - 34.f });
            ret->addChild(menu);

            auto circleBg = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
            if (!circleBg) {
                circleBg = CCSprite::create();
            }
            circleBg->setScale(0.75f);

            auto gmText = CCLabelBMFont::create("GM", "bigFont.fnt");
            gmText->setScale(0.45f);
            gmText->setPosition(circleBg->getContentSize() / 2);
            circleBg->addChild(gmText);

            auto btn = CCMenuItemSpriteExtra::create(
                circleBg, ret, menu_selector(GMButtonNode::onOpenGM)
            );
            btn->setPosition({ 0.f, 0.f });
            menu->addChild(btn);

            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

static void ensureGMButtonOnScene(CCScene* scene) {
    if (!scene) return;
    bool hideBtn = Mod::get()->getSettingValue<bool>("hide-button");
    bool inPlayLayer = scene->getChildByType<PlayLayer>(0) != nullptr;
    auto existing = scene->getChildByID("gm-floating-button"_spr);

    if (hideBtn || inPlayLayer) {
        if (existing) existing->removeFromParent();
        return;
    }

    if (!existing) {
        if (auto btnNode = GMButtonNode::create()) {
            scene->addChild(btnNode);
        }
    }
}

class $modify(GMDirectorHook, CCDirector) {
    void willSwitchToScene(CCScene* scene) {
        CCDirector::willSwitchToScene(scene);
        ensureGMButtonOnScene(scene);
    }

    void drawScene() {
        CCDirector::drawScene();
        ensureGMButtonOnScene(this->getRunningScene());
    }
};

class $modify(GMPlayLayer, PlayLayer) {
    struct Fields {
        CCDrawNode* hitboxDraw = nullptr;
        CCLabelBMFont* hudLabel = nullptr;
        CCDrawNode* indicatorDot = nullptr;
        float fpsTimer = 0.f;
        int frameCounter = 0;
        int currentFPS = 60;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        if (auto scene = this->getParent()) {
            if (auto existing = scene->getChildByID("gm-floating-button"_spr)) {
                existing->removeFromParent();
            }
        }

        syncFromGeodeSettings();
        g_state.totalFrames = 0;
        g_state.noclipHitFrames = 0;
        g_state.noclipDeaths = 0;
        g_state.wasCollidingThisTick = false;
        g_state.playerTrailHistory.clear();

        m_fields->hitboxDraw = CCDrawNode::create();
        m_fields->hitboxDraw->setZOrder(500);
        if (m_objectLayer) {
            m_objectLayer->addChild(m_fields->hitboxDraw);
        }

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_fields->hudLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_fields->hudLabel->setScale(0.55f);
        m_fields->hudLabel->setAnchorPoint({ 0.f, 1.f });
        m_fields->hudLabel->setPosition({ 18.f, winSize.height - 8.f });
        m_fields->hudLabel->setZOrder(1000);
        this->addChild(m_fields->hudLabel);

        m_fields->indicatorDot = CCDrawNode::create();
        m_fields->indicatorDot->setPosition({ 10.f, winSize.height - 13.f });
        m_fields->indicatorDot->setZOrder(1000);
        this->addChild(m_fields->indicatorDot);

        if (g_state.smoothOptimize && m_particleLayer) {
            m_particleLayer->setVisible(false);
        }

        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (g_state.noclip) {
            g_state.noclipHitFrames++;
            if (!g_state.wasCollidingThisTick) {
                g_state.noclipDeaths++;
                g_state.wasCollidingThisTick = true;
            }
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        g_state.totalFrames = 0;
        g_state.noclipHitFrames = 0;
        g_state.noclipDeaths = 0;
        g_state.wasCollidingThisTick = false;
        g_state.playerTrailHistory.clear();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        g_state.totalFrames++;
        g_state.wasCollidingThisTick = false;

        m_fields->fpsTimer += dt / std::max(0.01f, g_state.speed);
        m_fields->frameCounter++;
        if (m_fields->fpsTimer >= 0.5f) {
            m_fields->currentFPS = static_cast<int>(m_fields->frameCounter / m_fields->fpsTimer);
            m_fields->fpsTimer = 0.f;
            m_fields->frameCounter = 0;
        }

        auto now = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        while (!g_state.clickTimestamps.empty() && (now - g_state.clickTimestamps.front()) > 1.0) {
            g_state.clickTimestamps.pop_front();
        }
        int cps = static_cast<int>(g_state.clickTimestamps.size());

        if (m_fields->hitboxDraw) {
            m_fields->hitboxDraw->clear();
            if (m_player1 && (g_state.showHitboxes || g_state.hitboxTrail)) {
                CCRect pRect = m_player1->getObjectRect();

                if (g_state.hitboxTrail) {
                    g_state.playerTrailHistory.push_back(pRect);
                    while (static_cast<int>(g_state.playerTrailHistory.size()) > g_state.maxTrailLength) {
                        g_state.playerTrailHistory.pop_front();
                    }

                    ccColor4F trailCol = {
                        g_state.trailColor.r / 255.f,
                        g_state.trailColor.g / 255.f,
                        g_state.trailColor.b / 255.f,
                        0.45f
                    };

                    for (const auto& r : g_state.playerTrailHistory) {
                        CCPoint pts[4] = {
                            { r.getMinX(), r.getMinY() },
                            { r.getMaxX(), r.getMinY() },
                            { r.getMaxX(), r.getMaxY() },
                            { r.getMinX(), r.getMaxY() }
                        };
                        m_fields->hitboxDraw->drawPolygon(pts, 4, { 0, 0, 0, 0 }, 0.6f, trailCol);
                    }
                }

                if (g_state.showHitboxes) {
                    CCPoint pts[4] = {
                        { pRect.getMinX(), pRect.getMinY() },
                        { pRect.getMaxX(), pRect.getMinY() },
                        { pRect.getMaxX(), pRect.getMaxY() },
                        { pRect.getMinX(), pRect.getMaxY() }
                    };
                    m_fields->hitboxDraw->drawPolygon(pts, 4, { 1.f, 0.f, 0.f, 0.25f }, 1.0f, { 1.f, 0.2f, 0.2f, 1.f });
                }
            }
        }

        if (m_fields->hudLabel && m_fields->indicatorDot) {
            m_fields->hudLabel->setVisible(g_state.showLabels);
            m_fields->indicatorDot->setVisible(g_state.showLabels);

            if (g_state.showLabels) {
                GLubyte alpha = static_cast<GLubyte>(std::clamp(g_state.hudOpacity, 0.f, 1.f) * 255);
                m_fields->hudLabel->setOpacity(alpha);

                float acc = 100.0f;
                if (g_state.totalFrames > 0) {
                    acc = std::max(0.0f, 100.0f * (1.0f - (static_cast<float>(g_state.nocl
