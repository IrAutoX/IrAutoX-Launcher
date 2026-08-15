#include <IrAutoXSDK.h>

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    IrAutoX::Client client;
    IrAutoX::Options options;
    options.gameId = 42;
    options.gameName = "IrAutoX SDK Demo";

    if (!client.initialize(options)) {
        std::cerr << client.lastError() << '\n';
        return 1;
    }

    const IrAutoX::User user = client.currentUser();
    std::cout << "User: " << user.username << " (" << user.id << ")\n";

    IrAutoX::Presence presence;
    presence.playing = true;
    presence.details = "SDK integration test";
    presence.state = "Main Menu";
    presence.partySize = 1;
    presence.partyMax = 4;
    client.setPresence(presence);

    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        client.heartbeat();
    }

    client.clearPresence();
    return 0;
}
