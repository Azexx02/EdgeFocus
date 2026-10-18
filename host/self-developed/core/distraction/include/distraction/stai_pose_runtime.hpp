#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace distraction {

struct PoseTensorContract {
    int input_size{};
    std::size_t input_elements{};
    std::size_t output_candidates{};
    std::size_t output_elements{};
    std::string input_dtype;
    std::string output_dtype;
    std::string backend;
};

struct PoseRuntimeResult {
    std::vector<float> bcn_output;
    double inference_ms{};
};

class StaiPoseRuntime {
public:
    explicit StaiPoseRuntime(const std::string& nbg_path);
    ~StaiPoseRuntime();
    StaiPoseRuntime(const StaiPoseRuntime&) = delete;
    StaiPoseRuntime& operator=(const StaiPoseRuntime&) = delete;
    StaiPoseRuntime(StaiPoseRuntime&&) noexcept;
    StaiPoseRuntime& operator=(StaiPoseRuntime&&) noexcept;

    const PoseTensorContract& contract() const;
    PoseRuntimeResult run(const std::vector<float>& rgb_nchw_0_to_1);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace distraction
