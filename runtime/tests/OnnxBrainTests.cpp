#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>
#include <string>
#include <vector>

#include "brain/OnnxBrain.h"
#include "core/SafeState.h"
#include "core/Tx12Layout.h"

using namespace pilotfly;
using Catch::Matchers::WithinAbs;

namespace {

class FakeFrameSource : public IFrameSource {
public:
    bool ready = true;
    bool grabWorks = true;
    float value = 0.25f;
    int opens = 0;
    int grabs = 0;

    std::string name() const override { return "Fake"; }
    FrameStatus open() override {
        ++opens;
        return {ready, ready ? "fake picture" : "no picture"};
    }
    void close() override {}
    bool grab(std::vector<float>& gray, int width, int height) override {
        ++grabs;
        if (!grabWorks) {
            gray.assign(3, std::numeric_limits<float>::quiet_NaN());
            return false;
        }
        gray.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), value);
        return true;
    }
};

std::string dataFile(const std::string& name) {
    return std::string(PILOTFLY_TEST_DATA_DIR) + "/" + name;
}

void requireAll(const ChannelValues& channels, float expected) {
    for (const float channel : channels) {
        REQUIRE_THAT(channel, WithinAbs(expected, 1e-4));
    }
}

}

TEST_CASE("the test brain loads and reports its rate") {
    FakeFrameSource frames;
    OnnxBrain brain(dataFile("test_brain.onnx"), frames);

    REQUIRE(brain.preferredRateHz() == 0.0);
    const BrainStatus status = brain.load();
    REQUIRE(status.ready);
    REQUIRE(status.message.find("running on CPU") != std::string::npos);
    REQUIRE(status.message.find("fake picture") != std::string::npos);
    REQUIRE(brain.rateHz() == 50.0);
    REQUIRE(brain.preferredRateHz() == 50.0);
}

TEST_CASE("loading again opens the picture again and keeps working") {
    FakeFrameSource frames;
    OnnxBrain brain(dataFile("test_brain.onnx"), frames);

    REQUIRE(brain.load().ready);
    REQUIRE(brain.load().ready);
    REQUIRE(frames.opens == 2);
    brain.reset();
    requireAll(brain.step(0.02), -0.5f);
}

TEST_CASE("a step follows the known function of the test brain") {
    FakeFrameSource frames;
    OnnxBrain brain(dataFile("test_brain.onnx"), frames);
    REQUIRE(brain.load().ready);
    brain.reset();

    frames.value = 0.25f;
    requireAll(brain.step(0.02), -0.5f);

    brain.reset();
    frames.value = 0.75f;
    requireAll(brain.step(0.02), 0.5f);
}

TEST_CASE("the state advances across steps and reset starts over") {
    FakeFrameSource frames;
    OnnxBrain brain(dataFile("test_brain.onnx"), frames);
    REQUIRE(brain.load().ready);
    brain.reset();

    requireAll(brain.step(0.02), -0.5f);
    requireAll(brain.step(0.02), 0.5f);
    requireAll(brain.step(0.02), 1.0f);

    brain.reset();
    requireAll(brain.step(0.02), -0.5f);
}

TEST_CASE("a missing brain file is reported") {
    FakeFrameSource frames;
    const std::string path = dataFile("not_here.onnx");
    OnnxBrain brain(path, frames);

    const BrainStatus status = brain.load();
    REQUIRE_FALSE(status.ready);
    REQUIRE(status.message.find("Brain file not found: " + path) != std::string::npos);
    REQUIRE(brain.preferredRateHz() == 0.0);
    const ControllerState fallback = fromChannels(brain.step(0.02));
    REQUIRE(fallback == safeState(tx12Layout()));
}

TEST_CASE("a picture that is not ready is reported") {
    FakeFrameSource frames;
    frames.ready = false;
    OnnxBrain brain(dataFile("test_brain.onnx"), frames);

    const BrainStatus status = brain.load();
    REQUIRE_FALSE(status.ready);
    REQUIRE(status.message == "no picture");
    REQUIRE(brain.preferredRateHz() == 50.0);

    frames.ready = true;
    REQUIRE(brain.load().ready);
}

TEST_CASE("a failed grab reuses the last good picture") {
    FakeFrameSource frames;
    OnnxBrain brain(dataFile("test_brain.onnx"), frames);
    REQUIRE(brain.load().ready);
    brain.reset();

    frames.value = 0.75f;
    requireAll(brain.step(0.02), 0.5f);

    frames.grabWorks = false;
    brain.reset();
    requireAll(brain.step(0.02), 0.5f);
    REQUIRE(frames.grabs == 2);
}

TEST_CASE("before any good picture a black picture is used") {
    FakeFrameSource frames;
    frames.grabWorks = false;
    OnnxBrain brain(dataFile("test_brain.onnx"), frames);
    REQUIRE(brain.load().ready);
    brain.reset();

    requireAll(brain.step(0.02), -1.0f);
}

TEST_CASE("a brain file with the wrong picture shape is rejected with a clear message") {
    FakeFrameSource frames;
    OnnxBrain brain(dataFile("wrong_frame_brain.onnx"), frames);

    const BrainStatus status = brain.load();
    REQUIRE_FALSE(status.ready);
    REQUIRE(status.message.find("input 'frame'") != std::string::npos);
    REQUIRE(status.message.find("[1, 1, 64, 128]") != std::string::npos);
    REQUIRE(status.message.find("[1, 3, 64, 128]") != std::string::npos);
}

TEST_CASE("a file that is not a model is rejected") {
    FakeFrameSource frames;
    OnnxBrain brain(dataFile("make_test_brain.py"), frames);

    const BrainStatus status = brain.load();
    REQUIRE_FALSE(status.ready);
    REQUIRE(status.message.find("could not be loaded") != std::string::npos);
}
