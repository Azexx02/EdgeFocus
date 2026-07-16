#include "demo/BoardDemoRuntime.h"

#include "app/AppContext.h"
#include "app/Logger.h"
#include "app/SystemStateStore.h"
#include "vision/DistractionPipelineFeature.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <utility>
#include <vector>

#ifdef EDGEFOCUS_WITH_OPENCV
#include <opencv2/videoio.hpp>
#endif

namespace edgefocus {

namespace {

constexpr std::int64_t kPhoneHeartbeatMs = 2000;
constexpr std::int64_t kPhoneFreshnessMs = 5000;
constexpr std::int64_t kInferenceStartupGraceMs = 3500;
constexpr std::int64_t kInferenceTimeoutMs = 2000;

}  // namespace

const char* boardDemoModeName(BoardDemoRuntime::Mode mode) {
    switch (mode) {
        case BoardDemoRuntime::Mode::LiveCamera: return "LIVE_CAMERA";
        case BoardDemoRuntime::Mode::RecordedNpu: return "RECORDED_NPU";
        case BoardDemoRuntime::Mode::FrontendReplay: return "FRONTEND_REPLAY";
    }
    return "LIVE_CAMERA";
}

BoardDemoRuntime::BoardDemoRuntime(SystemStateStore& compatibility_store,
                                   std::string video_path)
    : Module("BoardDemoRuntime"),
      compatibility_store_(compatibility_store),
      video_path_(configuredVideoPath(std::move(video_path))),
      replay_path_(configuredReplayPath(video_path_)) {}

BoardDemoRuntime::~BoardDemoRuntime() {
    stop();
}

std::int64_t BoardDemoRuntime::monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string BoardDemoRuntime::configuredVideoPath(std::string requested) {
    if (!requested.empty()) return requested;
    const char* configured = std::getenv("EDGEFOCUS_DEMO_VIDEO");
    if (configured && *configured) return configured;

    constexpr const char* kShowcaseVideo = "assets/demo/p01_showcase.mp4";
    if (std::filesystem::exists(kShowcaseVideo)) return kShowcaseVideo;

    // Accept the unedited filename used while exhibition material is still being
    // assembled.  An explicit environment variable or the frozen showcase name
    // always wins once the final cut is ready.
    constexpr const char* kWorkingVideo = "assets/demo/p01.mp4";
    if (std::filesystem::exists(kWorkingVideo)) return kWorkingVideo;
    return kShowcaseVideo;
}

std::string BoardDemoRuntime::configuredReplayPath(const std::string& fallback) {
    if (const char* configured = std::getenv("EDGEFOCUS_REPLAY_VIDEO");
        configured && *configured) {
        return configured;
    }
    constexpr const char* kReplayVideo =
        "assets/demo/no_npu/segments/r01.mp4";
    return std::filesystem::exists(kReplayVideo) ? kReplayVideo : fallback;
}

std::string BoardDemoRuntime::selectVideoForRun(const std::string& current,
                                                std::uint64_t run_id) {
    if (const char* configured = std::getenv("EDGEFOCUS_DEMO_VIDEO");
        configured && *configured) {
        return current;
    }

    const std::filesystem::path current_path(current);
    const auto current_name = current_path.filename().string();
    const auto current_stem = current_path.stem().string();
    const bool numbered_clip =
        current_path.extension() == ".mp4" && current_stem.size() == 3 &&
        current_stem[0] == 'p' &&
        std::isdigit(static_cast<unsigned char>(current_stem[1])) &&
        std::isdigit(static_cast<unsigned char>(current_stem[2]));
    if (!numbered_clip && current_name != "p01_showcase.mp4") {
        return current;
    }

    const auto directory = current_path.has_parent_path()
                               ? current_path.parent_path()
                               : std::filesystem::path(".");
    std::vector<std::filesystem::path> clips;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        const auto path = it->path();
        const auto stem = path.stem().string();
        if (path.extension() == ".mp4" && stem.size() == 3 && stem[0] == 'p' &&
            std::isdigit(static_cast<unsigned char>(stem[1])) &&
            std::isdigit(static_cast<unsigned char>(stem[2]))) {
            clips.push_back(path);
        }
    }
    if (clips.empty()) return current;
    std::sort(clips.begin(), clips.end());
    return clips[run_id % clips.size()].string();
}

