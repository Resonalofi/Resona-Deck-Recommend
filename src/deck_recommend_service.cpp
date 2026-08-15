#include "deck_recommend_service.h"

#include "fetch.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <cctype>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

const std::array<Server, 3> servers = {Server::jp, Server::cn, Server::tw};

struct RequestContext {
    Server server;
    std::string liveType;
    std::string target;
    std::string userId;
};

class SlotPermit {
public:
    explicit SlotPermit(std::counting_semaphore<>& slots) : slots(slots) { slots.acquire(); }
    ~SlotPermit() { slots.release(); }

private:
    std::counting_semaphore<>& slots;
};

void requireType(
    const nlohmann::json& object,
    const char* key,
    nlohmann::json::value_t type,
    bool optional = false,
    bool nullable = false
) {
    const auto value = object.find(key);
    if (value == object.end()) {
        if (optional)
            return;
        throw RequestError(std::string("missing field: ") + key);
    }
    if (nullable && value->is_null())
        return;
    const auto matches = type == nlohmann::json::value_t::number_integer
        ? value->is_number_integer()
        : value->type() == type;
    if (!matches)
        throw RequestError(std::string("invalid field type: ") + key);
}

void validateCardConfig(const nlohmann::json& value, bool requireCardId) {
    if (!value.is_object())
        throw RequestError("card configuration must be an object");
    if (requireCardId)
        requireType(value, "card_id", nlohmann::json::value_t::number_integer);
    for (const auto* key : {
            "disable", "level_max", "episode_read", "master_max", "skill_max", "canvas"
        })
        requireType(value, key, nlohmann::json::value_t::boolean, true);
}

RequestContext validateRequest(const nlohmann::json& request) {
    if (!request.is_object())
        throw RequestError("request body must be an object");

    requireType(request, "server", nlohmann::json::value_t::string);
    requireType(request, "music_id", nlohmann::json::value_t::number_integer);
    requireType(request, "music_diff", nlohmann::json::value_t::string);
    requireType(request, "user_data", nlohmann::json::value_t::object);
    requireType(request, "live_type", nlohmann::json::value_t::string, true);
    requireType(request, "force_wl", nlohmann::json::value_t::boolean, true);
    requireType(request, "character_id", nlohmann::json::value_t::number_integer, true, true);
    requireType(request, "event_id", nlohmann::json::value_t::number_integer, true, true);
    requireType(request, "event_attr", nlohmann::json::value_t::string, true, true);
    requireType(request, "event_unit", nlohmann::json::value_t::string, true, true);
    requireType(request, "cal_tar", nlohmann::json::value_t::string, true);
    requireType(request, "real_skill", nlohmann::json::value_t::number_integer, true, true);
    requireType(
        request,
        "multi_live_teammate_power",
        nlohmann::json::value_t::number_integer,
        true
    );

    for (const auto* key : {"tar_bonus_list", "require_characters"}) {
        requireType(request, key, nlohmann::json::value_t::array, true);
        const auto values = request.find(key);
        if (values != request.end())
            for (const auto& value : *values)
                if (!value.is_number_integer())
                    throw RequestError(std::string("invalid array item: ") + key);
    }

    requireType(request, "require_cards", nlohmann::json::value_t::array, true);
    if (const auto values = request.find("require_cards"); values != request.end())
        for (const auto& value : *values)
            validateCardConfig(value, true);

    requireType(request, "card_config", nlohmann::json::value_t::object, true);
    if (const auto configs = request.find("card_config"); configs != request.end())
        for (const auto& [key, value] : configs->items()) {
            static_cast<void>(key);
            validateCardConfig(value, false);
        }

    requireType(request, "bonus_cards", nlohmann::json::value_t::object, true, true);
    if (const auto bonus = request.find("bonus_cards");
        bonus != request.end() && !bonus->is_null())
        for (const auto* key : {"force", "max_mr", "max_skill", "canvas"})
            requireType(*bonus, key, nlohmann::json::value_t::boolean, true);

    const auto liveType = request.value("live_type", std::string("multi"));
    if (liveType != "multi" && liveType != "solo" && liveType != "auto" &&
        liveType != "challenge")
        throw RequestError("invalid live_type: " + liveType);
    const auto target = request.value("cal_tar", std::string("score"));
    if (target != "score" && target != "power" && target != "skill" && target != "bonus")
        throw RequestError("invalid cal_tar: " + target);

    try {
        return {
            parseServer(request.at("server").get<std::string>()),
            liveType,
            target,
            request.value("user_id", std::string{}),
        };
    }
    catch (const std::invalid_argument& error) {
        throw RequestError(error.what());
    }
}

