#include "StatusPage.h"
#include "app/Logger.h"
#include "app/SessionController.h"
#include "app/SessionPhase.h"
#include "storage/StorageEvent.h"
#include <cstdio>
#include <string>
#include <cmath>
#include <chrono>
#include <cctype>
#include <nlohmann/json.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace edgefocus {

namespace {
int64_t monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t wallClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::pair<std::string, std::string> alertHeadlineCopy(CommandLevel level) {
    if (level == CommandLevel::LEVEL_3) {
        return {"保持", "专注"};
    }
    return {"回到", "专注"};
}

std::string reminderMessageLabel(const std::string& message) {
    if (message == "Phone distraction detected") return "手机使用提醒";
    if (message == "High phone distraction detected") return "手机强干扰";
    if (message == "Gentle focus reminder") return "专注提醒";
    if (message == "Focus reminder") return "请回到专注";
    if (message == "Strong focus reminder") return "强专注提醒";
    return message;
}

std::string titleCaseHint(std::string value) {
    for (char& ch : value) {
        if (ch == '_') ch = ' ';
    }
    bool word_start = true;
    for (char& ch : value) {
        if (ch == ' ') {
            word_start = true;
        } else if (word_start) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            word_start = false;
        }
    }
    return value;
}

std::string distanceHintLabel(const std::string& hint) {
    if (hint.empty()) return "等待画面";
    if (hint == "camera_waiting") return "等待相机";
    if (hint == "lighting_too_dark") return "光线太暗";
    if (hint == "lighting_too_bright") return "逆光太强";
    if (hint == "image_blurry") return "画面模糊";
    if (hint == "move_farther_or_center") return "后退居中";
    if (hint == "move_closer") return "靠近一点";
    if (hint == "adjust_distance") return "调整距离";
    if (hint == "distance_ok") return "距离合适";
    return titleCaseHint(hint);
}

std::string poseHintLabel(const std::string& hint) {
    if (hint.empty()) return "保持可见";
    if (hint == "keep_visible") return "保持可见";
    if (hint == "increase_light") return "增加光线";
    if (hint == "reduce_backlight") return "减少逆光";
    if (hint == "hold_still") return "稳住";
    if (hint == "keep_head_in_frame") return "头部入镜";
    if (hint == "sit_in_front_of_camera") return "坐到前方";
    if (hint == "face_camera") return "看镜头";
    if (hint == "center_face") return "脸居中";
    if (hint == "turn_head_left") return "向左转";
    if (hint == "turn_head_right") return "向右转";
    if (hint == "lower_camera_or_head") return "低头一点";
    if (hint == "raise_camera_or_head") return "抬头一点";
    if (hint == "pose_ok") return "姿态合适";
    return titleCaseHint(hint);
}

std::string durationLabel(int minutes) {
    char buf[16];
    if (minutes >= 60) {
        const int h = minutes / 60;
        const int m = minutes % 60;
        if (m > 0) snprintf(buf, sizeof(buf), "%d时%02d分", h, m);
        else snprintf(buf, sizeof(buf), "%d时", h);
    } else {
        snprintf(buf, sizeof(buf), "%d分", minutes);
    }
    return buf;
}

std::string powerStatusLabel(const DeviceStatus& device) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Power %s / %s",
             device.power_profile == PowerProfile::HOME ? "HOME" : "GO",
             device.external_power_state.c_str());
    return buf;
}

bool boardOwnsPrecheck(const SessionInfo& session) {
    return session.precheck_owner.empty() ||
           session.precheck_owner == "board" ||
           session.precheck_owner == "local";
}

