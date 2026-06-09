#include "SettingsPage.h"
#include <cstdio>

namespace edgefocus {

void SettingsPage::render(FrameBuffer& fb, const SystemSnapshot& snap) {
    const int W = fb.width(); // 240
    const int H = fb.height(); // 320

    fb.clear(Color::bgDark());

    // NOTE: Global header (Mode, Camera, Battery) is drawn by UIManager at the top 40px.

    // --- Mode ---
    const int MODE_Y = 60;
    FontInfo label_font;
    label_font.scale = 1;
    fb.drawText(20, MODE_Y, "MODE", Color::textSecondary(), label_font);

    // Active mode (Focus / Silent)
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

    // --- Camera ---
    const int CAM_Y = mode_panel_y + 80;
    fb.drawText(20, CAM_Y, "CAMERA", Color::textSecondary(), label_font);

    int cam_panel_y = CAM_Y + 20;
    fb.fillRoundRect(Rect(20, cam_panel_y, W - 40, 60), 8, Color::bgCard());
    
    // Left text
    fb.drawTextCentered(Rect(20, cam_panel_y, (W - 40) / 2, 60), "Vision AI", Color::textPrimary(), label_font);

    // ON button
    fb.fillRoundRect(Rect(20 + btn_w, cam_panel_y, btn_w, 60), 8, Color::primary());
    fb.drawTextCentered(Rect(20 + btn_w, cam_panel_y, btn_w, 60), "ON", Color::bgDark(), btn_font);
}

bool SettingsPage::onTouch(int x, int y) {
    // Tap Mode: Focus / Silent
    const int W = 240;
    const int MODE_Y = 60;
    int mode_panel_y = MODE_Y + 20;
    int btn_w = (W - 40) / 2;

    if (y >= mode_panel_y && y <= mode_panel_y + 60) {
        if (x >= 20 && x <= 20 + btn_w) {
            // Tapped Focus (NORM)
            if (store_) {
                DeviceStatus ds = store_->snapshot().device;
                ds.system_mode = SystemMode::NORMAL;
                store_->updateDeviceStatus(ds);
            }
            return true;
        } else if (x >= 20 + btn_w && x <= W - 20) {
            // Tapped Silent
            if (store_) {
                DeviceStatus ds = store_->snapshot().device;
                ds.system_mode = SystemMode::SILENT;
                store_->updateDeviceStatus(ds);
            }
            return true;
        }
    }

    return false;
}

} // namespace edgefocus
