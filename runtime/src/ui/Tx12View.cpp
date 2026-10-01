#include "ui/Tx12View.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

#include "imgui.h"

namespace pilotfly {

namespace {

constexpr double kHighlightSeconds = 0.6;
constexpr float kAxisChangeThreshold = 0.01f;
constexpr float kBodyAspect = 1.25f;
constexpr float kStickTravel = 0.11f;
constexpr float kPi = 3.14159265f;

const ImVec4 kBodyFill{0.16f, 0.17f, 0.20f, 1.0f};
const ImVec4 kBodyEdge{0.42f, 0.44f, 0.50f, 1.0f};
const ImVec4 kWellFill{0.08f, 0.085f, 0.10f, 1.0f};
const ImVec4 kGuide{0.34f, 0.36f, 0.41f, 1.0f};
const ImVec4 kNeutral{0.80f, 0.82f, 0.86f, 1.0f};
const ImVec4 kLabel{0.72f, 0.74f, 0.79f, 1.0f};
const ImVec4 kAccent{1.00f, 0.68f, 0.12f, 1.0f};
const ImVec4 kLampOff{0.27f, 0.29f, 0.33f, 1.0f};
const ImVec4 kLampOn{1.00f, 0.88f, 0.25f, 1.0f};
const ImVec4 kScreenFill{0.20f, 0.30f, 0.27f, 1.0f};
const ImVec4 kBannerFill{0.80f, 0.12f, 0.12f, 0.93f};
const ImVec4 kWhite{1.0f, 1.0f, 1.0f, 1.0f};
const ImVec4 kDarkText{0.08f, 0.08f, 0.08f, 1.0f};

ImU32 colour(const ImVec4& value) {
    return ImGui::ColorConvertFloat4ToU32(value);
}

ImU32 colour(const ImVec4& value, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(value.x, value.y, value.z, value.w * alpha));
}

ImU32 mix(const ImVec4& from, const ImVec4& to, float t) {
    const float k = std::clamp(t, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        from.x + (to.x - from.x) * k,
        from.y + (to.y - from.y) * k,
        from.z + (to.z - from.z) * k,
        from.w + (to.w - from.w) * k));
}

struct Canvas {
    ImDrawList* list;
    ImVec2 origin;
    float width;
    float height;
    float fontSize;

    ImVec2 at(float x, float y) const {
        return ImVec2(origin.x + x * width, origin.y + y * height);
    }

    float line(float factor = 1.0f) const {
        return std::max(1.0f, width * 0.0025f * factor);
    }

    ImVec2 textSize(const std::string& text, float scale = 1.0f) const {
        return ImGui::GetFont()->CalcTextSizeA(fontSize * scale, FLT_MAX, 0.0f, text.c_str());
    }

    void text(ImVec2 centre, ImU32 textColour, const std::string& value, float scale = 1.0f) const {
        const ImVec2 size = textSize(value, scale);
        list->AddText(ImGui::GetFont(), fontSize * scale,
                      ImVec2(std::floor(centre.x - size.x * 0.5f), std::floor(centre.y - size.y * 0.5f)),
                      textColour, value.c_str());
    }
};

float axisValue(const ControllerState& state, int axis) {
    if (axis < 0 || axis >= kAxisCount) {
        return 0.0f;
    }
    return std::clamp(state.axes[axis], -1.0f, 1.0f);
}

bool buttonValue(const ControllerState& state, int button) {
    if (button < 0 || button >= kButtonCount) {
        return false;
    }
    return state.buttons[button];
}

