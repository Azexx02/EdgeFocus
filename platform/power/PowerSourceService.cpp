#include "platform/power/PowerSourceService.h"

#include "app/AppContext.h"
#include "app/Logger.h"

#include <thread>
#include <utility>

namespace edgefocus::power {

std::int64_t PowerSourceService::defaultClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

PowerSourceService::PowerSourceService(std::unique_ptr<IPowerSourceIo> io,
                                       WorkloadProfilePolicy& policy, Clock clock,
                                       Sleeper sleeper)
    : Module("PowerSourceService"),
      io_(std::move(io)),
      policy_(policy),
      clock_(clock ? std::move(clock) : Clock(defaultClockMs)),
      sleeper_(sleeper ? std::move(sleeper)
                       : Sleeper([](std::chrono::milliseconds delay) {
                             std::this_thread::sleep_for(delay);
                         })) {}

PowerSourceService::~PowerSourceService() { stop(); }

bool PowerSourceService::init(AppContext& /*ctx*/) {
    std::string error;
    if (!start(&error)) {
        LOGW("PowerSourceService", "STUSB4500 unavailable: %s", error.c_str());
        // This service is optional. UNKNOWN/Go is the fail-safe startup state.
        return true;
    }
    return true;
}

void PowerSourceService::runOnce(AppContext& /*ctx*/) {
    sampleOnce();
    sleeper_(std::chrono::milliseconds(250));
}

void PowerSourceService::shutdown(AppContext& /*ctx*/) { stop(); }

bool PowerSourceService::start(std::string* error) {
    SnapshotPublisher publisher;
    SourceSnapshot source;
    ProfileSnapshot profile;
    bool available = false;
    std::string local_error;
    const auto now = clock_();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_) return available_;
        started_ = true;
        available_ = io_ && io_->open(local_error);
        available = available_;
        policy_.updateExternalPower(ExternalPowerState::Unknown);
        source = debouncer_.snapshot();
        profile = policy_.snapshot();
        publisher = publisher_;
    }
    if (publisher) publisher(source, profile, now);
    if (error) *error = local_error;
    return available;
}

void PowerSourceService::stop() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (io_) io_->close();
    started_ = false;
    available_ = false;
}

void PowerSourceService::sampleOnce() {
    const auto now = clock_();
    SourceSnapshot next;
    ProfileSnapshot profile;
    SnapshotPublisher publisher;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::uint8_t port = 0;
        std::uint8_t monitoring = 0;
        std::string error;
        if (available_ && io_->readStatus(port, monitoring, error)) {
            next = debouncer_.updateSuccess(port, monitoring, now);
        } else {
            next = debouncer_.updateFailure(now);
            if (!available_ && io_ && io_->open(error)) available_ = true;
        }
        policy_.updateExternalPower(next.state);
        profile = policy_.snapshot();
        publisher = publisher_;
    }
    if (publisher) publisher(next, profile, now);
}

SourceSnapshot PowerSourceService::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return debouncer_.snapshot();
}

bool PowerSourceService::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_;
}

void PowerSourceService::setSnapshotPublisher(SnapshotPublisher publisher) {
    std::lock_guard<std::mutex> lock(mutex_);
    publisher_ = std::move(publisher);
}

}  // namespace edgefocus::power