std::string BoardDemoRuntime::selectReplayForRun(const std::string& current,
                                                 std::uint64_t run_id) {
    if (const char* configured = std::getenv("EDGEFOCUS_REPLAY_VIDEO");
        configured && *configured) {
        return current;
    }
    const std::filesystem::path directory =
        "assets/demo/no_npu/segments";
    std::vector<std::filesystem::path> clips;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        const auto path = it->path();
        const auto stem = path.stem().string();
        if (path.extension() == ".mp4" && stem.size() == 3 && stem[0] == 'r' &&
            std::isdigit(static_cast<unsigned char>(stem[1])) &&
            std::isdigit(static_cast<unsigned char>(stem[2]))) {
            clips.push_back(path);
        }
    }
    if (clips.empty()) return current;
    std::sort(clips.begin(), clips.end());
    return clips[run_id % clips.size()].string();
}

bool BoardDemoRuntime::init(AppContext&) {
    LOGI("BoardDemoRuntime", "Ready; recorded input=%s; replay input=%s",
         video_path_.c_str(), replay_path_.c_str());
    return true;
}

void BoardDemoRuntime::runOnce(AppContext&) {
    if (mode_.load() == Mode::RecordedNpu) {
        const auto now_ms = monotonicMs();
        const auto visual = snapshots_.read().visual;
        if (recorded_started_ms_ > 0 &&
            now_ms - recorded_started_ms_ > kInferenceStartupGraceMs &&
            (visual.evidence_timestamp_ms <= 0 ||
             now_ms - visual.evidence_timestamp_ms > kInferenceTimeoutMs)) {
            // The operator may have stopped the demo after this watchdog read
            // the old snapshot.  Re-check the mode while holding the lifecycle
            // lock before an automatic fallback is allowed to take ownership.
            enterReplay(true, "npu_evidence_timeout", true);
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void BoardDemoRuntime::shutdown(AppContext&) {
    stop();
}

bool BoardDemoRuntime::startRecorded(std::string* error) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mutex_);
    stop();
    if (formal_reserved_.load()) {
        if (error) *error = "formal session owns the vision runtime";
        return false;
    }
    std::string selected_video;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        video_path_ = selectVideoForRun(video_path_, run_id_.load());
        selected_video = video_path_;
    }
    if (!std::filesystem::exists(selected_video)) {
        enterReplay(true, "recorded_video_missing");
        if (error) *error = "recorded video is missing: " + selected_video;
        return false;
    }

    const auto now_ms = monotonicMs();
    session::SessionContext context;
    context.session_id = "demo_" + std::to_string(now_ms);
    context.source_mode = session::SourceMode::RecordedInput;
    context.started_monotonic_ms = now_ms;
    context.started_wall_ms = 0;
    context.target_duration_ms = 24 * 60 * 60 * 1000LL;
    snapshots_.begin(context, now_ms);

    auto pipeline = std::make_unique<DistractionPipelineFeature>(
        frames_, phone_facts_, snapshots_, compatibility_store_, std::string{}, std::string{}, false);
    const auto prepared = pipeline->prepare(context);
    if (!prepared.ready) {
        snapshots_.end(monotonicMs());
        enterReplay(true, prepared.detail.empty() ? "npu_prepare_failed" : prepared.detail);
        if (error) *error = prepared.detail;
        return false;
    }
    if (formal_reserved_.load()) {
        pipeline->stop(session::StopReason::Cancelled);
        snapshots_.end(monotonicMs());
        if (error) *error = "formal session acquired the vision runtime";
        return false;
    }
    const auto demo_started = pipeline->beginFormalSession({now_ms, 0});
    if (!demo_started.ready) {
        pipeline->stop(session::StopReason::PrepareFailed);
        snapshots_.end(monotonicMs());
        enterReplay(true, demo_started.detail.empty() ? "npu_start_failed"
                                                      : demo_started.detail);
        if (error) *error = demo_started.detail;
        return false;
    }
    pipeline->enterPhase(session::Phase::Focusing);
    snapshots_.setPhase(session::Phase::Focusing, now_ms);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_ = std::move(pipeline);
        demo_session_id_ = context.session_id;
        phone_sequence_ = 0;
        phone_active_ = false;
        phone_category_.reset();
        last_phone_heartbeat_ms_ = 0;
        automatic_fallback_ = false;
        reason_.clear();
        recorded_started_ms_ = now_ms;
        media_timestamp_ms_ = 0;
        ++run_id_;
        mode_ = Mode::RecordedNpu;
        feed_running_ = true;
    }
    LOGI("BoardDemoRuntime", "Starting recorded clip: %s", selected_video.c_str());
    feed_thread_ = std::thread(&BoardDemoRuntime::feedLoop, this);
    return true;
}