std::string pushStateJson(const SystemSnapshot& snap) {
    nlohmann::json data = {
        {"active", snap.session.active},
        {"paused", snap.session.paused},
        {"session_id", snap.session.session_id},
        {"focus_state", static_cast<int>(snap.focus_state)},
        {"elapsed_ms", snap.session.elapsed_ms},
        {"remaining_ms", snap.session.remaining_ms},
        {"target_duration_ms", snap.session.target_duration_ms},
        {"distraction_count", snap.session.distraction_count},
        {"reminder_count", snap.session.reminder_count},
        {"calibration_completed", snap.session.calibration_completed},
        {"session_phase", sessionPhaseToString(deriveSessionPhase(snap.session))}
    };
    return nlohmann::json{{"cmd", "push_state"}, {"data", data}}.dump();
}
} // namespace

StatusPage::PrecheckViewModel StatusPage::precheckViewModelForTest(
    const SystemSnapshot& snap, int target_duration_min) {
    PrecheckViewModel model;
    model.camera_status = snap.device.camera_ok ? "相机OK" : "相机等待";
    model.preview_status = snap.camera_preview_image.available ? "画面就绪" : "等待画面";
    if (snap.frame_quality.score > 0.0f) {
        char quality_buf[16];
        snprintf(quality_buf, sizeof(quality_buf), "%d%%",
                 static_cast<int>(std::round(snap.frame_quality.score * 100.0f)));
        model.quality_status = quality_buf;
    } else {
        model.quality_status = "--";
    }
    model.distance_status = "距离: " + distanceHintLabel(snap.frame_quality.distance_hint);
    model.pose_status = "姿态: " + poseHintLabel(snap.frame_quality.pose_hint);
    model.timer_status = "时长: " + durationLabel(target_duration_min);
    model.ready = snap.device.camera_ok && snap.frame_quality.precheck_ready;
    model.action_enabled = boardOwnsPrecheck(snap.session);
    model.primary_tip = model.action_enabled
        ? (model.ready ? "可以开始" : "调整位置")
        : "请在App继续";
    model.action_label = model.action_enabled ? "开始专注" : "使用App";
    model.show_gimbal = snap.preferences.gimbal_enabled &&
                        snap.device.gimbal_online &&
                        snap.device.gimbal_safe;
    if (model.show_gimbal) {
        model.gimbal_status = "云台就绪";
    }
    return model;
}

