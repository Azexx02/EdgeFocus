#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace distraction {

struct DenseLayer {
    std::size_t input_size{};
    std::size_t output_size{};
    std::vector<double> weights;  // row-major [input_size, output_size]
    std::vector<double> bias;
};

struct PortableMlp {
    std::vector<std::string> classes;
    std::size_t steps{};
    std::size_t feature_dim{};
    std::int64_t window_ms{};
    std::int64_t maximum_gap_ms{};
    std::vector<double> mean;
    std::vector<double> scale;
    std::vector<DenseLayer> layers;
    std::string activation{"relu"};

    void validate(bool frozen_prone_contract = false) const;
    std::vector<double> predict_probabilities(const std::vector<double>& flattened) const;
};

class OnlineProneClassifier {
public:
    explicit OnlineProneClassifier(PortableMlp model);
    std::optional<double> update(std::int64_t timestamp_ms, const std::vector<double>& feature);
    void reset();

private:
    PortableMlp model_;
    std::size_t prone_index_{};
    std::deque<std::pair<std::int64_t, std::vector<double>>> records_;
};

}  // namespace distraction