void BoardDemoRuntime::startFrontendReplay(std::string reason) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mutex_);
    stop();
    if (formal_reserved_.load()) return;
    enterReplay(false, std::move(reason));
}

void BoardDemoRuntime::enterReplay(bool automatic, std::string reason,
                                   bool only_if_recorded) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mutex_);
    if (formal_reserved_.load()) return;
    if (only_if_recorded && mode_.load() != Mode::RecordedNpu) return;
    if (mode_.load() == Mode::RecordedNpu) stop();
    std::lock_guard<std::mutex> lock(mutex_);
    replay_path_ = selectReplayForRun(replay_path_, run_id_.load());
    automatic_fallback_ = automatic;
    reason_ = std::move(reason);
    media_timestamp_ms_ = 0;
    ++run_id_;
    mode_ = Mode::FrontendReplay;
}

bool BoardDemoRuntime::acquireForFormalSession(std::string* error) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mutex_);
    formal_reserved_.store(true);
    if (!stop(error)) {
        formal_reserved_.store(false);
        return false;
    }
    return true;
}

void BoardDemoRuntime::releaseFormalSession() {
    formal_reserved_.store(false);
}

bool BoardDemoRuntime::stop(std::string*) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mutex_);
    feed_running_ = false;
    if (feed_thread_.joinable() && feed_thread_.get_id() != std::this_thread::get_id()) {
        feed_thread_.join();
    }

    std::unique_ptr<DistractionPipelineFeature> pipeline;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline = std::move(pipeline_);
        demo_session_id_.clear();
        phone_active_ = false;
        phone_category_.reset();
        media_timestamp_ms_ = 0;
        recorded_started_ms_ = 0;
        automatic_fallback_ = false;
        reason_.clear();
        mode_ = Mode::LiveCamera;
    }
    if (pipeline) pipeline->stop(session::StopReason::Cancelled);
    preview_frames_.clear();
    snapshots_.end(monotonicMs());
    while (frames_.tryPop().has_value()) {
    }
    return true;
}

bool BoardDemoRuntime::publishPhone(const std::string& category, std::string* error) {
    if (mode_.load() != Mode::RecordedNpu) {
        if (error) *error = "manual phone facts are only available in recorded NPU mode";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (category == "OFF") {
            phone_active_ = false;
            phone_category_.reset();
        } else if (category == "LEARNING") {
            phone_active_ = true;
            phone_category_ = session::PhoneCategory::Learning;
        } else if (category == "ENTERTAINMENT") {
            phone_active_ = true;
            phone_category_ = session::PhoneCategory::Entertainment;
        } else if (category == "GENERAL_OR_UNKNOWN") {
            phone_active_ = true;
            phone_category_ = session::PhoneCategory::GeneralOrUnknown;
        } else {
            if (error) *error = "invalid phone category";
            return false;
        }
    }
    publishPhoneHeartbeat(monotonicMs(), true);
    return true;
}