void drawBody(const Canvas& canvas) {
    const ImVec2 min = canvas.at(0.0f, 0.0f);
    const ImVec2 max = canvas.at(1.0f, 1.0f);
    const float rounding = canvas.width * 0.06f;
    canvas.list->AddRectFilled(min, max, colour(kBodyFill), rounding);
    canvas.list->AddRect(min, max, colour(kBodyEdge), rounding, 0, canvas.line(1.5f));

    const float inset = canvas.width * 0.012f;
    canvas.list->AddRect(ImVec2(min.x + inset, min.y + inset), ImVec2(max.x - inset, max.y - inset),
                         colour(kGuide, 0.6f), rounding - inset, 0, canvas.line());

    const ImVec2 screenMin = canvas.at(0.40f, 0.71f);
    const ImVec2 screenMax = canvas.at(0.60f, 0.85f);
    const float screenRounding = canvas.width * 0.008f;
    canvas.list->AddRectFilled(screenMin, screenMax, colour(kScreenFill), screenRounding);
    canvas.list->AddRect(screenMin, screenMax, colour(kBodyEdge), screenRounding, 0, canvas.line());
    canvas.text(canvas.at(0.5f, 0.78f), colour(kNeutral, 0.75f), "PilotFly TX12", 0.9f);
}

void drawStick(const Canvas& canvas, const StickSpec& spec, const ControllerState& state,
               float glowX, float glowY) {
    const ImVec2 centre = canvas.at(spec.x, spec.y);
    const float travel = canvas.width * kStickTravel;
    const float well = travel * 1.18f;
    const float glow = std::max(glowX, glowY);
    const ImVec2 wellMin(centre.x - well, centre.y - well);
    const ImVec2 wellMax(centre.x + well, centre.y + well);
    const float rounding = well * 0.22f;

    canvas.list->AddRectFilled(wellMin, wellMax, colour(kWellFill), rounding);
    if (glow > 0.0f) {
        canvas.list->AddRect(wellMin, wellMax, colour(kAccent, glow * 0.45f), rounding, 0, canvas.line(4.0f));
    }
    canvas.list->AddRect(wellMin, wellMax, mix(kBodyEdge, kAccent, glow), rounding, 0, canvas.line(1.5f));
    canvas.list->AddCircle(centre, travel, colour(kGuide), 48, canvas.line());
    canvas.list->AddLine(ImVec2(centre.x - travel, centre.y), ImVec2(centre.x + travel, centre.y),
                         colour(kGuide), canvas.line());
    canvas.list->AddLine(ImVec2(centre.x, centre.y - travel), ImVec2(centre.x, centre.y + travel),
                         colour(kGuide), canvas.line());

    const ImVec2 knob(centre.x + axisValue(state, spec.axisX) * travel,
                      centre.y - axisValue(state, spec.axisY) * travel);
    const float knobRadius = canvas.width * 0.022f;
    canvas.list->AddLine(centre, knob, mix(kGuide, kAccent, glow), canvas.line(3.0f));
    if (glow > 0.0f) {
        canvas.list->AddCircleFilled(knob, knobRadius * 1.6f, colour(kAccent, glow * 0.35f), 32);
    }
    canvas.list->AddCircleFilled(knob, knobRadius, mix(kNeutral, kAccent, glow), 32);
    canvas.list->AddCircle(knob, knobRadius, colour(kWellFill), 32, canvas.line());

    const float gap = canvas.fontSize * 0.75f;
    canvas.text(ImVec2(centre.x, wellMin.y - gap), mix(kNeutral, kAccent, glow), spec.name);
    canvas.text(ImVec2(centre.x, wellMax.y + gap), mix(kLabel, kAccent, glowX), "< " + spec.labelX + " >", 0.9f);

    const float labelWidth = canvas.textSize(spec.labelY, 0.9f).x;
    const float side = spec.x < 0.5f ? -1.0f : 1.0f;
    const float labelX = centre.x + side * (well + canvas.fontSize * 0.5f + labelWidth * 0.5f);
    canvas.text(ImVec2(labelX, centre.y - canvas.fontSize * 0.55f), mix(kLabel, kAccent, glowY), "^", 0.9f);
    canvas.text(ImVec2(labelX, centre.y + canvas.fontSize * 0.45f), mix(kLabel, kAccent, glowY), spec.labelY, 0.9f);
}

