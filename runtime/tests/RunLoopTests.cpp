#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include "app/RunLoop.h"
#include "controller/DummySink.h"
#include "core/SafeState.h"

using namespace pilotfly;

namespace {

class FixedBrain : public IBrain {
public:
    bool ready = true;
    int resets = 0;

    std::string name() const override { return "Fixed"; }
    BrainStatus load() override { return {ready, ready ? "ok" : "no brain file"}; }
    void reset() override { ++resets; }
    ChannelValues step(double) override {
        ChannelValues channels;
        channels.fill(1.0f);
        return channels;
    }
    double preferredRateHz() const override { return 0.0; }
};

class BrokenSink : public IControllerSink {
public:
    std::string name() const override { return "Broken"; }
    SinkStatus open() override { return {false, "driver missing"}; }
    void close() override {}
    bool send(const ControllerState&) override { return false; }
};

class FailingSink : public IControllerSink {
public:
    std::string name() const override { return "Failing"; }
    SinkStatus open() override { return {true, "ok"}; }
    void close() override {}
    bool send(const ControllerState&) override {
        ++sends;
        return false;
    }
    std::atomic<int> sends{0};
};

class CountingStopKey : public IStopKey {
public:
    std::string keyName() const override { return "Test"; }
    bool usableFromAnyThread() const override { return true; }
    bool consumePress() override { return presses.exchange(false); }
    std::atomic<bool> presses{false};
};

void waitForSends(const DummySink& sink, int count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (sink.sendCount() < count && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

ControllerState allOn() {
    ControllerState state;
    state.axes.fill(1.0f);
    state.buttons.fill(true);
    return state;
}

}

TEST_CASE("status check reports ready parts without starting") {
    FixedBrain brain;
    DummySink sink;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);

    loop.checkStatus();
    const RunSnapshot snapshot = loop.snapshot();

    REQUIRE(snapshot.sinkReady);
    REQUIRE(snapshot.brainReady);
    REQUIRE_FALSE(snapshot.running);
    REQUIRE_FALSE(sink.isOpen());
    REQUIRE(snapshot.state == safeState(tx12Layout()));
}

TEST_CASE("the loop sends brain output to the controller") {
    FixedBrain brain;
    DummySink sink;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);

    REQUIRE(loop.start());
    waitForSends(sink, 5);

    REQUIRE(sink.sendCount() >= 5);
    REQUIRE(sink.lastState() == allOn());
    REQUIRE(loop.snapshot().running);
    REQUIRE(loop.snapshot().state == allOn());
    REQUIRE(brain.resets == 1);

    loop.stop();
}

TEST_CASE("the stop key holds the safe state until released") {
    FixedBrain brain;
    DummySink sink;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);
    REQUIRE(loop.start());
    waitForSends(sink, 3);

    loop.toggleHold();
    waitForSends(sink, sink.sendCount() + 3);
    REQUIRE(loop.snapshot().held);
    REQUIRE(sink.lastState() == safeState(tx12Layout()));

    loop.toggleHold();
    waitForSends(sink, sink.sendCount() + 3);
    REQUIRE_FALSE(loop.snapshot().held);
    REQUIRE(sink.lastState() == allOn());

    loop.stop();
}

TEST_CASE("stopping leaves the controller in the safe state and closes it") {
    FixedBrain brain;
    DummySink sink;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);
    REQUIRE(loop.start());
    waitForSends(sink, 3);

    loop.stop();

    REQUIRE(sink.lastState() == safeState(tx12Layout()));
    REQUIRE_FALSE(sink.isOpen());
    REQUIRE_FALSE(loop.snapshot().running);
    REQUIRE(loop.snapshot().state == safeState(tx12Layout()));
}

TEST_CASE("the loop does not start without a controller") {
    FixedBrain brain;
    BrokenSink sink;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);

    REQUIRE_FALSE(loop.start());
    const RunSnapshot snapshot = loop.snapshot();
    REQUIRE_FALSE(snapshot.running);
    REQUIRE_FALSE(snapshot.sinkReady);
    REQUIRE(snapshot.sinkMessage == "driver missing");
}

TEST_CASE("the loop does not start without a brain") {
    FixedBrain brain;
    brain.ready = false;
    DummySink sink;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);

    REQUIRE_FALSE(loop.start());
    REQUIRE_FALSE(loop.snapshot().running);
    REQUIRE_FALSE(loop.snapshot().brainReady);
    REQUIRE_FALSE(sink.isOpen());
}

TEST_CASE("the stop key does nothing while stopped") {
    FixedBrain brain;
    DummySink sink;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);

    loop.toggleHold();
    REQUIRE_FALSE(loop.snapshot().held);
}

TEST_CASE("the loop can be started again after a stop") {
    FixedBrain brain;
    DummySink sink;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);

    REQUIRE(loop.start());
    waitForSends(sink, 2);
    loop.stop();
    const int before = sink.sendCount();
    REQUIRE(loop.start());
    waitForSends(sink, before + 2);
    REQUIRE(sink.sendCount() >= before + 2);
    REQUIRE(brain.resets == 2);
    loop.stop();
}

TEST_CASE("a restart clears the hold") {
    FixedBrain brain;
    DummySink sink;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);

    REQUIRE(loop.start());
    loop.toggleHold();
    REQUIRE(loop.snapshot().held);
    loop.stop();
    REQUIRE_FALSE(loop.snapshot().held);
    REQUIRE(loop.start());
    waitForSends(sink, sink.sendCount() + 3);
    REQUIRE_FALSE(loop.snapshot().held);
    REQUIRE(sink.lastState() == allOn());
    loop.stop();
}

TEST_CASE("a failing controller is reported while running and after stopping") {
    FixedBrain brain;
    FailingSink sink;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);

    REQUIRE(loop.start());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (sink.sends < 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(sink.sends >= 3);
    REQUIRE_FALSE(loop.snapshot().sinkReady);

    loop.stop();
    REQUIRE_FALSE(loop.snapshot().sinkReady);
    REQUIRE_FALSE(loop.snapshot().running);
}

TEST_CASE("a stop key polled by the worker holds the safe state") {
    FixedBrain brain;
    DummySink sink;
    CountingStopKey stopKey;
    RunLoop loop(brain, sink, tx12Layout(), 200.0);
    loop.setWorkerStopKey(&stopKey);

    REQUIRE(loop.start());
    waitForSends(sink, 3);
    stopKey.presses = true;
    waitForSends(sink, sink.sendCount() + 4);
    REQUIRE(loop.snapshot().held);
    REQUIRE(sink.lastState() == safeState(tx12Layout()));
    loop.stop();
}
