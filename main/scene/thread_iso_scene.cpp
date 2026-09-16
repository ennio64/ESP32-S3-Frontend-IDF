// thread_iso_scene.cpp
#include "thread_iso_scene.h"
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
#include <vector>

// ==================================================================
//  TABELLA DEI PASSI NORMALIZZATI (ISO 724 + ISO 262) – FINO A M100
// ==================================================================
const ThreadPitchInfo threadStandardPitches[] = {
    {1.0, 0.25, 0.20, 0, 0},
    {1.1, 0.25, 0.20, 0, 0},
    {1.2, 0.25, 0.20, 0, 0},
    {1.4, 0.30, 0.20, 0, 0},
    {1.6, 0.35, 0.20, 0, 0},
    {1.8, 0.35, 0.20, 0, 0},
    {2.0, 0.40, 0.25, 0, 0},
    {2.2, 0.45, 0.25, 0, 0},
    {2.5, 0.45, 0.35, 0, 0},
    {3.0, 0.50, 0.35, 0, 0},
    {3.5, 0.60, 0.35, 0, 0},
    {4.0, 0.70, 0.50, 0, 0},
    {5.0, 0.80, 0.50, 0, 0},
    {6.0, 1.00, 0.75, 0, 0},
    {7.0, 1.00, 0.75, 0, 0},
    {8.0, 1.25, 1.00, 0.75, 0},
    {10.0, 1.50, 1.25, 1.00, 0.75},
    {12.0, 1.75, 1.50, 1.25, 1.00},
    {14.0, 2.00, 1.50, 1.25, 1.00},
    {16.0, 2.00, 1.50, 1.00, 0},
    {18.0, 2.50, 2.00, 1.50, 1.00},
    {20.0, 2.50, 2.00, 1.50, 1.00},
    {22.0, 2.50, 2.00, 1.50, 1.00},
    {24.0, 3.00, 2.00, 1.50, 1.00},
    {27.0, 3.00, 2.00, 1.50, 1.00},
    {30.0, 3.50, 3.00, 2.00, 1.50},
    {33.0, 3.50, 3.00, 2.00, 1.50},
    {36.0, 4.00, 3.00, 2.00, 1.50},
    {39.0, 4.00, 3.00, 2.00, 1.50},
    {42.0, 4.50, 4.00, 3.00, 2.00},
    {45.0, 4.50, 4.00, 3.00, 2.00},
    {48.0, 5.00, 4.00, 3.00, 2.00},
    {52.0, 5.00, 4.00, 3.00, 2.00},
    {56.0, 5.50, 4.00, 3.00, 2.00},
    {60.0, 5.50, 4.00, 3.00, 2.00},
    {64.0, 6.00, 4.00, 3.00, 2.00},
    {68.0, 6.00, 4.00, 3.00, 2.00},
    {72.0, 6.00, 4.00, 3.00, 2.00},
    {76.0, 6.00, 4.00, 3.00, 2.00},
    {80.0, 6.00, 4.00, 3.00, 2.00},
    {85.0, 6.00, 4.00, 3.00, 2.00},
    {90.0, 6.00, 4.00, 3.00, 2.00},
    {95.0, 6.00, 4.00, 3.00, 2.00},
    {100.0, 6.00, 4.00, 3.00, 2.00}};
const int threadStandardPitchesCount = sizeof(threadStandardPitches) / sizeof(threadStandardPitches[0]);

