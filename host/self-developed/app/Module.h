#pragma once
#include <string>
#include <atomic>

namespace edgefocus {

class AppContext;

/// Base class for all focus_app modules.
/// Each module runs its own thread loop and must support graceful shutdown.
class Module {
    friend class AppContext;
public:
    explicit Module(const std::string& name) : name_(name), running_(false) {}
    virtual ~Module() = default;

    const std::string& name() const { return name_; }
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }

    /// Called once before the thread starts. Return false on failure.
    virtual bool init(AppContext& ctx) = 0;

    /// Main loop body. Called repeatedly while running() is true.
    /// Should return quickly when stop is requested.
    virtual void runOnce(AppContext& ctx) = 0;

    /// Called once after the thread exits. Clean up resources here.
    virtual void shutdown(AppContext& ctx) = 0;

    void requestStop() { running_.store(false, std::memory_order_release); }

protected:
    void markRunning() { running_.store(true, std::memory_order_release); }

private:
    std::string name_;
    std::atomic<bool> running_;
};

} // namespace edgefocus
