#include "ModelRegistry.h"

#include <fstream>
#include <sstream>

namespace edgefocus {

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string stringValue(const std::string& text, const std::string& key, const std::string& fallback = "") {
    const std::string marker = "\"" + key + "\"";
    size_t pos = text.find(marker);
    if (pos == std::string::npos) return fallback;
    pos = text.find(':', pos);
    if (pos == std::string::npos) return fallback;
    pos = text.find('"', pos);
    if (pos == std::string::npos) return fallback;
    const size_t end = text.find('"', pos + 1);
    if (end == std::string::npos) return fallback;
    return text.substr(pos + 1, end - pos - 1);
}

float floatValue(const std::string& text, const std::string& key, float fallback) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = text.find(marker);
    if (pos == std::string::npos) return fallback;
    pos = text.find(':', pos);
    if (pos == std::string::npos) return fallback;
    try {
        return std::stof(text.substr(pos + 1));
    } catch (...) {
        return fallback;
    }
}

} // namespace

bool ModelRegistry::load(const std::string& path) {
    const std::string text = readFile(path);
    if (text.empty()) {
        last_error_ = "model registry not found: " + path;
        has_pose_package_ = false;
        return false;
    }

    ModelPackage pkg;
    pkg.name = stringValue(text, "name", "yolov8n_pose");
    pkg.runtime = stringValue(text, "runtime", "stai_mpu");
    pkg.model_path = stringValue(text, "model_path");
    pkg.labels_path = stringValue(text, "labels_path");
    pkg.preprocess_path = stringValue(text, "preprocess_path");
    pkg.postprocess_path = stringValue(text, "postprocess_path");
    pkg.runtime_config_path = stringValue(text, "runtime_config_path");
    pkg.checksum = stringValue(text, "checksum");
    pkg.confidence_threshold = floatValue(text, "confidence_threshold", 0.65f);
    pkg.iou_threshold = floatValue(text, "iou_threshold", 0.50f);

    if (pkg.model_path.empty()) {
        last_error_ = "pose package missing model_path";
        has_pose_package_ = false;
        return false;
    }

    pose_package_ = pkg;
    has_pose_package_ = true;
    last_error_.clear();
    return true;
}

} // namespace edgefocus
