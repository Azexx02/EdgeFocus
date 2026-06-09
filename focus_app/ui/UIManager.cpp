#include "UIManager.h"
#include "app/AppContext.h"
#include "app/Logger.h"
#include "device/GpioHelper.h"

#include <chrono>
#include <cstring>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <glob.h>
#endif

namespace edgefocus {

UIManager::UIManager(SystemStateStore& store)
    : Module("UIManager")
    , store_(store) {}

UIManager::~UIManager() = default;

bool UIManager::init(AppContext& /*ctx*/) {
    LOGI("UIManager", "Initializing UI system...");

    if (!fb_.init()) {
        LOGE("UIManager", "Failed to initialize framebuffer");
        return false;
    }
    LOGI("UIManager", "FrameBuffer: %dx%d (real=%s)",
         fb_.width(), fb_.height(), fb_.isReal() ? "yes" : "no");

#ifdef __linux__
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
        LOGW("UIManager", "Could not reset touch controller (PB4 unavailable)");
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
    renderGlobalHeader(snap);
    pages_[current_page_]->render(fb_, snap);
    fb_.flush();
}

void UIManager::renderGlobalHeader(const SystemSnapshot& snap) {
    // Basic header logic...
}

void UIManager::handleTouch() {
#ifdef __linux__
    if (touch_fd_ < 0) return;
    
    // I2C direct touch handling
    if (use_i2c_touch_) {
        uint8_t buf[7];
        // Read TD_STATUS to check if touches > 0
        buf[0] = 0x02;
        if (write(touch_fd_, buf, 1) == 1) {
            if (read(touch_fd_, buf, 1) == 1 && (buf[0] & 0x0F) > 0) {
                // Read P1_XH to P1_YL (0x03 to 0x06)
                buf[0] = 0x03;
                if (write(touch_fd_, buf, 1) == 1 && read(touch_fd_, buf, 4) == 4) {
                    int x = ((buf[0] & 0x0F) << 8) | buf[1];
                    int y = ((buf[2] & 0x0F) << 8) | buf[3];
                    
                    if (!touch_pending_) {
                        touch_start_x_ = x;
                        touch_start_y_ = y;
                        touch_pending_ = true;
                    }
                    touch_x_ = x;
                    touch_y_ = y;
                }
            } else if (touch_pending_) {
                touch_pending_ = false;
                
                int dx = touch_x_ - touch_start_x_;
                int dy = touch_y_ - touch_start_y_;
                
                // Scale from touch to screen if necessary
                int sx = touch_start_x_ * fb_.width() / 240;
                int sy = touch_start_y_ * fb_.height() / 320;
                
                if (dx > 50 && abs(dy) < 150) {
                    if (current_page_ == PAGE_STATS) switchPage(PAGE_STATUS);
                    else if (current_page_ == PAGE_SETTINGS) switchPage(PAGE_STATS);
                } else if (dx < -50 && abs(dy) < 150) {
                    if (current_page_ == PAGE_STATUS) switchPage(PAGE_STATS);
                    else if (current_page_ == PAGE_STATS) switchPage(PAGE_SETTINGS);
                } else {
                    pages_[current_page_]->onTouch(sx, sy);
                }
            }
        }
        return;
    }

    struct input_event ev;
    while (read(touch_fd_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) touch_x_ = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) touch_y_ = ev.value;
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value == 1) {
                touch_start_x_ = touch_x_;
                touch_start_y_ = touch_y_;
                touch_pending_ = true;
            } else if (ev.value == 0 && touch_pending_) {
                touch_pending_ = false;
                int dx = touch_x_ - touch_start_x_;
                int dy = touch_y_ - touch_start_y_;
                
                // Evdev usually gives 0-1024 or 0-240, scale appropriately
                // Let's assume standard event is 0-240 and 0-320
                if (dx > 50 && abs(dy) < 150) {
                    if (current_page_ == PAGE_STATS) switchPage(PAGE_STATUS);
                    else if (current_page_ == PAGE_SETTINGS) switchPage(PAGE_STATS);
                } else if (dx < -50 && abs(dy) < 150) {
                    if (current_page_ == PAGE_STATUS) switchPage(PAGE_STATS);
                    else if (current_page_ == PAGE_STATS) switchPage(PAGE_SETTINGS);
                } else {
                    pages_[current_page_]->onTouch(touch_x_ * fb_.width() / 240, touch_y_ * fb_.height() / 320);
                }
            }
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT && touch_pending_) {
            // Drag handling (omitted for brevity)
        }
    }
#endif
}

} // namespace edgefocus
