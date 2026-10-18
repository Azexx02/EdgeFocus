#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace edgefocus {

struct SpiLcdContext;


// ---------------------------------------------------------------------------
// Color — 32-bit ARGB pixel
// ---------------------------------------------------------------------------

struct Color {
    uint8_t b, g, r, a;

    Color() : b(0), g(0), r(0), a(255) {}
    Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : b(b), g(g), r(r), a(a) {}

    uint32_t toARGB() const {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8)  |
               static_cast<uint32_t>(b);
    }

    static Color fromRGB(uint8_t r, uint8_t g, uint8_t b) { return Color(r, g, b); }

    // Preset colors
    static Color black()       { return Color(0, 0, 0); }
    static Color white()       { return Color(255, 255, 255); }
    static Color transparent() { return Color(0, 0, 0, 0); }

    // Focus state colors
    static Color focused()          { return Color(76, 175, 80); }    // green
    static Color lightDistracted()  { return Color(255, 193, 7); }    // yellow
    static Color mediumDistracted() { return Color(255, 152, 0); }    // orange
    static Color severeDistracted() { return Color(244, 67, 54); }    // red
    static Color away()             { return Color(158, 158, 158); }  // gray
    static Color lowConfidence()    { return Color(100, 100, 100); }  // dark gray
    static Color paused()           { return Color(33, 150, 243); }   // blue
    static Color calibrating()      { return Color(255, 235, 59); }   // bright yellow

    // UI colors (from DESIGN.md / Gemini C++ UI code)
    static Color bgDark()      { return Color(15, 23, 36); }     // #0F1724 (COL_BG)
    static Color bgCard()      { return Color(36, 42, 56); }     // #242A38 (COL_PANEL)
    static Color textPrimary() { return Color(255, 255, 255); }  // #FFFFFF (COL_TEXT)
    static Color textSecondary(){ return Color(150, 160, 170); } // #96A0AA (COL_TEXT_DIM)
    static Color primary()     { return Color(94, 224, 139); }   // #5EE08B (COL_GREEN)
    static Color primaryDim()  { return Color(60, 150, 90); }    // #3C965A (COL_GREEN_DIM)
    static Color secondary()   { return Color(255, 198, 64); }   // Warning/Secondary 
    static Color error()       { return Color(255, 180, 180); }  // #FFB4B4 (COL_ALERT_RED)
    static Color errorContainer(){ return Color(255, 240, 240); } // #FFF0F0 (COL_ALERT_BG)
    static Color divider()     { return Color(45, 52, 73); }     // Darker panel divider
};

// ---------------------------------------------------------------------------
// Rect — axis-aligned rectangle
// ---------------------------------------------------------------------------

struct Rect {
    int x, y, w, h;

    Rect() : x(0), y(0), w(0), h(0) {}
    Rect(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {}

    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    Rect inset(int dx, int dy) const {
        return Rect(x + dx, y + dy, w - 2 * dx, h - 2 * dy);
    }
};

// ---------------------------------------------------------------------------
// FontInfo — bitmap font metrics (built-in 8x16 monospace)
// ---------------------------------------------------------------------------

struct FontInfo {
    int char_width = 8;
    int char_height = 16;
    int scale = 2;  // render at 2x for 720x1280 screen
};

// ---------------------------------------------------------------------------
// FrameBuffer — Linux /dev/fb0 or offscreen buffer
// ---------------------------------------------------------------------------

class FrameBuffer {
public:
    FrameBuffer();
    ~FrameBuffer();

    // Open framebuffer device. Falls back to offscreen buffer if unavailable.
    bool init(const char* device = "/dev/fb0");

    // Initialize only the in-memory render target. Used when App/phone owns the
    // display surface and board screen hardware must not be touched.
    bool initOffscreen(int width = 240, int height = 320);

    // Close and release resources.
    void close();

    // Screen dimensions
    int width() const { return width_; }
    int height() const { return height_; }
    bool isReal() const { return real_fb_; }

    // Clear entire buffer
    void clear(const Color& bg = Color::bgDark());

    // Pixel operations
    void setPixel(int x, int y, const Color& color);

    // Shape primitives
    void fillRect(const Rect& rect, const Color& color);
    void drawRect(const Rect& rect, const Color& color, int thickness = 1);
    void fillCircle(int cx, int cy, int radius, const Color& color);
    void drawRing(int cx, int cy, int radius, int thickness, const Color& color);
    void drawArc(int cx, int cy, int radius, int thickness, float start_angle, float end_angle, const Color& color);
    void drawLine(int x0, int y0, int x1, int y1, const Color& color, int thickness = 1);

    // Rounded rectangle
    void fillRoundRect(const Rect& rect, int radius, const Color& color);
    void drawRoundRect(const Rect& rect, int radius, const Color& color, int thickness = 1);

    // Text rendering (built-in 8x16 bitmap font, scaled)
    void drawText(int x, int y, const std::string& text, const Color& color,
                  const FontInfo& font = FontInfo());
    int textWidth(const std::string& text, const FontInfo& font = FontInfo()) const;
    int textHeight(const FontInfo& font = FontInfo()) const;

    // Centered text within a rect
    void drawTextCentered(const Rect& area, const std::string& text,
                          const Color& color, const FontInfo& font = FontInfo());

    // Progress bar
    void drawProgressBar(const Rect& rect, float progress, const Color& fg,
                         const Color& bg, const Color& border);

    // Icon drawing primitives
    void drawIconGear(int cx, int cy, int radius, const Color& color);
    void drawIconBattery(int x, int y, int w, int h, int percent, const Color& color);
    void drawIconTrophy(int cx, int cy, int size, const Color& color);
    void drawIconWarning(int cx, int cy, int size, const Color& color);
    void drawIconClock(int cx, int cy, int radius, const Color& color);
    void drawIconMuted(int cx, int cy, int size, const Color& color);

    // Flush buffer to screen (for real fbdev)
    void flush();

    // Save buffer to PNG file (for headless verification)
    bool savePNG(const std::string& path) const;

    // Get raw pixel buffer (for advanced rendering)
    uint32_t* pixels() { return buffer_.data(); }
    const uint32_t* pixels() const { return buffer_.data(); }

private:
    bool initSpiLcd();
    void flushSpiLcd();
    void drawChar(int x, int y, char c, const Color& color, const FontInfo& font);

    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;        // bytes per row
    int bpp_ = 32;          // bits per pixel
    bool real_fb_ = false;  // true if using real /dev/fb0
    int fb_fd_ = -1;
    uint8_t* fb_mem_ = nullptr;
    size_t fb_size_ = 0;

    std::vector<uint32_t> buffer_;  // offscreen render buffer
    SpiLcdContext* spi_ctx_ = nullptr;
};

} // namespace edgefocus
