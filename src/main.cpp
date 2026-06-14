#include "ChatOverlay.hpp"
#include "ComsPlusSettings.hpp"
#include "PrivacyNames.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/AccountLayer.hpp>
#include <Geode/modify/GJAccountSettingsLayer.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
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
            if (!this->getChildByID("comsplus-chat-overlay"_spr)) {
                if (auto overlay = comsplus::ComsPlusChatOverlay::create()) {
                    this->addChild(overlay);
                }
            }
        }

        applyPrivacyTo(this);
        return true;
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
        }
        return true;
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
