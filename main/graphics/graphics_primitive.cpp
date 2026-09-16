#include "graphics_primitive.h"
#include "graphics_lgfx.h"
#include <cstdio>
#include <cstdarg>

static LGFX_ILI9488& tft = get_tft();

void tft_set_rotation(int rot) {
    tft.setRotation(rot);
}
void tft_fill_screen(uint16_t color) { tft.fillScreen(color); }
void tft_draw_string(const char* str, int x, int y, uint16_t color, int size) {
    tft.setTextColor(color);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print(str);
}
void tft_set_text_color(uint16_t color) { tft.setTextColor(color); }
void tft_set_text_color_bg(uint16_t fg, uint16_t bg) { tft.setTextColor(fg, bg); }
void tft_set_text_size(uint8_t size) { tft.setTextSize(size); }
void tft_set_cursor(int16_t x, int16_t y) { tft.setCursor(x, y); }
void tft_print(const char* str) { tft.print(str); }
void tft_printf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    tft.print(buffer);
}
int16_t tft_text_width(const char* str) { return tft.textWidth(str); }
int16_t tft_font_height() { return tft.fontHeight(); }
void tft_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) { tft.fillRect(x, y, w, h, color); }
void tft_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) { tft.drawRect(x, y, w, h, color); }
void tft_fill_round_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) { tft.fillRoundRect(x, y, w, h, r, color); }
void tft_draw_round_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) { tft.drawRoundRect(x, y, w, h, r, color); }
void tft_fill_circle(int32_t x, int32_t y, int32_t r, uint16_t color) { tft.fillCircle(x, y, r, color); }
void tft_draw_circle(int32_t x, int32_t y, int32_t r, uint16_t color) { tft.drawCircle(x, y, r, color); }
void tft_fill_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color) { tft.fillTriangle(x0, y0, x1, y1, x2, y2, color); }
void tft_draw_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color) {
    tft.drawLine(x0, y0, x1, y1, color);
    tft.drawLine(x1, y1, x2, y2, color);
    tft.drawLine(x2, y2, x0, y0, color);
}
void tft_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color) { tft.drawLine(x0, y0, x1, y1, color); }
void tft_draw_pixel(int32_t x, int32_t y, uint16_t color) { tft.drawPixel(x, y, color); }
void tft_set_font(const void* font) { tft.setFont((const lgfx::IFont*)font); }
void tft_set_text_datum(uint8_t datum) { tft.setTextDatum((textdatum_t)datum); }