// ==================================================================
//  SCOSTAMENTI (OFFSET) PER VITE (ESTERNA) e DADO (INTERNA)
// ==================================================================
struct OffsetExternal
{
    float pitch;
    float off_dmax, off_dmin, off_d2max, off_d2min, off_d1max;
    float h;
    int nap;
};
static const OffsetExternal externalOffsets[] = {
    {0.20, -0.018, -0.085, -0.180, -0.233, -0.289, 0.153, 3},
    {0.25, -0.018, -0.085, -0.180, -0.233, -0.289, 0.153, 3},
    {0.30, -0.018, -0.093, -0.213, -0.269, -0.343, 0.184, 3},
    {0.35, -0.019, -0.104, -0.246, -0.309, -0.398, 0.215, 3},
    {0.40, -0.019, -0.114, -0.279, -0.346, -0.452, 0.245, 3},
    {0.45, -0.020, -0.120, -0.312, -0.383, -0.507, 0.276, 3},
    {0.50, -0.020, -0.126, -0.345, -0.420, -0.561, 0.307, 4},
    {0.60, -0.021, -0.146, -0.411, -0.496, -0.671, 0.368, 4},
    {0.70, -0.022, -0.162, -0.477, -0.567, -0.780, 0.429, 4},
    {0.75, -0.022, -0.152, -0.488, -0.565, -0.833, 0.460, 4},
    {0.80, -0.024, -0.174, -0.544, -0.639, -0.890, 0.491, 4},
    {1.00, -0.026, -0.206, -0.676, -0.788, -1.109, 0.613, 5},
    {1.25, -0.028, -0.240, -0.840, -0.958, -1.381, 0.767, 6},
    {1.50, -0.032, -0.268, -1.006, -1.138, -1.656, 0.920, 6},
    {1.75, -0.034, -0.299, -1.171, -1.321, -1.928, 1.074, 6},
    {2.00, -0.038, -0.318, -1.337, -1.497, -2.203, 1.227, 7},
    {2.50, -0.042, -0.377, -1.666, -1.836, -2.748, 1.534, 8},
    {3.00, -0.048, -0.423, -1.997, -2.197, -3.296, 1.840, 9},
    {3.50, -0.053, -0.478, -2.326, -2.538, -3.842, 2.147, 10},
    {4.00, -0.060, -0.535, -2.658, -2.882, -4.390, 2.461, 11},
    {4.50, -0.063, -0.563, -2.986, -3.222, -4.934, 2.768, 12},
    {5.00, -0.071, -0.601, -3.319, -3.569, -5.484, 3.076, 13},
    {5.50, -0.075, -0.635, -3.647, -3.912, -6.029, 3.384, 14},
    {6.00, -0.080, -0.650, -3.975, -4.250, -6.500, 3.700, 15}};
struct OffsetInternal
{
    float pitch;
    float off_D2min, off_D2max, off_D1min, off_D1max;
    float H;
    int nap;
};
static const OffsetInternal internalOffsets[] = {
    {0.20, -0.162, -0.106, -0.271, -0.215, 0.135, 3},
    {0.25, -0.162, -0.106, -0.271, -0.215, 0.135, 3},
    {0.30, -0.195, -0.120, -0.325, -0.240, 0.162, 3},
    {0.35, -0.227, -0.142, -0.379, -0.279, 0.189, 3},
    {0.40, -0.260, -0.170, -0.433, -0.321, 0.216, 3},
    {0.45, -0.292, -0.197, -0.487, -0.362, 0.244, 3},
    {0.50, -0.325, -0.225, -0.541, -0.401, 0.271, 4},
    {0.60, -0.390, -0.278, -0.650, -0.490, 0.325, 4},
    {0.70, -0.455, -0.337, -0.758, -0.578, 0.379, 4},
    {0.75, -0.480, -0.356, -0.800, -0.610, 0.406, 4},
    {0.80, -0.520, -0.395, -0.866, -0.666, 0.433, 4},
    {1.00, -0.650, -0.500, -1.083, -0.847, 0.541, 5},
    {1.25, -0.812, -0.652, -1.353, -1.088, 0.677, 6},
    {1.50, -0.974, -0.794, -1.624, -1.324, 0.812, 6},
    {1.75, -1.137, -0.937, -1.894, -1.559, 0.947, 6},
    {2.00, -1.299, -1.087, -2.165, -1.790, 1.083, 7},
    {2.50, -1.624, -1.400, -2.706, -2.256, 1.353, 8},
    {3.00, -1.949, -1.684, -3.248, -2.748, 1.624, 9},
    {3.50, -2.273, -1.993, -3.789, -3.229, 1.894, 10},
    {4.00, -2.598, -2.298, -4.330, -3.730, 2.165, 11},
    {4.50, -2.923, -2.608, -4.871, -4.201, 2.436, 12},
    {5.00, -3.248, -2.913, -5.413, -4.703, 2.706, 13},
    {5.50, -3.573, -3.217, -5.954, -5.204, 2.977, 14},
    {6.00, -3.898, -3.522, -6.495, -5.704, 3.248, 15}};

static const OffsetExternal *findExternalOffset(float pitch)
{
    for (size_t i = 0; i < sizeof(externalOffsets) / sizeof(externalOffsets[0]); ++i)
        if (fabs(externalOffsets[i].pitch - pitch) < 0.001f)
            return &externalOffsets[i];
    return nullptr;
}
static const OffsetInternal *findInternalOffset(float pitch)
{
    for (size_t i = 0; i < sizeof(internalOffsets) / sizeof(internalOffsets[0]); ++i)
        if (fabs(internalOffsets[i].pitch - pitch) < 0.001f)
            return &internalOffsets[i];
    return nullptr;
}

