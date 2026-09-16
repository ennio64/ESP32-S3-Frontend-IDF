// threading_scene.cpp
#include "threading_scene.h"
#include "scene_utils.h"
#include "thread_iso_scene.h"
#include "msgbox.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_vars.h"
#include "global_settings.h"
#include "graphics_lgfx.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "keypad.h"
#include "cnc_manager.h"
#include "grbl_commands.h"
#include "colors.h"
#include "main_scene.h"
#include "GenerateGcode.h"
#include <cmath>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>

static const char *TAG = "THREADING";

static unsigned long millis_idf()
{
    return (unsigned long)(esp_timer_get_time() / 1000);
}

static inline float to_display()
{
    return (settings_get_xmode() == 0) ? 2.0f : 1.0f;
}
static inline float from_input_to_raggio()
{
    return (settings_get_xmode() == 0) ? 0.5f : 1.0f;
}

// Parametri statici
static int threadingSelectedField = 0;
static const int threadingTotalFields = 18;

static float threadingStartZ = 0.0f;
static float threadingActualRadius = 0.0f;
static bool threadingIsExternal = true;
static bool threadingRightHand = true;
static float threadingPitch = 0.0f;
static float threadingLength = 0.0f;
static float threadingThreadDepth = 0.0f;
static float threadingFirstCut = 0.0f;
static float threadingOffset = 0.0f;
static int threadingSpringPasses = 1;
static int threadingPasses = 0;
static float threadingR = 1.0f;
static int threadingL = 0;
static float threadingE = 0.0f;
static float threadingSpindleRPM = 0.0f;
static bool threadingSpindleCW = true;

static bool needsRedraw = false;

static const char *fieldLabels[] = {
    "Z Current", "X Current",
    "EXT", "INT", "RH", "LH",
    "Pitch (P)", "Length(Z)", "Depth (K)", "FirstCut(J)", "Offset (I)",
    "SpringPass(H)", "Taper (L)", "Length(E)",
    "1 < R < 2", "RPM", "Direction",
    "Estim. Passes"};

// Forward declaration
static void calcThreadingPasses(float K, float J, float R);
static void refreshParametersFromPitchAndDRO();

static inline float get_current_depth_coeff() {
    if (threadingIsExternal)
        return settings_get_ext_depth_coeff();
    else
        return settings_get_int_depth_coeff();
}

static float findNominalDiameterForPitch(float pitch, float referenceDiam) {
    extern const ThreadPitchInfo threadStandardPitches[];
    extern const int threadStandardPitchesCount;
    float bestDiam = 0.0f;
    float bestDiff = 100.0f;
    for (int i = 0; i < threadStandardPitchesCount; ++i) {
        const auto &info = threadStandardPitches[i];
        if (fabs(info.coarse_pitch - pitch) < 0.02f ||
            fabs(info.fine_pitch1 - pitch) < 0.02f ||
            (info.fine_pitch2 > 0 && fabs(info.fine_pitch2 - pitch) < 0.02f) ||
            (info.fine_pitch3 > 0 && fabs(info.fine_pitch3 - pitch) < 0.02f))
        {
            float diff = fabs(info.diameter - referenceDiam);
            if (diff < bestDiff) {
                bestDiff = diff;
                bestDiam = info.diameter;
            }
        }
    }
    if (bestDiam == 0.0f) bestDiam = roundf(referenceDiam);
    return bestDiam;
}

static void refreshParametersFromPitchAndDRO() {
    if (threadingPitch <= 0.0f) return;
    float startRad = threadingActualRadius;
    if (startRad <= 0.0f) return;

    float dn;
    if (threadingIsExternal) {
        float ref = startRad * 2.0f;
        dn = findNominalDiameterForPitch(threadingPitch, ref);
    } else {
        float ref = startRad * 2.0f + threadingPitch;
        dn = findNominalDiameterForPitch(threadingPitch, ref);
    }

    float coeff = get_current_depth_coeff();
    threadingThreadDepth = 0.6134f * threadingPitch * coeff;
    threadingFirstCut = threadingThreadDepth * 0.1f;

    if (threadingIsExternal) {
        float dmax, dmin, d2max, d2min, d1max, h;
        int nap;
        if (getExternalLimits(dn, threadingPitch, dmax, dmin, d2max, d2min, d1max, h, nap)) {
            float currentDiam = startRad * 2.0f;
            float desiredStart;
            if (currentDiam > dmax) {
                desiredStart = dmax - 0.1f;
            } else if (currentDiam < dmin) {
                desiredStart = currentDiam;
            } else {
                desiredStart = currentDiam - 0.075f;
            }
            float delta = (desiredStart - currentDiam) / 2.0f;
            threadingOffset = delta;
            if (threadingOffset > 0) threadingOffset = -threadingOffset;
        } else {
            float nominalD = roundf(startRad * 2.0f);
            float dMax = nominalD - 0.15f;
            float delta = (dMax - startRad * 2.0f) / 2.0f;
            threadingOffset = delta;
            if (threadingOffset > 0) threadingOffset = -threadingOffset;
        }
    } else {
        float D, D2min, D2max, D1min, D1max, H;
        int nap;
        if (getInternalLimits(dn, threadingPitch, D, D2min, D2max, D1min, D1max, H, nap)) {
            float currentHole = startRad * 2.0f;
            float idealHole = dn - threadingPitch;
            float neededOffset = (idealHole - currentHole) / 2.0f;
            float minOffset = 0.02f, maxOffset = 0.2f;
            if (neededOffset < minOffset) neededOffset = minOffset;
            if (neededOffset > maxOffset) neededOffset = maxOffset;
            threadingOffset = neededOffset;
            if (threadingOffset < 0) threadingOffset = -threadingOffset;
        } else {
            float nominalD = roundf(startRad * 2.0f);
            float dMin = nominalD + 0.15f;
            float delta = (dMin - startRad * 2.0f) / 2.0f;
            threadingOffset = delta;
            if (threadingOffset < 0) threadingOffset = -threadingOffset;
        }
    }
    calcThreadingPasses(threadingThreadDepth, threadingFirstCut, threadingR);
    needsRedraw = true;
}

