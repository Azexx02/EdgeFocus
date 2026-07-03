#include "UIManager.h"
#include "app/AppContext.h"
#include "app/Logger.h"
#include "device/GpioHelper.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <glob.h>
#endif

namespace edgefocus {

namespace {

bool envFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    if (!value) return false;
    return std::strcmp(value, "1") == 0 ||
           std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 ||
           std::strcmp(value, "on") == 0 ||
           std::strcmp(value, "ON") == 0 ||
           std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}

bool envStringEquals(const char* name, const char* expected) {
    const char* value = std::getenv(name);
    return value && std::strcmp(value, expected) == 0;
}

bool shouldBypassBoardScreen(const Preferences& prefs) {
    return prefs.screen_output_route == "phone" ||
           envFlagEnabled("EDGEFOCUS_DISABLE_LCD") ||
           envStringEquals("EDGEFOCUS_LCD_BACKEND", "offscreen");
}

} // namespace

UIManager::UIManager(SystemStateStore& store)
    : Module("UIManager")
    , store_(store) {}

UIManager::~UIManager() = default;

bool UIManager::init(AppContext& /*ctx*/) {
    LOGI("UIManager", "Initializing UI system...");

    const auto prefs = store_.snapshot().preferences;
    screen_hardware_bypassed_ = shouldBypassBoardScreen(prefs);
    const bool framebuffer_ok = screen_hardware_bypassed_
        ? fb_.initOffscreen()
        : fb_.init();
    if (!framebuffer_ok) {
        LOGE("UIManager", "Failed to initialize framebuffer");
        return false;
    }
    LOGI("UIManager", "FrameBuffer: %dx%d (real=%s)",
         fb_.width(), fb_.height(), fb_.isReal() ? "yes" : "no");
    if (screen_hardware_bypassed_) {
        LOGI("UIManager", "Board screen hardware bypassed; App/phone owns display output");
    }

#ifdef __linux__
    if (!screen_hardware_bypassed_) {
    // 0. Reset FT6336 touch controller via PB4
    auto ctp_rst = device::GpioHelper::openLine("PB4", true, 1);
    if (ctp_rst.isValid()) {
        device::GpioHelper::setValue(ctp_rst, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        device::GpioHelper::setValue(ctp_rst, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        device::GpioHelper::closeLine(ctp_rst);
        LOGI("UIManager", "Touch controller reset sequence completed");
    } else {
        LOGI("UIManager", "PB4 unavailable for touch reset; it may be held high by the LCD power/wake path");
    }

    // 1. Try direct I2C for FT6336
    touch_fd_ = open("/dev/i2c-2", O_RDWR | O_CLOEXEC);
    if (touch_fd_ >= 0) {
        if (ioctl(touch_fd_, I2C_SLAVE, 0x38) >= 0) {
            use_i2c_touch_ = true;
            LOGI("UIManager", "Using direct I2C for FT6336 touch");
        } else {
            close(touch_fd_);
            touch_fd_ = -1;
        }
    }

    // 2. Fallback to /dev/input/event*
    if (touch_fd_ < 0) {
        glob_t glob_result;
        if (glob("/dev/input/event*", 0, nullptr, &glob_result) == 0) {
            for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                int fd = open(glob_result.gl_pathv[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd >= 0) {
                    unsigned long evbit = 0;
                    ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit);
                    if (evbit & (1 << EV_ABS)) {
                        char name[256] = "Unknown";
                        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                        if (std::string(name).find("touch") != std::string::npos ||
                            std::string(name).find("FT6336") != std::string::npos ||
                            std::string(name).find("ft6236") != std::string::npos ||
                            std::string(name).find("goodix") != std::string::npos) {
                            touch_fd_ = fd;
                            use_i2c_touch_ = false;
                            LOGI("UIManager", "Found touch device: %s (%s)", glob_result.gl_pathv[i], name);
                            break;
                        }
                    }
                    if (touch_fd_ < 0) close(fd);
                }
            }
            globfree(&glob_result);
        }
    }
    
    if (touch_fd_ < 0) {
        LOGW("UIManager", "No touch input device found");
    }
    } else {
        LOGI("UIManager", "Touch hardware skipped because board screen is bypassed");
    }
#endif

    // Create pages
    pages_.push_back(std::make_unique<StatusPage>());
    pages_.push_back(std::make_unique<StatsPage>());
    pages_.push_back(std::make_unique<SettingsPage>());
    pages_.push_back(std::make_unique<ReviewPage>());

    for (auto& page : pages_) {
        page->setPageSwitcher([this](int id) { this->switchPage(id); });
        page->setStore(&store_);
    }

    // Wire report generator to StatusPage
    if (auto* sp = dynamic_cast<StatusPage*>(pages_[0].get())) {
        sp->setReportGenerator(report_gen_);
        sp->setSQLiteStore(sqlite_store_);
    }

    // Wire mode manager to SettingsPage
    if (auto* sp = dynamic_cast<SettingsPage*>(pages_[2].get())) {
        sp->setModeManager(mode_mgr_);
    }

#ifdef __linux__
    if (touch_fd_ >= 0 && use_i2c_touch_) {
        touch_thread_running_ = true;
        touch_thread_ = std::thread(&UIManager::touchWorkerLoop, this);
        LOGI("UIManager", "Started FT6336 touch polling thread");
    }
#endif

    pages_[current_page_]->onEnter();
    LOGI("UIManager", "UI initialized with %zu pages", pages_.size());
    return true;
}

void UIManager::runOnce(AppContext& ctx) {
    auto start = std::chrono::steady_clock::now();
    handleTouch();
    renderFrame();
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (ms < FRAME_INTERVAL_MS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_INTERVAL_MS - ms));
    }
}

