#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class Server : std::uint8_t {
    jp,
    cn,
    tw,
};

struct FetchSource {
    std::filesystem::path location;
    std::vector<std::string> fallback;
};

struct MasterdataServer : FetchSource {
    std::filesystem::path musicmeta;
    std::vector<std::string> musicmetaFallback;
    bool masterdataOverride = false;
};

struct Settings {
    std::string host = "127.0.0.1";
    std::uint16_t port = 23457;
    std::string resonaSecret;
    std::size_t poolSize = 1;
    int defaultTimeoutMs = 8000;
    int worldBloomTimeoutMs = 12000;
    std::array<MasterdataServer, 3> masterdata;

    const MasterdataServer& masterdataFor(Server server) const;
    FetchSource sourceFor(Server server, const std::string& key) const;
    FetchSource musicmetaSourceFor(Server server) const;
    std::string masterdataIdentity(Server server) const;
    std::string musicmetaIdentity(Server server) const;
};

Settings loadSettings(const std::filesystem::path& path);
Server parseServer(const std::string& value);
std::string serverName(Server server);
std::size_t serverIndex(Server server);
