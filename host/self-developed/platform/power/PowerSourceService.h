#pragma once

#include "app/Module.h"
#include "platform/power/IPowerSourceIo.h"
#include "platform/power/Stusb4500Status.h"
#include "platform/power/WorkloadProfilePolicy.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace edgefocus::power {

class PowerSourceService final : public Module {
public:
    using Clock = std::function<std::int64_t()>;
    using Sleeper = std::function<void(std::chrono::milliseconds)>;
    using SnapshotPublisher =
        std::function<void(const SourceSnapshot&, const ProfileSnapshot&, std::int64_t)>;

    PowerSourceService(std::unique_ptr<IPowerSourceIo> io, WorkloadProfilePolicy& policy,
                       Clock clock = {}, Sleeper sleeper = {});
    ~PowerSourceService() override;

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

    bool start(std::string* error = nullptr);
    void stop() noexcept;
    void sampleOnce();
    SourceSnapshot snapshot() const;
    bool available() const;
    void setSnapshotPublisher(SnapshotPublisher publisher);

private:
    static std::int64_t defaultClockMs();

    std::unique_ptr<IPowerSourceIo> io_;
    WorkloadProfilePolicy& policy_;
    Clock clock_;
    Sleeper sleeper_;
    mutable std::mutex mutex_;
    SourceDebouncer debouncer_;
    SnapshotPublisher publisher_;
    bool started_{};
    bool available_{};
};

}  // namespace edgefocus::power
