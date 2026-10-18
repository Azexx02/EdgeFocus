#include "distraction/temporal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace distraction {

void PortableMlp::validate(bool frozen) const {
    if (steps < 2 || feature_dim == 0 || window_ms <= 0 || maximum_gap_ms <= 0) {
        throw std::invalid_argument("invalid temporal model dimensions or durations");
    }
    const std::size_t flattened = steps * feature_dim;
    if (classes.empty() || mean.size() != flattened || scale.size() != flattened || layers.empty()) {
        throw std::invalid_argument("incomplete temporal model");
    }
    for (std::size_t index = 0; index < flattened; ++index) {
        if (!std::isfinite(mean[index]) || !std::isfinite(scale[index]) || scale[index] == 0.0) {
            throw std::invalid_argument("non-finite or zero scaler value");
        }
    }
    std::size_t expected_input = flattened;
    for (const auto& layer : layers) {
        if (layer.input_size != expected_input || layer.output_size == 0 ||
            layer.weights.size() != layer.input_size * layer.output_size ||
            layer.bias.size() != layer.output_size) {
            throw std::invalid_argument("dense layer dimensions do not connect");
        }
        for (double value : layer.weights) {
            if (!std::isfinite(value)) throw std::invalid_argument("non-finite dense weight");
        }
        for (double value : layer.bias) {
            if (!std::isfinite(value)) throw std::invalid_argument("non-finite dense bias");
        }
        expected_input = layer.output_size;
    }
    if (activation != "relu") throw std::invalid_argument("unsupported hidden activation");
    if (expected_input != 1 && expected_input != classes.size()) {
        throw std::invalid_argument("output dimension does not match class contract");
    }
    if (frozen) {
        if (classes != std::vector<std::string>{"NORMAL_STUDY", "PRONE"} || steps != 16 ||
            feature_dim != 58 || window_ms != 2000 || maximum_gap_ms != 500 ||
            layers.size() != 3 || layers[0].input_size != 928 || layers[0].output_size != 32 ||
            layers[1].input_size != 32 || layers[1].output_size != 16 ||
            layers[2].input_size != 16 || layers[2].output_size != 1) {
            throw std::invalid_argument("model does not match frozen prone contract");
        }
    }
}

std::vector<double> PortableMlp::predict_probabilities(const std::vector<double>& flattened) const {
    validate(false);
    if (flattened.size() != steps * feature_dim) {
        throw std::invalid_argument("flattened features do not match temporal model");
    }
    std::vector<double> values(flattened.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!std::isfinite(flattened[index])) throw std::invalid_argument("non-finite feature value");
        values[index] = (flattened[index] - mean[index]) / scale[index];
    }
    for (std::size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        const auto& layer = layers[layer_index];
        std::vector<double> next(layer.output_size, 0.0);
        for (std::size_t output = 0; output < layer.output_size; ++output) {
            double total = layer.bias[output];
            for (std::size_t input = 0; input < layer.input_size; ++input) {
                total += values[input] * layer.weights[input * layer.output_size + output];
            }
            next[output] = total;
        }
        if (layer_index + 1 < layers.size()) {
            for (double& value : next) value = std::max(0.0, value);
        }
        values = std::move(next);
    }
    if (values.size() == 1) {
        const double logit = std::clamp(values[0], -700.0, 700.0);
        const double positive = 1.0 / (1.0 + std::exp(-logit));
        return {1.0 - positive, positive};
    }
    const double maximum = *std::max_element(values.begin(), values.end());
    double total = 0.0;
    for (double& value : values) {
        value = std::exp(value - maximum);
        total += value;
    }
    for (double& value : values) value /= total;
    return values;
}

OnlineProneClassifier::OnlineProneClassifier(PortableMlp model) : model_(std::move(model)) {
    model_.validate(false);
    const auto found = std::find(model_.classes.begin(), model_.classes.end(), "PRONE");
    if (found == model_.classes.end()) throw std::invalid_argument("temporal model has no PRONE class");
    prone_index_ = static_cast<std::size_t>(std::distance(model_.classes.begin(), found));
}

std::optional<double> OnlineProneClassifier::update(
    std::int64_t timestamp_ms,
    const std::vector<double>& feature) {
    if (timestamp_ms < 0 || feature.size() != model_.feature_dim) {
        throw std::invalid_argument("invalid online classifier update");
    }
    if (!records_.empty() && timestamp_ms < records_.back().first) {
        throw std::invalid_argument("online classifier timestamps must be monotonic");
    }
    if (!records_.empty() && timestamp_ms - records_.back().first > model_.maximum_gap_ms) {
        records_.clear();
    }
    records_.emplace_back(timestamp_ms, feature);
    const std::int64_t window_start = timestamp_ms - model_.window_ms;
    while (records_.size() > 1 && records_[1].first <= window_start) records_.pop_front();
    if (records_.empty() || records_.front().first > window_start) return std::nullopt;

    std::vector<double> flattened;
    flattened.reserve(model_.steps * model_.feature_dim);
    std::size_t right = 1;
    for (std::size_t step = 0; step < model_.steps; ++step) {
        const double target = static_cast<double>(window_start) +
            static_cast<double>(timestamp_ms - window_start) * step / (model_.steps - 1);
        while (right < records_.size() && records_[right].first < target) ++right;
        if (right >= records_.size()) right = records_.size() - 1;
        const std::size_t left = right == 0 ? 0 : right - 1;
        const double left_time = records_[left].first;
        const double right_time = records_[right].first;
        const double ratio = right_time == left_time ? 0.0 : (target - left_time) / (right_time - left_time);
        for (std::size_t feature_index = 0; feature_index < model_.feature_dim; ++feature_index) {
            const double first = records_[left].second[feature_index];
            const double second = records_[right].second[feature_index];
            flattened.push_back(first + ratio * (second - first));
        }
    }
    const auto probabilities = model_.predict_probabilities(flattened);
    return probabilities.at(prone_index_);
}

void OnlineProneClassifier::reset() {
    records_.clear();
}

}  // namespace distraction
