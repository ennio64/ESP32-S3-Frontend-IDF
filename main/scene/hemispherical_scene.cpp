// hemispherical_scene.cpp
#include "hemispherical_scene.h"
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

static const char *TAG = "HEMISPHERICAL";

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

static int hemisphericalSelectedField = 0;
static const int hemisphericalTotalFields = 13;

static float hemisphericalStartZ = 10.0f;
static float hemisphericalStartX = 10.0f;
static float hemisphericalRadius = 1.0f;
static float hemisphericalClearance = 0.5f;
static float hemisphericalPassDepth = 0.1f;
static float hemisphericalLastPassDepth = 0.02f;
static float hemisphericalFeedRate = 200.0f;
static float hemisphericalLastFeedRate = 100.0f;
static int hemisphericalSpringPasses = 1;
static bool hemisphericalUseCSS = false;
static float hemisphericalSpindleMmin = 75.0f;
static float hemisphericalSpindleRPM = 1200.0f;
static bool hemisphericalSpindleCW = true;
static bool hemisphericalProfile = true; // true = CONCAVO, false = CONVESSO

static bool needsRedraw = false;

static const char *fieldLabels[] = {
    "Z Current", "X Current", "XZ Retract", "Profile Type", "FilletRadius",
    "Pass Depth", "Feed Rate", "Last Pass", "Last Feed", "Spring Passes",
    "CSS", "RPM/m/min", "Direction"
};

static void getHemisphericalFieldGeometry(int index, int &centerX, int &centerY, int &w, int &h, int &offsetX, int &decimals)
{
    const int colX[] = {128, 278, 425};
    const int rowY[] = {57, 87, 117, 147, 177, 207};
    w = (index == 9 || index == 12) ? 36 : 70;
    h = 24;
    offsetX = 100;
    decimals = 3;

    switch (index)
    {
    case 0: centerX = colX[0]; centerY = rowY[0] + 7; offsetX = 108; break;
    case 1: centerX = colX[1]; centerY = rowY[0] + 7; break;
    case 2: centerX = colX[2]; centerY = rowY[0] + 7; offsetX = 104; break;
    case 3: centerX = colX[0] + 30; centerY = rowY[1] + 19; w = 100; offsetX = 100; break;
    case 4: centerX = colX[0]; centerY = rowY[2] + 19; offsetX = 108; break;
    case 5: centerX = colX[0]; centerY = rowY[3] + 30; offsetX = 108; break;
    case 6: centerX = colX[1]; centerY = rowY[3] + 30; break;
    case 7: centerX = colX[0]; centerY = rowY[4] + 30; offsetX = 108; break;
    case 8: centerX = colX[1]; centerY = rowY[4] + 30; break;
    case 9: centerX = colX[2] + 15; centerY = rowY[4] + 30; offsetX = 110; decimals = 0; break;
    case 10: centerX = colX[0] + 80; centerY = rowY[5] + 42; break;
    case 11: centerX = colX[1]; centerY = rowY[5] + 42; offsetX = 80; break;
    case 12: centerX = colX[2] + 15; centerY = rowY[5] + 42; offsetX = 80; decimals = 0; break;
    default: centerX = -999; centerY = -999; break;
    }
}

static int getHemisphericalFieldState(int index, int selectedIndex)
{
    if (index == 0 || index == 1) return -2;
    if (selectedIndex == index) return 1;
    return 0;
}

