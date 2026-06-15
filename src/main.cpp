#include "ChatOverlay.hpp"
#include "ComsPlusSettings.hpp"
#include "PrivacyNames.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/AccountLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/CCLabelBMFont.hpp>
#include <Geode/modify/CCLabelTTF.hpp>
#include <Geode/modify/GJAccountSettingsLayer.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/utils/Keyboard.hpp>

#include <chrono>
#include <vector>

using namespace geode::prelude;

namespace {

std::string g_lastFakeName;

void applyPrivacyTo(CCNode* node) {
    auto settings = comsplus::readSettings();
    if (!settings.privacyEnabled) return;

    auto fake = settings.fakeName;
    auto count = 0;
    for (auto const& real : comsplus::localRealNameCandidates()) {
        count += comsplus::replaceOwnNameLabels(node, real, fake);
    }
    if (!g_lastFakeName.empty() && comsplus::sanitizeName(g_lastFakeName) != comsplus::sanitizeName(fake)) {
        count += comsplus::replaceOwnNameLabels(node, g_lastFakeName, fake);
    }
    g_lastFakeName = fake;

    if (count > 0) {
        log::debug("ComsPlus replaced {} local name label(s)", count);
    }
}

class ComsPlusPrivacyRefresher final : public CCNode {
public:
    static ComsPlusPrivacyRefresher* create() {
        auto ret = new ComsPlusPrivacyRefresher();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;

        this->setID("comsplus-privacy-refresher"_spr);
        this->schedule(schedule_selector(ComsPlusPrivacyRefresher::refresh), 0.35f);
        return true;
    }

    void refresh(float) {
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            applyPrivacyTo(scene);
        }
    }
};

void ensurePrivacyRefresher() {
    if (!comsplus::readSettings().privacyEnabled) return;

    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene || scene->getChildByID("comsplus-privacy-refresher"_spr)) return;

    if (auto refresher = ComsPlusPrivacyRefresher::create()) {
        scene->addChild(refresher, 100001);
    }
}

void refreshCurrentScenePrivacy() {
    ensurePrivacyRefresher();
    if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
        applyPrivacyTo(scene);
    }
}

#ifndef GEODE_IS_ANDROID
std::int64_t steadyMs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

bool consumeChatShortcut() {
    static std::int64_t s_lastShortcutMs = 0;

    auto now = steadyMs();
    if (now - s_lastShortcutMs < 140) {
        return false;
    }
    s_lastShortcutMs = now;
    return true;
}

std::vector<Keybind> openChatKeybinds() {
    auto bindings = Mod::get()->getSettingValue<std::vector<Keybind>>("open-chat-keybind");
    if (bindings.empty()) {
        bindings.emplace_back(KEY_C, KeyboardModifier::None);
    }
    return bindings;
}

bool isOpenChatKey(KeyboardInputData const& data) {
    if (data.action != KeyboardInputData::Action::Press) return false;

    auto pressed = Keybind(data.key, data.modifiers);
    for (auto const& bind : openChatKeybinds()) {
        if (bind == pressed) {
            return true;
        }
    }
    return false;
}

bool hasPlainCChatKeybind() {
    auto plainC = Keybind(KEY_C, KeyboardModifier::None);
    for (auto const& bind : openChatKeybinds()) {
        if (bind == plainC) {
            return true;
        }
    }
    return false;
}

bool hasModifierKeyPressed() {
    auto dispatcher = CCKeyboardDispatcher::get();
    return dispatcher && (
        dispatcher->getShiftKeyPressed() ||
        dispatcher->getControlKeyPressed() ||
        dispatcher->getAltKeyPressed() ||
        dispatcher->getCommandKeyPressed()
    );
}

bool isTextInputActive() {
    auto dispatcher = CCIMEDispatcher::sharedDispatcher();
    return dispatcher && dispatcher->hasDelegate();
}

bool ensureChatOverlay() {
    if (comsplus::activeChatOverlay()) return true;

    auto scene = CCDirector::sharedDirector()->getRunningScene();
    auto parent = static_cast<CCNode*>(scene);
    if (!parent) {
        if (auto gm = GameManager::get()) {
            parent = gm->m_playLayer;
        }
    }
    if (!parent) return false;

    if (auto overlay = comsplus::ComsPlusChatOverlay::create()) {
        parent->addChild(overlay, 100000);
        return true;
    }
    return false;
}

