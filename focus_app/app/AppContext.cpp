#include "AppContext.h"
#include "Module.h"
#include <chrono>
#include <thread>
#include <csignal>

namespace edgefocus {

AppContext* AppContext::s_instance = nullptr;

AppContext::AppContext() : running_(false) {
    s_instance = this;
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);
}

AppContext::~AppContext() {
    if (running()) stopAll();
    s_instance = nullptr;
}

void AppContext::signalHandler(int signum) {
    (void)signum;
    if (s_instance) {
        s_instance->requestShutdown();
    }
}

void AppContext::addModule(std::unique_ptr<Module> mod) {
    modules_.push_back(std::move(mod));
}

bool AppContext::startAll() {
    running_.store(true, std::memory_order_release);

    for (auto& mod : modules_) {
        LOGI("AppContext", "Initializing module: %s", mod->name().c_str());
        if (!mod->init(*this)) {
            LOGE("AppContext", "Module %s failed to init", mod->name().c_str());
            running_.store(false, std::memory_order_release);
            return false;
        }
    }

    for (auto& mod : modules_) {
        Module* raw = mod.get();
        threads_.emplace_back([this, raw]() {
            raw->markRunning();
            LOGI(raw->name().c_str(), "Thread started");
            while (running() && raw->isRunning()) {
                raw->runOnce(*this);
            }
            LOGI(raw->name().c_str(), "Thread stopped");
        });
    }

    LOGI("AppContext", "All %zu modules started", modules_.size());
    return true;
}

void AppContext::stopAll() {
    LOGI("AppContext", "Stopping all modules...");
    running_.store(false, std::memory_order_release);

    for (auto& mod : modules_) {
        mod->requestStop();
    }

    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();

    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
        (*it)->shutdown(*this);
    }
    LOGI("AppContext", "All modules stopped");
}

void AppContext::requestShutdown() {
    running_.store(false, std::memory_order_release);
}

void AppContext::waitForShutdown() {
    while (running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

} // namespace edgefocus
