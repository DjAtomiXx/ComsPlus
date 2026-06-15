#include "ChatOverlay.hpp"
#include "ComsPlusSettings.hpp"
#include "PrivacyNames.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/AccountLayer.hpp>
#include <Geode/modify/CCLabelBMFont.hpp>
#include <Geode/modify/CCLabelTTF.hpp>
#include <Geode/modify/GJAccountSettingsLayer.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/ProfilePage.hpp>

using namespace geode::prelude;

namespace {

void applyPrivacyTo(CCNode* node) {
    auto settings = comsplus::readSettings();
    if (!settings.privacyEnabled) return;

    auto real = comsplus::localRealName();
    auto fake = settings.fakeName;
    auto count = comsplus::replaceOwnNameLabels(node, real, fake);
    if (count > 0) {
        log::debug("ComsPlus replaced {} local name label(s)", count);
    }
}

} // namespace

$execute {
    log::info("ComsPlus loaded");
    comsplus::GlobedBridge::get().initialize();
}

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
        return true;
    }
};

class $modify(ComsPlusProfilePage, ProfilePage) {
    bool init(int accountId, bool ownProfile) {
        if (!ProfilePage::init(accountId, ownProfile)) return false;
        if (ownProfile || accountId == comsplus::localAccountId()) {
            applyPrivacyTo(this);
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
        return true;
    }
};

class $modify(ComsPlusAccountLayer, AccountLayer) {
    void customSetup() {
        AccountLayer::customSetup();
        applyPrivacyTo(this);
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
        }
        return true;
    }
};

class $modify(ComsPlusPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        applyPrivacyTo(this);
        comsplus::raiseActiveChatOverlay();
    }

    void onResume(CCObject* sender) {
        comsplus::collapseActiveChatOverlay();
        PauseLayer::onResume(sender);
    }
};

class $modify(ComsPlusBMFontLabel, CCLabelBMFont) {
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
    void setString(char const* text) {
        auto spoofed = comsplus::privacySpoofText(text ? text : "");
        CCLabelTTF::setString(spoofed.c_str());
    }
};
