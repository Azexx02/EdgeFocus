#include "session/SessionRuntime.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace edgefocus::session {

SessionRuntime::SessionRuntime(SessionContext context) : context_(std::move(context)) {}

SessionRuntime::~SessionRuntime() {
    stop(StopReason::ProcessShutdown);
}

void SessionRuntime::addFeature(std::unique_ptr<ISessionFeature> feature, bool required) {
    if (!feature) throw std::invalid_argument("session feature is null");
    if (prepared_ || stopped_) throw std::logic_error("cannot add feature after prepare");
    features_.push_back({std::move(feature), required, false, false});
}

bool SessionRuntime::prepare(std::string* error) {
    if (stopped_) {
        if (error) *error = "runtime already stopped";
        return false;
    }
    if (prepared_) return true;

    for (auto& entry : features_) {
        PrepareResult result;
        try {
            result = entry.feature->prepare(context_);
        } catch (const std::exception& exception) {
            result = PrepareResult::failure(exception.what());
        } catch (...) {
            result = PrepareResult::failure("unknown prepare exception");
        }

        if (!result.ready && entry.required) {
            if (error) {
                *error = std::string(entry.feature->name()) + ": " + result.detail;
            }
            stop(StopReason::PrepareFailed);
            return false;
        }
        entry.prepared = result.ready;
        entry.degraded = result.degraded || (!result.ready && !entry.required);
    }

    prepared_ = true;
    return true;
}

void SessionRuntime::enterPhase(Phase phase) {
    if (!prepared_ || stopped_) throw std::logic_error("runtime is not active");
    for (auto& entry : features_) {
        if (!entry.prepared) continue;
        try {
            entry.feature->enterPhase(phase);
        } catch (...) {
            entry.degraded = true;
            if (entry.required) throw;
        }
    }
    phase_ = phase;
}

void SessionRuntime::stop(StopReason reason) noexcept {
    if (stopped_) return;
    for (auto iterator = features_.rbegin(); iterator != features_.rend(); ++iterator) {
        if (!iterator->prepared) continue;
        try {
            iterator->feature->stop(reason);
        } catch (...) {
            // Session shutdown is best-effort and must remain noexcept.
        }
        iterator->prepared = false;
    }
    prepared_ = false;
    stopped_ = true;
    phase_ = Phase::Ending;
}

std::vector<std::string> SessionRuntime::degradedFeatures() const {
    std::vector<std::string> names;
    for (const auto& entry : features_) {
        if (entry.degraded) names.emplace_back(entry.feature->name());
    }
    return names;
}

}  // namespace edgefocus::session
