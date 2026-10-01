#include "HttpApiServer.h"

#include "ApiJson.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace {

std::string reasonPhrase(int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        case 101: return "Switching Protocols";
        default: return "OK";
    }
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

uint32_t leftRotate(uint32_t value, uint32_t bits) {
    return (value << bits) | (value >> (32U - bits));
}

std::array<uint8_t, 20> sha1(const std::string& input) {
    std::vector<uint8_t> data(input.begin(), input.end());
    uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8U;
    data.push_back(0x80U);
    while ((data.size() % 64U) != 56U) {
        data.push_back(0U);
    }
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((bitLength >> (i * 8)) & 0xffU));
    }

    uint32_t h0 = 0x67452301U;
    uint32_t h1 = 0xEFCDAB89U;
    uint32_t h2 = 0x98BADCFEU;
    uint32_t h3 = 0x10325476U;
    uint32_t h4 = 0xC3D2E1F0U;

    for (size_t chunk = 0; chunk < data.size(); chunk += 64U) {
        uint32_t w[80]{};
        for (size_t i = 0; i < 16U; ++i) {
            size_t j = chunk + i * 4U;
            w[i] = (static_cast<uint32_t>(data[j]) << 24U) |
                   (static_cast<uint32_t>(data[j + 1U]) << 16U) |
                   (static_cast<uint32_t>(data[j + 2U]) << 8U) |
                   static_cast<uint32_t>(data[j + 3U]);
        }
        for (size_t i = 16U; i < 80U; ++i) {
            w[i] = leftRotate(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (size_t i = 0; i < 80U; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20U) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999U;
            } else if (i < 40U) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            } else if (i < 60U) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }
            uint32_t temp = leftRotate(a, 5U) + f + e + k + w[i];
            e = d;
            d = c;
            c = leftRotate(b, 30U);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<uint8_t, 20> digest{};
    const uint32_t h[5] = {h0, h1, h2, h3, h4};
    for (size_t i = 0; i < 5U; ++i) {
        digest[i * 4U] = static_cast<uint8_t>((h[i] >> 24U) & 0xffU);
        digest[i * 4U + 1U] = static_cast<uint8_t>((h[i] >> 16U) & 0xffU);
        digest[i * 4U + 2U] = static_cast<uint8_t>((h[i] >> 8U) & 0xffU);
        digest[i * 4U + 3U] = static_cast<uint8_t>(h[i] & 0xffU);
    }
    return digest;
}

std::string base64Encode(const uint8_t* data, size_t size) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < size; i += 3U) {
        uint32_t value = static_cast<uint32_t>(data[i]) << 16U;
        if (i + 1U < size) {
            value |= static_cast<uint32_t>(data[i + 1U]) << 8U;
        }
        if (i + 2U < size) {
            value |= static_cast<uint32_t>(data[i + 2U]);
        }
        out.push_back(table[(value >> 18U) & 0x3fU]);
        out.push_back(table[(value >> 12U) & 0x3fU]);
        out.push_back(i + 1U < size ? table[(value >> 6U) & 0x3fU] : '=');
        out.push_back(i + 2U < size ? table[value & 0x3fU] : '=');
    }
    return out;
}

} // namespace

HttpApiServer::HttpApiServer(ApiServer& api, std::string host, unsigned short port, std::string corsAllowedOrigin)
    : api_(api), host_(std::move(host)), port_(port), corsAllowedOrigin_(std::move(corsAllowedOrigin)),
      broadcaster_(api_.outboundQueue()) {
    broadcaster_.addSink([this](const OutboundMessage& message) {
        broadcastToClients(message);
    });
}

HttpApiServer::~HttpApiServer() {
    stop();
}

void HttpApiServer::start() {
    if (running_.exchange(true)) {
        return;
    }
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        running_.store(false);
        throw std::runtime_error("WSAStartup failed");
    }
