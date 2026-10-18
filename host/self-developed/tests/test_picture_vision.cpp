#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cctype>
#include <string>
#include <vector>

#include "app/BoundedQueue.h"
#include "app/SystemStateStore.h"
#include "app/AppContext.h"
#include "behavior/BehaviorEngine.h"
#include "camera/CameraFrame.h"
#include "storage/SQLiteStore.h"
#include "vision/InferenceScheduler.h"

#ifdef EDGEFOCUS_WITH_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

using namespace edgefocus;

namespace fs = std::filesystem;

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string getenvSafe(const char* key) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, key) == 0 && value) {
        std::string out(value);
        std::free(value);
        return out;
    }
    return {};
#else
    const char* value = std::getenv(key);
    return value ? std::string(value) : std::string();
#endif
}

bool isImageFile(const fs::path& path) {
    if (!path.has_extension()) return false;
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

bool hasPictureDir(const fs::path& dir, std::vector<fs::path>& images) {
    images.clear();
    if (!fs::exists(dir) || !fs::is_directory(dir)) return false;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (!isImageFile(entry.path())) continue;
        images.push_back(entry.path());
    }
    return !images.empty();
}

std::vector<fs::path> guessPictureDirs(const fs::path& exe_path) {
    std::vector<fs::path> dirs;
    const std::string env_dir = getenvSafe("EDGEFOCUS_PICTURE_TEST_DIR");
    if (!env_dir.empty()) {
        dirs.push_back(fs::path(env_dir));
    }

    const fs::path cwd = fs::current_path();
    dirs.push_back(cwd);
    dirs.push_back(cwd.parent_path());
    dirs.push_back(cwd.parent_path().parent_path());

    // When launched via ctest, executable path is available and often inside build/bin.
    const fs::path exe_dir = exe_path.parent_path();
    dirs.push_back(exe_dir);
    dirs.push_back(exe_dir.parent_path());
    dirs.push_back(exe_dir.parent_path().parent_path());

    std::vector<fs::path> expanded;
    for (const auto& base : dirs) {
        if (base.empty()) continue;
        expanded.push_back(base / "picture_test");
        expanded.push_back(base);
    }
    return expanded;
}

bool resolvePictureDir(const fs::path& exe_path, fs::path& out) {
    auto dirs = guessPictureDirs(exe_path);
    for (const auto& dir : dirs) {
        std::vector<fs::path> images;
        if (hasPictureDir(dir, images)) {
            out = dir;
            return true;
        }
    }
    return false;
}

#ifdef EDGEFOCUS_WITH_OPENCV
bool loadImageToFrame(const fs::path& file, CameraFrame& frame) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return false;

    const std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(input)),
                                       std::istreambuf_iterator<char>());
    if (encoded.empty()) return false;

    const cv::Mat raw(1, static_cast<int>(encoded.size()), CV_8UC1, const_cast<uint8_t*>(encoded.data()));
    const cv::Mat decoded = cv::imdecode(raw, cv::IMREAD_UNCHANGED);
    if (decoded.empty()) return false;

    cv::Mat image;
    if (decoded.channels() == 4) {
        cv::cvtColor(decoded, image, cv::COLOR_BGRA2BGR);
    } else if (decoded.channels() == 3) {
        image = decoded;
    } else if (decoded.channels() == 1) {
        image = decoded;
    } else {
        return false;
    }

    frame.timestamp_ms = nowMs();
    frame.width = image.cols;
    frame.height = image.rows;
    frame.channels = image.channels();
    frame.format = frame.channels == 1 ? PixelFormat::GRAY8 : PixelFormat::BGR24;
    frame.data.assign(image.data, image.data + static_cast<size_t>(image.total() * image.elemSize()));
    frame.source = file.filename().string();
    return frame.valid();
}
#endif

void validateVisionResult(const VisionResult& result, const CameraFrame& frame) {
    assert(result.timestamp_ms == frame.timestamp_ms);
    assert(result.frame_quality.level != FrameQualityLevel::UNKNOWN);
    assert(result.frame_quality.valid);
    assert(result.presence.valid);
    assert(result.presence.confidence >= 0.0f && result.presence.confidence <= 1.0f);
    assert(result.head_down_score >= 0.0f && result.head_down_score <= 1.0f);
    assert(result.reason_code != ReasonCode::MODEL_NOT_LOADED);
}

void runBehaviorSmoke(const VisionResult& result) {
    BoundedQueue<VisionResult> vision_queue(4, false);
    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    assert(sqlite.init(ctx));

    BehaviorEngine behavior(vision_queue, store, sqlite);
    assert(behavior.init(ctx));

    auto now = nowMs();
    SessionInfo session;
    session.active = true;
    session.calibrating = false;
    session.calibration_completed = true;
    session.session_id = "picture_smoke";
    session.start_ms = now;
    session.elapsed_ms = 0;
    session.target_duration_ms = 60000;
    session.remaining_ms = 60000;
    store.updateSession(session);

    assert(vision_queue.push(result));
    behavior.runOnce(ctx);

    auto snap = store.snapshot();
    const int state = static_cast<int>(snap.focus_state);
    assert(state >= static_cast<int>(FocusState::FOCUSED));
    assert(state <= static_cast<int>(FocusState::PHONE_IN_PLAYING));
    behavior.shutdown(ctx);
}

} // namespace

int main(int argc, char** argv) {
    fs::path picture_dir;
    std::vector<fs::path> image_files;
    const fs::path exe_path = (argc > 0 && argv && argv[0]) ? fs::absolute(fs::path(argv[0])) : fs::current_path();

#ifdef EDGEFOCUS_WITH_OPENCV
    if (!resolvePictureDir(exe_path, picture_dir) || !hasPictureDir(picture_dir, image_files)) {
        std::cout << "[SKIP] picture_test directory not found under current workspace.\n";
        return 0;
    }

    std::sort(image_files.begin(), image_files.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() < b.filename().string();
              });

    const size_t limit = std::min<size_t>(4, image_files.size());
    if (limit < 4) {
        std::cout << "[SKIP] picture_test needs at least 4 images, found " << image_files.size() << "\n";
        return 0;
    }

    BoundedQueue<CameraFrame> frame_queue(8, true);
    BoundedQueue<VisionResult> result_queue(8, false);
    InferenceScheduler scheduler(frame_queue, result_queue);
    assert(scheduler.init(VisionRuntimeMode::FALLBACK, "."));
    std::vector<VisionResult> produced;

    for (size_t idx = 0; idx < limit; ++idx) {
        const fs::path& img = image_files[idx];
        CameraFrame frame;
        assert(loadImageToFrame(img, frame));
        const CameraFrame staged_frame = frame;
        assert(frame_queue.push(std::move(frame)));

        bool got = false;
        VisionResult result;
        for (int i = 0; i < 5; ++i) {
            if (!scheduler.runOnce()) {
                continue;
            }
            auto out = result_queue.tryPop();
            if (out.has_value()) {
                result = *out;
                got = true;
                break;
            }
        }

        assert(got);
        validateVisionResult(result, staged_frame);
        produced.push_back(result);
        std::cout << "Processed: " << img.filename().string()
                  << " -> reason " << static_cast<int>(result.reason_code) << "\n";
    }

    if (!produced.empty()) {
        runBehaviorSmoke(produced.front());
    }

    std::cout << "Picture -> Vision pipeline smoke test passed with " << produced.size() << " frames.\n";
#else
    std::cout << "[SKIP] EDGEFOCUS_WITH_OPENCV is disabled. Picture vision test is skipped.\n";
#endif
    return 0;
}