nlohmann::json cardConfig(const nlohmann::json& value) {
    return {
        {"disable", value.value("disable", false)},
        {"level_max", value.value("level_max", true)},
        {"episode_read", value.value("episode_read", true)},
        {"master_max", value.value("master_max", false)},
        {"skill_max", value.value("skill_max", false)},
        {"canvas", value.value("canvas", false)},
    };
}

nlohmann::json requiredCard(const nlohmann::json& value) {
    auto result = cardConfig(value);
    result["card_id"] = value.at("card_id").get<int>();
    return result;
}

void copyOptional(
    const nlohmann::json& source,
    nlohmann::json& target,
    const char* sourceKey,
    const char* targetKey = nullptr
) {
    const auto it = source.find(sourceKey);
    if (it != source.end() && !it->is_null())
        target[targetKey == nullptr ? sourceKey : targetKey] = *it;
}

}

DeckRecommendService::DeckRecommendService(Settings settings)
    : settings(std::move(settings)),
      slots(static_cast<std::ptrdiff_t>(this->settings.poolSize)) {
    for (auto& generation : desiredGenerations)
        generation.store(0, std::memory_order_relaxed);
    auto initial = std::make_shared<State>();
    initial->engine = std::make_shared<sekai_deck_recommend::NativeEngine>();
    initial->generations.fill(std::numeric_limits<std::uint64_t>::max());
    storeState(std::move(initial));
}

std::shared_ptr<const DeckRecommendService::State> DeckRecommendService::loadState() const {
    std::lock_guard lock(stateMutex);
    return state;
}

void DeckRecommendService::storeState(std::shared_ptr<const State> next) {
    std::lock_guard lock(stateMutex);
    state = std::move(next);
}

void DeckRecommendService::reload(Server server) {
    const auto generation = nextGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    desiredGenerations[serverIndex(server)].store(generation, std::memory_order_release);
}

std::shared_ptr<const DeckRecommendService::State> DeckRecommendService::stateFor(Server server) {
    const auto index = serverIndex(server);
    auto desired = desiredGenerations[index].load(std::memory_order_acquire);
    auto current = loadState();
    if (current->generations[index] == desired)
        return current;

    std::lock_guard lock(loadMutex);
    desired = desiredGenerations[index].load(std::memory_order_acquire);
    current = loadState();
    if (current->generations[index] == desired)
        return current;

    auto next = std::make_shared<State>(*current);
    auto engine = std::make_shared<sekai_deck_recommend::NativeEngine>(*current->engine);
    const auto masterIdentity = settings.masterdataIdentity(server);
    const auto musicIdentity = settings.musicmetaIdentity(server);

    std::optional<Server> sharedMaster;
    std::optional<Server> sharedMusic;
    for (const auto candidate : servers) {
        const auto candidateIndex = serverIndex(candidate);
        if (candidate == server || current->generations[candidateIndex] != desired)
            continue;
        if (current->masterdataIdentities[candidateIndex] == masterIdentity)
            sharedMaster = candidate;
        if (current->musicmetaIdentities[candidateIndex] == musicIdentity)
            sharedMusic = candidate;
    }

    std::map<std::string, std::string> masterdata;
    if (sharedMaster.has_value()) {
        masterdata.emplace(
            "honors",
            fetchWithFallback(settings.sourceFor(server, "honors"), "honors")
        );
        next->eventCards[index] = current->eventCards[serverIndex(*sharedMaster)];
    }
    else {
        for (const auto& key : sekai_deck_recommend::requiredMasterdataKeys())
            masterdata.emplace(key, fetchWithFallback(settings.sourceFor(server, key), key));
        for (const auto& key : sekai_deck_recommend::optionalMasterdataKeys())
            masterdata.emplace(key, fetchWithFallback(settings.sourceFor(server, key), key));
        next->eventCards[index] = std::make_shared<const nlohmann::json>(
            nlohmann::json::parse(masterdata.at("eventCards"))
        );
    }
    engine->updateMasterdata(
        std::move(masterdata),
        serverName(server),
        sharedMaster.has_value()
            ? std::optional<std::string>(serverName(*sharedMaster))
            : std::nullopt
    );

    if (sharedMusic.has_value()) {
        engine->updateMusicmetas("", serverName(server), serverName(*sharedMusic));
    }
    else {
        engine->updateMusicmetas(
            fetchWithFallback(settings.musicmetaSourceFor(server), "music_metas"),
            serverName(server)
        );
    }

    next->engine = std::move(engine);
    next->generations[index] = desired;
    next->masterdataIdentities[index] = masterIdentity;
    next->musicmetaIdentities[index] = musicIdentity;
    storeState(next);
    return next;
}

