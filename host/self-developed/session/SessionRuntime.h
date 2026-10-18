#pragma once

#include "session/ISessionFeature.h"

#include <memory>
#include <string>
#include <vector>

namespace edgefocus::session {

class SessionRuntime {
public:
    struct FeatureEntry {
        std::unique_ptr<ISessionFeature> feature;
        bool required{};
        bool prepared{};
        bool degraded{};
    };

    explicit SessionRuntime(SessionContext context);
    ~SessionRuntime();

    void addFeature(std::unique_ptr<ISessionFeature> feature, bool required);
    bool prepare(std::string* error = nullptr);
    bool beginFormalSession(const FormalStartContext& context, std::string* error = nullptr);
    void enterPhase(Phase phase);
    void stop(StopReason reason) noexcept;

    const SessionContext& context() const noexcept { return context_; }
    Phase phase() const noexcept { return phase_; }
    bool prepared() const noexcept { return prepared_; }
    bool stopped() const noexcept { return stopped_; }
    bool formalStarted() const noexcept { return formal_started_; }
    std::vector<std::string> degradedFeatures() const;

private:
    SessionContext context_;
    std::vector<FeatureEntry> features_;
    Phase phase_{Phase::Idle};
    bool prepared_{};
    bool formal_started_{};
    bool stopped_{};
};

}  // namespace edgefocus::session
