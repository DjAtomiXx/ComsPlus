#include "PrivacyNames.hpp"

#include "ChatCore.hpp"
#include "ComsPlusSettings.hpp"

#include <Geode/Bindings.hpp>

using namespace geode::prelude;

namespace comsplus {
namespace {

bool replaceLabelText(CCLabelProtocol* label, std::string const& realName, std::string const& fakeName) {
    if (!label) return false;

    auto currentText = label->getString();
    if (!currentText) return false;

    auto current = std::string(currentText);
    auto spoofed = replaceOwnNameText(current, realName, fakeName);
    if (spoofed == current) return false;

    label->setString(spoofed.c_str());
    return true;
}

} // namespace

std::string privacySpoofText(std::string const& input) {
    auto settings = readSettings();
    if (!settings.privacyEnabled) return input;

    auto text = input;
    for (auto const& realName : localRealNameCandidates()) {
        text = replaceOwnNameText(text, realName, settings.fakeName);
    }
    return text;
}

int replaceOwnNameLabels(CCNode* root, std::string const& realName, std::string const& fakeName) {
    if (!root) return 0;

    int replaced = 0;

    if (auto label = typeinfo_cast<CCLabelBMFont*>(root)) {
        replaced += replaceLabelText(label, realName, fakeName) ? 1 : 0;
    } else if (auto label = typeinfo_cast<CCLabelTTF*>(root)) {
        replaced += replaceLabelText(label, realName, fakeName) ? 1 : 0;
    } else if (auto label = dynamic_cast<CCLabelProtocol*>(root)) {
        replaced += replaceLabelText(label, realName, fakeName) ? 1 : 0;
    }

    if (auto children = root->getChildren()) {
        for (auto child : CCArrayExt<CCNode*>(children)) {
            replaced += replaceOwnNameLabels(child, realName, fakeName);
        }
    }

    return replaced;
}

} // namespace comsplus