void UIManager::shutdown(AppContext& /*ctx*/) {
    LOGI("UIManager", "Shutting down UI...");
#ifdef __linux__
    touch_thread_running_ = false;
    if (touch_thread_.joinable()) {
        touch_thread_.join();
    }
    if (touch_fd_ >= 0) {
        close(touch_fd_);
        touch_fd_ = -1;
    }
#endif
    if (current_page_ >= 0 && current_page_ < static_cast<int>(pages_.size())) {
        pages_[current_page_]->onLeave();
    }
    pages_.clear();
    fb_.close();
    LOGI("UIManager", "UI shutdown complete");
}

void UIManager::switchPage(int id) {
    if (id < 0 || id >= static_cast<int>(pages_.size())) return;
    if (id == current_page_) return;
    pages_[current_page_]->onLeave();
    current_page_ = id;
    pages_[current_page_]->onEnter();
    LOGI("UIManager", "Switched to page: %s", pages_[current_page_]->name());
}

void UIManager::renderFrame() {
    SystemSnapshot snap = store_.snapshot();
    if (!last_session_active_ && snap.session.active) {
        switchPage(PAGE_STATUS);
    }
    last_session_active_ = snap.session.active;

    pages_[current_page_]->render(fb_, snap);
    renderGlobalHeader(snap);
    fb_.flush();
}

void UIManager::renderGlobalHeader(const SystemSnapshot& snap) {
    const int W = fb_.width();
    const int HEADER_H = 40;

    // Background
    fb_.fillRect(Rect(0, 0, W, HEADER_H), Color::bgCard());

    // Left: Mode icon
    FontInfo header_font;
    header_font.scale = 1;
    const char* mode_str = "N";
    if (snap.device.system_mode == SystemMode::SILENT) mode_str = "S";
    else if (snap.device.system_mode == SystemMode::HOME) mode_str = "H";
    fb_.drawText(10, 12, mode_str, Color::primary(), header_font);

    // Center: Camera status
    const char* cam_str = snap.device.camera_ok ? "CAM OK" : "CAM --";
    Color cam_color = snap.device.camera_ok ? Color::primary() : Color::textSecondary();
    fb_.drawTextCentered(Rect(0, 12, W, 20), cam_str, cam_color, header_font);

    // Right: Battery percentage
    char bat_buf[8];
    if (snap.device.battery_percent >= 0) {
        snprintf(bat_buf, sizeof(bat_buf), "%d%%", snap.device.battery_percent);
    } else {
        snprintf(bat_buf, sizeof(bat_buf), "--");
    }
    fb_.drawText(W - 40, 12, bat_buf, Color::textSecondary(), header_font);

    // Bottom divider line
    fb_.fillRect(Rect(0, HEADER_H - 1, W, 1), Color::divider());
}

void UIManager::queueTouchSwipe(int direction) {
    std::lock_guard<std::mutex> lock(touch_event_mutex_);
    pending_swipe_direction_ = direction;
    pending_tap_ = false;
}

void UIManager::queueTouchTap(int x, int y) {
    std::lock_guard<std::mutex> lock(touch_event_mutex_);
    if (pending_swipe_direction_ != 0) return;
    pending_tap_x_ = x;
    pending_tap_y_ = y;
    pending_tap_ = true;
}

