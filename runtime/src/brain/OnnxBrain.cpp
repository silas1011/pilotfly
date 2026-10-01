#include "brain/OnnxBrain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>

#include <onnxruntime_cxx_api.h>

#include "core/SafeState.h"
#include "core/Tx12Layout.h"

#ifdef _WIN32
#include <dml_provider_factory.h>
#endif

namespace pilotfly {

namespace {

constexpr const char* kFrameInput = "frame";
constexpr const char* kStateInput = "state";
constexpr const char* kChannelsOutput = "channels";
constexpr const char* kStateOutput = "state_out";
constexpr const char* kRateKey = "pilotfly_rate_hz";
constexpr std::size_t kFramePixels =
    static_cast<std::size_t>(kBrainFrameWidth) * static_cast<std::size_t>(kBrainFrameHeight);

struct TensorInfo {
    ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    std::vector<std::int64_t> shape;
};

std::string shapeText(const std::vector<std::int64_t>& shape) {
    std::string text = "[";
    for (std::size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            text += ", ";
        }
        text += shape[i] < 0 ? "?" : std::to_string(shape[i]);
    }
    return text + "]";
}

bool shapeMatches(const std::vector<std::int64_t>& actual, const std::vector<std::int64_t>& expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i] >= 0 && actual[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

std::string mismatch(const std::string& what) {
    return "Brain file does not match PilotFly: " + what + ". Export the brain again.";
}

std::optional<TensorInfo> findTensor(Ort::Session& session, const char* wanted, bool input) {
    Ort::AllocatorWithDefaultOptions allocator;
    const std::size_t count = input ? session.GetInputCount() : session.GetOutputCount();
    for (std::size_t i = 0; i < count; ++i) {
        const auto tensorName = input ? session.GetInputNameAllocated(i, allocator)
                                      : session.GetOutputNameAllocated(i, allocator);
        if (std::string(tensorName.get()) != wanted) {
            continue;
        }
        const Ort::TypeInfo typeInfo = input ? session.GetInputTypeInfo(i) : session.GetOutputTypeInfo(i);
        if (typeInfo.GetONNXType() != ONNX_TYPE_TENSOR) {
            return TensorInfo{};
        }
        const auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
        return TensorInfo{tensorInfo.GetElementType(), tensorInfo.GetShape()};
    }
    return std::nullopt;
}

std::string checkTensor(const std::optional<TensorInfo>& info, const std::string& label,
                        const std::vector<std::int64_t>& expected) {
    if (!info) {
        return mismatch(label + " is missing");
    }
    if (info->type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        return mismatch(label + " must be float32");
    }
    if (!shapeMatches(info->shape, expected)) {
        return mismatch(label + " must have shape " + shapeText(expected) + " but has " + shapeText(info->shape));
    }
    return "";
}

double readRate(Ort::Session& session) {
    Ort::AllocatorWithDefaultOptions allocator;
    const Ort::ModelMetadata metadata = session.GetModelMetadata();
    const auto value = metadata.LookupCustomMetadataMapAllocated(kRateKey, allocator);
    if (!value) {
        return kDefaultBrainRateHz;
    }
    try {
        const double rate = std::stod(value.get());
        if (std::isfinite(rate) && rate >= 1.0 && rate <= 1000.0) {
            return rate;
        }
    } catch (const std::exception&) {
    }
    return kDefaultBrainRateHz;
}

ChannelValues safeChannels() {
    const ControllerState safe = safeState(tx12Layout());
    ChannelValues channels;
    channels.fill(-1.0f);
    for (int i = 0; i < kAxisCount; ++i) {
        channels[i] = safe.axes[i];
    }
    return channels;
}

#ifdef _WIN32
bool appendDirectMl(Ort::SessionOptions& options) {
    try {
        options.DisableMemPattern();
        options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, 0));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
#endif

}

struct OnnxBrain::Runtime {
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "pilotfly"};
    Ort::Session session{nullptr};
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
};

OnnxBrain::OnnxBrain(std::string modelPath, IFrameSource& frames)
    : modelPath_(std::move(modelPath)), frames_(frames), frame_(kFramePixels, 0.0f), lastChannels_(safeChannels()) {}

OnnxBrain::~OnnxBrain() = default;

std::string OnnxBrain::name() const {
    return "Fly brain";
}

double OnnxBrain::rateHz() const {
    return rateHz_;
}

double OnnxBrain::preferredRateHz() const {
    return runtime_ ? rateHz_ : 0.0;
}