bool openDesktopChat(bool respectTextInput, bool debounce) {
    if (!comsplus::readSettings().chatEnabled) return false;
    if (respectTextInput && isTextInputActive()) return false;
    if (debounce && !consumeChatShortcut()) return false;
    if (!ensureChatOverlay()) return false;

    comsplus::toggleActiveChatOverlay();
    return true;
}
#endif

} // namespace

$execute {
    log::info("ComsPlus loaded");
    comsplus::GlobedBridge::get().initialize();

#ifndef GEODE_IS_ANDROID
    listenForKeybindSettingPresses("open-chat-keybind", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return false;
        return openDesktopChat(false, true);
    });

    KeyboardInputEvent().listen([](KeyboardInputData& data) {
        if (!isOpenChatKey(data)) {
            return ListenerResult::Propagate;
        }
        if (!openDesktopChat(true, true)) {
            return ListenerResult::Propagate;
        }
        return ListenerResult::Stop;
    }).leak();
#endif

    listenForSettingChanges<bool>("privacy-enabled", [](bool) {
        refreshCurrentScenePrivacy();
    });
    listenForSettingChanges<std::string>("fake-name", [](std::string) {
        refreshCurrentScenePrivacy();
    });
}

#ifndef GEODE_IS_ANDROID
class $modify(ComsPlusKeyboardDispatcher, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool isKeyDown, bool isKeyRepeat, double timestamp) {
        auto handled = CCKeyboardDispatcher::dispatchKeyboardMSG(key, isKeyDown, isKeyRepeat, timestamp);

        if (
            key == KEY_C &&
            isKeyDown &&
            !isKeyRepeat &&
            hasPlainCChatKeybind() &&
            !hasModifierKeyPressed()
        ) {
            openDesktopChat(true, true);
        }

        return handled;
    }
};
#endif

class $modify(ComsPlusPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto settings = comsplus::readSettings();
        if (settings.chatEnabled) {
            auto scene = CCDirector::sharedDirector()->getRunningScene();
            auto parent = scene ? static_cast<CCNode*>(scene) : static_cast<CCNode*>(this);
            if (!parent->getChildByID("comsplus-chat-overlay"_spr)) {
                if (auto overlay = comsplus::ComsPlusChatOverlay::create()) {
                    parent->addChild(overlay, 100000);
                }
            }
        }

        applyPrivacyTo(this);
        ensurePrivacyRefresher();
        return true;
    }

    void resume() {
        comsplus::collapseActiveChatOverlay();
        PlayLayer::resume();
    }
};

class $modify(ComsPlusMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        applyPrivacyTo(this);
        ensurePrivacyRefresher();
        return true;
    }
};

class $modify(ComsPlusProfilePage, ProfilePage) {
    bool init(int accountId, bool ownProfile) {
        if (!ProfilePage::init(accountId, ownProfile)) return false;
        if (ownProfile || accountId == comsplus::localAccountId()) {
            applyPrivacyTo(this);
            ensurePrivacyRefresher();
            this->scheduleOnce(schedule_selector(ComsPlusProfilePage::delayedPrivacyRefresh), 0.05f);
            this->scheduleOnce(schedule_selector(ComsPlusProfilePage::delayedPrivacyRefresh), 0.25f);
        }
        return true;
    }

    void delayedPrivacyRefresh(float) {
        applyPrivacyTo(this);
    }
};

class $modify(ComsPlusGarageLayer, GJGarageLayer) {
    bool init() {
        if (!GJGarageLayer::init()) return false;
        applyPrivacyTo(this);
        ensurePrivacyRefresher();
        return true;
    }
};

class $modify(ComsPlusAccountLayer, AccountLayer) {
    void customSetup() {
        AccountLayer::customSetup();
        applyPrivacyTo(this);
        ensurePrivacyRefresher();
        this->scheduleOnce(schedule_selector(ComsPlusAccountLayer::delayedPrivacyRefresh), 0.05f);
    }

    void delayedPrivacyRefresh(float) {
        applyPrivacyTo(this);
    }
};