static void drawThreadingStaticElements()
{
    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("THREADING (G76 Cycle)", 240, 15, 2, TFT_YELLOW);
    tft_fill_rect(20, 30, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 72, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 144, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 216, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 258, 440, 1, TFT_WHITE);
    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_text_datum(TL_DATUM);
    tft_set_text_color(TFT_YELLOW);
    tft_set_cursor(20, 155);
    tft_print("Advanced Settings  (linuxCNC)");
    drawMachiningButtons();
}

static std::string getThreadingConfirmationMessage()
{
    float startX = threadingActualRadius + threadingOffset;
    float minorDiameter, majorDiameter;
    if (threadingIsExternal) {
        majorDiameter = 2.0f * startX;
        minorDiameter = 2.0f * (startX - threadingThreadDepth);
    } else {
        majorDiameter = 2.0f * (startX + threadingThreadDepth);
        minorDiameter = 2.0f * startX;
    }

    float pitch = threadingPitch;
    float reference = threadingIsExternal ? threadingActualRadius * 2.0f : threadingActualRadius * 2.0f + pitch;
    float dn = findNominalDiameterForPitch(pitch, reference);
    ThreadStandardType stdType = checkStandardThread(dn, pitch, threadingIsExternal);
    char msg[512];
    std::string warnFeasibility;
    if (threadingIsExternal) {
        float idealStartDiam = dn;
        float diff = majorDiameter - idealStartDiam;
        if (diff > 0.2f) warnFeasibility = "\n[WARN] Excessive excess stock (diameter too large)";
        else if (diff < -0.2f) warnFeasibility = "\n[WARN] Starting diameter too small (incomplete thread risk)";
    } else {
        float idealHoleDiam = dn - pitch;
        float diff = (threadingActualRadius * 2.0f) - idealHoleDiam;
        if (diff > 0.1f) warnFeasibility = "\n[WARN] Hole too large (shallow thread)";
        else if (diff < -0.1f) warnFeasibility = "\n[WARN] Hole too small (tool breakage risk)";
    }

    if (stdType == THREAD_STANDARD_COARSE || stdType == THREAD_STANDARD_FINE) {
        bool isFine = (stdType == THREAD_STANDARD_FINE);
        const char *threadType = isFine ? "fine pitch" : "coarse pitch";
        const char *extInt = threadingIsExternal ? "external (EXT)" : "internal (INT)";
        const char *hand = threadingRightHand ? "right-hand (RH)" : "left-hand (LH)";
        char des[64];
        snprintf(des, sizeof(des), "M%.1f x %.3f", dn, pitch);
        if (threadingIsExternal) {
            float dmax, dmin, d2max, d2min, d1max, h;
            int nap;
            if (getExternalLimits(dn, pitch, dmax, dmin, d2max, d2min, d1max, h, nap)) {
                std::string warnMajor, warnMinor;
                if (majorDiameter < dmin - 0.001f) warnMajor = " [WARN] (below min)";
                else if (majorDiameter > dmax + 0.001f) warnMajor = " [WARN] (above max)";
                if (minorDiameter > d1max + 0.001f) warnMinor = " [WARN] (above max)";
                else if (minorDiameter < d1max - 0.2f) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), " [WARN] (d1 max = %.3f mm)", d1max);
                    warnMinor = buf;
                }
                snprintf(msg, sizeof(msg),
                         "Metric %s thread (UNI)\n%s %s\nDesignation: %s\n\n"
                         "Nominal diameter (d) = %.3f mm%s\n"
                         "Minor diameter (d1) = %.3f mm%s\n"
                         "Length = %.3f mm%s",
                         threadType, extInt, hand, des,
                         majorDiameter, warnMajor.c_str(),
                         minorDiameter, warnMinor.c_str(),
                         threadingLength, warnFeasibility.c_str());
            } else {
                snprintf(msg, sizeof(msg),
                         "Metric %s thread (UNI)\n%s %s\nDesignation: %s\n\n"
                         "Nominal diameter (d) = %.3f mm\n"
                         "Minor diameter (d1) = %.3f mm\n"
                         "Length = %.3f mm%s",
                         threadType, extInt, hand, des,
                         majorDiameter, minorDiameter,
                         threadingLength, warnFeasibility.c_str());
            }
        } else {
            float D, D2min, D2max, D1min, D1max, H;
            int nap;
            if (getInternalLimits(dn, pitch, D, D2min, D2max, D1min, D1max, H, nap)) {
                std::string warnMajor, warnMinor;
                if (minorDiameter < D1min - 0.001f) warnMinor = " [WARN] (below min)";
                else if (minorDiameter > D1max + 0.001f) warnMinor = " [WARN] (above max)";
                if (majorDiameter < dn - 0.001f) warnMajor = " [WARN] (below nominal)";
                snprintf(msg, sizeof(msg),
                         "Metric %s thread (UNI)\n%s %s\nDesignation: %s\n\n"
                         "Nominal diameter (D) = %.3f mm%s\n"
                         "Minor diameter (D1) = %.3f mm%s\n"
                         "Length = %.3f mm%s",
                         threadType, extInt, hand, des,
                         majorDiameter, warnMajor.c_str(),
                         minorDiameter, warnMinor.c_str(),
                         threadingLength, warnFeasibility.c_str());
            } else {
                snprintf(msg, sizeof(msg),
                         "Metric %s thread (UNI)\n%s %s\nDesignation: %s\n\n"
                         "Nominal diameter (D) = %.3f mm\n"
                         "Minor diameter (D1) = %.3f mm\n"
                         "Length = %.3f mm%s",
                         threadType, extInt, hand, des,
                         majorDiameter, minorDiameter,
                         threadingLength, warnFeasibility.c_str());
            }
        }
    } else {
        const char *extInt = threadingIsExternal ? "external (EXT)" : "internal (INT)";
        const char *hand = threadingRightHand ? "right-hand (RH)" : "left-hand (LH)";
        snprintf(msg, sizeof(msg),
                 "Non-standard thread %s %s\n\n"
                 "Nominal diameter = %.3f mm\n"
                 "Minor diameter  = %.3f mm\n"
                 "Pitch = %.3f mm\n"
                 "Depth K = %.3f mm\n"
                 "Offset I = %.3f mm\n"
                 "First cut J = %.3f mm\n"
                 "Length = %.3f mm%s",
                 extInt, hand,
                 majorDiameter, minorDiameter,
                 threadingPitch, threadingThreadDepth,
                 threadingOffset, threadingFirstCut,
                 threadingLength, warnFeasibility.c_str());
    }
    return std::string(msg);
}

