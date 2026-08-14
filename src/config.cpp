#include "config.h"

#include <limits>
#include <sstream>
#include <stdexcept>

#include <toml++/toml.hpp>

namespace {

MasterdataServer loadMasterdataServer(const toml::table& root, const char* name) {
    const auto* table = root["masterdata"][name].as_table();
    if (table == nullptr)
        throw std::runtime_error(std::string("missing [masterdata.") + name + "]");

    MasterdataServer result;
    result.location = table->at_path("location").value_or(std::string{});
    result.musicmeta = table->at_path("musicmeta").value_or(std::string{});
    result.masterdataOverride = table->at_path("masterdata_override").value_or(false);

    if (const auto* fallback = table->at_path("fallback").as_array())
        for (const auto& value : *fallback)
            result.fallback.push_back(value.value_or(std::string{}));
    if (const auto* fallback = table->at_path("musicmeta_fallback").as_array())
        for (const auto& value : *fallback)
            result.musicmetaFallback.push_back(value.value_or(std::string{}));
    return result;
}

}

const MasterdataServer& Settings::masterdataFor(Server server) const {
    return masterdata[serverIndex(server)];
}

FetchSource Settings::sourceFor(Server server, const std::string& key) const {
    if (key == "honors") {
        const auto& source = masterdataFor(server);
        return {source.location, source.fallback};
    }
    const auto& source = masterdataFor(masterdataFor(Server::jp).masterdataOverride ? Server::jp : server);
    return {source.location, source.fallback};
}

FetchSource Settings::musicmetaSourceFor(Server server) const {
    const auto& source = masterdataFor(masterdataFor(Server::jp).masterdataOverride ? Server::jp : server);
    return {source.musicmeta, source.musicmetaFallback};
}

std::string Settings::masterdataIdentity(Server server) const {
    std::ostringstream identity;
    const auto& source = sourceFor(server, "cards");
    identity << source.location.string();
    for (const auto& fallback : source.fallback)
        identity << '\n' << fallback;
    return identity.str();
}

std::string Settings::musicmetaIdentity(Server server) const {
    std::ostringstream identity;
    const auto source = musicmetaSourceFor(server);
    identity << source.location.string();
    for (const auto& fallback : source.fallback)
        identity << '\n' << fallback;
    return identity.str();
}

Settings loadSettings(const std::filesystem::path& path) {
    const auto config = toml::parse_file(path.string());
    Settings settings;
    settings.host = config.at_path("runtime.host").value_or(settings.host);
    const auto port = config.at_path("runtime.port").value_or(
        static_cast<std::int64_t>(settings.port)
    );
    if (port < 1 || port > 65535)
        throw std::runtime_error("runtime.port must be between 1 and 65535");
    settings.port = static_cast<std::uint16_t>(port);
    settings.resonaSecret = config.at_path("auth.resona_secret").value_or(std::string{});
    const auto poolSize = config.at_path("worker.pool_size").value_or(
        static_cast<std::int64_t>(settings.poolSize)
    );
    if (poolSize < 1 || static_cast<std::uint64_t>(poolSize) >
        static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max() / 4))
        throw std::runtime_error("worker.pool_size is out of range");
    settings.poolSize = static_cast<std::size_t>(poolSize);

    const auto defaultTimeout = config.at_path("worker.default_timeout_ms").value_or(
        static_cast<std::int64_t>(settings.defaultTimeoutMs)
    );
    const auto worldBloomTimeout = config.at_path("worker.wl_timeout_ms").value_or(
        static_cast<std::int64_t>(settings.worldBloomTimeoutMs)
    );
    if (defaultTimeout < 1 || defaultTimeout > std::numeric_limits<int>::max() ||
        worldBloomTimeout < 1 || worldBloomTimeout > std::numeric_limits<int>::max())
        throw std::runtime_error("worker timeouts are out of range");
    settings.defaultTimeoutMs = static_cast<int>(defaultTimeout);
    settings.worldBloomTimeoutMs = static_cast<int>(worldBloomTimeout);
    settings.masterdata = {
        loadMasterdataServer(config, "jp"),
        loadMasterdataServer(config, "cn"),
        loadMasterdataServer(config, "tw"),
    };
    if (settings.resonaSecret.empty())
        throw std::runtime_error("auth.resona_secret is required");
    return settings;
}

Server parseServer(const std::string& value) {
    if (value == "jp")
        return Server::jp;
    if (value == "cn")
        return Server::cn;
    if (value == "tw")
        return Server::tw;
    throw std::invalid_argument("invalid server: " + value);
}

std::string serverName(Server server) {
    switch (server) {
    case Server::jp:
        return "jp";
    case Server::cn:
        return "cn";
    case Server::tw:
        return "tw";
    }
    throw std::invalid_argument("invalid server value");
}

std::size_t serverIndex(Server server) {
    return static_cast<std::size_t>(server);
}
