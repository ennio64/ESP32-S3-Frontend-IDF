// chamfer_billet_scene.cpp
#include "chamfer_billet_scene.h"
#include "scene_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_vars.h"
#include "global_settings.h"
#include "graphics_lgfx.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "msgbox.h"
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

static const char *TAG = "CHAMFER_BILLET";

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

enum ChamferBilletMode : int
{
    MODE_NONE = 0,
    MODE_CHAMFER = 1,
    MODE_BILLET = 2
};

static ChamferBilletMode chamferBilletMode = MODE_NONE;
static int chamferBilletSelectedField = 0;
static const int chamferBilletTotalFields = 16;

static float chamferBilletStartZ = 10.0f;
static float chamferBilletStartX = 10.0f;
static float chamferBilletClearance = 0.5f;
static float chamferBilletDistanceZ = 1.0f;
static float chamferBilletDistanceX = 1.0f;
static float chamferBilletAngle = 45.0f;
static float chamferBilletPassDepth = 0.1f;
static float chamferBilletLastPassDepth = 0.02f;
static float chamferBilletFeedRate = 200.0f;
static float chamferBilletLastFeedRate = 100.0f;
static int chamferBilletSpringPasses = 1;
static bool chamferBilletUseCSS = false;
static float chamferBilletSpindleMmin = 75.0f;
static float chamferBilletSpindleRPM = 1200.0f;
static bool chamferBilletSpindleCW = true;

static bool needsRedraw = false;

static const char *fieldLabels[] = {
    "Z Current", "X Current", "XZ Retract", "Chamfer", "Billet",
    "Z Distance", "X Distance", "Angle", "Pass Depth", "Feed Rate",
    "Last Pass", "Last Feed", "Spring Passes", "CSS", "RPM/m/min", "Direction"
};

static void calculateAngleFromDistances()
{
    if (chamferBilletDistanceZ == 0 && chamferBilletDistanceX == 0) return;
    if (chamferBilletDistanceZ == 0 && chamferBilletDistanceX > 0) {
        chamferBilletAngle = 90.0f; return;
    }
    if (chamferBilletDistanceX == 0 && chamferBilletDistanceZ > 0) {
        chamferBilletAngle = 0.0f; return;
    }
    if (chamferBilletDistanceZ > 0 && chamferBilletDistanceX > 0) {
        float rad = atan(chamferBilletDistanceX / chamferBilletDistanceZ);
        chamferBilletAngle = rad * (180.0f / M_PI);
        if (chamferBilletAngle > 90) chamferBilletAngle = 90;
        if (chamferBilletAngle < 0) chamferBilletAngle = 0;
    }
}

static void calculateDistanceXFromAngle()
{
    if (chamferBilletAngle < 0) chamferBilletAngle = 0;
    if (chamferBilletAngle > 90) chamferBilletAngle = 90;
    if (chamferBilletAngle == 90) {
        chamferBilletDistanceX = (chamferBilletDistanceZ > 0) ? chamferBilletDistanceZ : 1.0f;
        return;
    }
    if (chamferBilletAngle == 0) {
        chamferBilletDistanceX = 0;
        return;
    }
    float rad = chamferBilletAngle * M_PI / 180.0f;
    if (chamferBilletDistanceZ > 0) {
        chamferBilletDistanceX = chamferBilletDistanceZ * tan(rad);
    } else if (chamferBilletDistanceX == 0 && chamferBilletAngle > 0) {
        chamferBilletDistanceZ = 1.0f;
        chamferBilletDistanceX = tan(rad);
    }
}

static void calculateDistanceZFromAngle()
{
    if (chamferBilletAngle < 0) chamferBilletAngle = 0;
    if (chamferBilletAngle > 90) chamferBilletAngle = 90;
    if (chamferBilletAngle == 0) {
        chamferBilletDistanceZ = (chamferBilletDistanceX > 0) ? chamferBilletDistanceX : 1.0f;
        return;
    }
    if (chamferBilletAngle == 90) {
        chamferBilletDistanceZ = 0;
        return;
    }
    float rad = chamferBilletAngle * M_PI / 180.0f;
    if (chamferBilletDistanceX > 0) {
        chamferBilletDistanceZ = chamferBilletDistanceX / tan(rad);
    } else if (chamferBilletDistanceZ == 0 && chamferBilletAngle > 0) {
        chamferBilletDistanceX = 1.0f;
        chamferBilletDistanceZ = 1.0f / tan(rad);
    }
}

