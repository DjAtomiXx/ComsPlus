#include "PrivacyNames.hpp"

#include "ChatCore.hpp"

#include <Geode/Bindings.hpp>

using namespace geode::prelude;

namespace comsplus {

int replaceOwnNameLabels(CCNode* root, std::string const& realName, std::string const& fakeName) {
    if (!root) return 0;

    auto cleanReal = sanitizeName(realName);
    auto cleanFake = sanitizeName(fakeName);
    if (cleanReal.empty() || cleanFake.empty() || cleanReal == cleanFake) return 0;

    int replaced = 0;

    if (auto label = typeinfo_cast<CCLabelBMFont*>(root)) {
        if (std::string(label->getString()) == cleanReal) {
            label->setString(cleanFake.c_str());
            ++replaced;
        }
    } else if (auto label = typeinfo_cast<CCLabelTTF*>(root)) {
        if (std::string(label->getString()) == cleanReal) {
            label->setString(cleanFake.c_str());
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
