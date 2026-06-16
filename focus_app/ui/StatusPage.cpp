#include "StatusPage.h"
#include "app/Logger.h"
#include <cstdio>
#include <string>
#include <cmath>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace edgefocus {

void StatusPage::render(FrameBuffer& fb, const SystemSnapshot& snap) {
    const int W = fb.width();
    const int H = fb.height();

    // Check if we have an active level 2+ reminder (Red Alert Screen - Modal)
    if (snap.reminder.level >= CommandLevel::LEVEL_2 && !snap.reminder.message.empty()) {
        fb.clear(Color::errorContainer());
        
        // Draw Triangle Alert
        fb.drawIconWarning(W / 2, H / 3, 60, Color::error());
        
        // "REST NOW" or similar text
        FontInfo huge_font;
        huge_font.scale = 4;
        fb.drawTextCentered(Rect(0, H / 2 - 40, W, 50), "REST", Color::error(), huge_font);
        fb.drawTextCentered(Rect(0, H / 2 + 10, W, 50), "NOW", Color::error(), huge_font);

        // Subtext
        FontInfo sub_font;
        sub_font.scale = 2;
        fb.drawTextCentered(Rect(0, H / 2 + 80, W, 30), snap.reminder.message, Color::error(), sub_font);

        // "I GOT IT" button
        fb.fillRoundRect(Rect(20, H - 90, W - 40, 60), 8, Color(80, 0, 0));
        fb.drawTextCentered(Rect(20, H - 90, W - 40, 60), "I GOT IT", Color::error(), huge_font);
        return;
    }

    // Normal backgrounds
    fb.clear(Color::bgDark());

    // NOTE: Global header (Mode, Camera, Battery) is drawn by UIManager after this!
    // So we reserve the top 40px.

    if (internal_state_ == InternalState::READY) {
        // --- Ready to focus screen ---
        FontInfo ready_font;
        ready_font.scale = 3;
        fb.drawTextCentered(Rect(0, 55, W, 30), "Ready to", Color::textPrimary(), ready_font);
        fb.drawTextCentered(Rect(0, 82, W, 30), "focus?", Color::textPrimary(), ready_font);

        // Duration selector: [-] 25min [+]
        const int SEL_Y = 120;
        const int SEL_H = 40;
        const int BTN_W = 40;
        const int VAL_W = 80;

        FontInfo sel_font;
        sel_font.scale = 2;
        FontInfo sel_label;
        sel_label.scale = 1;

        fb.drawTextCentered(Rect(0, SEL_Y - 15, W, 15), "DURATION", Color::textSecondary(), sel_label);

        // [-] button
        fb.fillRoundRect(Rect(30, SEL_Y, BTN_W, SEL_H), 8, Color::bgCard());
        fb.drawTextCentered(Rect(30, SEL_Y, BTN_W, SEL_H), "-", Color::textPrimary(), sel_font);

        // Value display
        fb.fillRoundRect(Rect(30 + BTN_W + 5, SEL_Y, VAL_W, SEL_H), 8, Color::bgCard());
        char dur_buf[16];
        if (target_duration_min_ >= 60) {
            int h = target_duration_min_ / 60;
            int m = target_duration_min_ % 60;
            if (m > 0) snprintf(dur_buf, sizeof(dur_buf), "%dh%02dm", h, m);
            else snprintf(dur_buf, sizeof(dur_buf), "%dh", h);
        } else {
            snprintf(dur_buf, sizeof(dur_buf), "%dmin", target_duration_min_);
        }
        fb.drawTextCentered(Rect(30 + BTN_W + 5, SEL_Y, VAL_W, SEL_H), dur_buf, Color::primary(), sel_font);

        // [+] button
        fb.fillRoundRect(Rect(30 + BTN_W + 5 + VAL_W + 5, SEL_Y, BTN_W, SEL_H), 8, Color::bgCard());
        fb.drawTextCentered(Rect(30 + BTN_W + 5 + VAL_W + 5, SEL_Y, BTN_W, SEL_H), "+", Color::textPrimary(), sel_font);

        // START Button
        fb.fillRoundRect(Rect(20, 175, W - 40, 55), 12, Color::primary());
        fb.drawTextCentered(Rect(20, 175, W - 40, 55), "START", Color::bgDark(), ready_font);

        // Today's Goal Card
        fb.fillRoundRect(Rect(20, 245, W - 40, 60), 8, Color::bgCard());
        fb.drawTextCentered(Rect(20, 250, W - 40, 20), "TODAY'S GOAL", Color::textSecondary(), sel_label);
        
        FontInfo goal_font;
        goal_font.scale = 2;
        std::string goal_str = "No Goal";
        if (snap.session.target_duration_ms > 0) {
            int goal_min = static_cast<int>(snap.session.target_duration_ms / 60000);
            int h = goal_min / 60;
            int m = goal_min % 60;
            char buf[16];
            if (h > 0) snprintf(buf, sizeof(buf), "%dh %02dm", h, m);
            else snprintf(buf, sizeof(buf), "%dm", m);
            goal_str = buf;
        }
        fb.drawTextCentered(Rect(20, 270, W - 40, 30), goal_str, Color::primary(), goal_font);

    } else if (internal_state_ == InternalState::CALIBRATING) {
        // --- Pre-session Check Page ---
        FontInfo title_font;
        title_font.scale = 2;
        fb.drawTextCentered(Rect(0, 45, W, 25), "Pre-Check", Color::textPrimary(), title_font);

        FontInfo item_font;
        item_font.scale = 1;
        FontInfo val_font;
        val_font.scale = 1;

        const int LIST_Y = 80;
        const int ROW_H = 30;

        // 1. Camera status
        fb.drawText(20, LIST_Y, "Camera:", Color::textSecondary(), item_font);
        if (snap.device.camera_ok) {
            fb.drawText(100, LIST_Y, "OK", Color::primary(), val_font);
        } else {
            fb.drawText(100, LIST_Y, "Not Found", Color::error(), val_font);
        }

        // 2. Frame quality
        fb.drawText(20, LIST_Y + ROW_H, "Quality:", Color::textSecondary(), item_font);
        if (snap.frame_quality.score >= 0.6f) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(snap.frame_quality.score * 100));
            fb.drawText(100, LIST_Y + ROW_H, buf, Color::primary(), val_font);
        } else if (snap.frame_quality.score > 0.0f) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(snap.frame_quality.score * 100));
            fb.drawText(100, LIST_Y + ROW_H, buf, Color::secondary(), val_font);
        } else {
            fb.drawText(100, LIST_Y + ROW_H, "N/A", Color::textSecondary(), val_font);
        }

        // 3. BLE band
        fb.drawText(20, LIST_Y + ROW_H * 2, "Band:", Color::textSecondary(), item_font);
        if (snap.device.bracelet_connected) {
            fb.drawText(100, LIST_Y + ROW_H * 2, "Connected", Color::primary(), val_font);
        } else if (snap.device.ble_enabled) {
            fb.drawText(100, LIST_Y + ROW_H * 2, "Searching", Color::secondary(), val_font);
        } else {
            fb.drawText(100, LIST_Y + ROW_H * 2, "Off", Color::textSecondary(), val_font);
        }

        // 4. Mode
        fb.drawText(20, LIST_Y + ROW_H * 3, "Mode:", Color::textSecondary(), item_font);
        const char* mode_str = "Normal";
        if (snap.device.system_mode == SystemMode::SILENT) mode_str = "Silent";
        else if (snap.device.system_mode == SystemMode::HOME) mode_str = "Home";
        fb.drawText(100, LIST_Y + ROW_H * 3, mode_str, Color::textPrimary(), val_font);

        // 5. Target duration
        fb.drawText(20, LIST_Y + ROW_H * 4, "Timer:", Color::textSecondary(), item_font);
        char timer_buf[16];
        if (target_duration_min_ >= 60) {
            int h = target_duration_min_ / 60;
            int m = target_duration_min_ % 60;
            if (m > 0) snprintf(timer_buf, sizeof(timer_buf), "%dh%02dm", h, m);
            else snprintf(timer_buf, sizeof(timer_buf), "%dh", h);
        } else {
            snprintf(timer_buf, sizeof(timer_buf), "%dmin", target_duration_min_);
        }
        fb.drawText(100, LIST_Y + ROW_H * 4, timer_buf, Color::textPrimary(), val_font);

        // Tip
        FontInfo tip_font;
        tip_font.scale = 1;
        if (!snap.device.camera_ok) {
            fb.drawTextCentered(Rect(0, LIST_Y + ROW_H * 5 + 10, W, 15), "Tip: Connect camera for", Color::textSecondary(), tip_font);
            fb.drawTextCentered(Rect(0, LIST_Y + ROW_H * 5 + 25, W, 15), "posture detection", Color::textSecondary(), tip_font);
        } else if (snap.frame_quality.score < 0.4f) {
            fb.drawTextCentered(Rect(0, LIST_Y + ROW_H * 5 + 10, W, 15), "Tip: Adjust lighting or", Color::textSecondary(), tip_font);
            fb.drawTextCentered(Rect(0, LIST_Y + ROW_H * 5 + 25, W, 15), "camera angle", Color::textSecondary(), tip_font);
        }

        // GO Button
        fb.fillRoundRect(Rect(20, H - 80, W - 40, 55), 10, Color::primary());
        FontInfo go_font;
        go_font.scale = 3;
        fb.drawTextCentered(Rect(20, H - 80, W - 40, 55), "GO", Color::bgDark(), go_font);

    } else if (internal_state_ == InternalState::FOCUSED) {
        // --- Active Session Screen ---
        FontInfo title_font;
        title_font.scale = 2;
        fb.drawTextCentered(Rect(0, 50, W, 30), "FOCUSING", Color::primary(), title_font);

        FontInfo small_font;
        small_font.scale = 1;
        char dist_buf[32];
        snprintf(dist_buf, sizeof(dist_buf), "%d distractions", snap.session.distraction_count);
        fb.drawTextCentered(Rect(0, 80, W, 20), dist_buf, Color::textSecondary(), small_font);

        // Calculate elapsed time from system clock (not dependent on vision data)
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t elapsed_ms = 0;
        if (snap.session.start_ms > 0) {
            elapsed_ms = now_ms - snap.session.start_ms;
        }
        if (elapsed_ms < 0) elapsed_ms = 0;

        // Draw circular progress
        int cx = W / 2;
        int cy = 170;
        int radius = 50;
        int thickness = 8;
        fb.drawRing(cx, cy, radius, thickness, Color::bgCard());
        float progress = 0.0f;
        if (snap.session.target_duration_ms > 0) {
            progress = static_cast<float>(elapsed_ms) / static_cast<float>(snap.session.target_duration_ms);
            if (progress > 1.0f) progress = 1.0f;
        }
        fb.drawArc(cx, cy, radius, thickness, -M_PI/2, -M_PI/2 + (progress * 2.0f * M_PI), Color::primary());

        // Time text inside ring
        FontInfo time_font;
        time_font.scale = 2;
        int64_t elapsed_sec = elapsed_ms / 1000;
        std::string time_str = formatDuration(elapsed_sec);
        fb.drawTextCentered(Rect(cx - radius, cy - 10, radius * 2, 20), time_str, Color::textPrimary(), time_font);

        // Remaining time display
        if (snap.session.target_duration_ms > 0) {
            int64_t remaining_ms = snap.session.target_duration_ms - elapsed_ms;
            if (remaining_ms < 0) remaining_ms = 0;
            int64_t remaining_sec = remaining_ms / 1000;
            std::string remain_str = formatDuration(remaining_sec);
            FontInfo remain_font;
            remain_font.scale = 1;
            fb.drawTextCentered(Rect(0, cy + radius + 5, W, 15), remain_str, Color::textSecondary(), remain_font);
        }

        // PAUSE Button
        fb.fillRoundRect(Rect(20, 250, W - 40, 50), 8, Color::bgCard());
        fb.drawTextCentered(Rect(20, 250, W - 40, 50), "PAUSE", Color::textPrimary(), title_font);
        
        // Focus State
        Color state_color = focusStateColor(snap.focus_state);
        const char* state_text = focusStateText(snap.focus_state);
        if (snap.focus_state != FocusState::FOCUSED) {
            fb.drawTextCentered(Rect(0, 220, W, 20), state_text, state_color, small_font);
        }
    }
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
        const int SEL_Y = 120;
        const int SEL_H = 40;
        const int BTN_W = 40;
        const int VAL_W = 80;

        // [-] button
        if (y >= SEL_Y && y <= SEL_Y + SEL_H && x >= 30 && x <= 30 + BTN_W) {
            target_duration_min_ -= DURATION_STEP;
            if (target_duration_min_ < DURATION_MIN) target_duration_min_ = DURATION_MIN;
            return true;
        }

        // [+] button
        int plus_x = 30 + BTN_W + 5 + VAL_W + 5;
        if (y >= SEL_Y && y <= SEL_Y + SEL_H && x >= plus_x && x <= plus_x + BTN_W) {
            target_duration_min_ += DURATION_STEP;
            if (target_duration_min_ > DURATION_MAX) target_duration_min_ = DURATION_MAX;
            return true;
        }

        // START button
        if (y > 175 && y < 230 && x > 20 && x < W - 20) {
            // START button tapped - begin session with calibration
            internal_state_ = InternalState::CALIBRATING;
            calibration_start_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

            SessionInfo s;
            s.active = true;
            s.calibrating = true;
            s.calibration_completed = false;
            s.session_id = "session_" + std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            s.start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            s.elapsed_ms = 0;
            s.distraction_count = 0;
            s.reminder_count = 0;
            s.away_since_ms = 0;
            s.away_timeout_ms = snap.session.away_timeout_ms;
            s.target_duration_ms = target_duration_min_ * 60000;
            store_->updateSession(s);
            store_->notifySessionChange();

            LOGI("StatusPage", "Session started (calibration, target=%dmin)", target_duration_min_);
            return true;
        }
    } else if (internal_state_ == InternalState::CALIBRATING) {
        if (y > H - 90) {
            // GO button tapped - start focused monitoring
            internal_state_ = InternalState::FOCUSED;

            SessionInfo s = store_->snapshot().session;
            s.calibrating = false;
            s.calibration_completed = true;
            store_->updateSession(s);
            return true;
        }
    } else if (internal_state_ == InternalState::FOCUSED) {
        if (y > 250 && y < 300 && x > 20 && x < W - 20) {
            // PAUSE button tapped - end session
            internal_state_ = InternalState::READY;

            SessionInfo s = store_->snapshot().session;
            s.active = false;
            store_->updateSession(s);
            store_->notifySessionChange();

            // Generate report before navigating to review
            if (report_gen_ && !s.session_id.empty()) {
                SessionReport report = report_gen_->generateReport(s.session_id);
                store_->updateLastReport(report);
            }

            store_->resetSession();

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
        case FocusState::FOCUSED:           return "Focused";
        case FocusState::LOOKING_AWAY:      return "Looking Away";
        case FocusState::SLEEPY_OR_SLUMPED: return "Fatigued";
        case FocusState::PHONE_IN_USE:      return "Phone Detected";
        case FocusState::PHONE_IN_LEARNING: return "Phone Learning";
        case FocusState::PHONE_IN_PLAYING:  return "Phone Gaming!";
        case FocusState::AWAY:              return "Away";
        case FocusState::LOW_CONFIDENCE:    return "Low Confidence";
        default:                            return "Unknown";
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

} // namespace edgefocus
