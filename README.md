# ComsPlus

ComsPlus is a Geode mod by Exploited for Geometry Dash on Windows and Android.

It provides:

- a local-only fake display name for your own visible labels where ComsPlus can safely identify them
- an in-level ComsPlus chat overlay
- optional Globed server-event chat when Globed 2.1.4+ is installed and connected
- optional main menu chat through the bundled relay server
- chat display-name mode: real name, fake name, or automatic
- Android floating chat bubble with adjustable size and opacity
- Windows Open chat keybind, defaulting to C

## Privacy

ComsPlus does not spoof your Geometry Dash account, account id, RobTop login, or Globed identity. The fake name is local UI text and an optional ComsPlus chat display name. Other players only see the fake name in ComsPlus chat if you choose a fake/automatic chat name mode and privacy is enabled.

## Building

Install the Geode CLI and SDK, then:

```bash
GEODE_SDK=/path/to/geode-sdk geode build --ninja
```

For Android:

```bash
GEODE_SDK=/path/to/geode-sdk ANDROID_NDK_ROOT=/path/to/android-ndk geode sdk install-binaries -p android64
GEODE_SDK=/path/to/geode-sdk ANDROID_NDK_ROOT=/path/to/android-ndk geode build -p android64 --ninja
```

## Installing

Copy the built `.geode` file into your Geode mods folder.

Android Geode Launcher path:

```text
/storage/emulated/0/Android/media/com.geode.launcher/game/geode/mods/
```

## Chat

The in-level network chat uses Globed 2.1.4+ server events through the bundled MIT-licensed Soft Link API headers. Globed is still optional at runtime; if it is missing, outdated, offline, or not active in a level, ComsPlus keeps running and the overlay shows the exact status.

The main menu chat uses the relay in `server/`. New installs use `https://hexasystems.xyz/comsplus` by default; deploy the Cloudflare Worker in `server/cloudflare/` on that route or run the Node relay behind a reverse proxy at the same path.

Android opens the chat through the draggable bubble. Windows opens it through the Open chat keybind in Geode's keybind menu; the default is C.