void StatusPage::render(FrameBuffer& fb, const SystemSnapshot& snap) {
    syncFromSnapshot(snap);

    const int W = fb.width();
    const int H = fb.height();

    // Check if we have an active level 2+ reminder (Red Alert Screen - Modal)
    if (snap.reminder.level >= CommandLevel::LEVEL_2 && !snap.reminder.message.empty()) {
        fb.clear(Color::errorContainer());
        const auto title = alertHeadline(snap.reminder.level, snap.reminder.message);
        
        // Draw Triangle Alert
        fb.drawIconWarning(W / 2, H / 3, 60, Color::error());
        
        // Reminder headline varies by reminder semantics.
        FontInfo huge_font;
        huge_font.scale = 4;
        fb.drawTextCentered(Rect(0, H / 2 - 40, W, 50), title.first, Color::error(), huge_font);
        fb.drawTextCentered(Rect(0, H / 2 + 10, W, 50), title.second, Color::error(), huge_font);

        // Subtext
        FontInfo sub_font;
        sub_font.scale = 2;
        fb.drawTextCentered(Rect(0, H / 2 + 80, W, 30),
                            reminderMessageLabel(snap.reminder.message),
                            Color::error(), sub_font);

        // Acknowledge button
        fb.fillRoundRect(Rect(20, H - 90, W - 40, 60), 8, Color(80, 0, 0));
        fb.drawTextCentered(Rect(20, H - 90, W - 40, 60), "知道了", Color::error(), huge_font);
        return;
    }

    // Normal backgrounds
    fb.clear(Color::bgDark());

    // NOTE: Global header (Mode, Camera, Power profile) is drawn by UIManager after this.
    // So we reserve the top 40px.

    if (internal_state_ == InternalState::READY) {
        // --- Ready to focus screen ---
        FontInfo ready_font;
        ready_font.scale = 2;
        fb.drawTextCentered(Rect(0, 54, W, 36), "准备", Color::textPrimary(), ready_font);
        fb.drawTextCentered(Rect(0, 88, W, 36), "专注?", Color::textPrimary(), ready_font);

        // Duration selector: [-] 25min [+]
        const int SEL_Y = 148;
        const int SEL_H = 40;
        const int BTN_W = 40;
        const int VAL_W = 80;

        FontInfo sel_font;
        sel_font.scale = 2;
        FontInfo sel_label;
        sel_label.scale = 1;

        fb.drawTextCentered(Rect(0, SEL_Y - 15, W, 15), "时长", Color::textSecondary(), sel_label);

        // [-] button
        fb.fillRoundRect(Rect(30, SEL_Y, BTN_W, SEL_H), 8, Color::bgCard());
        fb.drawTextCentered(Rect(30, SEL_Y, BTN_W, SEL_H), "-", Color::textPrimary(), sel_font);

        // Value display
        fb.fillRoundRect(Rect(30 + BTN_W + 5, SEL_Y, VAL_W, SEL_H), 8, Color::bgCard());
        char dur_buf[16];
        if (target_duration_min_ >= 60) {
            int h = target_duration_min_ / 60;
            int m = target_duration_min_ % 60;
            if (m > 0) snprintf(dur_buf, sizeof(dur_buf), "%d时%02d分", h, m);
            else snprintf(dur_buf, sizeof(dur_buf), "%d时", h);
        } else {
            snprintf(dur_buf, sizeof(dur_buf), "%d分", target_duration_min_);
        }
        fb.drawTextCentered(Rect(30 + BTN_W + 5, SEL_Y, VAL_W, SEL_H), dur_buf, Color::primary(), sel_font);

        // [+] button
        fb.fillRoundRect(Rect(30 + BTN_W + 5 + VAL_W + 5, SEL_Y, BTN_W, SEL_H), 8, Color::bgCard());
        fb.drawTextCentered(Rect(30 + BTN_W + 5 + VAL_W + 5, SEL_Y, BTN_W, SEL_H), "+", Color::textPrimary(), sel_font);

        FontInfo status_font;
        status_font.scale = 1;
        fb.drawTextCentered(Rect(0, 205, W, 16), powerStatusLabel(snap.device),
                            Color::textSecondary(), status_font);

        // START Button
        const int START_Y = 230;
        const int START_H = 58;
        fb.fillRoundRect(Rect(20, START_Y, W - 40, START_H), 12, Color::primary());
        fb.drawTextCentered(Rect(20, START_Y, W - 40, START_H), "开始", Color::bgDark(), ready_font);

    } else if (internal_state_ == InternalState::CALIBRATING) {
        const auto model = precheckViewModelForTest(snap, target_duration_min_);

        FontInfo title_font;
        title_font.scale = 2;
        fb.drawTextCentered(Rect(0, 45, W, 24), "专注预检", Color::textPrimary(), title_font);

        FontInfo small_font;
        small_font.scale = 1;
        FontInfo body_font;
        body_font.scale = 1;
        FontInfo action_font;
        action_font.scale = 2;

        const Color camera_color = snap.device.camera_ok ? Color::primary() : Color::secondary();
        const Color quality_color = snap.frame_quality.precheck_ready ? Color::primary()
                                  : (snap.frame_quality.score > 0.0f ? Color::secondary()
                                                                     : Color::textSecondary());
        fb.fillRoundRect(Rect(14, 76, 96, 26), 6, Color::bgCard());
        fb.drawTextCentered(Rect(14, 81, 96, 16), model.camera_status, camera_color, small_font);
        fb.fillRoundRect(Rect(124, 76, 82, 26), 6, Color::bgCard());
        fb.drawTextCentered(Rect(124, 81, 82, 16), model.quality_status, quality_color, small_font);

        const Rect preview_rect(18, 110, W - 36, 68);
        fb.fillRoundRect(preview_rect, 8, Color::bgCard());
        fb.drawRoundRect(preview_rect, 8, snap.camera_preview_image.available ? Color::primaryDim()
                                                                             : Color::divider(), 2);
        fb.drawLine(preview_rect.x + 12, preview_rect.y + 12,
                    preview_rect.x + preview_rect.w - 12, preview_rect.y + preview_rect.h - 12,
                    Color::divider(), 1);
        fb.drawLine(preview_rect.x + preview_rect.w - 12, preview_rect.y + 12,
                    preview_rect.x + 12, preview_rect.y + preview_rect.h - 12,
                    Color::divider(), 1);
        fb.drawTextCentered(Rect(preview_rect.x, preview_rect.y + 18,
                                 preview_rect.w, 18),
                            model.preview_status, Color::textPrimary(), small_font);

        const int INFO_Y = 188;
        const int LINE_H = 18;
        fb.drawText(18, INFO_Y, model.distance_status, Color::textPrimary(), body_font);
        fb.drawText(18, INFO_Y + LINE_H, model.pose_status, Color::textPrimary(), body_font);
        fb.drawText(18, INFO_Y + LINE_H * 2, model.timer_status, Color::textSecondary(), body_font);
        if (model.show_gimbal) {
            fb.drawText(18, INFO_Y + LINE_H * 3, model.gimbal_status, Color::primary(), body_font);
        }

        const int TIP_Y = 250;
        const int ACTION_Y = 272;
        const int ACTION_H = 42;
        fb.drawTextCentered(Rect(0, TIP_Y, W, 16), model.primary_tip,
                            model.ready ? Color::primary() : Color::secondary(), small_font);
        const Color action_bg = model.action_enabled ? Color::primary() : Color::bgCard();
        const Color action_fg = model.action_enabled ? Color::bgDark() : Color::textSecondary();
        fb.fillRoundRect(Rect(20, ACTION_Y, W - 40, ACTION_H), 10, action_bg);
        fb.drawTextCentered(Rect(20, ACTION_Y, W - 40, ACTION_H), model.action_label,
                            action_fg, action_font);

    } else if (internal_state_ == InternalState::FOCUSED) {
        // --- Active Session Screen ---
        FontInfo title_font;
        title_font.scale = 2;
        fb.drawTextCentered(Rect(0, 50, W, 30), "专注中", Color::primary(), title_font);

        FontInfo small_font;
        small_font.scale = 1;
        char dist_buf[32];
        snprintf(dist_buf, sizeof(dist_buf), "%d 次分心", snap.session.distraction_count);
        fb.drawTextCentered(Rect(0, 80, W, 20), dist_buf, Color::textSecondary(), small_font);

        const int64_t elapsed_sec = elapsedSeconds(snap);
        const int64_t display_sec = countdownSeconds(snap);

        // Draw circular progress
        int cx = W / 2;
        int cy = 170;
        int radius = 50;
        int thickness = 8;
        fb.drawRing(cx, cy, radius, thickness, Color::bgCard());
        float progress = 0.0f;
        if (snap.session.target_duration_ms > 0) {
            const int64_t remaining_ms = display_sec * 1000;
            progress = 1.0f - (static_cast<float>(remaining_ms) /
                               static_cast<float>(snap.session.target_duration_ms));
            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;
        }
        constexpr float kHalfPi = static_cast<float>(M_PI / 2.0);
        constexpr float kTwoPi = static_cast<float>(M_PI * 2.0);
        fb.drawArc(cx, cy, radius, thickness, -kHalfPi, -kHalfPi + (progress * kTwoPi), Color::primary());

        // Time text inside ring: match the mobile APP countdown direction.
        FontInfo time_font;
        time_font.scale = 2;
        std::string time_str = formatDuration(display_sec);
        fb.drawTextCentered(Rect(cx - radius, cy - 10, radius * 2, 20), time_str, Color::textPrimary(), time_font);

        // Secondary time display keeps elapsed time visible without changing the data contract.
        if (snap.session.target_duration_ms > 0) {
            std::string elapsed_str = formatDuration(elapsed_sec);
            FontInfo remain_font;
            remain_font.scale = 1;
            fb.drawTextCentered(Rect(0, cy + radius + 5, W, 15), elapsed_str, Color::textSecondary(), remain_font);
        }

        // PAUSE Button
        fb.fillRoundRect(Rect(20, 250, W - 40, 50), 8, Color::bgCard());
        fb.drawTextCentered(Rect(20, 250, W - 40, 50), "暂停", Color::textPrimary(), title_font);
        
        // Level-1 board screen reminder remains visible without taking over the whole page.
        if (snap.reminder.level == CommandLevel::LEVEL_1 && !snap.reminder.message.empty()) {
            fb.drawRoundRect(Rect(12, 214, W - 24, 28), 8, Color::secondary(), 2);
            fb.drawTextCentered(Rect(16, 220, W - 32, 16),
                                reminderMessageLabel(snap.reminder.message),
                                Color::secondary(), small_font);
        } else if (snap.focus_state != FocusState::FOCUSED) {
            Color state_color = focusStateColor(snap.focus_state);
            const char* state_text = focusStateText(snap.focus_state);
            fb.drawTextCentered(Rect(0, 220, W, 20), state_text, state_color, small_font);
        }
    } else if (internal_state_ == InternalState::PAUSED) {
        FontInfo title_font;
        title_font.scale = 2;
        fb.drawTextCentered(Rect(0, 80, W, 30), "已暂停", Color::secondary(), title_font);

        FontInfo small_font;
        small_font.scale = 1;
        fb.drawTextCentered(Rect(0, 110, W, 20), "专注会话已暂停", Color::textSecondary(), small_font);

        fb.fillRoundRect(Rect(20, 190, W - 40, 45), 8, Color::bgCard());
        fb.drawTextCentered(Rect(20, 190, W - 40, 45), "继续", Color::textPrimary(), title_font);

        fb.fillRoundRect(Rect(20, 250, W - 40, 45), 8, Color::bgCard());
        fb.drawTextCentered(Rect(20, 250, W - 40, 45), "结束", Color::error(), title_font);
    }
}