void drawSwitch(const Canvas& canvas, const SwitchSpec& spec, int position, float glow) {
    const ImVec2 centre = canvas.at(spec.x, spec.y);
    const float halfHeight = canvas.height * 0.035f;
    const float halfWidth = canvas.width * 0.009f;
    const float leverRadius = canvas.width * 0.013f;
    const ImVec2 slotMin(centre.x - halfWidth, centre.y - halfHeight - halfWidth);
    const ImVec2 slotMax(centre.x + halfWidth, centre.y + halfHeight + halfWidth);

    canvas.list->AddRectFilled(slotMin, slotMax, colour(kWellFill), halfWidth);
    canvas.list->AddRect(slotMin, slotMax, mix(kBodyEdge, kAccent, glow), halfWidth, 0, canvas.line());

    const int positions = std::max(spec.positions, 1);
    const int current = std::clamp(position, 0, positions - 1);
    ImVec2 lever = centre;
    for (int i = 0; i < positions; ++i) {
        const float fraction = positions > 1 ? static_cast<float>(i) / static_cast<float>(positions - 1) : 0.5f;
        const float y = centre.y + halfHeight - fraction * 2.0f * halfHeight;
        const float tick = halfWidth * 1.9f;
        canvas.list->AddLine(ImVec2(centre.x - tick, y), ImVec2(centre.x - halfWidth, y), colour(kGuide), canvas.line());
        canvas.list->AddLine(ImVec2(centre.x + halfWidth, y), ImVec2(centre.x + tick, y), colour(kGuide), canvas.line());
        if (i == current) {
            lever = ImVec2(centre.x, y);
        }
    }

    if (glow > 0.0f) {
        canvas.list->AddCircleFilled(lever, leverRadius * 1.8f, colour(kAccent, glow * 0.35f), 24);
    }
    if (spec.momentary) {
        canvas.list->AddNgonFilled(lever, leverRadius * 1.15f, mix(kNeutral, kAccent, glow), 4);
        canvas.text(ImVec2(centre.x + halfWidth * 1.9f + canvas.fontSize * 0.6f, centre.y),
                    mix(kLabel, kAccent, glow), "M", 0.85f);
    } else {
        canvas.list->AddCircleFilled(lever, leverRadius, mix(kNeutral, kAccent, glow), 24);
    }

    std::string label = spec.name;
    if (spec.role == SwitchRole::Arm) {
        label += " ARM";
    } else if (spec.role == SwitchRole::FlightMode) {
        label += " MODE";
    }
    canvas.text(ImVec2(centre.x, slotMax.y + leverRadius * 0.5f + canvas.fontSize * 0.6f),
                mix(kNeutral, kAccent, glow), label, 0.9f);
}

void drawDial(const Canvas& canvas, const DialSpec& spec, const ControllerState& state, float glow) {
    const ImVec2 centre = canvas.at(spec.x, spec.y);
    const float radius = canvas.width * 0.026f;
    const float sweep = kPi * 0.75f;
    const float up = -kPi * 0.5f;

    canvas.list->PathArcTo(centre, radius * 1.3f, up - sweep, up + sweep, 32);
    canvas.list->PathStroke(colour(kGuide), 0, canvas.line());
    if (glow > 0.0f) {
        canvas.list->AddCircleFilled(centre, radius * 1.25f, colour(kAccent, glow * 0.35f), 32);
    }
    canvas.list->AddCircleFilled(centre, radius, colour(kWellFill), 32);
    canvas.list->AddCircle(centre, radius, mix(kBodyEdge, kAccent, glow), 32, canvas.line(1.5f));

    const float angle = up + axisValue(state, spec.axis) * sweep;
    const ImVec2 tip(centre.x + std::cos(angle) * radius * 0.92f, centre.y + std::sin(angle) * radius * 0.92f);
    canvas.list->AddLine(centre, tip, mix(kNeutral, kAccent, glow), canvas.line(2.5f));

    canvas.text(ImVec2(centre.x, centre.y + radius * 1.3f + canvas.fontSize * 0.6f),
                mix(kNeutral, kAccent, glow), spec.name, 0.9f);
}

