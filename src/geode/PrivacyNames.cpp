#include "PrivacyNames.hpp"

#include "ChatCore.hpp"
#include "ComsPlusSettings.hpp"

#include <Geode/Bindings.hpp>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace comsplus {
namespace {

constexpr std::size_t kMaxPrivacyNodes = 4096;
constexpr std::uint16_t kMaxPrivacyDepth = 64;

std::string nodeDebugName(CCNode* node) {
    if (!node) return "<null>";

    auto id = std::string(node->getID());
    if (id.empty()) {
        id = "<no-id>";
    }
    return id + " tag=" + std::to_string(node->getTag());
}

void snapshotChildren(CCNode* node, std::vector<Ref<CCNode>>& out) {
    out.clear();
    if (!node) return;

    auto children = node->getChildren();
    if (!children || children->count() == 0) return;

    out.reserve(std::min<std::size_t>(children->count(), 128));
    for (auto child : CCArrayExt<CCNode*>(children)) {
        if (child) {
            out.emplace_back(child);
        }
    }
}

bool replaceLabelText(CCLabelProtocol* label, CCNode* node, std::string_view labelType, std::string const& realName, std::string const& fakeName) {
    if (!label) return false;

    auto currentText = label->getString();
    if (!currentText) return false;

    auto current = std::string(currentText);
    auto spoofed = replaceOwnNameText(current, realName, fakeName);
    if (spoofed == current) return false;

    label->setString(spoofed.c_str());

#ifdef GEODE_IS_ANDROID
    log::debug("ComsPlus privacy replaced {} {} '{}'", labelType, nodeDebugName(node), spoofed);
#endif

    return true;
}

bool replaceKnownLabel(CCNode* node, std::string const& realName, std::string const& fakeName) {
    if (!node) return false;

    // Android crash logs showed __dynamic_cast crashing in replaceOwnNameLabels.
    // Keep this to concrete Cocos label classes and avoid generic CCLabelProtocol casts.
    if (auto label = typeinfo_cast<CCLabelBMFont*>(node)) {
        return replaceLabelText(label, node, "CCLabelBMFont", realName, fakeName);
    }
    if (auto label = typeinfo_cast<CCLabelTTF*>(node)) {
        return replaceLabelText(label, node, "CCLabelTTF", realName, fakeName);
    }
    return false;
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
    std::size_t visitedCount = 0;
    std::unordered_set<CCNode*> visited;
    std::vector<std::pair<Ref<CCNode>, std::uint16_t>> stack;
    std::vector<Ref<CCNode>> children;

    visited.reserve(256);
    stack.reserve(128);
    stack.push_back({Ref<CCNode>(root), 0});

    while (!stack.empty()) {
        auto [nodeRef, depth] = std::move(stack.back());
        stack.pop_back();
        auto node = nodeRef.data();

        if (!node) {
#ifdef GEODE_IS_ANDROID
            log::debug("ComsPlus privacy skipped null node");
#endif
            continue;
        }

        if (!visited.insert(node).second) {
            continue;
        }

        if (++visitedCount > kMaxPrivacyNodes) {
#ifdef GEODE_IS_ANDROID
            log::warn("ComsPlus privacy stopped after {} nodes at {}", kMaxPrivacyNodes, nodeDebugName(node));
#endif
            break;
        }

        if (replaceKnownLabel(node, realName, fakeName)) {
            ++replaced;
        }

        if (depth >= kMaxPrivacyDepth) {
#ifdef GEODE_IS_ANDROID
            log::debug("ComsPlus privacy skipped children past depth {} at {}", kMaxPrivacyDepth, nodeDebugName(node));
#endif
            continue;
        }

        snapshotChildren(node, children);
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            auto child = it->data();
            if (child && !visited.contains(child)) {
                stack.push_back({*it, static_cast<std::uint16_t>(depth + 1)});
            }
        }
    }

    return replaced;
}

} // namespace comsplus
