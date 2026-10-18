#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "stai_mpu_network.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: npu_smoke <model.nb>\n";
        return 2;
    }

    try {
        stai_mpu_network net(std::string(argv[1]), true);
        const int inputs = net.get_num_inputs();
        const int outputs = net.get_num_outputs();
        std::cout << "stai_mpu model loaded\n";
        std::cout << "inputs=" << inputs << " outputs=" << outputs << "\n";

        auto input_infos = net.get_input_infos();
        auto output_infos = net.get_output_infos();
        for (std::size_t i = 0; i < input_infos.size(); ++i) {
            auto shape = input_infos[i].get_shape();
            std::cout << "input" << i << "_shape=";
            for (std::size_t j = 0; j < shape.size(); ++j) {
                if (j) std::cout << "x";
                std::cout << shape[j];
            }
            std::cout << "\n";
        }
        for (std::size_t i = 0; i < output_infos.size(); ++i) {
            auto shape = output_infos[i].get_shape();
            std::cout << "output" << i << "_shape=";
            for (std::size_t j = 0; j < shape.size(); ++j) {
                if (j) std::cout << "x";
                std::cout << shape[j];
            }
            std::cout << "\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "stai_mpu load failed: " << e.what() << "\n";
        return 1;
    }
}
