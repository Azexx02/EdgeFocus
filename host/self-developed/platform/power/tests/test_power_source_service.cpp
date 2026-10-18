#include "platform/power/PowerSourceService.h"

#include <cassert>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <string>

using namespace edgefocus::power;

namespace {

struct ReadResult {
    bool success{};
    std::uint8_t port_status{};
    std::uint8_t monitoring_status{};
};

class FakePowerSourceIo final : public IPowerSourceIo {
public:
    bool open(std::string& error) override {
        ++open_calls;
        if (open_calls <= fail_open_calls) {
            error = "injected open failure";
            return false;
        }
        opened = true;
        error.clear();
        return true;
    }

    void close() noexcept override {
        opened = false;
        ++close_calls;
    }

    bool readStatus(std::uint8_t& port_status, std::uint8_t& monitoring_status,
                    std::string& error) override {
        assert(opened);
        ++read_calls;
        assert(!reads.empty());
        const auto result = reads.front();
        reads.pop_front();
        if (!result.success) {
            error = "injected read failure";
            return false;
        }
        port_status = result.port_status;
        monitoring_status = result.monitoring_status;
        error.clear();
        return true;
    }

    int fail_open_calls{1};
    int open_calls{};
    int close_calls{};
    int read_calls{};
    bool opened{};
    std::deque<ReadResult> reads;
};

}  // namespace

int main() {
    WorkloadProfilePolicy policy;
    auto io = std::make_unique<FakePowerSourceIo>();
    auto* fake = io.get();
    std::int64_t now_ms = 0;
    PowerSourceService service(std::move(io), policy, [&now_ms] { return now_ms; },
                               [](std::chrono::milliseconds) {});
    int publish_count = 0;
    SourceSnapshot published_source;
    ProfileSnapshot published_profile;
    std::int64_t published_at_ms = -1;
    service.setSnapshotPublisher(
        [&](const SourceSnapshot& source, const ProfileSnapshot& profile,
            std::int64_t published_at) {
            ++publish_count;
            published_source = source;
            published_profile = profile;
            published_at_ms = published_at;
        });

    std::string error;
    assert(!service.start(&error));
    assert(!service.available());
    assert(!error.empty());
    assert(policy.snapshot().effective_profile == WorkloadProfile::Go);
    assert(publish_count == 1);
    assert(!published_source.sample_available);
    assert(published_profile.source == ProfileSource::AutoFailsafe);
    assert(published_at_ms == 0);

    // The optional service reconnects without preventing application startup.
    service.sampleOnce();
    assert(service.available());
    assert(fake->open_calls == 2);
    assert(fake->read_calls == 0);
    assert(publish_count == 2);
    assert(!published_source.sample_available);

    fake->reads.push_back({true, 0x00, 0x00});
    fake->reads.push_back({true, 0x00, 0x00});
    fake->reads.push_back({true, 0x00, 0x00});
    for (int i = 0; i < 3; ++i) {
        now_ms += 250;
        service.sampleOnce();
    }
    assert(service.snapshot().state == ExternalPowerState::Absent);
    assert(policy.snapshot().source == ProfileSource::AutoNoExternal);
    assert(published_source.sample_available);
    assert(published_source.state == ExternalPowerState::Absent);
    assert(published_profile.effective_profile == WorkloadProfile::Go);
    assert(published_at_ms == now_ms);

    // Manual selection survives UNKNOWN and same-state confirmation.
    policy.setManualOverride(WorkloadProfile::Home);
    fake->reads.push_back({true, 0x01, 0x00});
    now_ms += 250;
    service.sampleOnce();
    assert(policy.snapshot().manual_override_active);
    assert(policy.snapshot().effective_profile == WorkloadProfile::Home);

    fake->reads.push_back({true, 0x00, 0x00});
    fake->reads.push_back({true, 0x00, 0x00});
    fake->reads.push_back({true, 0x00, 0x00});
    for (int i = 0; i < 3; ++i) {
        now_ms += 250;
        service.sampleOnce();
    }
    assert(policy.snapshot().manual_override_active);

    // A confirmed power transition clears the manual override.
    fake->reads.push_back({true, 0x01, 0x08});
    fake->reads.push_back({true, 0x01, 0x08});
    fake->reads.push_back({true, 0x01, 0x08});
    for (int i = 0; i < 3; ++i) {
        now_ms += 250;
        service.sampleOnce();
    }
    const auto present = policy.snapshot();
    assert(!present.manual_override_active);
    assert(present.external_power_state == ExternalPowerState::Present);
    assert(present.effective_profile == WorkloadProfile::Home);
    assert(present.power_epoch == 1);

    // Three consecutive I2C failures degrade to UNKNOWN/Go.
    fake->reads.push_back({false, 0x00, 0x00});
    fake->reads.push_back({false, 0x00, 0x00});
    fake->reads.push_back({false, 0x00, 0x00});
    for (int i = 0; i < 3; ++i) {
        now_ms += 250;
        service.sampleOnce();
    }
    assert(service.snapshot().state == ExternalPowerState::Unknown);
    assert(policy.snapshot().source == ProfileSource::AutoFailsafe);
    assert(policy.snapshot().effective_profile == WorkloadProfile::Go);

    service.stop();
    assert(!service.available());
    assert(fake->close_calls == 1);

    std::cout << "power source service tests passed\n";
    return 0;
}
