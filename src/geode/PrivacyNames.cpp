#include "PrivacyNames.hpp"

#include "ChatCore.hpp"
#include "ComsPlusSettings.hpp"

#include <Geode/Bindings.hpp>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace comsplus {
namespace {

std::string lowercaseAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string replaceAll(std::string text, std::string const& needle, std::string const& replacement) {
    if (needle.empty() || needle == replacement) return text;

    auto lowerText = lowercaseAscii(text);
    auto lowerNeedle = lowercaseAscii(needle);
    auto lowerReplacement = lowercaseAscii(replacement);

    std::size_t pos = 0;
    while ((pos = lowerText.find(lowerNeedle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
        lowerText.replace(pos, lowerNeedle.size(), lowerReplacement);
        pos += replacement.size();
    }
    return text;
}

} // namespace

std::string privacySpoofText(std::string const& input) {
    auto settings = readSettings();
    if (!settings.privacyEnabled) return input;

    auto real = sanitizeName(localRealName());
    auto fake = sanitizeName(settings.fakeName);
    if (real.size() < 3 || fake.empty() || real == fake) return input;

    return replaceAll(input, real, fake);
}

int replaceOwnNameLabels(CCNode* root, std::string const& realName, std::string const& fakeName) {
    if (!root) return 0;

    auto cleanReal = sanitizeName(realName);
    auto cleanFake = sanitizeName(fakeName);
    if (cleanReal.empty() || cleanFake.empty() || cleanReal == cleanFake) return 0;

    int replaced = 0;

    if (auto label = typeinfo_cast<CCLabelBMFont*>(root)) {
        auto current = std::string(label->getString());
        auto spoofed = replaceAll(current, cleanReal, cleanFake);
        if (spoofed != current) {
            label->setString(spoofed.c_str());
            ++replaced;
        }
    } else if (auto label = typeinfo_cast<CCLabelTTF*>(root)) {
        auto current = std::string(label->getString());
        auto spoofed = replaceAll(current, cleanReal, cleanFake);
        if (spoofed != current) {
            label->setString(spoofed.c_str());
            ++replaced;
        }
    }

    if (auto children = root->getChildren()) {
        for (auto child : CCArrayExt<CCNode*>(children)) {
            replaced += replaceOwnNameLabels(child, cleanReal, cleanFake);
        }
    }

    return replaced;
}

} // namespace comsplus
