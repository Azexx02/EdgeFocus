#include "distraction/portable_model_json.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

namespace distraction {
namespace {

using Json = nlohmann::json;

const Json& required(const Json& object, const char* key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        throw std::runtime_error(std::string("portable model missing field: ") + key);
    }
    return *iterator;
}

std::vector<double> number_array(const Json& value, const char* name) {
    if (!value.is_array()) throw std::runtime_error(std::string(name) + " must be an array");
    std::vector<double> result;
    result.reserve(value.size());
    for (const auto& item : value) {
        if (!item.is_number()) {
            throw std::runtime_error(std::string(name) + " contains a non-number");
        }
        const double number = item.get<double>();
        if (!std::isfinite(number)) {
            throw std::runtime_error(std::string(name) + " contains non-finite value");
        }
        result.push_back(number);
    }
    return result;
}

std::int64_t integer_field(const Json& object, const char* key) {
    const auto& value = required(object, key);
    if (!value.is_number_integer()) {
        throw std::runtime_error(std::string("portable model field must be integer: ") + key);
    }
    return value.get<std::int64_t>();
}

std::string string_field(const Json& object, const char* key) {
    const auto& value = required(object, key);
    if (!value.is_string()) {
        throw std::runtime_error(std::string("portable model field must be string: ") + key);
    }
    return value.get<std::string>();
}

}  // namespace

PortableMlp load_portable_mlp_json(const std::string& path, bool frozen) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open portable model JSON: " + path);

    Json root;
    try {
        input >> root;
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("cannot parse portable model JSON: ") + error.what());
    }
    if (!root.is_object()) throw std::runtime_error("portable model root must be an object");
    if (string_field(root, "model_type") != "standard_scaler_mlp") {
        throw std::runtime_error("unsupported portable model type");
    }

    PortableMlp model;
    const auto steps = integer_field(root, "steps");
    const auto feature_dim = integer_field(root, "feature_dim");
    if (steps <= 0 || feature_dim <= 0) {
        throw std::runtime_error("portable model steps/feature_dim must be positive");
    }
    model.steps = static_cast<std::size_t>(steps);
    model.feature_dim = static_cast<std::size_t>(feature_dim);
    model.window_ms = integer_field(root, "window_ms");
    model.maximum_gap_ms = integer_field(root, "maximum_gap_ms");
    model.activation = string_field(root, "activation");

    const auto& classes = required(root, "classes");
    if (!classes.is_array()) throw std::runtime_error("portable model classes must be an array");
    for (const auto& value : classes) {
        if (!value.is_string()) throw std::runtime_error("portable model class must be a string");
        model.classes.emplace_back(value.get<std::string>());
    }

    const auto& scaler = required(root, "scaler");
    if (!scaler.is_object()) throw std::runtime_error("portable model scaler must be an object");
    model.mean = number_array(required(scaler, "mean"), "scaler.mean");
    model.scale = number_array(required(scaler, "scale"), "scaler.scale");

    const auto& layers = required(root, "layers");
    if (!layers.is_array()) throw std::runtime_error("portable model layers must be an array");
    for (const auto& layer_json : layers) {
        if (!layer_json.is_object()) throw std::runtime_error("portable model layer must be an object");
        const auto& rows = required(layer_json, "weights");
        if (!rows.is_array()) throw std::runtime_error("layer.weights must be an array");

        DenseLayer layer;
        layer.input_size = rows.size();
        for (std::size_t row = 0; row < rows.size(); ++row) {
            auto values = number_array(rows[row], "layer.weights row");
            if (row == 0) layer.output_size = values.size();
            if (values.size() != layer.output_size) throw std::runtime_error("ragged dense weights");
            layer.weights.insert(layer.weights.end(), values.begin(), values.end());
        }
        layer.bias = number_array(required(layer_json, "bias"), "layer.bias");
        model.layers.push_back(std::move(layer));
    }
    model.validate(frozen);
    return model;
}

}  // namespace distraction
