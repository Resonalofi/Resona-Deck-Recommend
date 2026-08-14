#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>
#include <sekai_deck_recommend/native.h>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("failed to open " + path.string());
    const auto size = file.tellg();
    if (size < 0)
        throw std::runtime_error("failed to determine size of " + path.string());
    std::string data(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(data.data(), size);
    if (!file)
        throw std::runtime_error("failed to read " + path.string());
    return data;
}

}

int main(int argc, char** argv) {
    try {
        if (argc != 7)
            throw std::invalid_argument(
                "usage: resona-native-core-runner REGION MASTERDATA HONORS MUSICMETAS DATA REQUEST"
            );

        const std::string region = argv[1];
        const std::filesystem::path masterdataPath = argv[2];
        const std::filesystem::path honorsPath = argv[3];
        const std::filesystem::path musicmetasPath = argv[4];
        sekai_deck_recommend::initializeDataPath(argv[5]);

        std::map<std::string, std::string> masterdata;
        for (const auto& key : sekai_deck_recommend::requiredMasterdataKeys())
            masterdata.emplace(key, readFile(masterdataPath / (key + ".json")));
        for (const auto& key : sekai_deck_recommend::optionalMasterdataKeys())
            masterdata.emplace(key, readFile(masterdataPath / (key + ".json")));
        masterdata["honors"] = readFile(honorsPath);

        sekai_deck_recommend::NativeEngine engine;
        engine.updateMasterdata(std::move(masterdata), region);
        engine.updateMusicmetas(readFile(musicmetasPath), region);
        nlohmann::json options;
        if (std::string(argv[6]) == "-")
            std::cin >> options;
        else
            options = nlohmann::json::parse(readFile(argv[6]));
        std::cout << engine.recommend(options).dump();
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