// ==================================================================
//  FUNZIONI PUBBLICHE PARAMETRICHE (esportate)
// ==================================================================
ThreadStandardType checkStandardThread(float dn, float pitch, bool isExternal)
{
    (void)isExternal;
    for (int i = 0; i < threadStandardPitchesCount; ++i)
    {
        if (fabs(threadStandardPitches[i].diameter - dn) < 0.05f)
        {
            const auto &info = threadStandardPitches[i];
            if (fabs(info.coarse_pitch - pitch) < 0.02f)
                return THREAD_STANDARD_COARSE;
            if (fabs(info.fine_pitch1 - pitch) < 0.02f)
                return THREAD_STANDARD_FINE;
            if (info.fine_pitch2 > 0 && fabs(info.fine_pitch2 - pitch) < 0.02f)
                return THREAD_STANDARD_FINE;
            if (info.fine_pitch3 > 0 && fabs(info.fine_pitch3 - pitch) < 0.02f)
                return THREAD_STANDARD_FINE;
            break;
        }
    }
    return THREAD_NON_STANDARD;
}

bool getExternalLimits(float dn, float pitch,
                       float &dmax, float &dmin,
                       float &d2max, float &d2min,
                       float &d1max,
                       float &h, int &nap)
{
    auto off = findExternalOffset(pitch);
    if (!off)
        return false;
    dmax = dn + off->off_dmax;
    dmin = dn + off->off_dmin;
    d2max = dn + off->off_d2max;
    d2min = dn + off->off_d2min;
    d1max = dn + off->off_d1max;
    h = off->h;
    nap = off->nap;
    return true;
}

bool getInternalLimits(float dn, float pitch,
                       float &D, float &D2min, float &D2max,
                       float &D1min, float &D1max,
                       float &H, int &nap)
{
    auto off = findInternalOffset(pitch);
    if (!off)
        return false;
    D = dn;
    D2min = dn + off->off_D2min;
    D2max = dn + off->off_D2max;
    D1min = dn + off->off_D1min;
    D1max = dn + off->off_D1max;
    H = off->H;
    nap = off->nap;
    return true;
}

// ==================================================================
//  FUNZIONI DI COMPATIBILITÀ (per threading_scene)
// ==================================================================
static std::vector<std::pair<float, float>> allRows;
int getThreadMaxIndexExternal(void)
{
    if (allRows.empty())
    {
        for (int i = 0; i < threadStandardPitchesCount; ++i)
        {
            const auto &info = threadStandardPitches[i];
            allRows.push_back({info.diameter, info.coarse_pitch});
            if (info.fine_pitch1 > 0)
                allRows.push_back({info.diameter, info.fine_pitch1});
            if (info.fine_pitch2 > 0)
                allRows.push_back({info.diameter, info.fine_pitch2});
            if (info.fine_pitch3 > 0)
                allRows.push_back({info.diameter, info.fine_pitch3});
        }
    }
    return allRows.size();
}
float getThreadPitchExternal(int index)
{
    getThreadMaxIndexExternal();
    if (index < 0 || index >= (int)allRows.size())
        return 0.0f;
    return allRows[index].second;
}
const char *getThreadDesignationExternal(int index)
{
    getThreadMaxIndexExternal();
    if (index < 0 || index >= (int)allRows.size())
        return "";
    static char buf[32];
    snprintf(buf, sizeof(buf), "M%.1f", allRows[index].first);
    return buf;
}
int getThreadMaxIndexInternal(void) { return getThreadMaxIndexExternal(); }
float getThreadPitchInternal(int index) { return getThreadPitchExternal(index); }
const char *getThreadDesignationInternal(int index) { return getThreadDesignationExternal(index); }

// ==================================================================
//  SCENA TABELLA (stile originale Arduino – label gialle, valori ciano)
// ==================================================================
static std::vector<std::pair<float, float>> displayRows;
static int currentIndex = 0;
static bool coarseOnly = true; // true = solo passi grossi, false = solo passi fini
static bool isInternalMode = false;

static void drawLabelBox(const char *label, int x, int y, int w, int h)
{
    resetTextStyle();
    tft_set_font(&fonts::Font2);
    tft_set_text_size(2);
    tft_fill_rect(x, y, w, h, TFT_BLACK);
    tft_draw_rect(x, y, w, h, TFT_YELLOW);
    drawCenteredTextAt(label, x + w / 2, y + h / 2, 1, TFT_YELLOW);
}

