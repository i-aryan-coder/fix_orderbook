#include "ApiServer.h"
#include "HttpApiServer.h"
#include "MatchingEngine.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t shutdownRequested = 0;

void handleShutdownSignal(int) {
    shutdownRequested = 1;
}

std::string nowIsoLike() {
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif
    std::ostringstream out;
    out << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S");
    return out.str();
}

void logInfo(const std::string& message) {
    std::cout << "[INFO] " << nowIsoLike() << ' ' << message << std::endl;
}

void logError(const std::string& message) {
    std::cerr << "[ERROR] " << nowIsoLike() << ' ' << message << std::endl;
}

std::string readEnvString(const char* name, const std::string& defaultValue) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return defaultValue;
    }
    return value;
}

unsigned short parsePort(const char* name, const char* value, unsigned short defaultValue) {
    try {
        int parsed = std::stoi(value);
        if (parsed <= 0 || parsed > 65535) {
            throw std::out_of_range("port outside valid range");
        }
        return static_cast<unsigned short>(parsed);
    } catch (const std::exception& e) {
        logError(std::string("Invalid ") + name + "='" + value + "' (" + e.what() + "), using " +
                 std::to_string(defaultValue));
        return defaultValue;
    }
}

unsigned short readRuntimePort(unsigned short defaultValue) {
    const char* renderPort = std::getenv("PORT");
    if (renderPort != nullptr && !std::string(renderPort).empty()) {
        return parsePort("PORT", renderPort, defaultValue);
    }

    const char* apiPort = std::getenv("API_PORT");
    if (apiPort != nullptr && !std::string(apiPort).empty()) {
        return parsePort("API_PORT", apiPort, defaultValue);
    }

    return defaultValue;
}

} // namespace

int main() {
    std::signal(SIGINT, handleShutdownSignal);
    std::signal(SIGTERM, handleShutdownSignal);

    const std::string host = readEnvString("API_HOST", "0.0.0.0");
    const unsigned short port = readRuntimePort(8080);
    const std::string corsAllowedOrigin = readEnvString("CORS_ALLOWED_ORIGIN", "*");

    MatchingEngine engine;
    ApiServer api(engine);
    HttpApiServer server(api, host, port, corsAllowedOrigin);

    engine.start();
    logInfo("Matching engine started");

    try {
        server.start();
        logInfo("API server listening host=" + server.host() + " port=" + std::to_string(server.port()));
        logInfo("REST endpoints=/health,/ready,/orders,/orderbook,/trades websocket=/ws");
        logInfo("Shutdown with SIGINT or SIGTERM");

        while (!shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        logInfo("Shutdown initiated");
        server.stop();
        logInfo("API server stopped");
        engine.stop(true);
        logInfo("Matching engine stopped");
    } catch (const std::exception& e) {
        logError(std::string("API server failed: ") + e.what());
        server.stop();
        engine.stop(true);
        return 1;
    }
    return 0;
}