static void getChamferBilletFieldGeometry(int index, int &centerX, int &centerY, int &w, int &h, int &offsetX, int &decimals)
{
    const int colX[] = {128, 278, 425};
    const int rowY[] = {57, 87, 117, 147, 177, 207};
    w = (index == 12 || index == 15) ? 36 : 70;
    h = 24;
    offsetX = 100;
    decimals = 3;
    switch (index)
    {
    case 0: centerX = colX[0]; centerY = rowY[0] + 7; offsetX = 108; break;
    case 1: centerX = colX[1]; centerY = rowY[0] + 7; break;
    case 2: centerX = colX[2]; centerY = rowY[0] + 7; offsetX = 104; break;
    case 3: centerX = colX[0] + 80; centerY = rowY[1] + 19; offsetX = 124; break;
    case 4: centerX = colX[1] + 80; centerY = rowY[1] + 19; offsetX = 108; break;
    case 5: centerX = colX[0]; centerY = rowY[2] + 19; offsetX = 108; break;
    case 6: centerX = colX[1]; centerY = rowY[2] + 19; offsetX = 108; break;
    case 7: centerX = colX[2]; centerY = rowY[2] + 19; offsetX = 80; decimals = 4; break;
    case 8: centerX = colX[0]; centerY = rowY[3] + 30; offsetX = 108; break;
    case 9: centerX = colX[1]; centerY = rowY[3] + 30; break;
    case 10: centerX = colX[0]; centerY = rowY[4] + 30; break;
    case 11: centerX = colX[1]; centerY = rowY[4] + 30; break;
    case 12: centerX = colX[2] + 15; centerY = rowY[4] + 30; offsetX = 110; decimals = 0; break;
    case 13: centerX = colX[0] + 80; centerY = rowY[5] + 42; break;
    case 14: centerX = colX[1]; centerY = rowY[5] + 42; offsetX = 80; break;
    case 15: centerX = colX[2] + 15; centerY = rowY[5] + 42; offsetX = 80; decimals = 0; break;
    default: centerX = -999; centerY = -999; break;
    }
}

static int getChamferBilletFieldState(int index, int selectedIndex)
{
    if (index == 0 || index == 1) return -2;
    if (index == 3 && chamferBilletMode != MODE_NONE && chamferBilletMode != MODE_CHAMFER) return -1;
    if (index == 4 && chamferBilletMode != MODE_NONE && chamferBilletMode != MODE_BILLET) return -1;
    if (chamferBilletMode == MODE_NONE) {
        if (index == 5 || index == 6 || index == 7) return -1;
        return (selectedIndex == index) ? 1 : 0;
    }
    if (chamferBilletMode == MODE_CHAMFER) {
        if (index == 6 || index == 7) return -2;
        if (index == 5) return (selectedIndex == index) ? 1 : 0;
        return (selectedIndex == index) ? 1 : 0;
    }
    return (selectedIndex == index) ? 1 : 0;
}

