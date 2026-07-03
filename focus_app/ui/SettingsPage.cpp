#include "SettingsPage.h"
#include <cstdio>
#include <chrono>

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
    int timeout_min = snap.session.active
        ? static_cast<int>(snap.session.away_timeout_ms / 60000)
        : snap.preferences.away_timeout_min;
    char timeout_buf[16];
    snprintf(timeout_buf, sizeof(timeout_buf), "%d min", timeout_min);
    fb.drawTextCentered(Rect(20 + small_btn_w, timeout_panel_y, value_w, 60), timeout_buf, Color::textPrimary(), btn_font);

    // Plus button
    fb.fillRoundRect(Rect(20 + small_btn_w + value_w, timeout_panel_y, small_btn_w, 60), 8, Color::bgCard());
    fb.drawTextCentered(Rect(20 + small_btn_w + value_w, timeout_panel_y, small_btn_w, 60), "+", Color::textPrimary(), btn_font);

    // --- Wristband (BLE) ---
    const int BLE_Y = timeout_panel_y + 80;
    fb.drawText(20, BLE_Y, "WRISTBAND", Color::textSecondary(), label_font);
    int ble_panel_y = BLE_Y + 20;
    bool ble_on = snap.device.ble_enabled;
    bool ble_conn = snap.device.bracelet_connected;
    const char* status_text = "Off";
    Color status_color = Color::textSecondary();
    if (ble_on && ble_conn) { status_text = "Connected"; status_color = Color::primary(); }
    else if (ble_on) { status_text = "Connecting..."; status_color = Color::secondary(); }
    fb.fillRoundRect(Rect(20, ble_panel_y, panel_w - btn_w, 60), 8, Color::bgCard());
    fb.drawTextCentered(Rect(20, ble_panel_y, panel_w - btn_w, 60), status_text, status_color, label_font);
    const char* btn_text = ble_on ? "Disconnect" : "Connect";
    Color btn_color = ble_on ? Color::error() : Color::primary();
    fb.fillRoundRect(Rect(20 + panel_w - btn_w, ble_panel_y, btn_w, 60), 8, btn_color);
    fb.drawTextCentered(Rect(20 + panel_w - btn_w, ble_panel_y, btn_w, 60), btn_text, Color::bgDark(), label_font);
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
                if (mode_mgr_) mode_mgr_->setMode(SystemMode::NORMAL);
            }
            return true;
        } else if (x >= 20 + btn_w && x <= W - 20) {
            if (store_) {
                DeviceStatus ds = store_->snapshot().device;
                ds.system_mode = SystemMode::SILENT;
                store_->updateDeviceStatus(ds);
                if (mode_mgr_) mode_mgr_->setMode(SystemMode::SILENT);
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
        auto snap = store_->snapshot();
        SessionInfo s = snap.session;
        Preferences prefs = snap.preferences;
        int current_min = s.active ? static_cast<int>(s.away_timeout_ms / 60000)
                                   : prefs.away_timeout_min;

        if (x >= 20 && x <= 20 + small_btn_w) {
            // Minus button
            current_min -= TIMEOUT_STEP;
            if (current_min < TIMEOUT_MIN_VAL) current_min = TIMEOUT_MIN_VAL;
            prefs.away_timeout_min = current_min;
            store_->updatePreferences(prefs);
            s.away_timeout_ms = current_min * 60 * 1000;
            if (s.active) store_->updateSessionIfCurrent(s);
            return true;
        } else if (x >= 20 + small_btn_w + value_w && x <= W - 20) {
            // Plus button
            current_min += TIMEOUT_STEP;
            if (current_min > TIMEOUT_MAX_VAL) current_min = TIMEOUT_MAX_VAL;
            prefs.away_timeout_min = current_min;
            store_->updatePreferences(prefs);
            s.away_timeout_ms = current_min * 60 * 1000;
            if (s.active) store_->updateSessionIfCurrent(s);
            return true;
        }
    }

    // BLE wristband toggle
    int ble_panel_y = timeout_panel_y + 100;
    int ble_btn_x = 20 + (W - 40) - btn_w;
    if (y >= ble_panel_y && y <= ble_panel_y + 60 && x >= ble_btn_x && x <= W - 20) {
        if (store_) {
            DeviceStatus ds = store_->snapshot().device;
            ds.ble_enabled = !ds.ble_enabled;
            store_->updateDeviceStatus(ds);
        }
        return true;
    }

    return false;
}

} // namespace edgefocus