nlohmann::json DeckRecommendService::buildOptions(
    nlohmann::json& request,
    Server server,
    const std::string& liveType,
    const std::string& target,
    const State& loadedState
) const {
    nlohmann::json options = {
        {"target", target},
        {"region", serverName(server)},
        {"user_data", std::move(request.at("user_data"))},
        {"live_type", liveType},
        {"music_id", request.at("music_id").get<int>()},
        {"music_diff", request.at("music_diff").get<std::string>()},
        {"algorithms", target == "bonus"
            ? nlohmann::json::array({"dfs"})
            : nlohmann::json::array({"dfs", "ga"})},
        {"parallel_algorithms", target != "bonus"},
        {"timeout_ms", settings.defaultTimeoutMs},
    };

    const auto requiredCards = request.value("require_cards", nlohmann::json::array());
    const auto requiredCharacters = request.value("require_characters", nlohmann::json::array());
    nlohmann::json eventCardConfigs = nlohmann::json::array();
    const auto bonus = request.find("bonus_cards");
    if (bonus != request.end() && !bonus->is_null() && bonus->value("force", false)) {
        const auto eventId = request.find("event_id");
        for (const auto& eventCard : *loadedState.eventCards[serverIndex(server)]) {
            if (eventId == request.end() || eventId->is_null() ||
                eventCard.at("eventId").get<int>() != eventId->get<int>() ||
                eventCard.at("bonusRate").get<double>() != 20.0)
                continue;
            eventCardConfigs.push_back({
                {"card_id", eventCard.at("cardId").get<int>()},
                {"level_max", true},
                {"episode_read", true},
                {"master_max", bonus->value("max_mr", false)},
                {"skill_max", bonus->value("max_skill", false)},
                {"canvas", bonus->value("canvas", false)},
            });
        }
    }

    if (!eventCardConfigs.empty()) {
        options["single_card_configs"] = eventCardConfigs;
        options["fixed_cards"] = nlohmann::json::array();
        for (const auto& card : requiredCards)
            options["fixed_cards"].push_back(card.at("card_id"));
        for (const auto& card : eventCardConfigs)
            options["fixed_cards"].push_back(card.at("card_id"));
    }
    else if (!requiredCards.empty()) {
        options["single_card_configs"] = nlohmann::json::array();
        options["fixed_cards"] = nlohmann::json::array();
        for (const auto& card : requiredCards) {
            options["single_card_configs"].push_back(requiredCard(card));
            options["fixed_cards"].push_back(card.at("card_id"));
        }
    }
    else if (!requiredCharacters.empty()) {
        options["fixed_characters"] = requiredCharacters;
    }

    if (liveType == "multi") {
        copyOptional(request, options, "real_skill", "multi_live_teammate_score_up");
        options["multi_live_teammate_power"] = request.value("multi_live_teammate_power", 250000);
    }

    const auto targetBonuses = request.value("tar_bonus_list", std::vector<int>{});
    if (!targetBonuses.empty()) {
        options["target_bonus_list"] = targetBonuses;
        options["limit"] = std::max(1, 6 / static_cast<int>(targetBonuses.size()));
    }
    else {
        options["limit"] = 6;
        const auto& configs = request.at("card_config");
        options["rarity_1_config"] = cardConfig(configs.at("1"));
        options["rarity_2_config"] = cardConfig(configs.at("2"));
        options["rarity_3_config"] = cardConfig(configs.at("3"));
        options["rarity_4_config"] = cardConfig(configs.at("4"));
        options["rarity_birthday_config"] = cardConfig(configs.at("birthday"));
    }

    if (liveType == "challenge") {
        copyOptional(request, options, "character_id", "challenge_live_character_id");
        options.erase("fixed_characters");
    }
    else if (request.value("force_wl", false)) {
        copyOptional(request, options, "event_id");
        const auto character = request.find("character_id");
        if (character != request.end() && !character->is_null() && character->get<int>() != 0)
            options["world_bloom_character_id"] = *character;
        options["timeout_ms"] = settings.worldBloomTimeoutMs;
    }
    else {
        copyOptional(request, options, "event_id");
        copyOptional(request, options, "event_attr");
        copyOptional(request, options, "event_unit");
    }
    return options;
}