void UIManager::touchWorkerLoop() {
#ifdef __linux__
    bool pending = false;
    bool swipe_sent = false;
    int start_x = 0;
    int start_y = 0;
    int last_x = 0;
    int last_y = 0;

    auto classifySwipe = [](int dx, int dy) -> int {
        constexpr int kSwipeThresholdPx = 24;
        constexpr int kSwipeMaxVerticalPx = 140;
        if (std::abs(dx) < kSwipeThresholdPx) return 0;
        if (std::abs(dy) > kSwipeMaxVerticalPx) return 0;
        if (std::abs(dx) < std::abs(dy)) return 0;
        return dx < 0 ? -1 : 1;
    };

    while (touch_thread_running_) {
        uint8_t reg = 0x02;
        uint8_t buf[6];
        bool has_touch = false;
        int x = 0;
        int y = 0;

        if (write(touch_fd_, &reg, 1) == 1 &&
            read(touch_fd_, buf, sizeof(buf)) == static_cast<ssize_t>(sizeof(buf)) &&
            (buf[0] & 0x0F) > 0) {
            has_touch = true;
            x = ((buf[1] & 0x0F) << 8) | buf[2];
            y = ((buf[3] & 0x0F) << 8) | buf[4];
        }

        if (has_touch) {
            if (!pending) {
                start_x = x;
                start_y = y;
                swipe_sent = false;
                pending = true;
            }
            last_x = x;
            last_y = y;

            if (!swipe_sent) {
                int direction = classifySwipe(last_x - start_x, last_y - start_y);
                if (direction != 0) {
                    queueTouchSwipe(direction);
                    swipe_sent = true;
                }
            }
        } else if (pending) {
            if (!swipe_sent) {
                int direction = classifySwipe(last_x - start_x, last_y - start_y);
                if (direction != 0) {
                    queueTouchSwipe(direction);
                } else {
                    queueTouchTap(start_x * fb_.width() / 240, start_y * fb_.height() / 320);
                }
            }
            pending = false;
            swipe_sent = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
#endif
}

void UIManager::handleTouch() {
#ifdef __linux__
    if (touch_fd_ < 0) return;

    int swipe_direction = 0;
    bool has_tap = false;
    int tap_x = 0;
    int tap_y = 0;
    {
        std::lock_guard<std::mutex> lock(touch_event_mutex_);
        swipe_direction = pending_swipe_direction_;
        pending_swipe_direction_ = 0;
        has_tap = pending_tap_;
        tap_x = pending_tap_x_;
        tap_y = pending_tap_y_;
        pending_tap_ = false;
    }

    if (swipe_direction < 0) {
        if (current_page_ == PAGE_STATUS) switchPage(PAGE_STATS);
        else if (current_page_ == PAGE_STATS) switchPage(PAGE_SETTINGS);
        return;
    }
    if (swipe_direction > 0) {
        if (current_page_ == PAGE_STATS) switchPage(PAGE_STATUS);
        else if (current_page_ == PAGE_SETTINGS) switchPage(PAGE_STATS);
        return;
    }
    if (has_tap) {
        pages_[current_page_]->onTouch(tap_x, tap_y);
        return;
    }

    if (use_i2c_touch_) return;

    auto handleSwipe = [this](int dx, int dy) -> bool {
        constexpr int kSwipeThresholdPx = 32;
        constexpr int kSwipeMaxVerticalPx = 120;

        if (std::abs(dx) < kSwipeThresholdPx) return false;
        if (std::abs(dy) > kSwipeMaxVerticalPx) return false;
        if (std::abs(dx) < std::abs(dy)) return false;

        if (dx > 0) {
            if (current_page_ == PAGE_STATS) switchPage(PAGE_STATUS);
            else if (current_page_ == PAGE_SETTINGS) switchPage(PAGE_STATS);
            else return false;
        } else {
            if (current_page_ == PAGE_STATUS) switchPage(PAGE_STATS);
            else if (current_page_ == PAGE_STATS) switchPage(PAGE_SETTINGS);
            else return false;
        }
        return true;
    };
    
    struct input_event ev;
    while (read(touch_fd_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) touch_x_ = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) touch_y_ = ev.value;
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value == 1) {
                touch_start_x_ = touch_x_;
                touch_start_y_ = touch_y_;
                touch_swipe_sent_ = false;
                touch_pending_ = true;
            } else if (ev.value == 0 && touch_pending_) {
                touch_pending_ = false;
                int dx = touch_x_ - touch_start_x_;
                int dy = touch_y_ - touch_start_y_;
                
                // Evdev usually gives 0-1024 or 0-240, scale appropriately
                // Let's assume standard event is 0-240 and 0-320
                if (!touch_swipe_sent_ && !handleSwipe(dx, dy)) {
                    pages_[current_page_]->onTouch(touch_x_ * fb_.width() / 240, touch_y_ * fb_.height() / 320);
                }
                touch_swipe_sent_ = false;
            }
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT && touch_pending_) {
            if (!touch_swipe_sent_) {
                int dx = touch_x_ - touch_start_x_;
                int dy = touch_y_ - touch_start_y_;
                touch_swipe_sent_ = handleSwipe(dx, dy);
            }
        }
    }
#endif
}

} // namespace edgefocus
