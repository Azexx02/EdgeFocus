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
        fb.drawTextCentered(Rect(0, 60, W, 30), "Ready to", Color::textPrimary(), ready_font);
        fb.drawTextCentered(Rect(0, 90, W, 30), "focus?", Color::textPrimary(), ready_font);

        // START Button -> leads to calibration
        fb.fillRoundRect(Rect(20, 140, W - 40, 60), 12, Color::primary());
        fb.drawTextCentered(Rect(20, 140, W - 40, 60), "START", Color::bgDark(), ready_font);

        // Today's Goal Card
        fb.fillRoundRect(Rect(20, 220, W - 40, 80), 8, Color::bgCard());
        FontInfo label_font;
        label_font.scale = 1;
        fb.drawTextCentered(Rect(20, 230, W - 40, 20), "TODAY'S GOAL", Color::textSecondary(), label_font);
        
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
        fb.drawTextCentered(Rect(20, 250, W - 40, 30), goal_str, Color::primary(), goal_font);
        fb.drawProgressBar(Rect(30, 285, W - 60, 6), 0.3f, Color::primary(), Color::bgDark(), Color::bgDark());

    } else if (internal_state_ == InternalState::CALIBRATING) {
        // --- Calibration Transition ---
        FontInfo title_font;
        title_font.scale = 2;
        fb.drawTextCentered(Rect(0, 50, W, 30), "Calibration", Color::textPrimary(), title_font);

        const int CIRCLE_Y = 160;
        const int CIRCLE_R = 50;

        Color q_color;
        const char* q_text;
        if (snap.frame_quality.score >= 0.6f) {
            q_color = Color::primary();
            q_text = "Good";
        } else if (snap.frame_quality.score >= 0.4f) {
            q_color = Color::secondary();
            q_text = "Fair";
        } else {
            q_color = Color::error();
            q_text = "Poor";
        }

        fb.fillCircle(W / 2, CIRCLE_Y, CIRCLE_R, q_color);
        char score_buf[16];
        snprintf(score_buf, sizeof(score_buf), "%d", static_cast<int>(snap.frame_quality.score * 100));
        FontInfo score_font;
        score_font.scale = 3;
        fb.drawTextCentered(Rect(W / 2 - CIRCLE_R, CIRCLE_Y - 20, CIRCLE_R * 2, 40), score_buf, Color::bgDark(), score_font);

        fb.drawTextCentered(Rect(0, CIRCLE_Y + CIRCLE_R + 10, W, 30), q_text, q_color, title_font);

        if (!snap.frame_quality.reason.empty()) {
            FontInfo reason_font;
            reason_font.scale = 1;
            fb.drawTextCentered(Rect(0, CIRCLE_Y + CIRCLE_R + 40, W, 20), snap.frame_quality.reason, Color::textSecondary(), reason_font);
        }

        // NEXT / PASS Button
        fb.fillRoundRect(Rect(20, H - 90, W - 40, 60), 8, Color::primary());
        fb.drawTextCentered(Rect(20, H - 90, W - 40, 60), "GO", Color::bgDark(), score_font);

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

        // Draw circular progress
        int cx = W / 2;
        int cy = 170;
        int radius = 50;
        int thickness = 8;
        fb.drawRing(cx, cy, radius, thickness, Color::bgCard());
        // Real progress based on elapsed/target
        float progress = 0.0f;
        if (snap.session.target_duration_ms > 0) {
            progress = static_cast<float>(snap.session.elapsed_ms) / static_cast<float>(snap.session.target_duration_ms);
            if (progress > 1.0f) progress = 1.0f;
        }
        fb.drawArc(cx, cy, radius, thickness, -M_PI/2, -M_PI/2 + (progress * 2.0f * M_PI), Color::primary());

        // Time text inside ring
        FontInfo time_font;
        time_font.scale = 2;
        int64_t elapsed_sec = snap.session.elapsed_ms / 1000;
        std::string time_str = formatDuration(elapsed_sec);
        fb.drawTextCentered(Rect(cx - radius, cy - 10, radius * 2, 20), time_str, Color::textPrimary(), time_font);

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
        if (y > 140 && y < 200 && x > 20 && x < W - 20) {
            // START button tapped - begin session with calibration
            internal_state_ = InternalState::CALIBRATING;

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
            s.away_timeout_ms = snap.session.away_timeout_ms; // preserve user setting
            store_->updateSession(s);
            store_->notifySessionChange();

            LOGI("StatusPage", "Session started (calibration phase)");
            return true;
        }
    } else if (internal_state_ == InternalState::CALIBRATING) {
        if (y > H - 90 && y < H - 30 && x > 20 && x < W - 20) {
            // GO button tapped - skip to focused monitoring
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