void BoardDemoRuntime::publishPhoneHeartbeat(std::int64_t now_ms, bool force) {
    session::PhoneFact fact;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mode_.load() != Mode::RecordedNpu || demo_session_id_.empty()) return;
        if (!force && now_ms - last_phone_heartbeat_ms_ < kPhoneHeartbeatMs) return;
        last_phone_heartbeat_ms_ = now_ms;
        fact.session_id = demo_session_id_;
        fact.sequence = ++phone_sequence_;
        fact.received_monotonic_ms = now_ms;
        fact.source_timestamp_ms = now_ms;
        fact.active = phone_active_;
        fact.category = phone_category_;
        fact.fresh_for_ms = kPhoneFreshnessMs;
    }
    std::string ignored;
    phone_facts_.publish(fact, &ignored);
}

void BoardDemoRuntime::feedLoop() {
#ifdef EDGEFOCUS_WITH_OPENCV
    std::string active_video;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_video = video_path_;
    }
    cv::VideoCapture capture(active_video);
    if (!capture.isOpened()) {
        LOGW("BoardDemoRuntime", "Cannot open recorded input: %s", active_video.c_str());
        return;
    }
    double fps = capture.get(cv::CAP_PROP_FPS);
    if (!(fps >= 5.0 && fps <= 60.0)) fps = 25.0;
    auto frame_period = std::chrono::duration<double>(1.0 / fps);
    auto next_frame = std::chrono::steady_clock::now();
    auto frame_source = "recorded_" + std::filesystem::path(active_video).stem().string();

    while (feed_running_.load() && mode_.load() == Mode::RecordedNpu) {
        cv::Mat image;
        if (!capture.read(image) || image.empty()) {
            std::string next_video;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                next_video = selectVideoForRun(video_path_, run_id_.load());
                video_path_ = next_video;
            }
            capture.release();
            if (!capture.open(next_video)) {
                LOGW("BoardDemoRuntime", "Cannot open next recorded input: %s",
                     next_video.c_str());
                enterReplay(true, "recorded_playlist_open_failed", true);
                return;
            }
            fps = capture.get(cv::CAP_PROP_FPS);
            if (!(fps >= 5.0 && fps <= 60.0)) fps = 25.0;
            frame_period = std::chrono::duration<double>(1.0 / fps);
            next_frame = std::chrono::steady_clock::now();
            frame_source =
                "recorded_" + std::filesystem::path(next_video).stem().string();
            if (pipeline_) pipeline_->resetContinuity();
            preview_frames_.clear();
            media_timestamp_ms_ = 0;
            ++run_id_;
            LOGI("BoardDemoRuntime", "Advancing recorded playlist to: %s",
                 next_video.c_str());
            continue;
        }
        CameraFrame frame;
        frame.timestamp_ms = monotonicMs();
        frame.media_timestamp_ms = static_cast<std::int64_t>(
            std::max(0.0, capture.get(cv::CAP_PROP_POS_MSEC)));
        frame.width = image.cols;
        frame.height = image.rows;
        frame.channels = image.channels();
        frame.format = frame.channels == 1 ? PixelFormat::GRAY8 : PixelFormat::BGR24;
        frame.source = frame_source;
        const auto bytes = image.total() * image.elemSize();
        frame.data.assign(image.data, image.data + bytes);
        if (!frame.valid()) continue;
        media_timestamp_ms_ = frame.media_timestamp_ms;
        preview_frames_.publish(frame);
        frames_.push(frame);
        publishPhoneHeartbeat(frame.timestamp_ms, false);
        next_frame += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frame_period);
        std::this_thread::sleep_until(next_frame);
    }
#else
    LOGW("BoardDemoRuntime", "Recorded input requires OpenCV");
#endif
}

BoardDemoRuntime::Status BoardDemoRuntime::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Status value;
    value.mode = mode_.load();
    value.run_id = run_id_.load();
    value.media_timestamp_ms = media_timestamp_ms_.load();
    value.video_ready = std::filesystem::exists(video_path_);
    value.replay_ready = std::filesystem::exists(replay_path_);
    value.phone_controls_enabled = value.mode == Mode::RecordedNpu;
    value.automatic_fallback = automatic_fallback_;
    value.reason = reason_;
    value.video_path = value.mode == Mode::FrontendReplay ? replay_path_ : video_path_;
    return value;
}

}  // namespace edgefocus