static void getThreadingFieldGeometry(int index, int &centerX, int &centerY, int &w, int &h, int &offsetX, int &decimals)
{
    const int colX[] = {128, 278, 425};
    const int rowY[] = {57, 87, 117, 147, 177, 207};
    w = (index == 11 || index == 16) ? 36 : 70;
    h = 24;
    offsetX = 100;
    decimals = 3;
    int yTop = 56, yBottom = 72;
    int xLeft = colX[2] - 40, xRight = colX[2] + 40;
    switch (index) {
    case 2: centerX = xLeft; centerY = yTop; w = 50; offsetX = 40; decimals = 0; return;
    case 3: centerX = xLeft; centerY = yBottom; w = 50; offsetX = 36; decimals = 0; return;
    case 4: centerX = xRight; centerY = yTop; w = 50; offsetX = 40; decimals = 0; return;
    case 5: centerX = xRight; centerY = yBottom; w = 50; offsetX = 39; decimals = 0; return;
    case 0: centerX = colX[0]; centerY = rowY[0] + 7; offsetX = 108; break;
    case 1: centerX = colX[1]; centerY = rowY[0] + 7; break;
    case 6: centerX = colX[0]; centerY = rowY[1] + 19; offsetX = 100; break;
    case 7: centerX = colX[1]; centerY = rowY[1] + 19; break;
    case 8: centerX = colX[2]; centerY = rowY[1] + 19; break;
    case 9: centerX = colX[0]; centerY = rowY[2] + 19; offsetX = 108; break;
    case 10: centerX = colX[1]; centerY = rowY[2] + 19; break;
    case 11: centerX = colX[2] + 15; centerY = rowY[2] + 19; offsetX = 110; decimals = 0; break;
    case 12: centerX = colX[0]; centerY = rowY[4] + 30; offsetX = 108; break;
    case 13: centerX = colX[1]; centerY = rowY[4] + 30; break;
    case 14: centerX = colX[2]; centerY = rowY[4] + 30; decimals = 2; break;
    case 15: centerX = colX[1]; centerY = rowY[5] + 42; offsetX = 80; decimals = 0; break;
    case 16: centerX = colX[2] + 15; centerY = rowY[5] + 42; offsetX = 80; decimals = 0; break;
    case 17: centerX = colX[2]; centerY = rowY[3] + 30; offsetX = 120; decimals = 0; break;
    default: centerX = -999; centerY = -999; break;
    }
}