void drawLamp(const Canvas& canvas, ImVec2 min, ImVec2 max, float rounding, bool lit, float glow) {
    if (glow > 0.0f) {
        const float spread = canvas.width * 0.005f;
        canvas.list->AddRectFilled(ImVec2(min.x - spread, min.y - spread), ImVec2(max.x + spread, max.y + spread),
                                   colour(kAccent, glow * 0.40f), rounding + spread);
    }
    canvas.list->AddRectFilled(min, max, colour(lit ? kLampOn : kLampOff), rounding);
    canvas.list->AddRect(min, max, mix(kBodyEdge, kAccent, glow), rounding, 0, canvas.line());
}

void drawTrim(const Canvas& canvas, const TrimSpec& spec, const ControllerState& state,
              float glowLow, float glowHigh) {
    const ImVec2 centre = canvas.at(spec.x, spec.y);
    const float half = canvas.width * 0.014f;
    const float offset = canvas.width * 0.019f;
    const float rounding = half * 0.3f;
    const ImVec2 low = spec.horizontal ? ImVec2(centre.x - offset, centre.y) : ImVec2(centre.x, centre.y + offset);
    const ImVec2 high = spec.horizontal ? ImVec2(centre.x + offset, centre.y) : ImVec2(centre.x, centre.y - offset);
    const bool lowLit = buttonValue(state, spec.buttonLow);
    const bool highLit = buttonValue(state, spec.buttonHigh);

    drawLamp(canvas, ImVec2(low.x - half, low.y - half), ImVec2(low.x + half, low.y + half), rounding, lowLit, glowLow);
    drawLamp(canvas, ImVec2(high.x - half, high.y - half), ImVec2(high.x + half, high.y + half), rounding, highLit, glowHigh);
    canvas.text(low, colour(lowLit ? kDarkText : kNeutral), "-", 0.9f);
    canvas.text(high, colour(highLit ? kDarkText : kNeutral), "+", 0.9f);
}

void drawHeldBanner(const Canvas& canvas) {
    const std::string message = "HELD BY STOP KEY";
    const float scale = 1.8f;
    const ImVec2 size = canvas.textSize(message, scale);
    const ImVec2 centre = canvas.at(0.5f, 0.5f);
    const float halfWidth = std::max(size.x * 0.5f + canvas.fontSize * 1.5f, canvas.width * 0.30f);
    const float halfHeight = size.y * 0.5f + canvas.fontSize * 0.8f;
    const ImVec2 min(centre.x - halfWidth, centre.y - halfHeight);
    const ImVec2 max(centre.x + halfWidth, centre.y + halfHeight);
    const float rounding = canvas.fontSize * 0.5f;

    canvas.list->AddRectFilled(canvas.at(0.0f, 0.0f), canvas.at(1.0f, 1.0f), IM_COL32(0, 0, 0, 90), canvas.width * 0.06f);
    canvas.list->AddRectFilled(min, max, colour(kBannerFill), rounding);
    canvas.list->AddRect(min, max, colour(kWhite), rounding, 0, canvas.line(2.0f));
    canvas.text(centre, colour(kWhite), message, scale);
}

}

Tx12View::Tx12View(const Tx12Layout& layout) : layout_(layout) {
    axisChangedAt_.fill(-1.0e9);
    buttonChangedAt_.fill(-1.0e9);
}

void Tx12View::trackChanges(const ControllerState& state, double nowSeconds) {
    if (hasPrevious_) {
        for (int i = 0; i < kAxisCount; ++i) {
            if (std::fabs(state.axes[i] - previous_.axes[i]) > kAxisChangeThreshold) {
                axisChangedAt_[i] = nowSeconds;
            }
        }
        for (int i = 0; i < kButtonCount; ++i) {
            if (state.buttons[i] != previous_.buttons[i]) {
                buttonChangedAt_[i] = nowSeconds;
            }
        }
    }
    previous_ = state;
    hasPrevious_ = true;
}

