#pragma once

#include <optional>
#include <string>

namespace pilotfly {

enum class StopKeyKind { Pause, ScrollLock, Function };

struct StopKey {
    StopKeyKind kind = StopKeyKind::Pause;
    int functionNumber = 0;

    bool operator==(const StopKey&) const = default;
};

struct Config {
    StopKey stopKey;
    int vjoyDevice = 1;
    double rateHz = 100.0;
};

std::optional<StopKey> parseStopKey(const std::string& text);
std::string stopKeyName(const StopKey& key);
Config parseConfig(const std::string& text);
Config loadConfig(const std::string& path);

}
