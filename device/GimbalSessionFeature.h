#pragma once

#include "session/ISessionFeature.h"

namespace edgefocus {

class GimbalMonitor;
class SystemStateStore;
namespace session {
class SessionSnapshotPublisher;
}

class GimbalSessionFeature final : public session::ISessionFeature {
public:
    GimbalSessionFeature(GimbalMonitor& monitor,
                         SystemStateStore& store,
                         session::SessionSnapshotPublisher& snapshots);

    const char* name() const noexcept override { return "gimbal"; }
    session::PrepareResult prepare(const session::SessionContext& context) override;
    void enterPhase(session::Phase phase) override;
    void stop(session::StopReason reason) noexcept override;
    session::FeatureHealth health() const override;

private:
    void publish(session::Phase phase);

    GimbalMonitor& monitor_;
    SystemStateStore& store_;
    session::SessionSnapshotPublisher& snapshots_;
    session::FeatureHealth health_;
    bool active_{};
};

}  // namespace edgefocus
