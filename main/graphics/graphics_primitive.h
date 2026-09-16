#ifndef GRAPHICS_PRIMITIVE_H
#define GRAPHICS_PRIMITIVE_H

#include <stdint.h>
#include <stdarg.h>
#include "Colors.h"

#ifdef __cplusplus
extern "C" {
#endif

void tft_set_rotation(int rot);
void tft_fill_screen(uint16_t color);
void tft_draw_string(const char* str, int x, int y, uint16_t color, int size);
void tft_set_text_color(uint16_t color);
void tft_set_text_color_bg(uint16_t fg, uint16_t bg);
void tft_set_text_size(uint8_t size);
void tft_set_cursor(int16_t x, int16_t y);
void tft_print(const char* str);
void tft_printf(const char* format, ...);
int16_t tft_text_width(const char* str);
int16_t tft_font_height();
void tft_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void tft_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void tft_fill_round_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
void tft_draw_round_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
void tft_fill_circle(int32_t x, int32_t y, int32_t r, uint16_t color);
void tft_draw_circle(int32_t x, int32_t y, int32_t r, uint16_t color);
void tft_fill_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
void tft_draw_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
void tft_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
void tft_draw_pixel(int32_t x, int32_t y, uint16_t color);
void tft_set_font(const void* font);
void tft_set_text_datum(uint8_t datum);

#ifdef __cplusplus
}
#endif

#endif