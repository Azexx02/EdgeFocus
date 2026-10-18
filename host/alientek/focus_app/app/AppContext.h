#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <csignal>

#include "Logger.h"

namespace edgefocus {

class Module;

/// Central application context.
/// Owns the running flag, registered modules, and their threads.
class AppContext {
public:
    AppContext();
    ~AppContext();

    /// Register a module. Must be called before startAll().
    void addModule(std::unique_ptr<Module> mod);

    /// Initialize all modules and start their threads.
    /// Returns false if any module fails to init.
    bool startAll();

    /// Request all modules to stop and join their threads.
    void stopAll();

    /// True while the application is running.
    bool running() const { return running_.load(std::memory_order_acquire); }

    /// Signal handler sets this to false.
    void requestShutdown();

    /// Block the calling thread until shutdown is requested.
    void waitForShutdown();

private:
    std::atomic<bool> running_;
    std::vector<std::unique_ptr<Module>> modules_;
    std::vector<std::thread> threads_;

    static AppContext* s_instance;
    static void signalHandler(int signum);
};

} // namespace edgefocus
