#pragma once

#include "app/BoundedQueue.h"
#include "app/Module.h"
#include "behavior/PhoneFactBuffer.h"
#include "camera/CameraFrame.h"
#include "camera/PreviewFrameStore.h"
#include "session/SessionSnapshotPublisher.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace edgefocus {

class DistractionPipelineFeature;
class SystemStateStore;

class BoardDemoRuntime final : public Module {
public:
    enum class Mode : std::uint8_t {
        LiveCamera,
        RecordedNpu,
        FrontendReplay,
    };

    struct Status {
        Mode mode{Mode::LiveCamera};
        std::uint64_t run_id{};
        std::int64_t media_timestamp_ms{};
        bool video_ready{};
        bool replay_ready{};
        bool phone_controls_enabled{};
        bool automatic_fallback{};
        std::string reason;
        std::string video_path;
    };

    explicit BoardDemoRuntime(SystemStateStore& compatibility_store,
                              std::string video_path = {});
    ~BoardDemoRuntime() override;

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

    bool startRecorded(std::string* error = nullptr);
    void startFrontendReplay(std::string reason = "forced_by_operator");
    bool stop(std::string* error = nullptr);
    bool acquireForFormalSession(std::string* error = nullptr);
    void releaseFormalSession();
    bool publishPhone(const std::string& category, std::string* error = nullptr);

    Status status() const;
    session::SessionSnapshot snapshot() const { return snapshots_.read(); }
    PreviewFrameStore& previewFrames() { return preview_frames_; }
    const PreviewFrameStore& previewFrames() const { return preview_frames_; }

private:
    using FrameQueue = BoundedQueue<CameraFrame>;

    static std::int64_t monotonicMs();
    static std::string configuredVideoPath(std::string requested);
    static std::string configuredReplayPath(const std::string& fallback);
    static std::string selectVideoForRun(const std::string& current,
                                         std::uint64_t run_id);
    static std::string selectReplayForRun(const std::string& current,
                                          std::uint64_t run_id);
    void feedLoop();
    void publishPhoneHeartbeat(std::int64_t now_ms, bool force);
    void enterReplay(bool automatic, std::string reason,
                     bool only_if_recorded = false);

    SystemStateStore& compatibility_store_;
    std::string video_path_;
    std::string replay_path_;
    FrameQueue frames_{3, true};
    PhoneFactBuffer phone_facts_;
    session::SessionSnapshotPublisher snapshots_{"focus-app-demo"};
    PreviewFrameStore preview_frames_;
    std::unique_ptr<DistractionPipelineFeature> pipeline_;

    mutable std::mutex mutex_;
    mutable std::recursive_mutex lifecycle_mutex_;
    std::thread feed_thread_;
    std::atomic<bool> feed_running_{false};
    std::atomic<Mode> mode_{Mode::LiveCamera};
    std::atomic<std::uint64_t> run_id_{0};
    std::atomic<std::int64_t> media_timestamp_ms_{0};
    std::atomic<bool> formal_reserved_{false};
    std::int64_t recorded_started_ms_{};
    std::int64_t last_phone_heartbeat_ms_{};
    std::uint64_t phone_sequence_{};
    bool phone_active_{};
    std::optional<session::PhoneCategory> phone_category_;
    bool automatic_fallback_{};
    std::string reason_;
    std::string demo_session_id_;
};

const char* boardDemoModeName(BoardDemoRuntime::Mode mode);

}  // namespace edgefocus
