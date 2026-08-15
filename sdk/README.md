# IrAutoX SDK 2.0.2

IrAutoX SDK is a small Windows C/C++ library that lets a game publish live presence through the installed IrAutoX Launcher. The game never receives the player's IrAutoX password. The SDK talks only to the local launcher bridge on `127.0.0.1:6769`; the authenticated launcher forwards presence to the IrAutoX server.

## Quick start

```cpp
#include <IrAutoXSDK.h>

int main()
{
    IrAutoX::Client client;
    IrAutoX::Options options;
    options.gameId = 42;
    options.gameName = "My Game";

    if (!client.initialize(options))
        return 1;

    IrAutoX::Presence presence;
    presence.playing = true;
    presence.details = "Competitive Match";
    presence.state = "Tehran Arena";
    presence.partySize = 2;
    presence.partyMax = 8;
    client.setPresence(presence);

    const auto user = client.currentUser();

    while (gameIsRunning()) {
        client.heartbeat();
        gameTick();
    }

    client.clearPresence();
    client.shutdown();
    return 0;
}
```

## Link

Dynamic SDK:

- include `sdk/include/IrAutoXSDK.h`
- link `IrAutoXSDK.lib`
- ship `IrAutoXSDK.dll` beside the game executable

Static SDK:

- include `sdk/include/IrAutoXSDK.h`
- define `IRAUTOX_SDK_STATIC`
- link `IrAutoXSDK_static.lib`, `ws2_32.lib`, `advapi32.lib`, `shell32.lib`

## API

- `initialize`: connects to the local launcher bridge. If the launcher is not running, the SDK can locate the installed launcher from `HKCU\Software\IrAutoX\Launcher\InstallDir` and start it with `--sdk-wake`.
- `setPresence`: publishes playing state, details, state and party information.
- `heartbeat`: refreshes a live session.
- `currentUser`: returns the authenticated IrAutoX user id and username from the launcher.
- `clearPresence`: immediately clears playing state.
- `shutdown`: clears presence and closes the local SDK connection.

## Automatic external detection

The launcher also scans the actual executable paths from the local IrAutoX library. A game launched by CMD, Explorer or a desktop shortcut is detected even when it was not started by the launcher's Play button, as long as the launcher is running in the tray.

## Tests

`ctest --test-dir build -C Release --output-on-failure` runs the SDK bridge protocol test in GitHub Actions. The test starts a mock local SDK bridge on an isolated port, initializes the SDK, verifies the returned user, publishes presence, sends a heartbeat, clears presence and shuts down.
