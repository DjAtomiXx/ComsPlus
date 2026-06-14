# ComsPlus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build ComsPlus, a Windows and Android Geode mod that provides a local privacy display name and Globed-backed ComsPlus chat.

**Architecture:** The mod has a small pure C++ core for chat policy and payload handling, with a Geode layer for settings, UI overlay, local label replacement, and Globed event bridging. The Globed bridge compiles safely when no stable Globed API is available and only enables network chat after runtime capability checks.

**Tech Stack:** C++20, Geode SDK, CMake, Geode CLI, Python standard library for lightweight unit/build verification scripts.

---

## File Structure

- `mod.json`: Geode metadata, Windows/Android GD targets, settings, and optional Globed dependency metadata.
- `CMakeLists.txt`: Geode CMake project, core library, test executable, and mod target.
- `src/main.cpp`: Geode entry point, hooks, overlay creation, and settings wiring.
- `src/core/ChatCore.hpp`: Pure C++ data types and APIs for name selection, sanitization, cooldowns, and payload handling.
- `src/core/ChatCore.cpp`: Core implementation.
- `src/geode/ChatOverlay.hpp`: Overlay layer interface.
- `src/geode/ChatOverlay.cpp`: Cocos/Geode chat panel rendering and input behavior.
- `src/geode/GlobedBridge.hpp`: Bridge interface for Globed capability checks, send, receive, and status.
- `src/geode/GlobedBridge.cpp`: Runtime bridge implementation or safe offline fallback.
- `src/geode/PrivacyNames.hpp`: Local-only own-name replacement interface.
- `src/geode/PrivacyNames.cpp`: Conservative label traversal and replacement.
- `test/chat_core_test.cpp`: Failing-first tests for core behavior.
- `scripts/package_inspect.py`: Verifies `.geode` zip contents.
- `README.md`: Build, install, privacy, and Globed notes.

### Task 1: Core Test Harness

**Files:**
- Create: `CMakeLists.txt`
- Create: `test/chat_core_test.cpp`
- Create: `src/core/ChatCore.hpp`
- Create: `src/core/ChatCore.cpp`

- [ ] **Step 1: Write failing tests for display names, sanitization, cooldowns, and payloads**

Create `test/chat_core_test.cpp` with assertions for:

```cpp
DisplayNameSettings autoSettings{true, "Hidden", ChatNameMode::Auto};
CHECK(selectDisplayName("RealUser", autoSettings) == "Hidden");
CHECK(sanitizeMessage(" hi\nthere\t ") == "hi there");
CHECK(sanitizeMessage(std::string(180, 'x')).size() == 120);
RateLimiter limiter(1000);
CHECK(limiter.canSend(1000));
limiter.markSent(1000);
CHECK(!limiter.canSend(1500));
CHECK(limiter.canSend(2000));
ChatMessage message{1, "abc", 42, "Hidden", "cube:1:2:3", "hello", 1234};
auto encoded = encodePayload(message);
auto decoded = decodePayload(encoded);
CHECK(decoded.has_value());
CHECK(decoded->displayName == "Hidden");
```

- [ ] **Step 2: Run tests and verify they fail because core code is missing**

Run: `cmake -S . -B build-tests -DCOMSPLUS_BUILD_TESTS=ON && cmake --build build-tests --target comsplus_core_tests && ./build-tests/comsplus_core_tests`

Expected: configure or compile fails because the core APIs are not implemented.

- [ ] **Step 3: Implement minimal core APIs**

Implement `ChatCore.hpp` and `ChatCore.cpp` with enum `ChatNameMode`, structs `DisplayNameSettings` and `ChatMessage`, functions `selectDisplayName`, `sanitizeName`, `sanitizeMessage`, `encodePayload`, `decodePayload`, and class `RateLimiter`.

- [ ] **Step 4: Run tests and verify they pass**

Run: `cmake -S . -B build-tests -DCOMSPLUS_BUILD_TESTS=ON && cmake --build build-tests --target comsplus_core_tests && ./build-tests/comsplus_core_tests`

Expected: test executable exits 0 and prints all checks passed.

### Task 2: Geode Manifest And Build Skeleton