static void drawValueBox(float value, int x, int y, int w, int h, int decimals)
{
    resetTextStyle();
    tft_set_font(&fonts::Font2);
    tft_set_text_size(2);
    tft_fill_rect(x, y, w, h, TFT_BLACK);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    drawCenteredTextAt(buf, x + w / 2, y + h / 2, 1, TFT_CYAN);
}

static void rebuildDisplayRows()
{
    resetTextStyle();
    displayRows.clear();
    for (int i = 0; i < threadStandardPitchesCount; ++i)
    {
        const auto &info = threadStandardPitches[i];
        if (coarseOnly)
        {
            displayRows.push_back({info.diameter, info.coarse_pitch});
        }
        else
        {
            if (info.fine_pitch1 > 0)
                displayRows.push_back({info.diameter, info.fine_pitch1});
            if (info.fine_pitch2 > 0)
                displayRows.push_back({info.diameter, info.fine_pitch2});
            if (info.fine_pitch3 > 0)
                displayRows.push_back({info.diameter, info.fine_pitch3});
        }
    }
    if (currentIndex >= (int)displayRows.size())
        currentIndex = displayRows.size() - 1;
    if (currentIndex < 0)
        currentIndex = 0;
}

static void updateCurrentRow()
{
    if (displayRows.empty())
        return;
    float dn = displayRows[currentIndex].first;
    float pitch = displayRows[currentIndex].second;

    // 1) Aggiorna M x P (in alto, sopra l'immagine)
    char buf[32];
    snprintf(buf, sizeof(buf), "M%.1f x %.2f", dn, pitch);
    int mx = 90, my = 45, mw = 140, mh = 24;
    tft_fill_round_rect(mx, my, mw, mh, 8, TFT_BLACK);
    tft_draw_round_rect(mx, my, mw, mh, 8, TFT_GREEN);
    resetTextStyle();
    tft_set_font(&fonts::Font2);
    tft_set_text_size(2);
    drawCenteredTextAt(buf, mx + mw / 2, my + mh / 2, 1, TFT_GREEN);

    // 2) Aggiorna i 7 campi (label + valore) – posizionati a destra dell'immagine
    for (int f = 1; f <= 7; ++f)
    {
        float value = 0;
        int decimals = 3;
        int nap = 0;
        std::string label;
        if (!isInternalMode)
        {
            float dmax, dmin, d2max, d2min, d1max, h;
            if (getExternalLimits(dn, pitch, dmax, dmin, d2max, d2min, d1max, h, nap))
            {
                switch (f)
                {
                case 1:
                    value = dmin;
                    label = "d min";
                    break;
                case 2:
                    value = dmax;
                    label = "d max";
                    break;
                case 3:
                    value = d2min;
                    label = "d2 min";
                    break;
                case 4:
                    value = d2max;
                    label = "d2 max";
                    break;
                case 5:
                    value = d1max;
                    label = "d1 max";
                    break;
                case 6:
                    value = h;
                    label = "H";
                    break;
                case 7:
                    value = nap;
                    label = "nap";
                    decimals = 0;
                    break;
                }
            }
        }
        else
        {
            float D, D2min, D2max, D1min, D1max, H;
            if (getInternalLimits(dn, pitch, D, D2min, D2max, D1min, D1max, H, nap))
            {
                switch (f)
                {
                case 1:
                    value = D;
                    label = "D";
                    break;
                case 2:
                    value = D2min;
                    label = "D2 min";
                    break;
                case 3:
                    value = D2max;
                    label = "D2 max";
                    break;
                case 4:
                    value = D1min;
                    label = "D1 min";
                    break;
                case 5:
                    value = D1max;
                    label = "D1 max";
                    break;
                case 6:
                    value = H;
                    label = "H";
                    break;
                case 7:
                    value = nap;
                    label = "nap";
                    decimals = 0;
                    break;
                }
            }
        }
        int row = f - 1;
        int cxLabel = 325; // centro label (spostato a destra dell'immagine)
        int cxValue = 415; // centro valore
        int cy = 55 + row * 30;
        int wLabel = 90, hLabel = 24;
        int wValue = 90, hValue = 24;
        int xLabel = cxLabel - wLabel / 2;
        int xValue = cxValue - wValue / 2;
        int y = cy - hLabel / 2;
        drawLabelBox(label.c_str(), xLabel, y, wLabel, hLabel);
        drawValueBox(value, xValue, y, wValue, hValue, decimals);
    }
}