BrainStatus OnnxBrain::createRuntime() {
    const std::filesystem::path path(modelPath_);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return {false, "Brain file not found: " + modelPath_ +
                           ". Train a brain and copy brain.onnx next to the program."};
    }

    try {
        auto runtime = std::make_unique<Runtime>();
        std::string provider = "running on CPU";

#ifdef _WIN32
        Ort::SessionOptions gpuOptions;
        if (appendDirectMl(gpuOptions)) {
            try {
                runtime->session = Ort::Session(runtime->env, path.c_str(), gpuOptions);
                provider = "running on DirectML (GPU)";
            } catch (const std::exception&) {
            }
        }
#endif

        if (!runtime->session) {
            Ort::SessionOptions cpuOptions;
            runtime->session = Ort::Session(runtime->env, path.c_str(), cpuOptions);
        }

        const auto stateInfo = findTensor(runtime->session, kStateInput, true);
        if (!stateInfo) {
            return {false, mismatch("input 'state' is missing")};
        }
        if (stateInfo->type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            return {false, mismatch("input 'state' must be float32")};
        }
        if (stateInfo->shape.size() != 2 || (stateInfo->shape[0] >= 0 && stateInfo->shape[0] != 1) ||
            stateInfo->shape[1] <= 0) {
            return {false, mismatch("input 'state' must have shape [1, S] with a fixed S but has " +
                                    shapeText(stateInfo->shape))};
        }
        const std::int64_t stateSize = stateInfo->shape[1];

        const std::array<std::string, 3> problems = {
            checkTensor(findTensor(runtime->session, kFrameInput, true), "input 'frame'",
                        {1, 1, kBrainFrameHeight, kBrainFrameWidth}),
            checkTensor(findTensor(runtime->session, kChannelsOutput, false), "output 'channels'",
                        {1, kChannelCount}),
            checkTensor(findTensor(runtime->session, kStateOutput, false), "output 'state_out'",
                        {1, stateSize})};
        for (const std::string& problem : problems) {
            if (!problem.empty()) {
                return {false, problem};
            }
        }
        if (runtime->session.GetInputCount() != 2) {
            return {false, mismatch("it must have exactly the inputs 'frame' and 'state' but has " +
                                    std::to_string(runtime->session.GetInputCount()) + " inputs")};
        }

        rateHz_ = readRate(runtime->session);
        state_.assign(static_cast<std::size_t>(stateSize), 0.0f);
        lastChannels_ = safeChannels();
        provider_ = provider;
        runtime_ = std::move(runtime);
        return {true, provider_};
    } catch (const std::exception& exception) {
        return {false, "Brain file could not be loaded: " + std::string(exception.what())};
    }
}

BrainStatus OnnxBrain::load() {
    const FrameStatus frameStatus = frames_.open();
    BrainStatus brainStatus{true, provider_};
    if (!runtime_) {
        brainStatus = createRuntime();
    }
    if (!frameStatus.ready) {
        return {false, frameStatus.message};
    }
    if (!brainStatus.ready) {
        return brainStatus;
    }
    const std::string fileName = std::filesystem::path(modelPath_).filename().string();
    return {true, fileName + " " + brainStatus.message + ", " + frameStatus.message};
}

void OnnxBrain::reset() {
    std::fill(state_.begin(), state_.end(), 0.0f);
    lastChannels_ = safeChannels();
}

ChannelValues OnnxBrain::step(double) {
    if (!runtime_) {
        return lastChannels_;
    }
    try {
        if (frames_.grab(grabbed_, kBrainFrameWidth, kBrainFrameHeight) && grabbed_.size() == kFramePixels) {
            frame_ = grabbed_;
        }

        const std::array<std::int64_t, 4> frameShape = {1, 1, kBrainFrameHeight, kBrainFrameWidth};
        const std::array<std::int64_t, 2> stateShape = {1, static_cast<std::int64_t>(state_.size())};
        std::array<Ort::Value, 2> inputs = {
            Ort::Value::CreateTensor<float>(runtime_->memory, frame_.data(), frame_.size(),
                                            frameShape.data(), frameShape.size()),
            Ort::Value::CreateTensor<float>(runtime_->memory, state_.data(), state_.size(),
                                            stateShape.data(), stateShape.size())};
        const std::array<const char*, 2> inputNames = {kFrameInput, kStateInput};
        const std::array<const char*, 2> outputNames = {kChannelsOutput, kStateOutput};

        const std::vector<Ort::Value> outputs = runtime_->session.Run(
            Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(),
            outputNames.data(), outputNames.size());

        if (outputs.size() != 2 || !outputs[0].IsTensor() || !outputs[1].IsTensor()) {
            return lastChannels_;
        }
        const auto channelsInfo = outputs[0].GetTensorTypeAndShapeInfo();
        const auto stateInfo = outputs[1].GetTensorTypeAndShapeInfo();
        if (channelsInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            stateInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            channelsInfo.GetElementCount() != static_cast<std::size_t>(kChannelCount) ||
            stateInfo.GetElementCount() != state_.size()) {
            return lastChannels_;
        }

        const float* channelData = outputs[0].GetTensorData<float>();
        const float* stateData = outputs[1].GetTensorData<float>();
        ChannelValues channels;
        for (int i = 0; i < kChannelCount; ++i) {
            channels[i] = std::isfinite(channelData[i]) ? channelData[i] : 0.0f;
        }
        std::copy(stateData, stateData + state_.size(), state_.begin());
        lastChannels_ = channels;
        return channels;
    } catch (...) {
        return lastChannels_;
    }
}

}