static void drawChamferBilletField(int index, int selectedIndex)
{
    resetTextStyle();
    int cx, cy, w, h, offsetX, decimals;
    getChamferBilletFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getChamferBilletFieldState(index, selectedIndex);
    int labelX = cx - offsetX;
    int labelY = cy - tft_font_height() - 4;
    int boxCenterY = cy - h / 2;

    switch (index)
    {
    case 0:
        drawParamBoxUnified(fieldLabels[0], chamferBilletStartZ, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 1:
    {
        int boxX = cx - w/2, boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2); tft_set_text_size(1);
        tft_set_cursor(labelX, labelY); tft_print(fieldLabels[1]);
        float displayValue = chamferBilletStartX * to_display();
        char numBuf[16]; snprintf(numBuf, sizeof(numBuf), "%.3f", displayValue);
        const char *suffix = (settings_get_xmode() == 0) ? "D" : "R";
        int16_t numWidth = tft_text_width(numBuf);
        int16_t suffixWidth = tft_text_width(suffix);
        int16_t totalWidth = numWidth + 2 + suffixWidth;
        int16_t startX = cx - totalWidth/2, textY = cy - 20;
        uint16_t numColor = (state == -2) ? TFT_GREEN : ((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_set_text_color(numColor);
        tft_draw_string(numBuf, startX, textY, numColor, 1);
        tft_set_text_color(TFT_ORANGE);
        tft_draw_string(suffix, startX + numWidth + 2, textY, TFT_ORANGE, 1);
        break;
    }
    case 2:
        drawParamBoxUnified(fieldLabels[2], chamferBilletClearance, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 3:
        drawCheckbox(fieldLabels[3], chamferBilletMode == MODE_CHAMFER, labelX, labelY, selectedIndex == index, state);
        break;
    case 4:
        drawCheckbox(fieldLabels[4], chamferBilletMode == MODE_BILLET, labelX, labelY, selectedIndex == index, state);
        break;
    case 5:
        drawParamBoxUnified(fieldLabels[5], chamferBilletDistanceZ, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 6:
        drawParamBoxUnified(fieldLabels[6], chamferBilletDistanceX, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 7:
        drawParamBoxUnified(fieldLabels[7], chamferBilletAngle, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 8:
        drawParamBoxUnified(fieldLabels[8], chamferBilletPassDepth, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 9:
        drawParamBoxUnified(fieldLabels[9], chamferBilletFeedRate, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 10:
        drawParamBoxUnified(fieldLabels[10], chamferBilletLastPassDepth, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 11:
        drawParamBoxUnified(fieldLabels[11], chamferBilletLastFeedRate, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 12:
        drawParamBoxUnified(fieldLabels[12], (float)chamferBilletSpringPasses, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 13:
        drawCheckbox(fieldLabels[13], chamferBilletUseCSS, labelX, labelY, selectedIndex == index, state);
        break;
    case 14:
    {
        const char *label = chamferBilletUseCSS ? "m/min" : "RPM";
        float value = chamferBilletUseCSS ? chamferBilletSpindleMmin : chamferBilletSpindleRPM;
        drawParamBoxUnified(label, value, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    }
    case 15:
    {
        int boxX = cx - w/2, boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2); tft_set_text_size(1);
        tft_set_cursor(labelX, labelY); tft_print(fieldLabels[15]);
        const char *dirText = chamferBilletSpindleCW ? "CW" : "CCW";
        int16_t textWidth = tft_text_width(dirText);
        int16_t textX = cx - textWidth/2, textY = cy - tft_font_height() - 4;
        tft_draw_string(dirText, textX, textY, state == 1 ? TFT_WHITE : TFT_CYAN, 1);
        break;
    }
    default: break;
    }
}

static void setChamferBilletField(int index, float value)
{
    switch (index)
    {
    case 0: chamferBilletStartZ = value; break;
    case 1: chamferBilletStartX = value * from_input_to_raggio(); break;
    case 2: chamferBilletClearance = value; break;
    case 3: case 4: break;
    case 5:
        chamferBilletDistanceZ = value;
        if (chamferBilletMode == MODE_CHAMFER) {
            chamferBilletDistanceX = value; chamferBilletAngle = 45.0f;
        } else if (chamferBilletMode == MODE_BILLET) {
            if (chamferBilletDistanceX > 0) calculateAngleFromDistances();
            else if (chamferBilletAngle > 0 && chamferBilletAngle < 90) calculateDistanceXFromAngle();
        }
        needsRedraw = true;
        break;
    case 6:
        if (chamferBilletMode == MODE_CHAMFER) break;
        chamferBilletDistanceX = value;
        if (chamferBilletMode == MODE_BILLET) {
            if (chamferBilletDistanceZ > 0) calculateAngleFromDistances();
            else if (chamferBilletAngle > 0 && chamferBilletAngle < 90) calculateDistanceZFromAngle();
        }
        needsRedraw = true;
        break;
    case 7:
        if (chamferBilletMode == MODE_CHAMFER) break;
        chamferBilletAngle = value;
        if (chamferBilletMode == MODE_BILLET) {
            if (chamferBilletDistanceZ > 0) calculateDistanceXFromAngle();
            else if (chamferBilletDistanceX > 0) calculateDistanceZFromAngle();
        }
        needsRedraw = true;
        break;
    case 8: chamferBilletPassDepth = value; break;
    case 9: chamferBilletFeedRate = (value < 0.01f) ? 0.01f : value; break;
    case 10: chamferBilletLastPassDepth = value; break;
    case 11: chamferBilletLastFeedRate = value; break;
    case 12: chamferBilletSpringPasses = (value < 0) ? 0 : (int)value; break;
    case 13: chamferBilletUseCSS = (value > 0); break;
    case 14:
        if (chamferBilletUseCSS) chamferBilletSpindleMmin = value;
        else chamferBilletSpindleRPM = value;
        break;
    case 15: chamferBilletSpindleCW = (value > 0); break;
    }
}

static void initChamferBilletDefaults()
{
    chamferBilletStartZ = DRO_Z; chamferBilletStartX = DRO_X;
    chamferBilletMode = MODE_NONE;
    chamferBilletDistanceZ = 1.0f; chamferBilletDistanceX = 1.0f;
    chamferBilletAngle = 45.0f; chamferBilletClearance = 0.5f;
    chamferBilletPassDepth = 0.1f; chamferBilletLastPassDepth = 0.02f;
    chamferBilletFeedRate = 200.0f; chamferBilletLastFeedRate = 100.0f;
    chamferBilletSpringPasses = 1;
    chamferBilletUseCSS = false; chamferBilletSpindleMmin = 75.0f;
    chamferBilletSpindleRPM = 1200.0f; chamferBilletSpindleCW = true;
    ESP_LOGI(TAG, "Chamfer/Billet parameters reset to default");
}

static void drawChamferBilletWarning(const char *msg)
{
    tft_fill_rect(0, 260, 480, 60, TFT_RED);
    drawCenteredTextAt(msg, 240, 290, 2, TFT_WHITE);
    vTaskDelay(pdMS_TO_TICKS(2500));
    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
    drawMachiningButtons();
}

static bool validateChamferBilletParams()
{
    if (chamferBilletMode == MODE_NONE) {
        drawChamferBilletWarning("Selezionare Chamfer o Billet"); return false;
    }
    if (chamferBilletMode == MODE_CHAMFER && chamferBilletDistanceZ <= 0) {
        drawChamferBilletWarning("Z Distance > 0 richiesta per Chamfer"); return false;
    }
    if (chamferBilletMode == MODE_BILLET) {
        if (chamferBilletDistanceZ <= 0 && chamferBilletDistanceX <= 0) {
            drawChamferBilletWarning("Inserire Z Distance o X Distance > 0"); return false;
        }
        if (chamferBilletDistanceZ > 0 && chamferBilletDistanceX > 0 &&
            (chamferBilletAngle <= 0 || chamferBilletAngle >= 90)) {
            drawChamferBilletWarning("Angle deve essere tra 0 e 90 gradi"); return false;
        }
        if (chamferBilletDistanceZ > 0 && chamferBilletDistanceX == 0 && chamferBilletAngle != 0) {
            drawChamferBilletWarning("Con X Distance=0, Angle deve essere 0"); return false;
        }
        if (chamferBilletDistanceX > 0 && chamferBilletDistanceZ == 0 && chamferBilletAngle != 90) {
            drawChamferBilletWarning("Con Z Distance=0, Angle deve essere 90"); return false;
        }
    }
    if (chamferBilletClearance < 0) {
        drawChamferBilletWarning("XZ Retract deve essere >= 0"); return false;
    }
    if (chamferBilletPassDepth <= 0) {
        drawChamferBilletWarning("Pass Depth > 0"); return false;
    }
    if (chamferBilletFeedRate <= 0) {
        drawChamferBilletWarning("Feed Rate > 0"); return false;
    }
    return true;
}

static void drawChamferBilletInputPreview(int index, const std::string &buffer, bool blinkBorder)
{
    int cx, cy, w, h, offsetX, decimals;
    getChamferBilletFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getChamferBilletFieldState(index, chamferBilletSelectedField);
    uint16_t fg, bg, border;
    if (state == 0) { fg = TFT_CYAN; bg = TFT_BLACK; border = TFT_YELLOW; }
    else if (state == 1) { fg = TFT_WHITE; bg = TFT_BLACK; border = TFT_YELLOW; }
    else { fg = TFT_RED; bg = TFT_BLACK; border = TFT_YELLOW; }
    cy = cy - h/2;
    drawInputPreviewBox(cx, cy, w, h, buffer, blinkBorder, bg, fg, border);
}

static void handleChamferBilletNumericInput(int index)
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
            setChamferBilletField(index, newValue);
            drawChamferBilletField(index, chamferBilletSelectedField);
            drawMachiningButtons();
            editing = false;
            break;
        }
        if (key == 'B') {
            drawChamferBilletField(index, chamferBilletSelectedField);
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
            drawChamferBilletInputPreview(index, buffer, blinkState);
        }
        if (millis_idf() - lastBlink > 400) {
            lastBlink = millis_idf();
            blinkState = !blinkState;
            drawChamferBilletInputPreview(index, buffer, blinkState);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void printChamferBilletParams()
{
    ESP_LOGI(TAG, "Chamfer/Billet Parameters:");
    ESP_LOGI(TAG, "Mode: %s", chamferBilletMode == MODE_CHAMFER ? "CHAMFER" : (chamferBilletMode == MODE_BILLET ? "BILLET" : "NONE"));
    ESP_LOGI(TAG, "Start Z: %.3f", chamferBilletStartZ);
    ESP_LOGI(TAG, "Start X (radius): %.3f", chamferBilletStartX);
    ESP_LOGI(TAG, "Clearance: %.3f", chamferBilletClearance);
    ESP_LOGI(TAG, "Z Distance: %.3f", chamferBilletDistanceZ);
    ESP_LOGI(TAG, "X Distance: %.3f", chamferBilletDistanceX);
    ESP_LOGI(TAG, "Angle: %.1f", chamferBilletAngle);
    ESP_LOGI(TAG, "Pass Depth: %.3f", chamferBilletPassDepth);
    ESP_LOGI(TAG, "Feed Rate: %.1f", chamferBilletFeedRate);
    ESP_LOGI(TAG, "Last Pass: %.3f", chamferBilletLastPassDepth);
    ESP_LOGI(TAG, "Last Feed: %.1f", chamferBilletLastFeedRate);
    ESP_LOGI(TAG, "Spring Passes: %d", chamferBilletSpringPasses);
    ESP_LOGI(TAG, "CSS: %s", chamferBilletUseCSS ? "ON" : "OFF");
    if (chamferBilletUseCSS) ESP_LOGI(TAG, "Spindle m/min: %.1f", chamferBilletSpindleMmin);
    else ESP_LOGI(TAG, "Spindle RPM: %.0f", chamferBilletSpindleRPM);
    ESP_LOGI(TAG, "Direction: %s", chamferBilletSpindleCW ? "CW" : "CCW");
}

static void updateChamferBilletRealtimeFields()
{
    static float lastZ = -9999, lastRadius = -9999;
    float newZ = DRO_Z, newRadius = DRO_X;
    if (fabs(newZ - lastZ) > 0.001f) {
        chamferBilletStartZ = newZ;
        drawChamferBilletField(0, chamferBilletSelectedField);
        lastZ = newZ;
    }
    if (fabs(newRadius - lastRadius) > 0.001f) {
        chamferBilletStartX = newRadius;
        drawChamferBilletField(1, chamferBilletSelectedField);
        lastRadius = newRadius;
    }
}

static void generateChamferBilletGcode()
{
    ChamferBilletParams cp;
    cp.startZ = chamferBilletStartZ; cp.startX = chamferBilletStartX;
    cp.clearance = chamferBilletClearance; cp.type = (int)chamferBilletMode;
    cp.distanceZ = chamferBilletDistanceZ; cp.distanceX = chamferBilletDistanceX;
    cp.angle = chamferBilletAngle; cp.passDepth = chamferBilletPassDepth;
    cp.feedRate = chamferBilletFeedRate; cp.lastPassDepth = chamferBilletLastPassDepth;
    cp.lastFeedRate = chamferBilletLastFeedRate; cp.springPasses = chamferBilletSpringPasses;
    cp.useCSS = chamferBilletUseCSS; cp.spindleMmin = chamferBilletSpindleMmin;
    cp.spindleRPM = chamferBilletSpindleRPM; cp.spindleCW = chamferBilletSpindleCW;
    GcodeBlock block = buildChamferBilletBlock(cp, true);
    GcodeGenerator(block);
}

static void drawChamferBilletScene()
{
    char *stopSpindle = strdup("M5\n");
    if (stopSpindle && xQueueSend(gcodeQueue, &stopSpindle, 0) != pdTRUE) free(stopSpindle);
    initChamferBilletDefaults();
    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("CHAMFER & BILLET", 240, 15, 2, TFT_YELLOW);
    tft_fill_rect(20, 30, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 72, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 144, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 216, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 258, 440, 1, TFT_WHITE);
    resetTextStyle();
    for (int i = 0; i < chamferBilletTotalFields; ++i)
        drawChamferBilletField(i, chamferBilletSelectedField);
    drawMachiningButtons();
}

static void editChamferBilletParams()
{
    chamferBilletSelectedField = 3;
    needsRedraw = true;
    while (true) {
        updateChamferBilletRealtimeFields();
        if (needsRedraw) {
            for (int i = 0; i < chamferBilletTotalFields; ++i)
                drawChamferBilletField(i, chamferBilletSelectedField);
            needsRedraw = false;
        }
        char key = readKeypad();
        if (key == 'D') { resetTextStyle(); return; }
        if (key == 'A') {
            if (validateChamferBilletParams()) {
                bool useCoolant = askCoolant();
                coolantEnabled = useCoolant;
                generateChamferBilletGcode();
                return;
            }
            continue;
        }
        if (key == 'B') { printChamferBilletParams(); continue; }
        if (key == '1') {
            int next = chamferBilletSelectedField;
            do { next = (next - 1 + chamferBilletTotalFields) % chamferBilletTotalFields; }
            while (getChamferBilletFieldState(next, next) < 0);
            chamferBilletSelectedField = next;
            needsRedraw = true;
            continue;
        }
        if (key == '2') {
            int next = chamferBilletSelectedField;
            do { next = (next + 1) % chamferBilletTotalFields; }
            while (getChamferBilletFieldState(next, next) < 0);
            chamferBilletSelectedField = next;
            needsRedraw = true;
            continue;
        }
        if (key == 'C') {
            int state = getChamferBilletFieldState(chamferBilletSelectedField, chamferBilletSelectedField);
            if (state < 0) { drawChamferBilletWarning("Campo non modificabile"); continue; }
            switch (chamferBilletSelectedField) {
            case 3:
                if (chamferBilletMode == MODE_CHAMFER) chamferBilletMode = MODE_NONE;
                else { chamferBilletMode = MODE_CHAMFER; chamferBilletAngle = 45.0f; chamferBilletDistanceX = 0.0f; chamferBilletDistanceZ = 0.0f; }
                for (int i = 0; i < chamferBilletTotalFields; ++i) drawChamferBilletField(i, chamferBilletSelectedField);
                needsRedraw = true;
                break;
            case 4:
                if (chamferBilletMode == MODE_BILLET) chamferBilletMode = MODE_NONE;
                else { chamferBilletMode = MODE_BILLET; chamferBilletAngle = 45.0f; chamferBilletDistanceX = 0.0f; chamferBilletDistanceZ = 0.0f; }
                for (int i = 0; i < chamferBilletTotalFields; ++i) drawChamferBilletField(i, chamferBilletSelectedField);
                needsRedraw = true;
                break;
            case 13:
                chamferBilletUseCSS = !chamferBilletUseCSS;
                tft_fill_rect(140, 217, 100, 40, TFT_NAVY);
                drawChamferBilletField(14, -1);
                needsRedraw = true;
                break;
            case 15:
                chamferBilletSpindleCW = !chamferBilletSpindleCW;
                needsRedraw = true;
                break;
            default:
                handleChamferBilletNumericInput(chamferBilletSelectedField);
                needsRedraw = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void chamfer_billet_scene_enter(void)
{
    drawChamferBilletScene();
    editChamferBilletParams();
}

ChamferBilletParams getCurrentChamferBilletParams(void)
{
    ChamferBilletParams cp;
    cp.startZ = chamferBilletStartZ; cp.startX = chamferBilletStartX;
    cp.clearance = chamferBilletClearance; cp.type = (int)chamferBilletMode;
    cp.distanceZ = chamferBilletDistanceZ; cp.distanceX = chamferBilletDistanceX;
    cp.angle = chamferBilletAngle; cp.passDepth = chamferBilletPassDepth;
    cp.feedRate = chamferBilletFeedRate; cp.lastPassDepth = chamferBilletLastPassDepth;
    cp.lastFeedRate = chamferBilletLastFeedRate; cp.springPasses = chamferBilletSpringPasses;
    cp.useCSS = chamferBilletUseCSS; cp.spindleMmin = chamferBilletSpindleMmin;
    cp.spindleRPM = chamferBilletSpindleRPM; cp.spindleCW = chamferBilletSpindleCW;
    return cp;
}