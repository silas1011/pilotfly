#include "ui/MainWindow.h"

#include <utility>

#include "imgui.h"

namespace pilotfly {

namespace {

const ImVec4 kGood{0.30f, 0.85f, 0.40f, 1.0f};
const ImVec4 kBad{0.95f, 0.30f, 0.30f, 1.0f};
const ImVec4 kWarn{0.98f, 0.82f, 0.25f, 1.0f};

void statusLine(const char* title, bool ready, const std::string& name, const std::string& message) {
    ImGui::TextColored(ready ? kGood : kBad, "%s", ready ? "OK" : "PROBLEM");
    ImGui::SameLine();
    ImGui::Text("%s: %s - %s", title, name.c_str(), message.c_str());
}

}

MainWindow::MainWindow(RunLoop& runLoop, const Tx12Layout& layout, std::string stopKeyName)
    : runLoop_(runLoop), view_(layout), stopKeyName_(std::move(stopKeyName)) {}

void MainWindow::draw(double nowSeconds) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("PilotFly", nullptr, flags)) {
        ImGui::End();
        return;
    }

    const RunSnapshot s = runLoop_.snapshot();
    const float fontSize = ImGui::GetFontSize();
    const ImVec2 bigButton(fontSize * 18.0f, fontSize * 2.8f);

    if (s.running) {
        if (ImGui::Button("Stop", bigButton)) {
            runLoop_.stop();
        }
    } else {
        const bool canStart = s.sinkReady && s.brainReady;
        ImGui::BeginDisabled(!canStart);
        if (ImGui::Button("Start brain + controller", bigButton)) {
            runLoop_.start();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Check again", ImVec2(fontSize * 9.0f, bigButton.y))) {
            runLoop_.checkStatus();
        }
    }

    ImGui::Spacing();
    statusLine("Controller", s.sinkReady, s.sinkName, s.sinkMessage);
    statusLine("Brain", s.brainReady, s.brainName, s.brainMessage);

    if (s.running) {
        const bool slow = s.measuredHz < 0.8 * s.targetHz;
        if (slow) {
            ImGui::TextColored(kWarn, "Loop: %.0f Hz of %.0f Hz", s.measuredHz, s.targetHz);
        } else {
            ImGui::Text("Loop: %.0f Hz of %.0f Hz", s.measuredHz, s.targetHz);
        }
    }

    ImGui::TextUnformatted("State:");
    ImGui::SameLine();
    if (s.held) {
        ImGui::TextColored(kWarn, "Held by stop key");
    } else if (s.running) {
        ImGui::TextColored(kGood, "Running");
    } else {
        ImGui::TextUnformatted("Stopped");
    }

    ImGui::TextWrapped(
        "Stop key: %s freezes the controller (sticks centred, throttle zero, arm off). Press again to release.",
        stopKeyName_.c_str());
    ImGui::Separator();

    view_.draw(s.state, s.held, nowSeconds);

    ImGui::End();
}

}
