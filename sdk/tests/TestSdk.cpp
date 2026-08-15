#include <IrAutoXSDK.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

namespace {

#ifdef _WIN32

bool sendAll(SOCKET socket, const std::string &text)
{
    const char *cursor = text.data();
    int remaining = static_cast<int>(text.size());
    while (remaining > 0) {
        const int count = send(socket, cursor, remaining, 0);
        if (count <= 0)
            return false;
        cursor += count;
        remaining -= count;
    }
    return true;
}

std::string receiveLine(SOCKET socket)
{
    std::string line;
    char c = 0;
    while (recv(socket, &c, 1, 0) == 1) {
        if (c == '\n')
            break;
        line += c;
    }
    return line;
}

void mockServer(std::atomic<bool> &ready)
{
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL reuse = TRUE;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(16769);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    assert(bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    assert(listen(server, 1) == 0);
    ready = true;
    SOCKET client = accept(server, nullptr, nullptr);
    assert(client != INVALID_SOCKET);
    while (true) {
        const std::string request = receiveLine(client);
        if (request.empty())
            break;
        std::string response = "{\"ok\":true}";
        if (request.find("\"cmd\":\"get_user\"") != std::string::npos)
            response = "{\"ok\":true,\"user_id\":77,\"username\":\"sdk-test-user\"}";
        sendAll(client, response + "\n");
        if (request.find("\"cmd\":\"shutdown\"") != std::string::npos)
            break;
    }
    closesocket(client);
    closesocket(server);
    WSACleanup();
}

#endif

}

int main()
{
#ifdef _WIN32
    std::atomic<bool> ready{false};
    std::thread server([&] { mockServer(ready); });
    while (!ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    IrAutoX::Client client;
    IrAutoX::Options options;
    options.gameId = 9001;
    options.gameName = "SDK Test";
    options.port = 16769;
    options.autoStartLauncher = false;
    assert(client.initialize(options));
    assert(client.connected());

    const auto user = client.currentUser();
    assert(user.id == 77);
    assert(user.username == "sdk-test-user");

    IrAutoX::Presence presence;
    presence.playing = true;
    presence.details = "integration";
    presence.state = "test";
    presence.partySize = 1;
    presence.partyMax = 2;
    assert(client.setPresence(presence));
    assert(client.heartbeat());
    assert(client.clearPresence());
    client.shutdown();
    server.join();
#endif
    return 0;
}
