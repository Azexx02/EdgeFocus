#include "CalibrationPage.h"
#include <cstdio>

namespace edgefocus {

void CalibrationPage::render(FrameBuffer& fb, const SystemSnapshot& snap) {
    const int W = fb.width();
    const int H = fb.height();

    fb.clear(Color::bgDark());

    // --- Header ---
    const int HEADER_H = 80;
    fb.fillRect(Rect(0, 0, W, HEADER_H), Color::bgCard());
    FontInfo title_font;
    title_font.scale = 3;
    fb.drawTextCentered(Rect(0, 0, W, HEADER_H), "校准",
                        Color::textPrimary(), title_font);

    // --- Quality indicator circle ---
    const int CIRCLE_Y = HEADER_H + 120;
    const int CIRCLE_R = 60;

    Color q_color;
    const char* q_text;
    if (snap.frame_quality.score >= 0.6f) {
        q_color = Color::focused();
        q_text = "好";
    } else if (snap.frame_quality.score >= 0.4f) {
        q_color = Color::lightDistracted();
        q_text = "一般";
    } else {
        q_color = Color::severeDistracted();
        q_text = "差";
    }

    fb.fillCircle(W / 2, CIRCLE_Y, CIRCLE_R, q_color);

    // Score text inside circle
    char score_buf[16];
    snprintf(score_buf, sizeof(score_buf), "%d", static_cast<int>(snap.frame_quality.score * 100));
    FontInfo score_font;
    score_font.scale = 3;
    fb.drawTextCentered(Rect(W / 2 - CIRCLE_R, CIRCLE_Y - 20, CIRCLE_R * 2, 40),
                        score_buf, Color::white(), score_font);

    // Quality label
    FontInfo label_font;
    label_font.scale = 2;
    int tw = fb.textWidth(q_text, label_font);
    fb.drawText((W - tw) / 2, CIRCLE_Y + CIRCLE_R + 15, q_text, q_color, label_font);

    // --- Quality reason ---
    const int REASON_Y = CIRCLE_Y + CIRCLE_R + 60;
    if (!snap.frame_quality.reason.empty()) {
        FontInfo reason_font;
        reason_font.scale = 2;
        int rw = fb.textWidth(snap.frame_quality.reason, reason_font);
        fb.drawText((W - rw) / 2, REASON_Y, snap.frame_quality.reason,
                    Color::textSecondary(), reason_font);
    }

    // --- Guidance tips ---
    const int TIPS_Y = REASON_Y + 60;
    FontInfo tip_font;
    tip_font.scale = 2;

    fb.drawText(40, TIPS_Y, "放置提示:", Color::accent(), tip_font);

    const char* tips[] = {
        "- 脸在画面中央",
        "- 光线保持充足",
        "- 避免逆光",
        "- 摄像头与眼平齐",
        "- 移除遮挡",
    };
    const int TIP_COUNT = 5;
    for (int i = 0; i < TIP_COUNT; i++) {
        fb.drawText(60, TIPS_Y + 40 + i * 36, tips[i], Color::textSecondary(), tip_font);
    }

    // --- Guidance needed banner ---
    if (snap.frame_quality.guidance_needed) {
        const int BAN_Y = TIPS_Y + 40 + TIP_COUNT * 36 + 20;
        fb.fillRoundRect(Rect(24, BAN_Y, W - 48, 80), 12, Color(80, 60, 10));
        fb.drawRoundRect(Rect(24, BAN_Y, W - 48, 80), 12, Color::lightDistracted(), 1);

        FontInfo ban_font;
        ban_font.scale = 2;
        fb.drawTextCentered(Rect(24, BAN_Y, W - 48, 80),
                            "调整角度或光线",
                            Color::lightDistracted(), ban_font);
    }

    // --- Bottom nav bar ---
    const int NAV_H = 120;
    const int NAV_Y = H - NAV_H;
    fb.fillRect(Rect(0, NAV_Y, W, NAV_H), Color::bgCard());
    fb.fillRect(Rect(0, NAV_Y, W, 2), Color::divider());

    const int BTN_W = W / 3;
    FontInfo nav_font;
    nav_font.scale = 2;

    fb.drawTextCentered(Rect(0, NAV_Y + 15, BTN_W, NAV_H - 30), "状态",
                        Color::textSecondary(), nav_font);
    fb.drawTextCentered(Rect(BTN_W, NAV_Y + 15, BTN_W, NAV_H - 30), "统计",
                        Color::textSecondary(), nav_font);

    fb.fillRoundRect(Rect(BTN_W * 2 + 10, NAV_Y + 15, BTN_W - 20, NAV_H - 30), 8, Color(40, 40, 65));
    fb.drawTextCentered(Rect(BTN_W * 2, NAV_Y + 15, BTN_W, NAV_H - 30), "更多",
                        Color::accent(), nav_font);
}

bool CalibrationPage::onTouch(int x, int y) {
    (void)x; (void)y;
    return false;
}

} // namespace edgefocus
