#pragma once

#include "session/SessionDomain.h"

#include <string>
#include <utility>

namespace edgefocus::session {

struct PrepareResult {
    bool ready{};
    bool degraded{};
    std::string detail;

    static PrepareResult success() { return {true, false, {}}; }
    static PrepareResult degraded_success(std::string detail) {
        return {true, true, std::move(detail)};
    }
    static PrepareResult failure(std::string detail) {
        return {false, false, std::move(detail)};
    }
};

class ISessionFeature {
public:
    virtual ~ISessionFeature() = default;
    virtual const char* name() const noexcept = 0;
    virtual PrepareResult prepare(const SessionContext& context) = 0;
    virtual PrepareResult beginFormalSession(const FormalStartContext& /*context*/) {
        return PrepareResult::success();
    }
    virtual void enterPhase(Phase phase) = 0;
    virtual void stop(StopReason reason) noexcept = 0;
    virtual FeatureHealth health() const = 0;
};

}  // namespace edgefocus::session
