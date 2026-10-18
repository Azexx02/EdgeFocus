#include "ThermalSource.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace edgefocus::fan::bridge {
namespace {

bool readLine(const std::filesystem::path& path, std::string& value) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::getline(input, value);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                              value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return true;
}

}  // namespace

ThermalSource::ThermalSource(std::filesystem::path thermal_root,
                             std::string required_type)
    : thermal_root_(std::move(thermal_root)), required_type_(std::move(required_type)) {}

bool ThermalSource::discover(std::string& error) {
    temperature_path_.clear();
    std::vector<std::filesystem::path> matches;
    std::error_code iterator_error;
    std::filesystem::directory_iterator iterator(thermal_root_, iterator_error);
    if (iterator_error) {
        error = "cannot enumerate thermal root " + thermal_root_.string() + ": " +
                iterator_error.message();
        return false;
    }

    for (const auto& entry : iterator) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("thermal_zone", 0) != 0) {
            continue;
        }
        std::string type;
        if (readLine(entry.path() / "type", type) && type == required_type_) {
            matches.push_back(entry.path() / "temp");
        }
    }

    if (matches.empty()) {
        error = "required thermal zone type not found: " + required_type_;
        return false;
    }
    if (matches.size() != 1) {
        error = "required thermal zone type is ambiguous: " + required_type_;
        return false;
    }

    temperature_path_ = matches.front();
    std::int32_t ignored = 0;
    if (!read(ignored, error)) {
        temperature_path_.clear();
        return false;
    }
    return true;
}

bool ThermalSource::read(std::int32_t& temperature_mc, std::string& error) const {
    if (temperature_path_.empty()) {
        error = "thermal source has not been discovered";
        return false;
    }
    std::ifstream input(temperature_path_);
    std::int64_t value = 0;
    if (!(input >> value)) {
        error = "cannot parse temperature from " + temperature_path_.string();
        return false;
    }
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) {
        error = "temperature value exceeds int32 range";
        return false;
    }
    temperature_mc = static_cast<std::int32_t>(value);
    return true;
}

}  // namespace edgefocus::fan::bridge