static void drawHemisphericalField(int index, int selectedIndex)
{
    resetTextStyle();
    int cx, cy, w, h, offsetX, decimals;
    getHemisphericalFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getHemisphericalFieldState(index, selectedIndex);
    int labelX = cx - offsetX;
    int labelY = cy - tft_font_height() - 4;
    int boxCenterY = cy - h / 2;

    switch (index)
    {
    case 0:
        drawParamBoxUnified(fieldLabels[0], hemisphericalStartZ, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 1:
    {
        int boxX = cx - w/2, boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2); tft_set_text_size(1);
        tft_set_cursor(labelX, labelY); tft_print(fieldLabels[1]);
        float displayValue = hemisphericalStartX * to_display();
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
        drawParamBoxUnified(fieldLabels[2], hemisphericalClearance, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 3:
    {
        drawCheckbox(fieldLabels[3], hemisphericalProfile, labelX, labelY, selectedIndex == index, state);
        tft_set_font(&fonts::Font2); tft_set_text_size(1);
        tft_set_text_color((state == 1) ? TFT_WHITE : TFT_CYAN);
        static int maxTextW = 0;
        if (maxTextW == 0) {
            int w1 = tft_text_width("CONCAVE");
            int w2 = tft_text_width("CONVEX");
            maxTextW = (w1 > w2) ? w1 : w2;
        }
        int textX = labelX + tft_text_width(fieldLabels[3]) + 22;
        int textY = labelY;
        const char *profileText = hemisphericalProfile ? "CONCAVE" : "CONVEX";
        tft_fill_rect(textX - 2, textY - 8, maxTextW + 6, 40, TFT_NAVY);
        tft_set_cursor(textX, textY); tft_print(profileText);
        break;
    }
    case 4:
        drawParamBoxUnified(fieldLabels[4], hemisphericalRadius, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 5:
        drawParamBoxUnified(fieldLabels[5], hemisphericalPassDepth, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 6:
        drawParamBoxUnified(fieldLabels[6], hemisphericalFeedRate, labelX, labelY, w, state, 1, cx, boxCenterY);
        break;
    case 7:
        drawParamBoxUnified(fieldLabels[7], hemisphericalLastPassDepth, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 8:
        drawParamBoxUnified(fieldLabels[8], hemisphericalLastFeedRate, labelX, labelY, w, state, 1, cx, boxCenterY);
        break;
    case 9:
        drawParamBoxUnified(fieldLabels[9], (float)hemisphericalSpringPasses, labelX, labelY, w, state, 0, cx, boxCenterY);
        break;
    case 10:
        drawCheckbox(fieldLabels[10], hemisphericalUseCSS, labelX, labelY, selectedIndex == index, state);
        break;
    case 11:
    {
        const char *label = hemisphericalUseCSS ? "m/min" : "RPM";
        float value = hemisphericalUseCSS ? hemisphericalSpindleMmin : hemisphericalSpindleRPM;
        drawParamBoxUnified(label, value, labelX, labelY, w, state, 0, cx, boxCenterY);
        break;
    }
    case 12:
    {
        int boxX = cx - w/2, boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2); tft_set_text_size(1);
        tft_set_cursor(labelX, labelY); tft_print(fieldLabels[12]);
        const char *dirText = hemisphericalSpindleCW ? "CW" : "CCW";
        int16_t textWidth = tft_text_width(dirText);
        int16_t textX = cx - textWidth/2, textY = cy - tft_font_height() - 4;
        tft_draw_string(dirText, textX, textY, (state == 1) ? TFT_WHITE : TFT_CYAN, 1);
        break;
    }
    default: break;
    }
}

static void setHemisphericalField(int index, float value)
{
    switch (index)
    {
    case 0: hemisphericalStartZ = value; break;
    case 1: hemisphericalStartX = value * from_input_to_raggio(); break;
    case 2: hemisphericalClearance = value; break;
    case 3: hemisphericalProfile = (value > 0); break;
    case 4: hemisphericalRadius = value; break;
    case 5: hemisphericalPassDepth = value; break;
    case 6: hemisphericalFeedRate = (value < 0.01f) ? 0.01f : value; break;
    case 7: hemisphericalLastPassDepth = value; break;
    case 8: hemisphericalLastFeedRate = value; break;
    case 9: hemisphericalSpringPasses = (value < 0) ? 0 : (int)value; break;
    case 10: hemisphericalUseCSS = (value > 0); break;
    case 11:
        if (hemisphericalUseCSS) hemisphericalSpindleMmin = value;
        else hemisphericalSpindleRPM = value;
        break;
    case 12: hemisphericalSpindleCW = (value > 0); break;
    }
}

static void initHemisphericalDefaults()
{
    hemisphericalStartZ = DRO_Z;
    hemisphericalStartX = DRO_X;
    hemisphericalRadius = 1.0f;
    hemisphericalClearance = 0.5f;
    hemisphericalPassDepth = 0.1f;
    hemisphericalLastPassDepth = 0.02f;
    hemisphericalFeedRate = 200.0f;
    hemisphericalLastFeedRate = 100.0f;
    hemisphericalSpringPasses = 1;
    hemisphericalUseCSS = false;
    hemisphericalSpindleMmin = 75.0f;
    hemisphericalSpindleRPM = 1200.0f;
    hemisphericalSpindleCW = true;
    hemisphericalProfile = true;
    ESP_LOGI(TAG, "Hemispherical parameters reset to default");
}

static void drawHemisphericalWarning(const char *msg)
{
    tft_fill_rect(0, 260, 480, 60, TFT_RED);
    drawCenteredTextAt(msg, 240, 290, 2, TFT_WHITE);
    vTaskDelay(pdMS_TO_TICKS(2500));
    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
    drawMachiningButtons();
}

static bool validateHemisphericalParams()
{
    if (hemisphericalRadius <= 0) {
        drawHemisphericalWarning("Radius deve essere > 0");
        return false;
    }
    if (hemisphericalClearance < 0) {
        drawHemisphericalWarning("Retract deve essere ≥ 0");
        return false;
    }
    if (hemisphericalPassDepth <= 0) {
        drawHemisphericalWarning("Pass Depth > 0");
        return false;
    }
    if (hemisphericalFeedRate <= 0) {
        drawHemisphericalWarning("Feed Rate > 0");
        return false;
    }
    if (hemisphericalUseCSS) {
        if (hemisphericalSpindleMmin <= 0) {
            drawHemisphericalWarning("Velocità superficiale (m/min) > 0");
            return false;
        }
        float rpm = (hemisphericalSpindleMmin * 1000.0f) / (M_PI * (hemisphericalStartX * 2.0f));
        if (rpm > maxSpindleRPM) {
            drawHemisphericalWarning("m/min troppo alto per il diametro attuale");
            return false;
        }
    } else {
        if (hemisphericalSpindleRPM <= 0) {
            drawHemisphericalWarning("Spindle RPM > 0");
            return false;
        }
    }
    return true;
}

static void drawHemisphericalInputPreview(int index, const std::string &buffer, bool blinkBorder)
{
    int cx, cy, w, h, offsetX, decimals;
    getHemisphericalFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getHemisphericalFieldState(index, hemisphericalSelectedField);
    uint16_t fg, bg, border;
    if (state == 0) { fg = TFT_CYAN; bg = TFT_BLACK; border = TFT_YELLOW; }
    else if (state == 1) { fg = TFT_WHITE; bg = TFT_BLACK; border = TFT_YELLOW; }
    else { fg = TFT_RED; bg = TFT_BLACK; border = TFT_YELLOW; }
    cy = cy - h/2;
    drawInputPreviewBox(cx, cy, w, h, buffer, blinkBorder, bg, fg, border);
}

static void handleHemisphericalNumericInput(int index)
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
            setHemisphericalField(index, newValue);
            drawHemisphericalField(index, hemisphericalSelectedField);
            drawMachiningButtons();
            editing = false;
            break;
        }
        if (key == 'B') {
            drawHemisphericalField(index, hemisphericalSelectedField);
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
            drawHemisphericalInputPreview(index, buffer, blinkState);
        }
        if (millis_idf() - lastBlink > 400) {
            lastBlink = millis_idf();
            blinkState = !blinkState;
            drawHemisphericalInputPreview(index, buffer, blinkState);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void printHemisphericalParams()
{
    ESP_LOGI(TAG, "Hemispherical Parameters:");
    ESP_LOGI(TAG, "Profile: %s", hemisphericalProfile ? "CONCAVE" : "CONVEX");
    ESP_LOGI(TAG, "Start Z: %.3f", hemisphericalStartZ);
    ESP_LOGI(TAG, "Start X (radius): %.3f", hemisphericalStartX);
    float centerX, centerZ;
    if (hemisphericalProfile) {
        centerX = hemisphericalStartX;
        centerZ = hemisphericalStartZ;
    } else {
        centerX = hemisphericalStartX - hemisphericalRadius;
        centerZ = hemisphericalStartZ - hemisphericalRadius;
    }
    ESP_LOGI(TAG, "Center X: %.3f, Center Z: %.3f", centerX, centerZ);
    ESP_LOGI(TAG, "Radius: %.3f", hemisphericalRadius);
    ESP_LOGI(TAG, "Retract: %.3f", hemisphericalClearance);
    ESP_LOGI(TAG, "Pass depth: %.3f", hemisphericalPassDepth);
    ESP_LOGI(TAG, "Feed rate: %.1f", hemisphericalFeedRate);
    ESP_LOGI(TAG, "Last pass: %.3f", hemisphericalLastPassDepth);
    ESP_LOGI(TAG, "Last feed: %.1f", hemisphericalLastFeedRate);
    ESP_LOGI(TAG, "Spring passes: %d", hemisphericalSpringPasses);
    ESP_LOGI(TAG, "CSS: %s", hemisphericalUseCSS ? "ON" : "OFF");
    ESP_LOGI(TAG, "Spindle: %.0f %s", hemisphericalUseCSS ? hemisphericalSpindleMmin : hemisphericalSpindleRPM,
             hemisphericalUseCSS ? "m/min" : "RPM");
    ESP_LOGI(TAG, "Direction: %s", hemisphericalSpindleCW ? "CW" : "CCW");
}

static void updateHemisphericalRealtimeFields()
{
    static float lastZ = -9999, lastRadius = -9999;
    float newZ = DRO_Z, newRadius = DRO_X;
    if (fabs(newZ - lastZ) > 0.001f) {
        hemisphericalStartZ = newZ;
        drawHemisphericalField(0, hemisphericalSelectedField);
        lastZ = newZ;
    }
    if (fabs(newRadius - lastRadius) > 0.001f) {
        hemisphericalStartX = newRadius;
        drawHemisphericalField(1, hemisphericalSelectedField);
        lastRadius = newRadius;
    }
}

static void generateHemisphericalGcode()
{
    HemisphericalParams hp;
    hp.startZ = hemisphericalStartZ;
    hp.startX = hemisphericalStartX;
    hp.clearance = hemisphericalClearance;
    hp.profileType = hemisphericalProfile;
    hp.radius = hemisphericalRadius;
    hp.passDepth = hemisphericalPassDepth;
    hp.feedRate = hemisphericalFeedRate;
    hp.lastPassDepth = hemisphericalLastPassDepth;
    hp.lastFeedRate = hemisphericalLastFeedRate;
    hp.springPasses = hemisphericalSpringPasses;
    hp.useCSS = hemisphericalUseCSS;
    hp.spindleMmin = hemisphericalSpindleMmin;
    hp.spindleRPM = hemisphericalSpindleRPM;
    hp.spindleCW = hemisphericalSpindleCW;

    GcodeBlock block = buildHemisphericalBlock(hp, true);
    GcodeGenerator(block);
}

static void drawHemisphericalScene()
{
    char *stopSpindle = strdup("M5\n");
    if (stopSpindle && xQueueSend(gcodeQueue, &stopSpindle, 0) != pdTRUE) {
        free(stopSpindle);
        ESP_LOGE(TAG, "Coda piena per M5");
    }

    initHemisphericalDefaults();

    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("HEMISPHERICAL", 240, 15, 2, TFT_YELLOW);
    tft_fill_rect(20, 30, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 72, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 144, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 216, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 258, 440, 1, TFT_WHITE);

    preload_hemispherical_image();
    draw_hemispherical_image(240, 75, 217, 68);

    resetTextStyle();
    for (int i = 0; i < hemisphericalTotalFields; ++i)
        drawHemisphericalField(i, hemisphericalSelectedField);

    drawMachiningButtons();
}

static void editHemisphericalParams()
{
    hemisphericalSelectedField = 3;
    needsRedraw = true;
    while (true) {
        updateHemisphericalRealtimeFields();
        if (needsRedraw) {
            for (int i = 0; i < hemisphericalTotalFields; ++i)
                drawHemisphericalField(i, hemisphericalSelectedField);
            needsRedraw = false;
        }
        char key = readKeypad();
        if (key == 'D') {
            resetTextStyle();
            DisplayManager();
            return;
        }
        if (key == 'A') {
            if (validateHemisphericalParams()) {
                bool useCoolant = askCoolant();
                coolantEnabled = useCoolant;
                generateHemisphericalGcode();
                return;
            }
            continue;
        }
        if (key == 'B') {
            printHemisphericalParams();
            continue;
        }
        if (key == '1') {
            int next = hemisphericalSelectedField;
            do {
                next = (next - 1 + hemisphericalTotalFields) % hemisphericalTotalFields;
            } while (getHemisphericalFieldState(next, next) < 0);
            hemisphericalSelectedField = next;
            needsRedraw = true;
            continue;
        }
        if (key == '2') {
            int next = hemisphericalSelectedField;
            do {
                next = (next + 1) % hemisphericalTotalFields;
            } while (getHemisphericalFieldState(next, next) < 0);
            hemisphericalSelectedField = next;
            needsRedraw = true;
            continue;
        }
        if (key == 'C') {
            int state = getHemisphericalFieldState(hemisphericalSelectedField, hemisphericalSelectedField);
            if (state < 0) {
                drawHemisphericalWarning("Campo non modificabile");
                continue;
            }
            switch (hemisphericalSelectedField) {
            case 3:
                hemisphericalProfile = !hemisphericalProfile;
                needsRedraw = true;
                break;
            case 10:
                hemisphericalUseCSS = !hemisphericalUseCSS;
                drawHemisphericalField(11, -1);
                needsRedraw = true;
                break;
            case 12:
                hemisphericalSpindleCW = !hemisphericalSpindleCW;
                needsRedraw = true;
                break;
            default:
                handleHemisphericalNumericInput(hemisphericalSelectedField);
                needsRedraw = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void hemispherical_scene_enter(void)
{
    drawHemisphericalScene();
    editHemisphericalParams();
}

HemisphericalParams getCurrentHemisphericalParams(void)
{
    HemisphericalParams hp;
    hp.startZ = hemisphericalStartZ;
    hp.startX = hemisphericalStartX;
    hp.clearance = hemisphericalClearance;
    hp.profileType = hemisphericalProfile;
    hp.radius = hemisphericalRadius;
    hp.passDepth = hemisphericalPassDepth;
    hp.feedRate = hemisphericalFeedRate;
    hp.lastPassDepth = hemisphericalLastPassDepth;
    hp.lastFeedRate = hemisphericalLastFeedRate;
    hp.springPasses = hemisphericalSpringPasses;
    hp.useCSS = hemisphericalUseCSS;
    hp.spindleMmin = hemisphericalSpindleMmin;
    hp.spindleRPM = hemisphericalSpindleRPM;
    hp.spindleCW = hemisphericalSpindleCW;
    return hp;
}