void StatusPage::syncWithSnapshotForTest(const SystemSnapshot& snap) {
    syncFromSnapshot(snap);
}

void StatusPage::syncFromSnapshot(const SystemSnapshot& snap) {
    const SessionPhase phase = deriveSessionPhase(snap.session);
    switch (phase) {
    case SessionPhase::PAUSED:
        internal_state_ = InternalState::PAUSED;
        break;
    case SessionPhase::PRECHECK:
        internal_state_ = InternalState::CALIBRATING;
        break;
    case SessionPhase::FOCUSING:
        internal_state_ = InternalState::FOCUSED;
        break;
    case SessionPhase::IDLE:
    default:
        internal_state_ = InternalState::READY;
        break;
    }

    const bool session_active = (phase != SessionPhase::IDLE);
    if (session_active && snap.session.target_duration_ms > 0) {
        target_duration_min_ = static_cast<int>(snap.session.target_duration_ms / (60 * 1000));
    } else if (!session_active) {
        if (was_session_active_) {
            local_duration_override_ = false;
        }
        if (!local_duration_override_) {
            target_duration_min_ = snap.preferences.default_focus_duration_min;
        }
    }
    was_session_active_ = session_active;
}

bool StatusPage::onTouch(int x, int y) {
    const int W = 240;
    const int H = 320;

    if (!store_) return false;
    SystemSnapshot snap = store_->snapshot();

    // Check if dismissing Level 2 alert
    if (snap.reminder.level >= CommandLevel::LEVEL_2 && !snap.reminder.message.empty()) {
        if (y > H - 100 && y < H - 20) {
            store_->clearReminder();
            return true;
        }
        return false;
    }

    if (internal_state_ == InternalState::READY) {
        const int SEL_Y = 148;
        const int SEL_H = 40;
        const int BTN_W = 40;
        const int VAL_W = 80;

        // [-] button
        if (y >= SEL_Y && y <= SEL_Y + SEL_H && x >= 30 && x <= 30 + BTN_W) {
            target_duration_min_ -= DURATION_STEP;
            if (target_duration_min_ < DURATION_MIN) target_duration_min_ = DURATION_MIN;
            local_duration_override_ = true;
            return true;
        }

        // [+] button
        int plus_x = 30 + BTN_W + 5 + VAL_W + 5;
        if (y >= SEL_Y && y <= SEL_Y + SEL_H && x >= plus_x && x <= plus_x + BTN_W) {
            target_duration_min_ += DURATION_STEP;
            if (target_duration_min_ > DURATION_MAX) target_duration_min_ = DURATION_MAX;
            local_duration_override_ = true;
            return true;
        }

        // START button
        const int START_Y = 230;
        const int START_H = 58;
        if (y > START_Y && y < START_Y + START_H && x > 20 && x < W - 20) {
            // START button tapped - begin session with calibration
            internal_state_ = InternalState::CALIBRATING;
            calibration_start_ms_ = monotonicMs();

            if (sqlite_store_) {
                SessionController controller(*store_, *sqlite_store_);
                controller.startSession(target_duration_min_, "local");
            }

            LOGI("StatusPage", "Session started (calibration, target=%dmin)", target_duration_min_);
            return true;
        }
    } else if (internal_state_ == InternalState::CALIBRATING) {
        const int ACTION_Y = 272;
        const int ACTION_H = 42;
        if (y >= ACTION_Y && y <= ACTION_Y + ACTION_H && x > 20 && x < W - 20) {
            if (!sqlite_store_) return false;
            if (!boardOwnsPrecheck(store_->snapshot().session)) return false;
            SessionController controller(*store_, *sqlite_store_);
            const bool ok = controller.skipPrecheck("local_skip_precheck");
            if (ok) {
                internal_state_ = InternalState::FOCUSED;
                store_->pushEvent(pushStateJson(store_->snapshot()));
            }
            return ok;
        }
    } else if (internal_state_ == InternalState::FOCUSED) {
        if (y > 250 && y < 300 && x > 20 && x < W - 20) {
            // PAUSE button tapped - pause session only
            if (!sqlite_store_) return false;

            SessionController controller(*store_, *sqlite_store_);
            controller.pauseSession("local_paused");
            internal_state_ = InternalState::PAUSED;
            store_->pushEvent(pushStateJson(store_->snapshot()));
            return true;
        }
    } else if (internal_state_ == InternalState::PAUSED) {
        if (y > 190 && y < 235 && x > 20 && x < W - 20) {
            // Resume button tapped
            if (!sqlite_store_) return false;
            SessionController controller(*store_, *sqlite_store_);
            controller.resumeSession("local_resumed");
            internal_state_ = InternalState::FOCUSED;
            store_->pushEvent(pushStateJson(store_->snapshot()));
            return true;
        }

        if (y > 250 && y < 295 && x > 20 && x < W - 20) {
            // End button tapped
            if (!sqlite_store_) return false;
            SessionController controller(*store_, *sqlite_store_);
            auto ended = controller.endSession("local_ended");
            if (ended.success && !ended.report.session_id.empty()) {
                nlohmann::json push_data = {
                    {"session_id", ended.report.session_id},
                    {"total_duration", ended.report.total_duration},
                    {"focus_duration", ended.report.focus_duration},
                    {"distraction_count", ended.report.distraction_count},
                    {"reminder_count", ended.report.reminder_count},
                    {"phone_distraction_count", ended.report.phone_distraction_count},
                    {"target_duration_ms", ended.target_duration_ms}
                };
                store_->pushEvent(nlohmann::json{{"cmd", "push_session_end"},
                                                 {"data", push_data}}.dump());
            }
            if (switch_page_) switch_page_(3); // PAGE_REVIEW
            return true;
        }
    }

    return false;
}