**Files:**
- Create: `mod.json`
- Modify: `CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `README.md`

- [ ] **Step 1: Add Geode metadata and settings**

Create `mod.json` with id `exploited.comsplus`, name `ComsPlus`, developer `Exploited`, Geode `5.6.1`, GD Windows and Android `2.2081`, and settings from the design.

- [ ] **Step 2: Add minimal Geode entry point**

Create `src/main.cpp` that logs mod load and reads settings without enabling chat networking yet.

- [ ] **Step 3: Configure Geode target**

Update `CMakeLists.txt` to include Geode's setup file when `GEODE_SDK` is present, build `ComsPlus`, and link the core sources.

- [ ] **Step 4: Build the default Geode target**

Run: `geode build --ninja`

Expected: either a `.geode` appears in the build folder, or the command fails with a toolchain-specific missing dependency that must be fixed before continuing.

### Task 3: Chat Overlay And Local Privacy

**Files:**
- Create: `src/geode/ChatOverlay.hpp`
- Create: `src/geode/ChatOverlay.cpp`
- Create: `src/geode/PrivacyNames.hpp`
- Create: `src/geode/PrivacyNames.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Add overlay class**

Implement `ComsPlusChatOverlay` as a `CCLayer` that holds status text, a bounded message list, an input box, and a send button.

- [ ] **Step 2: Add privacy replacement helper**

Implement `replaceOwnNameLabels(CCNode* root, std::string const& realName, std::string const& fakeName)` so it only replaces exact label matches and returns a replacement count.

- [ ] **Step 3: Hook known own-account/menu layers**

Add conservative Geode hooks that call the helper after known account/profile UI setup. If a binding is not available on both platforms, guard it with compile-time checks or omit it.

- [ ] **Step 4: Build both test and mod targets**

Run: `cmake --build build-tests --target comsplus_core_tests && geode build --ninja`

Expected: core tests pass and Geode build completes or reports a concrete API/toolchain issue.

### Task 4: Globed Bridge

**Files:**
- Create: `src/geode/GlobedBridge.hpp`
- Create: `src/geode/GlobedBridge.cpp`
- Modify: `src/geode/ChatOverlay.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Add bridge interface**

Define `GlobedBridge` with `isAvailable`, `isConnected`, `sendChat`, `pollReceived`, and `statusText`.

- [ ] **Step 2: Add safe fallback implementation**

Implement fallback behavior that returns unavailable status and never attempts network send.

- [ ] **Step 3: Add optional Globed event implementation if headers are available**

Use compile-time detection for local Globed API headers. If unavailable, keep fallback enabled and document that network chat requires the compatible Globed event API headers.

- [ ] **Step 4: Connect overlay send/receive to bridge**

Outgoing messages use `selectDisplayName`, sanitized message text, cooldown, and `encodePayload`. Incoming payloads use `decodePayload` and duplicate suppression.

### Task 5: Package Verification

**Files:**
- Create: `scripts/package_inspect.py`
- Modify: `README.md`

- [ ] **Step 1: Add package inspector**

The script accepts a `.geode` path, opens it as a zip file, validates `mod.json`, validates id/name/developer, and checks for at least one platform binary.

- [ ] **Step 2: Run Windows build**

Run: `geode build --ninja`

Expected: a Windows `.geode` exists under `build*`.

- [ ] **Step 3: Inspect Windows package**

Run: `python3 scripts/package_inspect.py <windows-geode-path>`

Expected: package inspection exits 0.

- [ ] **Step 4: Install Android binaries and build Android**

Run: `geode sdk install-binaries -p android64 && geode build -p android64 --ninja`

Expected: an Android `.geode` exists under `build-android64`.

- [ ] **Step 5: Inspect Android package**

Run: `python3 scripts/package_inspect.py <android-geode-path>`

Expected: package inspection exits 0.

## Self-Review

Spec coverage: The tasks cover core privacy policy, chat payload safety, Geode settings, UI overlay, optional Globed bridge, and Windows/Android packaging verification.

Placeholder scan: The plan intentionally leaves no `TBD` or unspecified test command. Optional Globed hard integration is bounded by local header availability and does not block offline-safe builds.

Type consistency: Core names used across tasks are `DisplayNameSettings`, `ChatNameMode`, `RateLimiter`, `ChatMessage`, `selectDisplayName`, `sanitizeMessage`, `encodePayload`, and `decodePayload`.