#endif
    listenSocket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == INVALID_SOCKET) {
        running_.store(false);
        throw std::runtime_error("socket creation failed");
    }

    int reuse = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    if (host_.empty() || host_ == "0.0.0.0") {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        address.sin_addr.s_addr = inet_addr(host_.c_str());
    }

    if (::bind(listenSocket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closeSocket(listenSocket_);
        running_.store(false);
        throw std::runtime_error("bind failed");
    }

    sockaddr_in boundAddress{};
#ifdef _WIN32
    int boundLength = sizeof(boundAddress);
#else
    socklen_t boundLength = sizeof(boundAddress);
#endif
    if (::getsockname(listenSocket_, reinterpret_cast<sockaddr*>(&boundAddress), &boundLength) == 0) {
        port_ = ntohs(boundAddress.sin_port);
    }

    if (::listen(listenSocket_, 16) == SOCKET_ERROR) {
        closeSocket(listenSocket_);
        running_.store(false);
        throw std::runtime_error("listen failed");
    }

    broadcaster_.start();
    acceptThread_ = std::thread(&HttpApiServer::acceptLoop, this);
}

void HttpApiServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    closeSocket(listenSocket_);
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    broadcaster_.stop();
    std::vector<ApiSocket> clients;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients.swap(webSocketClients_);
    }
    for (ApiSocket client : clients) {
        closeSocket(client);
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

bool HttpApiServer::isRunning() const {
    return running_.load();
}

const std::string& HttpApiServer::host() const {
    return host_;
}

unsigned short HttpApiServer::port() const {
    return port_;
}

void HttpApiServer::acceptLoop() {
    while (running_.load()) {
        ApiSocket client = ::accept(listenSocket_, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            continue;
        }
        std::thread(&HttpApiServer::handleClient, this, client).detach();
    }
}

void HttpApiServer::handleClient(ApiSocket client) {
    std::string raw;
    char buffer[4096];
    while (raw.find("\r\n\r\n") == std::string::npos) {
        int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            closeSocket(client);
            return;
        }
        raw.append(buffer, buffer + received);
        if (raw.size() > 65536U) {
            closeSocket(client);
            return;
        }
    }

    size_t headerEnd = raw.find("\r\n\r\n");
    size_t contentLength = 0;
    std::string headers = lowerCopy(raw.substr(0, headerEnd));
    size_t lengthPos = headers.find("content-length:");
    if (lengthPos != std::string::npos) {
        size_t valueStart = lengthPos + std::string("content-length:").size();
        while (valueStart < headers.size() && std::isspace(static_cast<unsigned char>(headers[valueStart]))) {
            ++valueStart;
        }
        contentLength = static_cast<size_t>(std::stoul(headers.substr(valueStart)));
    }
    while (raw.size() < headerEnd + 4U + contentLength) {
        int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            closeSocket(client);
            return;
        }
        raw.append(buffer, buffer + received);
    }

    HttpRequest request = parseRequest(raw.substr(0, headerEnd + 4U + contentLength));
    if (request.upgradeWebSocket && request.path == "/ws") {
        handleWebSocket(client, request);
        return;
    }

    int statusCode = 200;
    std::string body = route(request, statusCode);
    sendAll(client, buildHttpResponse(statusCode, body));
    closeSocket(client);
}

HttpApiServer::HttpRequest HttpApiServer::parseRequest(const std::string& raw) const {
    HttpRequest request;
    size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos) {
        return request;
    }
    std::istringstream start(raw.substr(0, lineEnd));
    start >> request.method >> request.path;
    size_t headerEnd = raw.find("\r\n\r\n");
    std::string headerBlock = raw.substr(lineEnd + 2U, headerEnd - lineEnd - 2U);
    std::istringstream headers(headerBlock);
    std::string line;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string name = lowerCopy(line.substr(0, colon));
        std::string value = line.substr(colon + 1U);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        if (name == "sec-websocket-key") {
            request.webSocketKey = value;
        } else if (name == "upgrade" && lowerCopy(value) == "websocket") {
            request.upgradeWebSocket = true;
        }
    }
    if (headerEnd != std::string::npos) {
        request.body = raw.substr(headerEnd + 4U);
    }
    return request;
}