Color StatusPage::focusStateColor(FocusState state) const {
    switch (state) {
        case FocusState::FOCUSED:           return Color::primary();
        case FocusState::LOOKING_AWAY:      return Color::secondary();
        case FocusState::SLEEPY_OR_SLUMPED: return Color::secondary();
        case FocusState::PHONE_IN_USE:      return Color::error();
        case FocusState::PHONE_IN_LEARNING: return Color::primary();
        case FocusState::PHONE_IN_PLAYING:  return Color::error();
        case FocusState::AWAY:              return Color::textSecondary();
        case FocusState::LOW_CONFIDENCE:    return Color::textSecondary();
        default:                            return Color::textSecondary();
    }
}

const char* StatusPage::focusStateText(FocusState state) const {
    switch (state) {
        case FocusState::FOCUSED:           return "专注";
        case FocusState::LOOKING_AWAY:      return "视线偏离";
        case FocusState::SLEEPY_OR_SLUMPED: return "疲劳";
        case FocusState::PHONE_IN_USE:      return "手机使用";
        case FocusState::PHONE_IN_LEARNING: return "学习App";
        case FocusState::PHONE_IN_PLAYING:  return "娱乐App";
        case FocusState::AWAY:              return "离开";
        case FocusState::LOW_CONFIDENCE:    return "低置信";
        default:                            return "未知";
    }
}

