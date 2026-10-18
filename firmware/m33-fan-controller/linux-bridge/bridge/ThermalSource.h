#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace edgefocus::fan::bridge {

class ThermalSource {
public:
    explicit ThermalSource(std::filesystem::path thermal_root = "/sys/class/thermal",
                           std::string required_type = "cpu-thermal");

    bool discover(std::string& error);
    bool read(std::int32_t& temperature_mc, std::string& error) const;
    void invalidate() { temperature_path_.clear(); }

    bool ready() const { return !temperature_path_.empty(); }
    const std::filesystem::path& temperaturePath() const { return temperature_path_; }
    const std::string& requiredType() const { return required_type_; }

private:
    std::filesystem::path thermal_root_;
    std::string required_type_;
    std::filesystem::path temperature_path_;
};

}  // namespace edgefocus::fan::bridge
