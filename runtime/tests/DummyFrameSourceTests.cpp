#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "capture/DummyFrameSource.h"

using namespace pilotfly;

TEST_CASE("the dummy picture says that it is a dummy") {
    DummyFrameSource source;
    const FrameStatus status = source.open();
    REQUIRE(status.ready);
    REQUIRE(status.message.find("Dummy") != std::string::npos);
}

TEST_CASE("the dummy picture has the requested size and stays between 0 and 1") {
    DummyFrameSource source;
    REQUIRE(source.open().ready);
    std::vector<float> gray;
    for (int i = 0; i < 300; ++i) {
        REQUIRE(source.grab(gray, 128, 64));
        REQUIRE(gray.size() == 128u * 64u);
        bool inRange = true;
        for (const float value : gray) {
            inRange = inRange && value >= 0.0f && value <= 1.0f;
        }
        REQUIRE(inRange);
    }
}

TEST_CASE("the dummy picture changes over time") {
    DummyFrameSource source;
    std::vector<float> first;
    std::vector<float> second;
    REQUIRE(source.grab(first, 128, 64));
    REQUIRE(source.grab(second, 128, 64));
    REQUIRE(first != second);
}

TEST_CASE("the dummy picture is the same on every run") {
    DummyFrameSource a;
    DummyFrameSource b;
    std::vector<float> fromA;
    std::vector<float> fromB;
    REQUIRE(a.grab(fromA, 32, 16));
    REQUIRE(b.grab(fromB, 32, 16));
    REQUIRE(fromA == fromB);
}

TEST_CASE("the dummy picture rejects an empty size") {
    DummyFrameSource source;
    std::vector<float> gray;
    REQUIRE_FALSE(source.grab(gray, 0, 64));
    REQUIRE_FALSE(source.grab(gray, 128, -1));
}
