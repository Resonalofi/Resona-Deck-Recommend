#include "config.h"
#include "deck_recommend_service.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sekai_deck_recommend/native.h>

namespace {

std::atomic<httplib::Server*> runningServer = nullptr;

void stopServer(int) {
    if (auto* server = runningServer.load(std::memory_order_relaxed); server != nullptr)
        server->stop();
}

bool constantTimeEqual(const std::string& left, const std::string& right) {
    const auto size = std::max(left.size(), right.size());
    std::size_t difference = left.size() ^ right.size();
    for (std::size_t index = 0; index < size; ++index) {
        const auto a = index < left.size() ? static_cast<unsigned char>(left[index]) : 0;
        const auto b = index < right.size() ? static_cast<unsigned char>(right[index]) : 0;
        difference |= a ^ b;
    }
    return difference == 0;
}

void writeLogTime(std::ostream& out) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    out << std::put_time(&local, "%m-%d %H:%M:%S") << ' ';
}

bool authorize(const httplib::Request& request, httplib::Response& response, const Settings& settings) {
    const auto secret = request.get_header_value("X-Resona-Secret");
    if (constantTimeEqual(secret, settings.resonaSecret))
        return true;
    response.status = 403;
    response.set_content(R"({"detail":"Access Denied"})", "application/json");
    return false;
}

std::string toUpper(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    try {
        std::filesystem::path configPath = "config.toml";
        std::filesystem::path dataPath = "data";
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--config" && index + 1 < argc)
                configPath = argv[++index];
            else if (argument == "--data" && index + 1 < argc)
                dataPath = argv[++index];
            else
                throw std::invalid_argument("usage: resona-deck-recommend [--config PATH] [--data PATH]");
        }

        const auto settings = loadSettings(configPath);
        sekai_deck_recommend::initializeDataPath(dataPath.string());
        DeckRecommendService service(settings);

        httplib::Server server;
        const auto httpThreads = std::max<std::size_t>(4, settings.poolSize * 4);
        server.new_task_queue = [httpThreads] { return new httplib::ThreadPool(httpThreads); };

        server.Get("/healthz", [](const httplib::Request&, httplib::Response& response) {
            response.set_content("ok", "text/plain");
        });

        server.Post("/deck/recommend", [&](const httplib::Request& request, httplib::Response& response) {
            if (!authorize(request, response, settings))
                return;
            try {
                response.set_content(service.recommend(request.body).dump(), "application/json");
            }
            catch (const RequestError& error) {
                response.status = 422;
                response.set_content(
                    nlohmann::json({{"detail", error.what()}}).dump(),
                    "application/json"
                );
            }
            catch (const std::exception&) {
                response.status = 500;
                response.set_content(R"({"detail":"Internal Server Error"})", "application/json");
            }
        });

        server.Post(R"(/(jp|cn|tw)/cache/reload)", [&](const httplib::Request& request, httplib::Response& response) {
            if (!authorize(request, response, settings))
                return;
            const auto serverName = request.matches[1].str();
            service.reload(parseServer(serverName));
            writeLogTime(std::cout);
            std::cout << " [" << toUpper(serverName) << "] Master cache reloaded" << '\n';
            response.set_content(R"({"status":"ok"})", "application/json");
        });

        std::signal(SIGINT, stopServer);
        std::signal(SIGTERM, stopServer);
        runningServer.store(&server, std::memory_order_relaxed);
        writeLogTime(std::cout);
        std::cout << "Resona-Deck-Recommend listening on " << settings.host << ':' << settings.port << '\n';
        if (!server.listen(settings.host, settings.port))
            throw std::runtime_error("failed to listen on configured address");
        runningServer.store(nullptr, std::memory_order_relaxed);
        return 0;
    }
    catch (const std::exception& error) {
        writeLogTime(std::cerr);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
