#include "ReviewPage.h"
#include <cstdio>
#include <string>

namespace edgefocus {

void ReviewPage::render(FrameBuffer& fb, const SystemSnapshot& snap) {
    const int W = fb.width(); // 240
    const int H = fb.height(); // 320

    fb.clear(Color::bgDark());

    // --- Header ---
    FontInfo cap_font;
    cap_font.scale = 2;
    fb.drawText(20, 10, "LEARN_OS", Color::primary(), cap_font);

    int bat_pct = snap.device.battery_percent >= 0 ? snap.device.battery_percent : 100;
    fb.drawIconBattery(W - 36, 10, 20, 12, bat_pct, Color::textSecondary());

    const auto& report = snap.last_report;

    // --- Trophy & Score ---
    fb.drawIconTrophy(W / 2, 60, 30, Color::primary());

    int score = 0;
    if (report.total_duration > 0) {
        float ratio = static_cast<float>(report.focus_duration) / static_cast<float>(report.total_duration);
        score = static_cast<int>(ratio * 100);
    }
    
    char score_buf[16];
    snprintf(score_buf, sizeof(score_buf), "%d", score);

    FontInfo score_font;
    score_font.scale = 6;
    fb.drawTextCentered(Rect(0, 90, W, 60), score_buf, Color::primary(), score_font);

    FontInfo label_font;
    label_font.scale = 1;
    fb.drawTextCentered(Rect(0, 155, W, 20), "FOCUS SCORE", Color::primary(), label_font);

    // --- Stats Card ---
    fb.fillRoundRect(Rect(20, 180, W - 40, 60), 8, Color::bgCard());
    
    // Left: Duration
    FontInfo label_font_small;
    label_font_small.scale = 1;
    fb.drawTextCentered(Rect(20, 185, W / 2 - 20, 20), "TIME", Color::textSecondary(), label_font_small);
    
    int mins = report.total_duration > 0 ? report.focus_duration / 60 : 0;
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%dm", mins);
    FontInfo stat_font;
    stat_font.scale = 2;
    fb.drawTextCentered(Rect(20, 210, W / 2 - 20, 20), time_buf, Color::textPrimary(), stat_font);

    // Divider
    fb.fillRect(Rect(W / 2, 190, 1, 40), Color::divider());

    // Right: Distractions
    fb.drawTextCentered(Rect(W / 2, 185, W / 2 - 20, 20), "DISTS", Color::textSecondary(), label_font_small);
    int dists = report.total_duration > 0 ? report.distraction_count : 0;
    char dist_buf[16];
    snprintf(dist_buf, sizeof(dist_buf), "%d", dists);
    fb.drawTextCentered(Rect(W / 2, 210, W / 2 - 20, 20), dist_buf, Color::textPrimary(), stat_font);

    // --- DONE Button ---
    fb.fillRoundRect(Rect(20, H - 70, W - 40, 60), 12, Color::primary());
    FontInfo btn_font;
    btn_font.scale = 2;
    fb.drawTextCentered(Rect(20, H - 70, W - 40, 60), "DONE", Color::bgDark(), btn_font);
}

bool ReviewPage::onTouch(int x, int y) {
    (void)x;
    const int H = 320;
    if (y > H - 80) {
        if (switch_page_) switch_page_(0); // PAGE_STATUS
        return true;
    }
    return false;
}

std::string ReviewPage::formatDuration(int64_t seconds) const {
    int h = static_cast<int>(seconds / 3600);
    int m = static_cast<int>((seconds % 3600) / 60);
    int s = static_cast<int>(seconds % 60);
    char buf[16];
    if (h > 0) snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    else snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return std::string(buf);
}

} // namespace edgefocus
