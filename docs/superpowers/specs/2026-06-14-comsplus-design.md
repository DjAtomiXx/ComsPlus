# ComsPlus Design

## Goal

ComsPlus is a Geode mod by Exploited for Geometry Dash on Windows and Android. It adds a local-only privacy display name and a small in-game chat overlay for players who are connected through Globed and also have ComsPlus installed.

## Scope

ComsPlus must produce installable `.geode` packages for Windows and Android when the required Geode SDK, platform binaries, compiler, and Android NDK are available. The mod must not spoof Geometry Dash, RobTop, or Globed account identity. The fake name is only used for local UI replacement and optional ComsPlus chat display.

## Architecture

The code is split into a testable core and a Geode integration layer.

The core contains:

- display-name selection from real name, fake name, and user mode
- chat message sanitization, length limits, and cooldown checks
- compact chat payload serialization and parsing
- duplicate suppression by message id

The Geode layer contains:

- mod settings in `mod.json`
- a persistent overlay layer for chat history and input
- hooks for known own-profile/account UI labels where local privacy replacement is safe
- a Globed bridge that prefers event-based soft integration and disables chat when Globed is unavailable

## Settings

Settings are defined in `mod.json`:

- `chat-enabled`: enables ComsPlus chat
- `privacy-enabled`: enables local fake-name display
- `fake-name`: local display name
- `chat-name-mode`: `auto`, `real`, or `fake`
- `chat-opacity`: overlay opacity
- `send-cooldown-ms`: minimum delay between outgoing messages
- `max-chat-messages`: retained overlay history count

## Privacy Rules

The fake name never changes the Geometry Dash account username, account id, Globed identity, or any network login value. For chat, the user chooses whether ComsPlus sends the real name, fake name, or automatic mode. Automatic mode uses the fake name only when privacy is enabled and the fake name is non-empty.

Local UI replacement only targets labels that are in known own-account screens or that exactly match the local real username. It does not globally rewrite arbitrary player names.

## Chat Protocol

ComsPlus sends one JSON payload per chat message through the Globed event path:

- protocol version
- random message id
- account id, if known
- display name
- icon metadata fields when available
- message text
- unix timestamp

Messages are capped and sanitized before sending. Received messages with unsupported protocol versions, invalid names, invalid text, or duplicate ids are ignored.

## Globed Integration

ComsPlus treats Globed as optional. If Globed is not loaded or no compatible event bridge is available, the overlay remains usable locally but shows an offline status and does not send chat messages.

The implementation avoids hard ABI dependencies unless the local Globed headers expose a stable API. If a hard dependency is unavailable, the mod compiles with a safe bridge stub and documents the exact missing API. Chat networking is only enabled when the bridge confirms that Globed can send and receive ComsPlus events.

## UI

The overlay appears in gameplay as a compact chat panel. It shows:

- recent messages
- icon placeholder or rendered icon metadata before the sender name
- sender display name
- message text
- Globed/ComsPlus connection status

The send action is rate-limited. Empty messages and messages that sanitize to an empty string are rejected locally.

## Build And Verification

Verification must include:

- core unit tests for display-name selection, sanitization, payload parsing, and cooldown behavior
- Geode package creation for Windows when the Windows Geode cross toolchain is available
- Geode package creation for Android when `ANDROID_NDK_ROOT` and Android Geode binaries are available
- inspection of each `.geode` package as a zip archive to confirm `mod.json` and the platform binary are present

If a platform cannot be built because a required toolchain component is missing or cannot be installed in the environment, the final report must state the exact command, exit status, and missing requirement.

## Sources Consulted

- Geode creating/building mods documentation: https://docs.geode-sdk.org/getting-started/create-mod/
- Geode CLI documentation: https://docs.geode-sdk.org/getting-started/geode-cli/
- Geode mod settings documentation: https://docs.geode-sdk.org/mods/settings/
- Geode mod configuration documentation: https://docs.geode-sdk.org/mods/configuring/
- Globed Geode page and developer API note: https://geode-sdk.org/mods/dankmeme.globed2
