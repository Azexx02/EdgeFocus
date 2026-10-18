#include "distraction/temporal.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace distraction;
    PortableMlp model;
    model.classes = {"NORMAL_STUDY", "PRONE"};
    model.steps = 3;
    model.feature_dim = 1;
    model.window_ms = 2000;
    model.maximum_gap_ms = 500;
    model.mean = {0, 0, 0};
    model.scale = {1, 1, 1};
    model.layers = {DenseLayer{3, 1, {1, 1, 1}, {0}}};
    OnlineProneClassifier classifier(model);
    require(!classifier.update(0, {0}).has_value(), "initial temporal warmup");
    require(!classifier.update(500, {0.5}).has_value(), "500ms gap remains contiguous");
    require(!classifier.update(1000, {1}).has_value(), "window not full");
    require(!classifier.update(1500, {1.5}).has_value(), "window still not full");
    const auto probability = classifier.update(2000, {2});
    require(probability.has_value(), "inclusive 2000ms window is ready");
    require(std::abs(*probability - (1.0 / (1.0 + std::exp(-3.0)))) < 1e-12, "interpolation and MLP parity");
    require(!classifier.update(2501, {2.5}).has_value(), ">500ms gap resets history");
    std::cout << "temporal tests passed\n";
    return 0;
}