nlohmann::json DeckRecommendService::buildResponse(
    nlohmann::json result,
    const std::string& target
) const {
    auto decks = std::move(result.at("decks"));
    const auto greater = [&target](const nlohmann::json& left, const nlohmann::json& right) {
        if (target == "power")
            return left.at("total_power").get<int>() > right.at("total_power").get<int>();
        if (target == "skill")
            return left.at("multi_live_score_up").get<double>() >
                right.at("multi_live_score_up").get<double>();
        if (target == "bonus")
            return std::pair(left.at("event_bonus_rate").get<double>(), left.at("score").get<double>()) >
                std::pair(right.at("event_bonus_rate").get<double>(), right.at("score").get<double>());
        return std::pair(left.at("score").get<double>(), left.at("multi_live_score_up").get<double>()) >
            std::pair(right.at("score").get<double>(), right.at("multi_live_score_up").get<double>());
    };
    std::stable_sort(decks.begin(), decks.end(), greater);

    nlohmann::json responseDecks = nlohmann::json::array();
    const auto count = std::min<std::size_t>(6, decks.size());
    for (std::size_t index = 0; index < count; ++index) {
        auto& deck = decks[index];
        nlohmann::json cards = nlohmann::json::array();
        for (const auto& card : deck.at("cards")) {
            cards.push_back({
                {"card_id", card.at("card_id")},
                {"master_rank", card.at("master_rank")},
                {"skill_level", card.at("skill_level")},
                {"skill_score_up", card.at("skill_score_up")},
                {"default_image", card.at("default_image")},
                {"canvas", card.at("has_canvas_bonus")},
            });
        }
        std::string source;
        for (const auto& algorithm : deck.at("algorithms")) {
            if (!source.empty())
                source += '+';
            source += algorithm.get<std::string>();
        }
        responseDecks.push_back({
            {"total_power", deck.at("total_power")},
            {"score", deck.at("score").get<double>()},
            {"multi_live_score_up", deck.at("multi_live_score_up").get<double>()},
            {"cards", std::move(cards)},
            {"event_bonus_rate", deck.at("event_bonus_rate").get<double>()},
            {"support_deck_bonus_rate", deck.at("support_deck_bonus_rate").get<double>()},
            {"source", std::move(source)},
        });
    }

    nlohmann::json durations = nlohmann::json::object();
    for (const auto& [algorithm, milliseconds] : result.at("algorithm_ms").items())
        durations[algorithm] = milliseconds.get<double>() / 1000.0;
    return {
        {"decks", std::move(responseDecks)},
        {"durations", std::move(durations)},
    };
}

nlohmann::json DeckRecommendService::recommend(nlohmann::json request){
    const auto context = validateRequest(request);
    const auto started = std::chrono::steady_clock::now();
    SlotPermit permit(slots);
    const auto loadedState = stateFor(context.server);
    auto result = loadedState->engine->recommend(
        buildOptions(request, context.server, context.liveType, context.target, *loadedState));
    auto to_upper = [](std::string s)
    {
        for (char &c : s)
            c = std::toupper(static_cast<unsigned char>(c));
        return s;
    };

    std::cout << '[' << to_upper(serverName(context.server)) << ']';
    if (!context.userId.empty())
        std::cout << ' ' << context.userId;
    std::cout << " Recommend Request Cost";

    const char *sep = "";
    for (const auto &[algorithm, ms] : result.at("algorithm_ms").items())
    {
        std::cout << sep << ' ' << to_upper(algorithm) << ": " << ms.get<double>() / 1000.0 << "s";
        sep = ",";
    }
    std::cout << '\n';

    const auto engineSeconds = result.at("total_ms").get<double>() / 1000.0;
    auto response = buildResponse(std::move(result), context.target);
    response["queue_wait"] = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                 .count() -
                             engineSeconds;

    return response;
}