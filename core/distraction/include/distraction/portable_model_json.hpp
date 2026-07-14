#pragma once

#include "distraction/temporal.hpp"

#include <string>

namespace distraction {

PortableMlp load_portable_mlp_json(const std::string& path, bool frozen_prone_contract = true);

}  // namespace distraction