std::string HttpApiServer::route(const HttpRequest& request, int& statusCode) const {
    if (request.method == "OPTIONS") {
        statusCode = 200;
        return "{}";
    }
    if (request.method == "GET" && (request.path == "/health" || request.path == "/ready")) {
        statusCode = 200;
        return ApiJson::serializeHealth(api_.isEngineRunning());
    }
    if (request.method == "GET" && request.path == "/orderbook") {
        statusCode = 200;
        return ApiJson::serializeOrderbook(api_.getOrderbook());
    }
    if (request.method == "GET" && request.path == "/trades") {
        statusCode = 200;
        return ApiJson::serializeTrades(api_.getRecentTrades());
    }
    if (request.method == "GET" && request.path == "/orders") {
        statusCode = 200;
        return ApiJson::serializeOrderStatuses(api_.getAllOrders());
    }
    if (request.method == "GET" && request.path.rfind("/orders/", 0) == 0) {
        std::string id = request.path.substr(std::string("/orders/").size());
        std::optional<ApiOrderStatus> status;
        try {
            status = api_.getOrderStatus(std::stoi(id));
        } catch (...) {
            status = api_.getOrderStatus(id);
        }
        if (!status) {
            statusCode = 404;
            return ApiJson::serializeError(404, "Order not found");
        }
        statusCode = 200;
        return ApiJson::serializeOrderStatus(*status);
    }
    if (request.method == "POST" && request.path == "/orders") {
        std::string error;
        auto parsed = ApiJson::parseNewOrderRequest(request.body, error);
        if (!parsed) {
            statusCode = 400;
            return ApiJson::serializeError(400, error);
        }
        ApiResponse response = api_.submitOrder(*parsed);
        statusCode = response.httpStatus;
        return ApiJson::serializeResponse(response);
    }
    if (request.method == "DELETE" && request.path == "/orders") {
        std::string error;
        auto parsed = ApiJson::parseCancelOrderRequest(request.body, error);
        if (!parsed) {
            statusCode = 400;
            return ApiJson::serializeError(400, error);
        }
        ApiResponse response = api_.cancelOrder(*parsed);
        statusCode = response.httpStatus;
        return ApiJson::serializeResponse(response);
    }
    if ((request.method == "PATCH" || request.method == "PUT") && request.path == "/orders") {
        std::string error;
        auto parsed = ApiJson::parseModifyOrderRequest(request.body, error);
        if (!parsed) {
            statusCode = 400;
            return ApiJson::serializeError(400, error);
        }
        ApiResponse response = api_.modifyOrder(*parsed);
        statusCode = response.httpStatus;
        return ApiJson::serializeResponse(response);
    }

    statusCode = 404;
    return ApiJson::serializeError(404, "Route not found");
}

std::string HttpApiServer::buildHttpResponse(int statusCode, const std::string& body) const {
    std::ostringstream out;
    out << "HTTP/1.1 " << statusCode << ' ' << reasonPhrase(statusCode) << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Access-Control-Allow-Origin: " << corsAllowedOrigin_ << "\r\n"
        << "Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

void HttpApiServer::handleWebSocket(ApiSocket client, const HttpRequest& request) {
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << webSocketAcceptKey(request.webSocketKey) << "\r\n\r\n";
    if (!sendAll(client, response.str())) {
        closeSocket(client);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        webSocketClients_.push_back(client);
    }
}

void HttpApiServer::broadcastToClients(const OutboundMessage& message) {
    std::string frame = webSocketTextFrame(message.payload);
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = webSocketClients_.begin();
    while (it != webSocketClients_.end()) {
        if (sendAll(*it, frame)) {
            ++it;
        } else {
            closeSocket(*it);
            it = webSocketClients_.erase(it);
        }
    }
}

bool HttpApiServer::sendAll(ApiSocket socket, const std::string& data) {
    size_t sentTotal = 0;
    while (sentTotal < data.size()) {
        int sent = send(socket, data.data() + sentTotal, static_cast<int>(data.size() - sentTotal), 0);
        if (sent <= 0) {
            return false;
        }
        sentTotal += static_cast<size_t>(sent);
    }
    return true;
}

void HttpApiServer::closeSocket(ApiSocket socket) {
    if (socket == INVALID_SOCKET) {
        return;
    }
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
    closesocket(socket);
#else
    shutdown(socket, SHUT_RDWR);
    close(socket);
#endif
}

std::string HttpApiServer::webSocketAcceptKey(const std::string& clientKey) {
    static const std::string guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    auto digest = sha1(clientKey + guid);
    return base64Encode(digest.data(), digest.size());
}

std::string HttpApiServer::webSocketTextFrame(const std::string& payload) {
    std::string frame;
    frame.push_back(static_cast<char>(0x81));
    if (payload.size() <= 125U) {
        frame.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 65535U) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((payload.size() >> 8U) & 0xffU));
        frame.push_back(static_cast<char>(payload.size() & 0xffU));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((payload.size() >> (i * 8)) & 0xffU));
        }
    }
    frame += payload;
    return frame;
}