class $modify(ComsPlusAccountSettingsLayer, GJAccountSettingsLayer) {
    bool init(int accountId) {
        if (!GJAccountSettingsLayer::init(accountId)) return false;
        if (accountId == comsplus::localAccountId()) {
            applyPrivacyTo(this);
            ensurePrivacyRefresher();
        }
        return true;
    }
};

class $modify(ComsPlusPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        applyPrivacyTo(this);
        ensurePrivacyRefresher();
        comsplus::raiseActiveChatOverlay();

#ifndef GEODE_IS_ANDROID
        if (!comsplus::readSettings().chatEnabled || this->getChildByID("comsplus-desktop-chat-menu"_spr)) return;

        auto chatSprite = ButtonSprite::create("Chat", "bigFont.fnt", "GJ_button_06.png", 0.58f);
        auto chatButton = CCMenuItemSpriteExtra::create(chatSprite, this, menu_selector(ComsPlusPauseLayer::onComsPlusChat));
        chatButton->setID("comsplus-desktop-chat-button"_spr);

        auto chatMenu = CCMenu::create();
        chatMenu->setID("comsplus-desktop-chat-menu"_spr);
        auto win = CCDirector::sharedDirector()->getWinSize();
        chatMenu->setPosition({win.width - 46.0f, 28.0f});
        chatMenu->addChild(chatButton);
        this->addChild(chatMenu, 50);
#endif
    }

    void onResume(CCObject* sender) {
        comsplus::collapseActiveChatOverlay();
        PauseLayer::onResume(sender);
    }

#ifndef GEODE_IS_ANDROID
    void onComsPlusChat(CCObject*) {
        openDesktopChat(false, false);
    }
#endif
};

class $modify(ComsPlusBMFontLabel, CCLabelBMFont) {
    bool initWithString(char const* text, char const* fntFile, float width, CCTextAlignment alignment, CCPoint imageOffset) {
        auto spoofed = comsplus::privacySpoofText(text ? text : "");
        return CCLabelBMFont::initWithString(spoofed.c_str(), fntFile, width, alignment, imageOffset);
    }

    void setString(char const* text) {
        auto spoofed = comsplus::privacySpoofText(text ? text : "");
        CCLabelBMFont::setString(spoofed.c_str());
    }

    void setString(char const* text, bool needUpdateLabel) {
        auto spoofed = comsplus::privacySpoofText(text ? text : "");
        CCLabelBMFont::setString(spoofed.c_str(), needUpdateLabel);
    }

    void setString(unsigned short* text, bool needUpdateLabel) {
        CCLabelBMFont::setString(text, needUpdateLabel);
    }

    void setCString(char const* text) {
        auto spoofed = comsplus::privacySpoofText(text ? text : "");
        CCLabelBMFont::setCString(spoofed.c_str());
    }
};

class $modify(ComsPlusTTFLabel, CCLabelTTF) {
    bool initWithString(char const* text, char const* fontName, float fontSize) {
        auto spoofed = comsplus::privacySpoofText(text ? text : "");
        return CCLabelTTF::initWithString(spoofed.c_str(), fontName, fontSize);
    }

    bool initWithString(char const* text, char const* fontName, float fontSize, CCSize const& dimensions, CCTextAlignment alignment) {
        auto spoofed = comsplus::privacySpoofText(text ? text : "");
        return CCLabelTTF::initWithString(spoofed.c_str(), fontName, fontSize, dimensions, alignment);
    }

    bool initWithString(char const* text, char const* fontName, float fontSize, CCSize const& dimensions, CCTextAlignment hAlignment, CCVerticalTextAlignment vAlignment) {
        auto spoofed = comsplus::privacySpoofText(text ? text : "");
        return CCLabelTTF::initWithString(spoofed.c_str(), fontName, fontSize, dimensions, hAlignment, vAlignment);
    }

    bool initWithStringAndTextDefinition(char const* text, ccFontDefinition& textDefinition) {
        auto spoofed = comsplus::privacySpoofText(text ? text : "");
        return CCLabelTTF::initWithStringAndTextDefinition(spoofed.c_str(), textDefinition);
    }

    void setString(char const* text) {
        auto spoofed = comsplus::privacySpoofText(text ? text : "");
        CCLabelTTF::setString(spoofed.c_str());
    }
};
