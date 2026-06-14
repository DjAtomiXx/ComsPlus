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

    RateLimiter limiter(1000);
    CHECK(limiter.canSend(1000));
    limiter.markSent(1000);
    CHECK(!limiter.canSend(1500));
    CHECK(limiter.canSend(2000));

    ChatMessage message{1, "abc", 42, "Hidden", "cube:1:2:3", "hello", 1234};
    auto encoded = encodePayload(message);
    auto decoded = decodePayload(encoded);
    CHECK(decoded.has_value());
    CHECK(decoded->protocolVersion == 1);
    CHECK(decoded->messageId == "abc");
    CHECK(decoded->accountId == 42);
    CHECK(decoded->displayName == "Hidden");
    CHECK(decoded->iconData == "cube:1:2:3");
    CHECK(decoded->text == "hello");
    CHECK(decoded->timestamp == 1234);

    CHECK(!decodePayload("{\"v\":2,\"id\":\"abc\"}").has_value());
    CHECK(!decodePayload("not-json").has_value());

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "all ComsPlus core checks passed\n";
    return EXIT_SUCCESS;
}
