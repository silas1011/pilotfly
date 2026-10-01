#include "app/Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace pilotfly {

namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::optional<double> parseNumber(const std::string& text) {
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}

std::optional<StopKey> parseStopKey(const std::string& text) {
    const std::string name = lower(trim(text));
    if (name == "pause") {
        return StopKey{StopKeyKind::Pause, 0};
    }
    if (name == "scrolllock") {
        return StopKey{StopKeyKind::ScrollLock, 0};
    }
    if (name.size() >= 2 && name[0] == 'f') {
        const auto number = parseNumber(name.substr(1));
        if (number && *number >= 1 && *number <= 12 && *number == static_cast<int>(*number)) {
            return StopKey{StopKeyKind::Function, static_cast<int>(*number)};
        }
    }
    return std::nullopt;
}

std::string stopKeyName(const StopKey& key) {
    switch (key.kind) {
        case StopKeyKind::Pause:
            return "Pause";
        case StopKeyKind::ScrollLock:
            return "ScrollLock";
        case StopKeyKind::Function:
            return "F" + std::to_string(key.functionNumber);
    }
    return "Pause";
}

Config parseConfig(const std::string& text) {
    Config config;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = lower(trim(line.substr(0, separator)));
        const std::string value = trim(line.substr(separator + 1));
        if (key == "stop_key") {
            if (const auto stopKey = parseStopKey(value)) {
                config.stopKey = *stopKey;
            }
        } else if (key == "vjoy_device") {
            const auto number = parseNumber(value);
            if (number && *number >= 1 && *number <= 16) {
                config.vjoyDevice = static_cast<int>(*number);
            }
        } else if (key == "rate_hz") {
            const auto number = parseNumber(value);
            if (number && *number >= 10 && *number <= 1000) {
                config.rateHz = *number;
            }
        } else if (key == "game_window") {
            if (!value.empty()) {
                config.gameWindow = value;
            }
        } else if (key == "brain_file") {
            if (!value.empty()) {
                config.brainFile = value;
            }
        }
    }
    return config;
}

Config loadConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return Config{};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseConfig(buffer.str());
}

}
