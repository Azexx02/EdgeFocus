#pragma once

#include <string>
#include <vector>

namespace edgefocus {

struct ModelPackage {
    std::string name;
    std::string runtime;
    std::string model_path;
    std::string labels_path;
    std::string preprocess_path;
    std::string postprocess_path;
    std::string runtime_config_path;
    std::string checksum;
    float confidence_threshold = 0.65f;
    float iou_threshold = 0.50f;
};

class ModelRegistry {
public:
    bool load(const std::string& path);
    const ModelPackage& posePackage() const { return pose_package_; }
    bool hasPosePackage() const { return has_pose_package_; }
    const std::string& lastError() const { return last_error_; }

private:
    ModelPackage pose_package_;
    bool has_pose_package_ = false;
    std::string last_error_;
};

} // namespace edgefocus
