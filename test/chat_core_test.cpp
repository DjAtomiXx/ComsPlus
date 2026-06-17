#include "ChatCore.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, char const* expression, int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << "\n";
    }
}

#define CHECK(expr) check(static_cast<bool>(expr), #expr, __LINE__)

} // namespace

int main() {
    using namespace comsplus;

    DisplayNameSettings autoSettings{true, "Hidden", ChatNameMode::Auto};
    CHECK(selectDisplayName("RealUser", autoSettings) == "Hidden");

    DisplayNameSettings realSettings{true, "Hidden", ChatNameMode::Real};
    CHECK(selectDisplayName("RealUser", realSettings) == "RealUser");

    DisplayNameSettings fakeSettings{false, "Hidden", ChatNameMode::Fake};
    CHECK(selectDisplayName("RealUser", fakeSettings) == "Hidden");

    CHECK(sanitizeName("  A\nB\tC  ") == "A B C");
    CHECK(sanitizeName(std::string(80, 'n')).size() == 24);
    CHECK(sanitizeMessage(" hi\nthere\t ") == "hi there");
    CHECK(sanitizeMessage(std::string(180, 'x')).size() == 120);
    CHECK(sanitizeMessage("\n\t   ").empty());

    CHECK(replaceOwnNameText("LUNASTRY joined Lunastry", "lunastry", "ComsPlayer") == "ComsPlayer joined ComsPlayer");
    CHECK(replaceOwnNameText("thethingch", "lunastry", "ComsPlayer") == "thethingch");

    RateLimiter limiter(1000);
    CHECK(limiter.canSend(1000));
    limiter.markSent(1000);
    CHECK(!limiter.canSend(1500));
    CHECK(limiter.canSend(2000));

    ChatMessage message{1, "abc", 42, "Hidden", "cube:1:2:3", "hello", 1234};
    auto encoded = encodePayload(message);
    auto decoded = decodePayload(encoded);
    CHECK(decoded.has_value());
    CHECK(decoded->kind == ChatMessageKind::User);
    CHECK(decoded->protocolVersion == 1);
    CHECK(decoded->messageId == "abc");
    CHECK(decoded->accountId == 42);
    CHECK(decoded->displayName == "Hidden");
    CHECK(decoded->iconData == "cube:1:2:3");
    CHECK(decoded->text == "hello");
    CHECK(decoded->timestamp == 1234);

    message.levelId = 9001;
    message.levelName = "Drillbit";
    message.colorMode = ChatColorMode::Rainbow;
    message.primaryColor = "73FFD6";
    message.secondaryColor = "FF4F7D";
    message.authorRole = ChatAuthorRole::Dev;
    auto styled = decodePayload(encodePayload(message));
    CHECK(styled.has_value());
    CHECK(styled->levelId == 9001);
    CHECK(styled->levelName == "Drillbit");
    CHECK(styled->colorMode == ChatColorMode::Rainbow);
    CHECK(styled->primaryColor == "73FFD6");
    CHECK(styled->secondaryColor == "FF4F7D");
    CHECK(styled->authorRole == ChatAuthorRole::Dev);

    ChatMessage systemMessage{1, "joined", 42, "Hidden", "", "joined the chat from Drillbit", 1235, ChatMessageKind::System};
    auto encodedSystem = encodePayload(systemMessage);
    auto decodedSystem = decodePayload(encodedSystem);
    CHECK(decodedSystem.has_value());
    CHECK(decodedSystem->kind == ChatMessageKind::System);

    ChatMessage moderationMessage;
    moderationMessage.messageId = "ban";
    moderationMessage.accountId = 42;
    moderationMessage.displayName = "Hidden";
    moderationMessage.iconData = "";
    moderationMessage.text = "banned Target. Reason: spam";
    moderationMessage.timestamp = 1236;
    moderationMessage.kind = ChatMessageKind::Moderation;
    moderationMessage.authorRole = ChatAuthorRole::Dev;
    moderationMessage.moderationAction = ChatModerationAction::Ban;
    moderationMessage.targetName = "Target";
    moderationMessage.targetAccountId = 123;
    moderationMessage.expiresAt = 0;
    auto decodedModeration = decodePayload(encodePayload(moderationMessage));
    CHECK(decodedModeration.has_value());
    CHECK(decodedModeration->kind == ChatMessageKind::Moderation);
    CHECK(decodedModeration->authorRole == ChatAuthorRole::Dev);
    CHECK(decodedModeration->moderationAction == ChatModerationAction::Ban);
    CHECK(decodedModeration->targetName == "Target");
    CHECK(decodedModeration->targetAccountId == 123);
    CHECK(decodedSystem->displayName == "Hidden");
    CHECK(decodedSystem->text == "joined the chat from Drillbit");

    moderationMessage.messageId = "clear";
    moderationMessage.text = "cleared the chat";
    moderationMessage.moderationAction = ChatModerationAction::Clear;
    moderationMessage.targetName = "";
    moderationMessage.targetAccountId = 0;
    auto decodedClear = decodePayload(encodePayload(moderationMessage));
    CHECK(decodedClear.has_value());
    CHECK(decodedClear->kind == ChatMessageKind::Moderation);
    CHECK(decodedClear->authorRole == ChatAuthorRole::Dev);
    CHECK(decodedClear->moderationAction == ChatModerationAction::Clear);
    CHECK(decodedClear->text == "cleared the chat");

    moderationMessage.messageId = "mute";
    moderationMessage.text = "muted Target. Reason: spam";
    moderationMessage.moderationAction = ChatModerationAction::Mute;
    moderationMessage.targetName = "Target";
    auto decodedMute = decodePayload(encodePayload(moderationMessage));
    CHECK(decodedMute.has_value());
    CHECK(decodedMute->moderationAction == ChatModerationAction::Mute);

    ChatMessage reportMessage;
    reportMessage.messageId = "report";
    reportMessage.accountId = 42;
    reportMessage.displayName = "Hidden";
    reportMessage.iconData = "";
    reportMessage.text = "toxic";
    reportMessage.timestamp = 1237;
    reportMessage.kind = ChatMessageKind::Report;
    reportMessage.targetName = "Target";
    auto decodedReport = decodePayload(encodePayload(reportMessage));
    CHECK(decodedReport.has_value());
    CHECK(decodedReport->kind == ChatMessageKind::Report);
    CHECK(decodedReport->targetName == "Target");

    auto legacyDecoded = decodePayload("{\"v\":1,\"id\":\"legacy\",\"aid\":42,\"name\":\"Hidden\",\"icon\":\"cube:1\",\"text\":\"hello\",\"ts\":1236}");
    CHECK(legacyDecoded.has_value());
    CHECK(legacyDecoded->kind == ChatMessageKind::User);

    CHECK(!decodePayload("{\"v\":2,\"id\":\"abc\"}").has_value());
    CHECK(!decodePayload("not-json").has_value());

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "all ComsPlus core checks passed\n";
    return EXIT_SUCCESS;
}
