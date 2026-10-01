#ifndef HTTPAPISERVER_H
#define HTTPAPISERVER_H

#include "ApiServer.h"
#include "WebSocketBroadcaster.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
using ApiSocket = SOCKET;
#else
using ApiSocket = int;
#endif

class HttpApiServer {
public:
    HttpApiServer(ApiServer& api, std::string host = "127.0.0.1", unsigned short port = 8080,
                  std::string corsAllowedOrigin = "*");
    ~HttpApiServer();

    HttpApiServer(const HttpApiServer&) = delete;
    HttpApiServer& operator=(const HttpApiServer&) = delete;

    void start();
    void stop();
    bool isRunning() const;
    const std::string& host() const;
    unsigned short port() const;

private:
    struct HttpRequest {
        std::string method;
        std::string path;
        std::string body;
        std::string webSocketKey;
        bool upgradeWebSocket{false};
    };

    void acceptLoop();
    void handleClient(ApiSocket client);
    void handleWebSocket(ApiSocket client, const HttpRequest& request);
    HttpRequest parseRequest(const std::string& raw) const;
    std::string route(const HttpRequest& request, int& statusCode) const;
    std::string buildHttpResponse(int statusCode, const std::string& body) const;
    void broadcastToClients(const OutboundMessage& message);

    static bool sendAll(ApiSocket socket, const std::string& data);
    static void closeSocket(ApiSocket socket);
    static std::string webSocketAcceptKey(const std::string& clientKey);
    static std::string webSocketTextFrame(const std::string& payload);

    ApiServer& api_;
    std::string host_;
    unsigned short port_;
    std::string corsAllowedOrigin_;
    ApiSocket listenSocket_{static_cast<ApiSocket>(~0ULL)};
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    WebSocketBroadcaster broadcaster_;
    mutable std::mutex clientsMutex_;
    std::vector<ApiSocket> webSocketClients_;
};

#endif // HTTPAPISERVER_H
