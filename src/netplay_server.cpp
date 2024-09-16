#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#if defined(__unix__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#elif defined(_WIN32)
#error "Not implemented"
#endif

#include "netplay_packet.hpp"

/*

Normally online peer-to-peer multiplayer would require port-forwarding. The router would refuse connections established
on closed ports. This can be circumvented by a concept referred to as NAT/UDP/TCP holepunching.

Essentially, there's still a server, but it only acts to bring people together, and then they peer to peer like normal.
The way it works is the following:

A host tells the server it wants to create a game room. The server remembers this game room and gives the host a code.
The host can give this code to people they want to invite.
The guests tell the server they want to connect to a code. The server then gets the public ip & port combo both the host
and guests used to connect to the server. The server then sends this combo to each of the clients.
The clients now know each other client's ip:port. They send a few garbage packets to all of them.

client A ---garbage--> client B
client A <--garbage--- client B

Because the firewall doesn't block a UDP packet if the client has recently sent one to that ip, after sending a few garbage
packets, the clients can now freely communicate with each other. They just have to send a keep alive packet every so often
so that the firewall remembers them.

This file implements the server part of the deal.

*/

// The server hosts a tcp socket on user port 27578, which is 161144 in hex,
// 16 1 14 4 being the indices of the letters in the word PAND
constexpr static int netplayServerPort = 27578;

static int netplayServerSocket;

constexpr static std::string_view serverCodeCharset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
constexpr static size_t serverCodeLength = 16;

// The netplay server wasn't really built with huge scalability in mind, you'd use something like
// epoll or kqueue for that. We don't expect more than 200 servers at once, and we wouldn't
// want to overload our server anyway.
constexpr static int maxServers = 200;

struct ClientState {
    int socket;
    char username[32];
    u32 ipv4Address;
    u16 port;
};

struct ServerState {
    ~ServerState() {
        close(socket);
        printf("Server %d:%d with code %s destroyed\n", ipv4Address, port, serverCode);
    }

    int socket;
    u32 ipv4Address;
    u16 port;
    char serverCode[serverCodeLength + 1];
    std::atomic_bool killed = {false};
    std::chrono::time_point<std::chrono::steady_clock> lastMessage = {};
    std::array<ClientState, 4> clients = {};
    std::string chat;
    std::jthread thread;
};

static std::mutex serverMutex = {};
static std::vector<std::shared_ptr<ServerState>> servers;

void serverError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    printf("Errno: %d\n", errno);
    exit(1);
}

// The watchdog thread will check every so often for dead connections and remove them.
// This is necessary because it could be the case the client crashes or quits without
// properly closing the connection.
void watchdogThread() {
    printf("Started watchdog thread\n");
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        std::lock_guard<std::mutex> lock(serverMutex);
        auto now = std::chrono::steady_clock::now();
        auto it = servers.begin();
        while (it != servers.end()) {
            if (now - (*it)->lastMessage > std::chrono::seconds(30)) {
                printf("Server %s:%d timed out\n", (*it)->serverCode, (*it)->port);
                (*it)->killed = true;
                servers.erase(it);
            } else {
                it++;
            }
        }
    }
}

void serverThread(std::shared_ptr<ServerState> server) {
    char buffer[16384];
    while (!server->killed) {
        int bytesRead = recv(server->socket, buffer, sizeof(buffer), 0);

        if (bytesRead == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                serverError("Failed to recv from server\n");
            }
        }
    }

    printf("Server %s:%d thread exiting\n", server->serverCode, server->port);

    std::lock_guard<std::mutex> lock(serverMutex);
    auto it = servers.begin();
    while (it != servers.end()) {
        int result = memcmp((*it)->serverCode, server->serverCode, serverCodeLength);
        if (result == 0) {
            servers.erase(it);
            break;
        }
    }

    // At this point, the thread main loop has finished, and jthread will join the thread
}

void startNetplayServer() {
    netplayServerSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (netplayServerSocket == -1) {
        serverError("Failed to create server socket\n");
    }
    
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(netplayServerPort);

    if (bind(netplayServerSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        serverError("Failed to bind server socket\n");
    }

    if (listen(netplayServerSocket, 1) == -1) {
        serverError("Failed to listen on server socket\n");
    }

    printf("Listening for connections on port %d\n", netplayServerPort);

    std::thread watchdog(watchdogThread);

    while (true) {
        int clientSocket = accept(netplayServerSocket, nullptr, nullptr);
        if (clientSocket == -1) {
            serverError("Failed to accept client connection\n");
        }

        // Get ip:port of client
        struct sockaddr_in clientAddress;
        socklen_t clientAddressSize = sizeof(clientAddress);
        if (getpeername(clientSocket, (struct sockaddr*)&clientAddress, &clientAddressSize) == -1) {
            serverError("Failed to get client address\n");
        }

        char newServerIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddress.sin_addr, newServerIp, INET_ADDRSTRLEN);
        int newServerPort = ntohs(clientAddress.sin_port);

        printf("Client connected from %s:%d\n", newServerIp, newServerPort);

        std::lock_guard<std::mutex> lock(serverMutex);

        // Check if this ip is already hosting a server
        bool allowedToHost = true;
        for (const std::shared_ptr<ServerState>& server : servers) {
            if (server->ipv4Address == clientAddress.sin_addr.s_addr) {
                printf("Client is already hosting a server\n");
                break;
            }
        }

        if (!allowedToHost || servers.size() >= maxServers) {
            close(clientSocket);
            continue;
        }

        std::shared_ptr<ServerState> newServer = std::make_shared<ServerState>();

        // Generate a new server code
        for (size_t i = 0; i < serverCodeLength; i++) {
            newServer->serverCode[i] = serverCodeCharset[rand() % serverCodeCharset.length()];
        }
        newServer->serverCode[serverCodeLength] = '\0';

        newServer->ipv4Address = clientAddress.sin_addr.s_addr;
        newServer->port = newServerPort;
        newServer->lastMessage = std::chrono::steady_clock::now();

        // Set a timeout on recv so that the watchdog can eventually kill the server
        // if it stops sending messages
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
            serverError("Failed to set socket timeout\n");
        }

        newServer->socket = clientSocket;
        newServer->thread = std::jthread(serverThread, newServer);

        servers.push_back(newServer);
    }
}

int main() {
    startNetplayServer();
    return 0;
}