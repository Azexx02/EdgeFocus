#include "StatsPage.h"
#include <cstdio>

namespace edgefocus {

void StatsPage::render(FrameBuffer& fb, const SystemSnapshot& snap) {
    const int W = fb.width(); // 240
    const int H = fb.height(); // 320

    fb.clear(Color::bgDark());

    // NOTE: Global header (Mode, Camera, Battery) is drawn by UIManager at the top 40px.

    // --- Big focus time ---
    FontInfo label_font;
    label_font.scale = 1;
    fb.drawTextCentered(Rect(0, 60, W, 20), "今日专注", Color::primary(), label_font);

    std::string focus_str = formatMinutes(snap.today.focus_seconds);
    FontInfo big_font;
    big_font.scale = 3;
    fb.drawTextCentered(Rect(0, 80, W, 50), focus_str, Color::primary(), big_font);

    fb.fillRoundRect(Rect(20, 140, W - 40, 80), 8, Color::bgCard());
    
    const int CHART_Y = 150;
    const int CHART_H = 60;
    const int NUM_BARS = 5;
    const int BAR_W = 20;
    const int SPACING = 15;
    const int CHART_W = NUM_BARS * BAR_W + (NUM_BARS - 1) * SPACING;
    const int START_X = (W - CHART_W) / 2;

    // Bar heights from today's sessions (simplified: use today focus_seconds distributed)
    int heights[NUM_BARS] = {0, 0, 0, 0, 0};
    if (snap.today.focus_seconds > 0) {
        int avg = static_cast<int>(snap.today.focus_seconds / NUM_BARS);
        for (int i = 0; i < NUM_BARS && i < snap.today.sessions; ++i) {
            heights[i] = std::min(60, avg / 60); // Scale to chart height
        }
    }
    for (int i = 0; i < NUM_BARS; i++) {
        if (heights[i] > 0) {
            fb.fillRect(Rect(START_X + i * (BAR_W + SPACING), CHART_Y + (CHART_H - heights[i]), BAR_W, heights[i]), Color::primary());
        }
    }

    // --- Stat cards ---
    const int CARDS_Y = 230;
    const int CARD_W = (W - 50) / 2; // (240-50)/2 = 95
    
    // Card 1: Efficiency
    fb.fillRoundRect(Rect(20, CARDS_Y, CARD_W, 80), 8, Color::bgCard());
    FontInfo label_font_small;
    label_font_small.scale = 1;
    fb.drawTextCentered(Rect(20, CARDS_Y + 10, CARD_W, 20), "效率", Color::textSecondary(), label_font_small);
    
    int eff = snap.today.total_seconds > 0 ? (snap.today.focus_seconds * 100) / snap.today.total_seconds : 0;
    char eff_buf[16];
    snprintf(eff_buf, sizeof(eff_buf), "%d%%", eff);
    FontInfo stat_font;
    stat_font.scale = 2;
    fb.drawTextCentered(Rect(20, CARDS_Y + 40, CARD_W, 30), eff_buf, Color::primary(), stat_font);

    // Card 2: Distractions
    fb.fillRoundRect(Rect(W - 20 - CARD_W, CARDS_Y, CARD_W, 80), 8, Color::bgCard());
    fb.drawTextCentered(Rect(W - 20 - CARD_W, CARDS_Y + 10, CARD_W, 20), "分心", Color::textSecondary(), label_font_small);

    int events = snap.today.events;
    char dist_buf[16];
    snprintf(dist_buf, sizeof(dist_buf), "%d", events);
    fb.drawTextCentered(Rect(W - 20 - CARD_W, CARDS_Y + 40, CARD_W, 30), dist_buf, Color::textSecondary(), stat_font);
}

bool StatsPage::onTouch(int x, int y) {
    // Navigation is now handled by Swipe in UIManager.
    (void)x; (void)y;
    return false;
}

std::string StatsPage::formatMinutes(int64_t seconds) const {
    int h = static_cast<int>(seconds / 3600);
    int m = static_cast<int>((seconds % 3600) / 60);
    char buf[32];
    if (h > 0) snprintf(buf, sizeof(buf), "%02d时%02d分", h, m);
    else snprintf(buf, sizeof(buf), "%02d分", m);
    return std::string(buf);
}

} // namespace edgefocus
