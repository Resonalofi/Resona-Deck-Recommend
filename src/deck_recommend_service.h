#pragma once

#include "config.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <semaphore>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>
#include <sekai_deck_recommend/native.h>

class RequestError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DeckRecommendService {
public:
    explicit DeckRecommendService(Settings settings);

    nlohmann::json recommend(const std::string& requestBody);
    void reload(Server server);

private:
    struct State {
        std::shared_ptr<const sekai_deck_recommend::NativeEngine> engine;
        std::array<std::uint64_t, 3> generations{};
        std::array<std::string, 3> masterdataIdentities;
        std::array<std::string, 3> musicmetaIdentities;
    };

    std::shared_ptr<const State> stateFor(Server server);
    nlohmann::json buildOptions(
        nlohmann::json& request,
        Server server,
        const std::string& liveType,
        const std::string& target
    ) const;
    nlohmann::json buildResponse(nlohmann::json result, const std::string& target) const;

    Settings settings;
    std::counting_semaphore<> slots;
    std::shared_ptr<const State> loadState() const;
    void storeState(std::shared_ptr<const State> next);

    std::atomic<std::uint64_t> nextGeneration = 0;
    std::array<std::atomic<std::uint64_t>, 3> desiredGenerations;
    std::shared_ptr<const State> state;
    mutable std::mutex stateMutex;
    std::mutex loadMutex;
};
