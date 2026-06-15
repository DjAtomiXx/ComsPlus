#pragma once

#include <Geode/Geode.hpp>

#include <string>

namespace comsplus {

std::string privacySpoofText(std::string const& input);
int replaceOwnNameLabels(cocos2d::CCNode* root, std::string const& realName, std::string const& fakeName);

} // namespace comsplus