static int getThreadingFieldState(int index, int selectedIndex)
{
    if (index == 0 || index == 1) return -2;
    if (index == 3 || index == 5) return 0;
    if (index == 13 && threadingL == 0) return -1;
    if (index == 17) return -2;
    if (selectedIndex == index) return 1;
    return 0;
}

static void drawThreadingField(int index, int selectedIndex)
{
    resetTextStyle();
    int cx, cy, w, h, offsetX, decimals;
    getThreadingFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getThreadingFieldState(index, selectedIndex);
    int labelX = cx - offsetX;
    int labelY = cy - tft_font_height() - 4;
    int boxCenterY = cy - h / 2;

    switch (index) {
    case 0:
        drawParamBoxUnified(fieldLabels[0], threadingStartZ, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 1: {
        int boxX = cx - w/2, boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2); tft_set_text_size(1);
        tft_set_cursor(labelX, labelY); tft_print(fieldLabels[1]);
        float displayValue = threadingActualRadius * to_display();
        char numBuf[16]; snprintf(numBuf, sizeof(numBuf), "%.3f", displayValue);
        const char *suffix = (settings_get_xmode() == 0) ? "D" : "R";
        int16_t numWidth = tft_text_width(numBuf);
        int16_t totalWidth = numWidth + 2 + tft_text_width(suffix);
        int16_t startX = cx - totalWidth/2, textY = cy - 20;
        uint16_t numColor = (state == -2) ? TFT_GREEN : ((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_set_text_color(numColor);
        tft_draw_string(numBuf, startX, textY, numColor, 1);
        tft_set_text_color(TFT_ORANGE);
        tft_draw_string(suffix, startX + numWidth + 2, textY, TFT_ORANGE, 1);
        break;
    }
    case 2:
        drawCheckbox(fieldLabels[2], threadingIsExternal, labelX, labelY, selectedIndex == index, state);
        break;
    case 3:
        drawCheckbox(fieldLabels[3], !threadingIsExternal, labelX, labelY, false, state);
        break;
    case 4:
        drawCheckbox(fieldLabels[4], threadingRightHand, labelX, labelY, selectedIndex == index, state);
        break;
    case 5:
        drawCheckbox(fieldLabels[5], !threadingRightHand, labelX, labelY, false, state);
        break;
    case 6:
        drawParamBoxUnified(fieldLabels[6], threadingPitch, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 7:
        drawParamBoxUnified(fieldLabels[7], threadingLength, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 8:
        drawParamBoxUnified(fieldLabels[8], threadingThreadDepth, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 9:
        drawParamBoxUnified(fieldLabels[9], threadingFirstCut, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 10:
        drawParamBoxUnified(fieldLabels[10], threadingOffset, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 11:
        drawParamBoxUnified(fieldLabels[11], (float)threadingSpringPasses, labelX, labelY, w, state, 0, cx, boxCenterY);
        break;
    case 12: {
        int boxX = cx - w/2, boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2); tft_set_text_size(1);
        tft_set_cursor(labelX, labelY); tft_print(fieldLabels[12]);
        const char *Ltext = "None";
        if (threadingL == 1) Ltext = "Entry";
        else if (threadingL == 2) Ltext = "Exit";
        else if (threadingL == 3) Ltext = "Both";
        int16_t textWidth = tft_text_width(Ltext);
        int16_t textX = cx - textWidth/2, textY = cy - tft_font_height() - 4;
        tft_set_text_color((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_draw_string(Ltext, textX, textY, (state == 1) ? TFT_WHITE : TFT_CYAN, 1);
        break;
    }
    case 13:
        drawParamBoxUnified(fieldLabels[13], threadingE, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 14:
        drawParamBoxUnified(fieldLabels[14], threadingR, labelX, labelY, w, state, 2, cx, boxCenterY);
        break;
    case 15:
        drawParamBoxUnified(fieldLabels[15], threadingSpindleRPM, labelX, labelY, w, state, 0, cx, boxCenterY);
        break;
    case 16: {
        int boxX = cx - w/2, boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2); tft_set_text_size(1);
        tft_set_cursor(labelX, labelY); tft_print(fieldLabels[16]);
        const char *dirText = threadingSpindleCW ? "CW" : "CCW";
        int16_t textWidth = tft_text_width(dirText);
        int16_t textX = cx - textWidth/2, textY = cy - tft_font_height() - 4;
        tft_draw_string(dirText, textX, textY, (state == 1) ? TFT_WHITE : TFT_CYAN, 1);
        break;
    }
    case 17:
        drawParamBoxUnified(fieldLabels[17], (float)threadingPasses, labelX, labelY, w, -2, 0, cx, boxCenterY);
        break;
    default: break;
    }
}

static void setThreadingField(int index, float value)
{
    switch (index) {
    case 0: threadingStartZ = value; break;
    case 1: threadingActualRadius = value * from_input_to_raggio(); if (threadingPitch > 0.0f) refreshParametersFromPitchAndDRO(); break;
    case 2: threadingIsExternal = true; if (threadingPitch > 0.0f) refreshParametersFromPitchAndDRO(); break;
    case 3: threadingIsExternal = false; if (threadingPitch > 0.0f) refreshParametersFromPitchAndDRO(); break;
    case 4: threadingRightHand = true; break;
    case 5: threadingRightHand = false; break;
    case 6: threadingPitch = fabs(value); if (threadingPitch > 0.0f) refreshParametersFromPitchAndDRO(); break;
    case 7: threadingLength = fabs(value); break;
    case 8: threadingThreadDepth = fabs(value); calcThreadingPasses(threadingThreadDepth, threadingFirstCut, threadingR); needsRedraw = true; break;
    case 9: threadingFirstCut = fabs(value); calcThreadingPasses(threadingThreadDepth, threadingFirstCut, threadingR); needsRedraw = true; break;
    case 10: threadingOffset = value; break;
    case 11: threadingSpringPasses = (value < 0) ? -(int)value : (int)value; break;
    case 12: threadingL = (int)value; break;
    case 13: threadingE = fabs(value); break;
    case 14: threadingR = value; if (threadingR < 1.0f) threadingR = 1.0f; if (threadingR > 2.0f) threadingR = 2.0f; calcThreadingPasses(threadingThreadDepth, threadingFirstCut, threadingR); needsRedraw = true; break;
    case 15: threadingSpindleRPM = fabs(value); break;
    case 16: threadingSpindleCW = (value > 0); break;
    default: break;
    }
}

static void initThreadingDefaults()
{
    threadingStartZ = DRO_Z;
    threadingActualRadius = DRO_X;
    threadingIsExternal = true;
    threadingRightHand = true;
    threadingPitch = 0.0f;
    threadingLength = 0.0f;
    threadingThreadDepth = 0.0f;
    threadingFirstCut = 0.0f;
    threadingOffset = 0.0f;
    threadingSpringPasses = 1;
    threadingR = 1.0f;
    threadingL = 0;
    threadingE = 0.0f;
    threadingSpindleRPM = 0.0f;
    threadingSpindleCW = true;
    threadingPasses = 0;
    ESP_LOGI(TAG, "Threading parameters reset to default");
}

static void calcThreadingPasses(float K, float J, float R)
{
    if (R < 1.0f) R = 1.0f;
    if (R > 2.0f) R = 2.0f;
    if (K <= 0.0f || J <= 0.0f) {
        threadingPasses = 0;
        ESP_LOGW(TAG, "calcThreadingPasses: K or J not positive, passes=0");
        return;
    }
    int N1 = (int)ceil(K / J);
    float Cmin = 0.005f;
    float ratio = 1.0f - (Cmin / K);
    float N2f = 1.0f / (1.0f - (ratio * ratio));
    int N2 = (int)ceil(N2f);
    float t = (R - 1.0f);
    float Nf = N1 + (N2 - N1) * t;
    threadingPasses = (int)ceil(Nf);
    if (threadingPasses > 500) threadingPasses = 500;

    ESP_LOGI(TAG, "=== Threading passes calculation ===");
    ESP_LOGI(TAG, "K (depth) = %.4f mm, J (first cut) = %.4f mm, R = %.2f", K, J, R);
    ESP_LOGI(TAG, "Model R=1 (constant depth): N1 = %d", N1);
    ESP_LOGI(TAG, "Model R=2 (constant area): N2 = %d", N2);
    ESP_LOGI(TAG, "Interpolated passes: %.2f → %d", Nf, threadingPasses);
    if (threadingPasses <= 200) {
        float A = (K * K) / threadingPasses;
        float prevP = 0.0f;
        ESP_LOGI(TAG, "Pass  P(mm)   Cut(mm)   Area");
        for (int n = 1; n <= threadingPasses; n++) {
            float P = sqrtf(n * A);
            if (P > K) P = K;
            float cut = P - prevP;
            if (cut < Cmin) cut = Cmin;
            float area = P * cut;
            ESP_LOGI(TAG, "%03d   %.4f   %.4f   %.4f", n, P, cut, area);
            prevP = P;
            if (prevP >= K) break;
        }
    }
    ESP_LOGI(TAG, "====================================");
}

static void drawThreadingWarning(const char *msg)
{
    tft_fill_rect(0, 260, 480, 60, TFT_RED);
    drawCenteredTextAt(msg, 240, 290, 2, TFT_WHITE);
    vTaskDelay(pdMS_TO_TICKS(2500));
    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
    drawMachiningButtons();
}

static bool validateThreadingParams()
{
    if (threadingLength <= 0.0f) { drawThreadingWarning("Length must be > 0"); return false; }
    if (threadingActualRadius <= 0.0f) { drawThreadingWarning("Start X (radius) must be > 0"); return false; }
    if (threadingPitch <= 0.0f) { drawThreadingWarning("Pitch must be > 0"); return false; }
    if (threadingThreadDepth <= 0.0f) { drawThreadingWarning("Depth (K) must be > 0"); return false; }
    if (threadingFirstCut <= 0.0f) { drawThreadingWarning("First Cut (J) must be > 0"); return false; }
    if (threadingFirstCut >= threadingThreadDepth) { drawThreadingWarning("J must be < K"); return false; }
    if (threadingFirstCut > 0.25f) { drawThreadingWarning("First Cut (J) too large (>0.25 mm)"); return false; }
    if (fabs(threadingOffset) > threadingThreadDepth) { drawThreadingWarning("Offset (I) too large relative to Depth (K)"); return false; }
    if (threadingIsExternal && threadingOffset > 0) { drawThreadingWarning("Offset (I) must be negative for external threads"); return false; }
    if (!threadingIsExternal && threadingOffset < 0) { drawThreadingWarning("Offset (I) must be positive for internal threads"); return false; }

    float startRad = threadingActualRadius;
    float offsetRad = threadingOffset;
    float Krad = threadingThreadDepth;
    float crestDiameter = 2.0f * (startRad + offsetRad);
    float rootDiameter;

    if (threadingIsExternal) {
        rootDiameter = 2.0f * (startRad - Krad + offsetRad);
        if (rootDiameter <= 0.0f) { drawThreadingWarning("Root diameter too small (<= 0)"); return false; }
        if (rootDiameter >= crestDiameter) { drawThreadingWarning("Root >= Crest: check K and I"); return false; }
        float reference = startRad * 2.0f;
        float dn = findNominalDiameterForPitch(threadingPitch, reference);
        float dmax, dmin, d2max, d2min, d1max, h; int nap;
        if (getExternalLimits(dn, threadingPitch, dmax, dmin, d2max, d2min, d1max, h, nap)) {
            if (crestDiameter < dmin - 0.001f) { drawThreadingWarning("Starting diameter too small (below dmin)"); return false; }
            if (crestDiameter > dmax + 0.2f) { drawThreadingWarning("Excessive excess stock (>0.2 mm above dmax)"); return false; }
        }
    } else {
        rootDiameter = 2.0f * (startRad + Krad + offsetRad);
        if (rootDiameter <= 0.0f) { drawThreadingWarning("Final diameter too small (<= 0)"); return false; }
        if (rootDiameter <= crestDiameter) { drawThreadingWarning("Root <= Crest: check K and I"); return false; }
        float reference = startRad * 2.0f + threadingPitch;
        float dn = findNominalDiameterForPitch(threadingPitch, reference);
        float D, D2min, D2max, D1min, D1max, H; int nap;
        if (getInternalLimits(dn, threadingPitch, D, D2min, D2max, D1min, D1max, H, nap)) {
            if (crestDiameter < D1min - 0.001f) { drawThreadingWarning("Minor diameter (D1) too small (below min)"); return false; }
            if (crestDiameter > D1max + 0.001f) { drawThreadingWarning("Minor diameter (D1) too large (above max)"); return false; }
        }
    }
    if (threadingSpringPasses < 0) { drawThreadingWarning("Spring Passes cannot be negative"); return false; }
    if (threadingSpindleRPM <= 0.0f) { drawThreadingWarning("RPM must be > 0"); return false; }
    if (maxFeedZ > 0 && threadingPitch > 0) {
        float rpmMax = maxFeedZ / threadingPitch;
        if (threadingSpindleRPM > rpmMax) {
            char msg[80];
            snprintf(msg, sizeof(msg), "RPM too high for this pitch. Max = %.0f", rpmMax);
            drawThreadingWarning(msg);
            return false;
        }
    }
    return true;
}

static void drawThreadingInputPreview(int index, const std::string &buffer, bool blinkBorder)
{
    int cx, cy, w, h, offsetX, decimals;
    getThreadingFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getThreadingFieldState(index, threadingSelectedField);
    uint16_t fg, bg, border;
    if (state == 0) { fg = TFT_CYAN; bg = TFT_BLACK; border = TFT_YELLOW; }
    else if (state == 1) { fg = TFT_WHITE; bg = TFT_BLACK; border = TFT_YELLOW; }
    else { fg = TFT_RED; bg = TFT_BLACK; border = TFT_YELLOW; }
    cy = cy - h/2;
    drawInputPreviewBox(cx, cy, w, h, buffer, blinkBorder, bg, fg, border);
}

static void handleThreadingNumericInput(int index)
{
    std::string buffer = "";
    bool editing = true, hasDecimal = false;
    drawInputButtons();
    unsigned long lastBlink = millis_idf();
    bool blinkState = false;
    while (editing) {
        char key = readKeypad();
        if (key == 'A' || key == 'D') continue;
        if (key == 'C') {
            float newValue = safe_stof(buffer);
            setThreadingField(index, newValue);
            drawThreadingField(index, threadingSelectedField);
            drawMachiningButtons();
            editing = false;
            break;
        }
        if (key == 'B') {
            drawThreadingField(index, threadingSelectedField);
            drawMachiningButtons();
            editing = false;
            break;
        }
        if (isValidNumericKey(key, buffer, hasDecimal)) {
            if (key == '#') {
                if (buffer.empty()) buffer = "-";
                else if (buffer[0] != '-') buffer.insert(0, 1, '-');
            } else if (key == '*') {
                if (buffer.empty()) buffer = "0.";
                else if (buffer == "-") buffer = "-0.";
                else if (buffer.find('.') == std::string::npos) buffer += '.';
                hasDecimal = true;
            } else {
                buffer += key;
            }
            drawThreadingInputPreview(index, buffer, blinkState);
        }
        if (millis_idf() - lastBlink > 400) {
            lastBlink = millis_idf();
            blinkState = !blinkState;
            drawThreadingInputPreview(index, buffer, blinkState);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void printThreadingParams()
{
    ESP_LOGI(TAG, "Threading Parameters:");
    ESP_LOGI(TAG, "Start Z: %.3f", threadingStartZ);
    ESP_LOGI(TAG, "Start X (radius): %.3f", threadingActualRadius);
    ESP_LOGI(TAG, "Type: %s", threadingIsExternal ? "EXTERNAL" : "INTERNAL");
    ESP_LOGI(TAG, "Hand: %s", threadingRightHand ? "RIGHT" : "LEFT");
    ESP_LOGI(TAG, "Pitch: %.3f", threadingPitch);
    ESP_LOGI(TAG, "Length: %.3f", threadingLength);
    ESP_LOGI(TAG, "Depth (K): %.3f", threadingThreadDepth);
    ESP_LOGI(TAG, "First Cut (J): %.3f", threadingFirstCut);
    ESP_LOGI(TAG, "Offset (I): %.3f", threadingOffset);
    ESP_LOGI(TAG, "Spring Passes: %d", threadingSpringPasses);
    ESP_LOGI(TAG, "Taper L: %d", threadingL);
    ESP_LOGI(TAG, "Extra length E: %.3f", threadingE);
    ESP_LOGI(TAG, "R (1..2): %.2f", threadingR);
    ESP_LOGI(TAG, "Spindle RPM: %.0f", threadingSpindleRPM);
    ESP_LOGI(TAG, "Direction: %s", threadingSpindleCW ? "CW" : "CCW");
    ESP_LOGI(TAG, "Estimated passes: %d", threadingPasses);
}

static void updateThreadingRealtimeFields()
{
    static float lastZ = -9999, lastRadius = -9999;
    float newZ = DRO_Z, newRadius = DRO_X;
    if (fabs(newZ - lastZ) > 0.001f) {
        threadingStartZ = newZ;
        drawThreadingField(0, threadingSelectedField);
        lastZ = newZ;
    }
    if (fabs(newRadius - lastRadius) > 0.001f) {
        threadingActualRadius = newRadius;
        drawThreadingField(1, threadingSelectedField);
        if (threadingPitch > 0.0f) refreshParametersFromPitchAndDRO();
        lastRadius = newRadius;
    }
}

static void generateThreadingGcode()
{
    ThreadingParams tp;
    tp.startZ = threadingStartZ;
    tp.actualRadius = threadingActualRadius;
    tp.threadType = threadingIsExternal;
    tp.rightHand = threadingRightHand;
    tp.pitch = threadingPitch;
    tp.length = threadingLength;
    tp.threadDepth = threadingThreadDepth;
    tp.firstCut = threadingFirstCut;
    tp.offset = threadingOffset;
    tp.springPasses = threadingSpringPasses;
    tp.R = threadingR;
    tp.L = threadingL;
    tp.E = threadingE;
    tp.spindleRPM = threadingSpindleRPM;
    tp.spindleCW = threadingSpindleCW;
    GcodeBlock block = buildThreadingBlock(tp, true);
    GcodeGenerator(block);
}

static void drawThreadingScene()
{
    char *stopSpindle = strdup("M5\n");
    if (stopSpindle && xQueueSend(gcodeQueue, &stopSpindle, 0) != pdTRUE) {
        free(stopSpindle);
        ESP_LOGE(TAG, "Coda piena per M5");
    }

    initThreadingDefaults();

    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("THREADING (G76 Cycle)", 240, 15, 2, TFT_YELLOW);
    tft_fill_rect(20, 30, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 72, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 144, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 216, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 258, 440, 1, TFT_WHITE);
    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_text_datum(TL_DATUM);
    tft_set_text_color(TFT_YELLOW);
    tft_set_cursor(20, 155);
    tft_print("Advanced Settings  (linuxCNC)");

    resetTextStyle();
    for (int i = 0; i < threadingTotalFields; ++i)
        drawThreadingField(i, threadingSelectedField);

    drawMachiningButtons();
}

static void editThreadingParams()
{
    threadingSelectedField = 0;
    needsRedraw = true;
    while (true) {
        updateThreadingRealtimeFields();
        if (needsRedraw) {
            for (int i = 0; i < threadingTotalFields; ++i)
                drawThreadingField(i, threadingSelectedField);
            needsRedraw = false;
        }
        char key = readKeypad();
        if (key == 'D') {
            resetTextStyle();
            return;
        }
        if (key == 'A') {
            if (validateThreadingParams()) {
                std::string msg = getThreadingConfirmationMessage();
                ESP_LOGI(TAG, "=== CONFIRMATION MESSAGE ===");
                ESP_LOGI(TAG, "%s", msg.c_str());
                ESP_LOGI(TAG, "============================");
                float reference = threadingIsExternal ? threadingActualRadius * 2.0f : threadingActualRadius * 2.0f + threadingPitch;
                float dn = findNominalDiameterForPitch(threadingPitch, reference);
                ThreadStandardType stdType = checkStandardThread(dn, threadingPitch, threadingIsExternal);
                const char *titolo = "";
                if (stdType == THREAD_STANDARD_COARSE) titolo = "STANDARD COARSE THREAD";
                else if (stdType == THREAD_STANDARD_FINE) titolo = "STANDARD FINE THREAD";
                else titolo = "NON STANDARD THREAD";

                bool confirmed = showYesNoBox(titolo, msg, "OK", 'C', "CANCEL", 'D', 420, 240);
                drawThreadingStaticElements();
                for (int i = 0; i < threadingTotalFields; ++i)
                    drawThreadingField(i, threadingSelectedField);
                drawMachiningButtons();

                if (confirmed) {
                    bool useCoolant = askCoolant();
                    coolantEnabled = useCoolant;
                    generateThreadingGcode();
                    return;
                } else {
                    needsRedraw = true;
                }
            }
            continue;
        }
        if (key == 'B') {
            printThreadingParams();
            continue;
        }
        if (key == '1') {
            int next = threadingSelectedField;
            do {
                next = (next - 1 + threadingTotalFields) % threadingTotalFields;
            } while (getThreadingFieldState(next, next) < 0 || next == 3 || next == 5);
            threadingSelectedField = next;
            needsRedraw = true;
            continue;
        }
        if (key == '2') {
            int next = threadingSelectedField;
            do {
                next = (next + 1) % threadingTotalFields;
            } while (getThreadingFieldState(next, next) < 0 || next == 3 || next == 5);
            threadingSelectedField = next;
            needsRedraw = true;
            continue;
        }
        if (key == 'C') {
            int state = getThreadingFieldState(threadingSelectedField, threadingSelectedField);
            if (state < 0) {
                drawThreadingWarning("Campo non modificabile");
                continue;
            }
            switch (threadingSelectedField) {
            case 2:
                threadingIsExternal = !threadingIsExternal;
                if (threadingPitch > 0.0f) refreshParametersFromPitchAndDRO();
                drawThreadingField(3, -1);
                drawThreadingField(2, threadingSelectedField);
                needsRedraw = true;
                break;
            case 3:
                break;
            case 4:
                threadingRightHand = !threadingRightHand;
                drawThreadingField(5, -1);
                drawThreadingField(4, threadingSelectedField);
                needsRedraw = true;
                break;
            case 5:
                break;
            case 12:
                threadingL = (threadingL + 1) % 4;
                needsRedraw = true;
                break;
            case 16:
                threadingSpindleCW = !threadingSpindleCW;
                needsRedraw = true;
                break;
            default:
                handleThreadingNumericInput(threadingSelectedField);
                needsRedraw = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void threading_scene_enter(void)
{
    drawThreadingScene();
    editThreadingParams();
}

ThreadingParams getCurrentThreadingParams(void)
{
    ThreadingParams tp;
    tp.startZ = threadingStartZ;
    tp.actualRadius = threadingActualRadius;
    tp.threadType = threadingIsExternal;
    tp.rightHand = threadingRightHand;
    tp.pitch = threadingPitch;
    tp.length = threadingLength;
    tp.threadDepth = threadingThreadDepth;
    tp.firstCut = threadingFirstCut;
    tp.offset = threadingOffset;
    tp.springPasses = threadingSpringPasses;
    tp.R = threadingR;
    tp.L = threadingL;
    tp.E = threadingE;
    tp.spindleRPM = threadingSpindleRPM;
    tp.spindleCW = threadingSpindleCW;
    return tp;
}