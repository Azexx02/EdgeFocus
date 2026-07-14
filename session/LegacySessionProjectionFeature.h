#pragma once

#include "session/ISessionFeature.h"

namespace edgefocus {

class SQLiteStore;
class SystemStateStore;

namespace session {

class LegacySessionProjectionFeature final : public ISessionFeature {
public:
    LegacySessionProjectionFeature(SystemStateStore& store, SQLiteStore& sqlite_store);

    const char* name() const noexcept override { return "legacy_session_projection"; }
    PrepareResult prepare(const SessionContext& context) override;
    void enterPhase(Phase phase) override;
    void stop(StopReason reason) noexcept override;
    FeatureHealth health() const override;

private:
    static std::int64_t monotonicMs();
    static const char* stopReasonName(StopReason reason);

    SystemStateStore& store_;
    SQLiteStore& sqlite_store_;
    SessionContext context_;
    FeatureHealth health_;
    bool active_{};
};

}  // namespace session
}  // namespace edgefocus
