// taper_morse_scene.cpp
#include "taper_morse_scene.h"
#include "scene_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_vars.h"
#include "graphics_lgfx.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "keypad.h"
#include "colors.h"
#include "main_scene.h"
#include "image_manager.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>

static const char *TAG = "TAPER_MORSE";

struct MorseTaper
{
    std::string size;
    float D1;
    float D2;
    float L;
    float angle;        // gradi decimali
    std::string isoThread;
};

static const MorseTaper morseTapers[] = {
    {"MT0", 9.045f, 6.401f, 50.8f, 1.4908f, "M6"},
    {"MT1", 12.065f, 9.373f, 53.975f, 1.4287f, "M10"},
    {"MT2", 17.780f, 14.529f, 65.087f, 1.4307f, "M10"},
    {"MT3", 23.825f, 19.761f, 80.962f, 1.4377f, "M12"},
    {"MT4", 31.267f, 25.908f, 103.188f, 1.4876f, "M16"},
    {"MT5", 44.399f, 37.465f, 131.762f, 1.5073f, "M16"},
    {"MT6", 63.348f, 53.746f, 184.150f, 1.4933f, "M20"},
    {"MT7", 83.058f, 69.850f, 254.0f, 1.4894f, "M24"}};
static const int morseTapersCount = sizeof(morseTapers) / sizeof(morseTapers[0]);
static int currentIndex = 0;

struct TaperField
{
    std::string label;
    int row;        // 0..4
    int decimals;   // 0 = stringa, >0 = float con decimali
};
static const TaperField fields[] = {
    {"D1", 0, 3},
    {"D2", 1, 3},
    {"L", 2, 3},
    {"Angle", 3, 4},
    {"MTB", 4, 0}};
static const int totalFields = sizeof(fields) / sizeof(fields[0]);

// Posizioni griglia
static const int labelColX = 325;   // centro label
static const int valueColX = 415;   // centro valore
static const int startY = 80;
static const int rowHeight = 32;
static const int boxW = 90;
static const int boxH = 24;

// Helper
static void drawLabelBox(const std::string &label, int x, int y, int w, int h)
{
    resetTextStyle();
    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_fill_rect(x, y, w, h, TFT_BLACK);
    tft_draw_rect(x, y, w, h, TFT_YELLOW);
    drawCenteredTextAt(label.c_str(), x + w/2, y + h/2, 1, TFT_YELLOW);
}

static void drawValueBox(float value, int x, int y, int w, int h, int decimals)
{
    resetTextStyle();
    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_fill_rect(x, y, w, h, TFT_BLACK);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    drawCenteredTextAt(buf, x + w/2, y + h/2, 1, TFT_CYAN);
}

static void drawTextValueBox(const std::string &text, int x, int y, int w, int h)
{
    resetTextStyle();
    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_fill_rect(x, y, w, h, TFT_BLACK);
    drawCenteredTextAt(text.c_str(), x + w/2, y + h/2, 1, TFT_CYAN);
}

static void drawSizeBox()
{
    int x = 90, y = 45, w = 140, h = 24;
    tft_fill_round_rect(x, y, w, h, 8, TFT_BLACK);
    tft_draw_round_rect(x, y, w, h, 8, TFT_GREEN);
    resetTextStyle();
    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    drawCenteredTextAt(morseTapers[currentIndex].size.c_str(), x + w/2, y + h/2, 1, TFT_GREEN);
}

static void drawTaperMorseImage()
{
    int x = 55, y = 80, w = 215, h = 110;
    tft_fill_round_rect(x, y, w, h, 8, TFT_WHITE);
    // Usa le immagini precaricate da image_manager
    draw_tabTaperMorseImage(64, 80, 0, 0, 0); // MorseTaper
    draw_tabTaperMorseImage(120, 200, 0, 0, 1); // MT_A_vs_B
}

static void updateAllFields()
{
    const MorseTaper &taper = morseTapers[currentIndex];
    drawSizeBox();

    for (int i = 0; i < totalFields; ++i)
    {
        int row = fields[i].row;
        int cy = startY + row * rowHeight;
        int yBox = cy - boxH/2;
        int labelX = labelColX - boxW/2;
        int valueX = valueColX - boxW/2;

        drawLabelBox(fields[i].label, labelX, yBox, boxW, boxH);

        if (fields[i].decimals == 0)
        {
            std::string text;
            if (i == 4) text = taper.isoThread;
            else text = "";
            drawTextValueBox(text, valueX, yBox, boxW, boxH);
        }
        else
        {
            float val = 0;
            switch (i)
            {
            case 0: val = taper.D1; break;
            case 1: val = taper.D2; break;
            case 2: val = taper.L; break;
            case 3: val = taper.angle; break;
            default: break;
            }
            drawValueBox(val, valueX, yBox, boxW, boxH, fields[i].decimals);
        }
    }
}

static void drawStaticBackground()
{
    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("MORSE TAPER DIMENSIONS", 240, 15, 2, TFT_YELLOW);
    tft_draw_line(20, 30, 460, 30, TFT_WHITE);
    tft_draw_line(20, 258, 460, 258, TFT_WHITE);

    drawTaperMorseImage();
    drawSizeBox();
    for (int i = 0; i < totalFields; ++i)
        updateAllFields(); // in realtà updateAllFields ridisegna tutto, ma va bene

    // Pulsanti (stile thread_iso)
    const int btnW = 75, btnH = 30, btnY = 270, btnSpacing = 12;
    const int totalWidth = btnW * 3 + btnSpacing * 2;
    const int startX = (480 - totalWidth) / 2;
    resetTextStyle();
    drawButton(startX, btnY, btnW, btnH, TFT_BLACK, '1', 2, TFT_WHITE, "PREV", 1, TFT_WHITE);
    drawButton(startX + btnW + btnSpacing, btnY, btnW, btnH, TFT_BLACK, '2', 2, TFT_WHITE, "NEXT", 1, TFT_WHITE);
    drawButton(startX + 2 * (btnW + btnSpacing), btnY, btnW, btnH, TFT_RED, 'D', 2, TFT_WHITE, "EXIT", 1, TFT_WHITE);
}

void taper_morse_scene_enter(void)
{
    preloadTabTaperMorseImages();
    currentIndex = 0;
    drawStaticBackground();
    updateAllFields();

    while (true)
    {
        char key = readKeypad();
        if (key == 'D')
        {
            resetTextStyle();
            // DisplayManager();
            return;
        }
        if (key == '1')
        {
            currentIndex = (currentIndex - 1 + morseTapersCount) % morseTapersCount;
            updateAllFields();
        }
        if (key == '2')
        {
            currentIndex = (currentIndex + 1) % morseTapersCount;
            updateAllFields();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}