#include "SettingsPage.h"
#include <cstdio>

namespace edgefocus {

void SettingsPage::render(FrameBuffer& fb, const SystemSnapshot& snap) {
    const int W = fb.width();
    const int H = fb.height();

    fb.clear(Color::bgDark());

    // --- Mode ---
    const int MODE_Y = 60;
    FontInfo label_font;
    label_font.scale = 1;
    fb.drawText(20, MODE_Y, "MODE", Color::textSecondary(), label_font);

    bool is_silent = (snap.device.system_mode == SystemMode::SILENT);

    int panel_w = W - 40;
    int btn_w = panel_w / 2;
    int mode_panel_y = MODE_Y + 20;

    FontInfo btn_font;
    btn_font.scale = 2;

    // NORM button
    fb.fillRoundRect(Rect(20, mode_panel_y, btn_w, 60), 8, is_silent ? Color::bgCard() : Color::primary());
    fb.drawTextCentered(Rect(20, mode_panel_y, btn_w, 60), "NORM", is_silent ? Color::textPrimary() : Color::bgDark(), btn_font);

    // SILENT button
    fb.fillRoundRect(Rect(20 + btn_w, mode_panel_y, btn_w, 60), 8, is_silent ? Color::primary() : Color::bgCard());
    fb.drawTextCentered(Rect(20 + btn_w, mode_panel_y, btn_w, 60), "SILENT", is_silent ? Color::bgDark() : Color::textPrimary(), btn_font);

    // --- Away Timeout ---
    const int TIMEOUT_Y = mode_panel_y + 80;
    fb.drawText(20, TIMEOUT_Y, "AWAY TIMEOUT", Color::textSecondary(), label_font);

    int timeout_panel_y = TIMEOUT_Y + 20;
    int small_btn_w = 50;
    int value_w = panel_w - small_btn_w * 2;

    // Minus button
    fb.fillRoundRect(Rect(20, timeout_panel_y, small_btn_w, 60), 8, Color::bgCard());
    fb.drawTextCentered(Rect(20, timeout_panel_y, small_btn_w, 60), "-", Color::textPrimary(), btn_font);

    // Value display
    fb.fillRoundRect(Rect(20 + small_btn_w, timeout_panel_y, value_w, 60), 8, Color::bgCard());
    int timeout_min = static_cast<int>(snap.session.away_timeout_ms / 60000);
    char timeout_buf[16];
    snprintf(timeout_buf, sizeof(timeout_buf), "%d min", timeout_min);
    fb.drawTextCentered(Rect(20 + small_btn_w, timeout_panel_y, value_w, 60), timeout_buf, Color::textPrimary(), btn_font);

    // Plus button
    fb.fillRoundRect(Rect(20 + small_btn_w + value_w, timeout_panel_y, small_btn_w, 60), 8, Color::bgCard());
    fb.drawTextCentered(Rect(20 + small_btn_w + value_w, timeout_panel_y, small_btn_w, 60), "+", Color::textPrimary(), btn_font);
}

bool SettingsPage::onTouch(int x, int y) {
    const int W = 240;
    const int MODE_Y = 60;
    int mode_panel_y = MODE_Y + 20;
    int btn_w = (W - 40) / 2;

    // Mode toggle
    if (y >= mode_panel_y && y <= mode_panel_y + 60) {
        if (x >= 20 && x <= 20 + btn_w) {
            if (store_) {
                DeviceStatus ds = store_->snapshot().device;
                ds.system_mode = SystemMode::NORMAL;
                store_->updateDeviceStatus(ds);
            }
            return true;
        } else if (x >= 20 + btn_w && x <= W - 20) {
            if (store_) {
                DeviceStatus ds = store_->snapshot().device;
                ds.system_mode = SystemMode::SILENT;
                store_->updateDeviceStatus(ds);
            }
            return true;
        }
    }

    // Away timeout adjustment
    int timeout_panel_y = mode_panel_y + 100;
    int small_btn_w = 50;
    int value_w = (W - 40) - small_btn_w * 2;

    if (y >= timeout_panel_y && y <= timeout_panel_y + 60) {
        if (!store_) return false;
        SessionInfo s = store_->snapshot().session;
        int current_min = static_cast<int>(s.away_timeout_ms / 60000);

        if (x >= 20 && x <= 20 + small_btn_w) {
            // Minus button
            current_min -= TIMEOUT_STEP;
            if (current_min < TIMEOUT_MIN_VAL) current_min = TIMEOUT_MIN_VAL;
            s.away_timeout_ms = current_min * 60 * 1000;
            store_->updateSession(s);
            return true;
        } else if (x >= 20 + small_btn_w + value_w && x <= W - 20) {
            // Plus button
            current_min += TIMEOUT_STEP;
            if (current_min > TIMEOUT_MAX_VAL) current_min = TIMEOUT_MAX_VAL;
            s.away_timeout_ms = current_min * 60 * 1000;
            store_->updateSession(s);
            return true;
        }
    }

    return false;
}

} // namespace edgefocus
