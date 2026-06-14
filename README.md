# ComsPlus

ComsPlus is a Geode mod by Exploited for Geometry Dash on Windows and Android.

It provides:

- a local-only fake display name for your own visible labels where ComsPlus can safely identify them
- an in-level ComsPlus chat overlay
- optional Globed server-event chat when Globed 2.2.0+ is installed and connected
- chat display-name mode: real name, fake name, or automatic

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

## Globed Chat

The network chat uses Globed 2.2.0+ server events through the bundled MIT-licensed Soft Link API headers. Globed is still optional at runtime; if it is missing, outdated, offline, or not active in a level, ComsPlus keeps running and the overlay shows the exact status.