const char* StatusPage::focusStateEmoji(FocusState state) const {
    switch (state) {
        case FocusState::FOCUSED:           return "[*]";
        case FocusState::LOOKING_AWAY:      return "[>]";
        case FocusState::SLEEPY_OR_SLUMPED: return "[~]";
        case FocusState::PHONE_IN_USE:      return "[!]";
        case FocusState::PHONE_IN_LEARNING: return "[L]";
        case FocusState::PHONE_IN_PLAYING:  return "[X]";
        case FocusState::AWAY:              return "[ ]";
        case FocusState::LOW_CONFIDENCE:    return "[?]";
        default:                            return "[ ]";
    }
}

std::string StatusPage::formatDuration(int64_t seconds) const {
    int h = static_cast<int>(seconds / 3600);
    int m = static_cast<int>((seconds % 3600) / 60);
    int s = static_cast<int>(seconds % 60);
    char buf[16];
    if (h > 0) snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    else snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return std::string(buf);
}

int64_t StatusPage::elapsedSeconds(const SystemSnapshot& snap) const {
    int64_t elapsed_ms = snap.session.elapsed_ms;
    if (elapsed_ms <= 0 && snap.session.start_ms > 0) {
        const int64_t now_ms = monotonicMs();
        elapsed_ms = now_ms - snap.session.start_ms;
    }
    if (elapsed_ms < 0) elapsed_ms = 0;
    return elapsed_ms / 1000;
}

int64_t StatusPage::countdownSeconds(const SystemSnapshot& snap) const {
    if (snap.session.target_duration_ms <= 0) {
        return elapsedSeconds(snap);
    }

    int64_t remaining_ms = snap.session.remaining_ms;
    if (remaining_ms <= 0 && snap.session.elapsed_ms < snap.session.target_duration_ms) {
        remaining_ms = snap.session.target_duration_ms - snap.session.elapsed_ms;
    }
    if (remaining_ms < 0) remaining_ms = 0;
    if (remaining_ms > snap.session.target_duration_ms) {
        remaining_ms = snap.session.target_duration_ms;
    }
    return remaining_ms / 1000;
}

int64_t StatusPage::countdownSecondsForTest(const SystemSnapshot& snap) const {
    return countdownSeconds(snap);
}

std::pair<std::string, std::string> StatusPage::alertHeadlineForTest(
    CommandLevel level, const std::string& message) {
    return alertHeadline(level, message);
}

std::pair<std::string, std::string> StatusPage::alertHeadline(
    CommandLevel level, const std::string& /*message*/) {
    return alertHeadlineCopy(level);
}

} // namespace edgefocus