float Tx12View::axisGlow(int axis, double nowSeconds) const {
    if (axis < 0 || axis >= kAxisCount) {
        return 0.0f;
    }
    const double age = nowSeconds - axisChangedAt_[axis];
    return static_cast<float>(std::clamp(1.0 - age / kHighlightSeconds, 0.0, 1.0));
}

float Tx12View::buttonGlow(int button, double nowSeconds) const {
    if (button < 0 || button >= kButtonCount) {
        return 0.0f;
    }
    const double age = nowSeconds - buttonChangedAt_[button];
    return static_cast<float>(std::clamp(1.0 - age / kHighlightSeconds, 0.0, 1.0));
}

float Tx12View::switchGlow(const SwitchSpec& spec, double nowSeconds) const {
    float glow = axisGlow(spec.axis, nowSeconds);
    for (int button : spec.buttons) {
        glow = std::max(glow, buttonGlow(button, nowSeconds));
    }
    return glow;
}

void Tx12View::draw(const ControllerState& state, bool held, double nowSeconds) {
    trackChanges(state, nowSeconds);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x < 8.0f || available.y < 8.0f) {
        return;
    }

    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::Dummy(available);

    const float margin = std::min(available.x, available.y) * 0.02f;
    const float width = std::max(1.0f, std::min(available.x - 2.0f * margin, (available.y - 2.0f * margin) * kBodyAspect));
    const float height = width / kBodyAspect;

    Canvas canvas;
    canvas.list = ImGui::GetWindowDrawList();
    canvas.origin = ImVec2(std::floor(start.x + (available.x - width) * 0.5f),
                           std::floor(start.y + (available.y - height) * 0.5f));
    canvas.width = width;
    canvas.height = height;
    canvas.fontSize = std::clamp(width * 0.021f, 9.0f, 26.0f);

    drawBody(canvas);

    for (const auto& spec : layout_.sticks) {
        drawStick(canvas, spec, state, axisGlow(spec.axisX, nowSeconds), axisGlow(spec.axisY, nowSeconds));
    }
    for (const auto& spec : layout_.switches) {
        drawSwitch(canvas, spec, switchPosition(spec, state), switchGlow(spec, nowSeconds));
    }
    for (const auto& spec : layout_.dials) {
        drawDial(canvas, spec, state, axisGlow(spec.axis, nowSeconds));
    }
    for (const auto& spec : layout_.trims) {
        drawTrim(canvas, spec, state, buttonGlow(spec.buttonLow, nowSeconds), buttonGlow(spec.buttonHigh, nowSeconds));
    }

    const int spareCount = static_cast<int>(layout_.spareButtons.size());
    const float spacing = spareCount > 1 ? std::min(0.05f, 0.80f / static_cast<float>(spareCount - 1)) : 0.0f;
    const float radius = canvas.width * 0.012f;
    for (int i = 0; i < spareCount; ++i) {
        const int button = layout_.spareButtons[i];
        const float x = 0.5f + (static_cast<float>(i) - static_cast<float>(spareCount - 1) * 0.5f) * spacing;
        const ImVec2 centre = canvas.at(x, 0.93f);
        const float glow = buttonGlow(button, nowSeconds);
        drawLamp(canvas, ImVec2(centre.x - radius, centre.y - radius), ImVec2(centre.x + radius, centre.y + radius),
                 radius, buttonValue(state, button), glow);
        canvas.text(ImVec2(centre.x, centre.y + radius + canvas.fontSize * 0.5f),
                    mix(kLabel, kAccent, glow), std::to_string(button + 1), 0.75f);
    }

    if (held) {
        drawHeldBanner(canvas);
    }
}

}
