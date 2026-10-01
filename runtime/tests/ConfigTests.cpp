#include <catch2/catch_test_macros.hpp>

#include "app/Config.h"

using namespace pilotfly;

TEST_CASE("stop key names are parsed") {
    REQUIRE(parseStopKey("Pause") == StopKey{StopKeyKind::Pause, 0});
    REQUIRE(parseStopKey(" scrolllock ") == StopKey{StopKeyKind::ScrollLock, 0});
    REQUIRE(parseStopKey("F9") == StopKey{StopKeyKind::Function, 9});
    REQUIRE(parseStopKey("f12") == StopKey{StopKeyKind::Function, 12});
    REQUIRE_FALSE(parseStopKey("F13").has_value());
    REQUIRE_FALSE(parseStopKey("F0").has_value());
    REQUIRE_FALSE(parseStopKey("banana").has_value());
    REQUIRE_FALSE(parseStopKey("").has_value());
}

TEST_CASE("stop key names round trip") {
    REQUIRE(stopKeyName(StopKey{StopKeyKind::Pause, 0}) == "Pause");
    REQUIRE(stopKeyName(StopKey{StopKeyKind::Function, 9}) == "F9");
    REQUIRE(parseStopKey(stopKeyName(StopKey{StopKeyKind::ScrollLock, 0})) == StopKey{StopKeyKind::ScrollLock, 0});
}

TEST_CASE("an empty config uses the defaults") {
    const Config config = parseConfig("");
    REQUIRE(config.stopKey == StopKey{StopKeyKind::Pause, 0});
    REQUIRE(config.vjoyDevice == 1);
    REQUIRE(config.rateHz == 100.0);
    REQUIRE(config.gameWindow == "Uncrashed");
    REQUIRE(config.brainFile == "brain.onnx");
}

TEST_CASE("the game window and brain file are read") {
    const Config config = parseConfig("game_window = Uncrashed : FPV Drone Simulator \nBRAIN_FILE=C:/brains/My Fly.onnx\n");
    REQUIRE(config.gameWindow == "Uncrashed : FPV Drone Simulator");
    REQUIRE(config.brainFile == "C:/brains/My Fly.onnx");
}

TEST_CASE("empty game window and brain file keep the defaults") {
    const Config config = parseConfig("game_window =\nbrain_file =   \n");
    REQUIRE(config.gameWindow == "Uncrashed");
    REQUIRE(config.brainFile == "brain.onnx");
}

TEST_CASE("config values are read") {
    const Config config = parseConfig("stop_key = F9\nvjoy_device=2\n rate_hz = 250 \n");
    REQUIRE(config.stopKey == StopKey{StopKeyKind::Function, 9});
    REQUIRE(config.vjoyDevice == 2);
    REQUIRE(config.rateHz == 250.0);
}

TEST_CASE("invalid config values keep the defaults") {
    const Config config = parseConfig("stop_key = nope\nvjoy_device = 99\nrate_hz = fast\nunknown = 1\nno separator");
    REQUIRE(config.stopKey == StopKey{StopKeyKind::Pause, 0});
    REQUIRE(config.vjoyDevice == 1);
    REQUIRE(config.rateHz == 100.0);
}

TEST_CASE("a missing config file uses the defaults") {
    const Config config = loadConfig("/definitely/not/here/pilotfly.ini");
    REQUIRE(config.vjoyDevice == 1);
}
