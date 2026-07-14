#include "distraction/stai_pose_runtime.hpp"

#include <stai_mpu_network.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace distraction {
namespace {

std::size_t elements(const std::vector<int>& shape) {
    std::size_t result = 1;
    for (int value : shape) {
        if (value <= 0) throw std::runtime_error("dynamic/non-positive STAI tensor dimension");
        result *= static_cast<std::size_t>(value);
    }
    return result;
}

}  // namespace

class StaiPoseRuntime::Impl {
public:
    explicit Impl(const std::string& path) : network(path, true) {
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("NBG file is missing");
        if (network.get_backend_engine() != stai_mpu_backend_engine::STAI_MPU_OVX_NPU_ENGINE) {
            throw std::runtime_error("NBG did not load through OVX_NPU");
        }
        const auto inputs = network.get_input_infos();
        const auto outputs = network.get_output_infos();
        if (inputs.size() != 1 || outputs.size() != 1) throw std::runtime_error("Pose NBG requires one input/output");
        const auto input_shape = inputs[0].get_shape();
        const auto output_shape = outputs[0].get_shape();
        if (input_shape.size() != 4 || input_shape[0] != 1 || input_shape[1] != 3 ||
            input_shape[2] != input_shape[3] || input_shape[2] <= 0 || input_shape[2] % 32 != 0) {
            throw std::runtime_error("Pose NBG input must be static NCHW square");
        }
        if (output_shape.size() != 3 || output_shape[0] != 1 || output_shape[1] != 56) {
            throw std::runtime_error("Pose NBG output must be [1,56,N]");
        }
        if (inputs[0].get_dtype() != STAI_MPU_DTYPE_FLOAT16 ||
            outputs[0].get_dtype() != STAI_MPU_DTYPE_FLOAT16) {
            throw std::runtime_error("unquantized Pose NBG requires float16 I/O");
        }
        const std::size_t expected_candidates =
            static_cast<std::size_t>((input_shape[2] / 8) * (input_shape[2] / 8) +
                                     (input_shape[2] / 16) * (input_shape[2] / 16) +
                                     (input_shape[2] / 32) * (input_shape[2] / 32));
        if (static_cast<std::size_t>(output_shape[2]) != expected_candidates) {
            throw std::runtime_error("Pose NBG candidate count does not match input strides");
        }
        contract = {input_shape[2], elements(input_shape), expected_candidates, elements(output_shape),
                    "float16", "float16", "OVX_NPU"};
        input.resize(contract.input_elements);
    }

    stai_mpu_network network;
    PoseTensorContract contract;
    std::vector<float> input;
};

StaiPoseRuntime::StaiPoseRuntime(const std::string& path) : impl_(std::make_unique<Impl>(path)) {}
StaiPoseRuntime::~StaiPoseRuntime() = default;
StaiPoseRuntime::StaiPoseRuntime(StaiPoseRuntime&&) noexcept = default;
StaiPoseRuntime& StaiPoseRuntime::operator=(StaiPoseRuntime&&) noexcept = default;

const PoseTensorContract& StaiPoseRuntime::contract() const { return impl_->contract; }

PoseRuntimeResult StaiPoseRuntime::run(const std::vector<float>& values) {
    if (values.size() != impl_->contract.input_elements) {
        throw std::invalid_argument("Pose input element count mismatch");
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!std::isfinite(values[index]) || values[index] < 0.0F || values[index] > 1.0F) {
            throw std::invalid_argument("Pose input must be finite in [0,1]");
        }
        impl_->input[index] = values[index];
    }
    impl_->network.set_input(0, impl_->input.data());
    const auto started = std::chrono::steady_clock::now();
    if (!impl_->network.run()) throw std::runtime_error("STAI inference returned false");
    const auto ended = std::chrono::steady_clock::now();
    void* raw = impl_->network.get_output(0);
    if (!raw) throw std::runtime_error("STAI output is null");
    std::unique_ptr<void, decltype(&std::free)> owned(raw, &std::free);
    // The stai_mpu C++ API accepts and returns float32 host buffers for floating
    // NBG tensors; the descriptor dtype describes the network-side storage.
    // Supplying packed half input makes set_input read beyond the buffer, while
    // half-decoding the returned host buffer splits every valid float in two.
    const auto* host_output = static_cast<const float*>(raw);
    std::vector<float> output(impl_->contract.output_elements);
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] = host_output[index];
        if (!std::isfinite(output[index])) throw std::runtime_error("STAI output contains non-finite value");
    }
    return {std::move(output), std::chrono::duration<double, std::milli>(ended - started).count()};
}

}  // namespace distraction