static void drawStaticBackground()
{
    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    std::string tipo = isInternalMode ? "INTERNAL" : "EXTERNAL";
    drawCenteredTextAt(("THREAD ISO METRIC " + tipo).c_str(), 240, 15, 2, TFT_YELLOW);
    tft_fill_rect(20, 30, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 258, 440, 1, TFT_WHITE);

    int circleX = 30, circleY = 60, radius = 12;
    tft_fill_circle(circleX, circleY, radius, TFT_YELLOW);
    drawCenteredTextAt(isInternalMode ? "6H" : "6g", circleX, circleY, 1, TFT_BLACK);
    tft_set_text_color(TFT_YELLOW);
    drawCenteredTextAt(isInternalMode ? "ISO 965-2" : "ISO 965-1", 55, 250, 1, TFT_YELLOW);

    // Immagine
    int xImg = 60, yImg = 80, wImg = 200, hImg = 140;
    tft_fill_round_rect(xImg, yImg, wImg, hImg, 8, TFT_WHITE);
    if (isInternalMode)
    {
        draw_tabThreadImage(98, 80, 0, 0, 0);
    }
    else
    {
        draw_tabThreadImage(64, 80, 0, 0, 1);
    }

    // Pulsanti
    resetTextStyle();
    const int btnW = 75, btnH = 30, btnY = 270, btnSpacing = 12;
    const int totalWidth = btnW * 5 + btnSpacing * 4;
    const int startX = (480 - totalWidth) / 2;
    drawButton(startX, btnY, btnW, btnH, TFT_BLACK, '1', 2, TFT_WHITE, "PREV", 1, TFT_WHITE);
    drawButton(startX + btnW + btnSpacing, btnY, btnW, btnH, TFT_BLACK, '2', 2, TFT_WHITE, "NEXT", 1, TFT_WHITE);
    const char *filterText = coarseOnly ? "COARSE" : "FINE";
    drawButton(startX + 2 * (btnW + btnSpacing), btnY, btnW, btnH, TFT_BLACK, 'A', 2, TFT_WHITE, filterText, 1, TFT_WHITE);
    const char *modeText = isInternalMode ? "INTERNAL" : "EXTERNAL";
    drawButton(startX + 3 * (btnW + btnSpacing), btnY, btnW, btnH, TFT_BLACK, 'B', 2, TFT_WHITE, modeText, 1, TFT_WHITE);
    drawButton(startX + 4 * (btnW + btnSpacing), btnY, btnW, btnH, TFT_RED, 'D', 2, TFT_WHITE, "EXIT", 1, TFT_WHITE);
}

void thread_iso_scene_enter(void)
{
    preloadTabThreadImages();
    getThreadMaxIndexExternal(); // popola allRows (non usato direttamente ma serve per compatibilità)
    coarseOnly = true;
    isInternalMode = false;
    rebuildDisplayRows();
    currentIndex = 0;
    drawStaticBackground();
    updateCurrentRow();

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
            if (currentIndex > 0)
            {
                currentIndex--;
                updateCurrentRow();
            }
        }
        if (key == '2')
        {
            if (currentIndex < (int)displayRows.size() - 1)
            {
                currentIndex++;
                updateCurrentRow();
            }
        }
        if (key == 'A')
        {
            coarseOnly = !coarseOnly;
            rebuildDisplayRows();
            currentIndex = 0;
            updateCurrentRow();
            // Pulisci l'area del pulsante A
            resetTextStyle();
            const int btnW = 75, btnH = 30, btnY = 270, btnSpacing = 12;
            const int totalWidth = btnW * 5 + btnSpacing * 4;
            const int startX = (480 - totalWidth) / 2;
            int btnAX = startX + 2 * (btnW + btnSpacing);
            tft_fill_rect(btnAX, btnY, btnW, btnH + 12, TFT_NAVY); // cancella con sfondo navy
            const char *filterText = coarseOnly ? "COARSE" : "FINE";
            drawButton(btnAX, btnY, btnW, btnH, TFT_BLACK, 'A', 2, TFT_WHITE, filterText, 1, TFT_WHITE);
        }
        if (key == 'B')
        {
            isInternalMode = !isInternalMode;
            drawStaticBackground(); // ridisegna titolo, cerchio, immagine, pulsanti
            updateCurrentRow();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}