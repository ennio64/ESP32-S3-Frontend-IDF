// facing_scene.cpp (completo)
#include "facing_scene.h"
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

static const char *TAG = "FACING";

#ifndef MC_DATUM
#define MC_DATUM 4
#endif
#ifndef ML_DATUM
#define ML_DATUM 3
#endif

static unsigned long millis_idf()
{
    return (unsigned long)(esp_timer_get_time() / 1000);
}

// -------------------------------------------------------------------
// Parametri di lavorazione (statici) – valori interni in raggio per X
// -------------------------------------------------------------------
static int facingSelectedField = 0;
static const int facingTotalFields = 17;

static float facingStartZ = 1.5f;
static float facingTargetZ = -0.1f;
static float facingClearanceX = 0.5f;
static float facingStartRadius = 5.0f;     // raggio iniziale (DRO_X)
static float facingEndRadius = 0.0f;
static float facingClearanceZ = 0.5f;
static bool facingChamferEnable = false;
static bool facingBilletEnable = false;
static float facingAddProfileDistance = 0.0f;
static float facingPassDepth = 0.1f;
static float facingLastPassDepth = 0.02f;
static float facingFeedRate = 200.0f;
static float facingLastFeedRate = 100.0f;
static int facingSpringPasses = 2;
static bool facingUseCSS = false;
static float facingSpindleMmin = 75.0f;
static float facingSpindleRPM = 500.0f;
static bool facingSpindleCW = true;

static bool facingNeedsRedraw = false;

// Array centralizzato delle label (base)
static const char *facingFieldLabels[] = {
    "Z current",     // 0
    "Z target",      // 1
    "X Retract",     // 2
    "X current",     // 3
    "X target",      // 4
    "Z Retract",     // 5
    "Chamfer",       // 6
    "Billet 30",    // 7
    "Distance",      // 8
    "Pass Depth",    // 9
    "Feed Rate",     // 10
    "Last Pass",     // 11
    "Last Feed",     // 12
    "Spring Passes", // 13
    "CSS",           // 14
    "RPM/m/min",     // 15 (label statica, ma dinamicamente mostrerà RPM o m/min)
    "Direction"      // 16
};

enum FacingAddProfileType
{
    PROFILE_NONE = 0,
    PROFILE_CHAMFER = 1,
    PROFILE_BILLET = -1
};
static FacingAddProfileType facingAddProfileType = PROFILE_NONE;

// -------------------------------------------------------------------
// Conversioni diametro/raggio (interfaccia utente)
// -------------------------------------------------------------------
static inline float to_display()
{
    return (settings_get_xmode() == 0) ? 2.0f : 1.0f;
}
static inline float from_input_to_raggio()
{
    return (settings_get_xmode() == 0) ? 0.5f : 1.0f;
}

// -------------------------------------------------------------------
// Geometria dei campi (coordinate centri, dimensioni, offset label, decimali)
// -------------------------------------------------------------------
static void getFacingFieldGeometry(int index, int &centerX, int &centerY, int &w, int &h, int &offsetX, int &decimals)
{
    const int colX[] = {128, 278, 425};
    const int rowY[] = {57, 87, 117, 147, 177, 207};
    w = (index == 13 || index == 16) ? 36 : 70;
    h = 24;
    offsetX = 100;
    decimals = 3;
    switch (index)
    {
    case 0:
        centerX = colX[0];
        centerY = rowY[0] + 7;
        offsetX = 108;
        break;
    case 1:
        centerX = colX[1];
        centerY = rowY[0] + 7;
        break;
    case 2:
        centerX = colX[2];
        centerY = rowY[0] + 7;
        offsetX = 100;
        break;
    case 3:
        centerX = colX[0];
        centerY = rowY[1] + 19;
        offsetX = 108;
        break;
    case 4:
        centerX = colX[1];
        centerY = rowY[1] + 19;
        break;
    case 5:
        centerX = colX[2];
        centerY = rowY[1] + 19;
        offsetX = 100;
        break;
    case 6:
        centerX = colX[0] + 80;
        centerY = rowY[2] + 19;
        offsetX = 124;
        break;
    case 7:
        centerX = colX[1] + 80;
        centerY = rowY[2] + 19;
        offsetX = 130;
        break;
    case 8:
        centerX = colX[2];
        centerY = rowY[2] + 19;
        break;
    case 9:
        centerX = colX[0];
        centerY = rowY[3] + 30;
        offsetX = 108;
        break;
    case 10:
        centerX = colX[1];
        centerY = rowY[3] + 30;
        break;
    case 11:
        centerX = colX[0];
        centerY = rowY[4] + 30;
        break;
    case 12:
        centerX = colX[1];
        centerY = rowY[4] + 30;
        break;
    case 13:
        centerX = colX[2] + 15;
        centerY = rowY[4] + 30;
        offsetX = 110;
        decimals = 0;
        break;
    case 14:
        centerX = colX[0] + 80;
        centerY = rowY[5] + 42;
        break;
    case 15:
        centerX = colX[1];
        centerY = rowY[5] + 42;
        offsetX = 80;
        break;
    case 16:
        centerX = colX[2] + 15;
        centerY = rowY[5] + 42;
        offsetX = 80;
        decimals = 0;
        break;
    default:
        centerX = -999;
        centerY = -999;
        break;
    }
}

// -------------------------------------------------------------------
// Stato del campo (modificabile, selezionato, DRO, disabilitato)
// -------------------------------------------------------------------
static int getFacingFieldState(int index, int selectedIndex)
{
    if (index == 0) return -2; // Z current (DRO)
    if (index == 3) return -2; // X current (DRO)
    if (index == 6 && facingBilletEnable) return -1;
    if (index == 7 && facingChamferEnable) return -1;
    if (index == 8 && !(facingChamferEnable || facingBilletEnable)) return -1;
    if (selectedIndex == index) return 1;
    return 0;
}

// -------------------------------------------------------------------
// Disegno di un singolo campo
// -------------------------------------------------------------------
static void drawFacingField(int index, int selectedIndex)
{
    resetTextStyle();
    int cx, cy, w, h, offsetX, decimals;
    getFacingFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getFacingFieldState(index, selectedIndex);
    int labelX = cx - offsetX;
    int labelY = cy - tft_font_height() - 4;
    int boxCenterY = cy - h / 2;
    switch (index)
    {
    case 0:
        drawParamBoxUnified(facingFieldLabels[0], facingStartZ, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 1:
        drawParamBoxUnified(facingFieldLabels[1], facingTargetZ, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 2:
        drawParamBoxUnified(facingFieldLabels[2], facingClearanceX, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 3:
    {
        int boxX = cx - w / 2;
        int boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2);
        tft_set_text_size(1);
        tft_set_cursor(labelX, labelY);
        tft_print(facingFieldLabels[3]);

        float displayValue = facingStartRadius * to_display();
        char numBuf[16];
        snprintf(numBuf, sizeof(numBuf), "%.3f", displayValue);
        const char *suffix = (settings_get_xmode() == 0) ? "D" : "R";
        int16_t numWidth = tft_text_width(numBuf);
        int16_t suffixWidth = tft_text_width(suffix);
        int16_t totalWidth = numWidth + 2 + suffixWidth;
        int16_t startX = cx - totalWidth / 2;
        int16_t textY = cy - 20;
        uint16_t numColor = (state == -2) ? TFT_GREEN : ((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_set_text_color(numColor);
        tft_draw_string(numBuf, startX, textY, numColor, 1);
        tft_set_text_color(TFT_ORANGE);
        tft_draw_string(suffix, startX + numWidth + 2, textY, TFT_ORANGE, 1);
        break;
    }
    case 4:
    {
        int boxX = cx - w / 2;
        int boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2);
        tft_set_text_size(1);
        tft_set_cursor(labelX, labelY);
        tft_print(facingFieldLabels[4]);

        float displayValue = facingEndRadius * to_display();
        char numBuf[16];
        snprintf(numBuf, sizeof(numBuf), "%.3f", displayValue);
        const char *suffix = (settings_get_xmode() == 0) ? "D" : "R";
        int16_t numWidth = tft_text_width(numBuf);
        int16_t suffixWidth = tft_text_width(suffix);
        int16_t totalWidth = numWidth + 2 + suffixWidth;
        int16_t startX = cx - totalWidth / 2;
        int16_t textY = cy - 20;
        uint16_t numColor = (state == -2) ? TFT_GREEN : ((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_set_text_color(numColor);
        tft_draw_string(numBuf, startX, textY, numColor, 1);
        tft_set_text_color(TFT_ORANGE);
        tft_draw_string(suffix, startX + numWidth + 2, textY, TFT_ORANGE, 1);
        break;
    }
    case 5:
        drawParamBoxUnified(facingFieldLabels[5], facingClearanceZ, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 6:
        drawCheckbox(facingFieldLabels[6], facingChamferEnable, labelX, labelY, selectedIndex == index, state);
        break;
    case 7:
        drawCheckbox(facingFieldLabels[7], facingBilletEnable, labelX, labelY, selectedIndex == index, state);
        break;
    case 8:
        drawParamBoxUnified(facingFieldLabels[8], facingAddProfileDistance, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 9:
        drawParamBoxUnified(facingFieldLabels[9], facingPassDepth, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 10:
        drawParamBoxUnified(facingFieldLabels[10], facingFeedRate, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 11:
        drawParamBoxUnified(facingFieldLabels[11], facingLastPassDepth, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 12:
        drawParamBoxUnified(facingFieldLabels[12], facingLastFeedRate, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 13:
        drawParamBoxUnified(facingFieldLabels[13], (float)facingSpringPasses, labelX, labelY, w, state, 0, cx, boxCenterY);
        break;
    case 14:
        drawCheckbox(facingFieldLabels[14], facingUseCSS, labelX, labelY, selectedIndex == index, state);
        break;
    case 15:
    {
        const char *rpmLabel = facingUseCSS ? "m/min" : "RPM";
        float rpmValue = facingUseCSS ? facingSpindleMmin : facingSpindleRPM;
        drawParamBoxUnified(rpmLabel, rpmValue, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    }
    case 16:
    {
        int boxX = cx - w / 2;
        int boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2);
        tft_set_text_size(1);
        tft_set_cursor(labelX, labelY);
        tft_print(facingFieldLabels[16]);

        const char *dirText = facingSpindleCW ? "CW" : "CCW";
        int16_t textWidth = tft_text_width(dirText);
        int16_t textHeight = tft_font_height();
        int16_t textX = cx - textWidth / 2;
        int16_t textY = cy - textHeight - 4;
        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_draw_string(dirText, textX, textY, state == 1 ? TFT_WHITE : TFT_CYAN, 1);
        break;
    }
    }
}

// -------------------------------------------------------------------
// Setter dei campi
// -------------------------------------------------------------------
static void setFacingField(int index, float value)
{
    switch (index)
    {
    case 0: facingStartZ = value; break;
    case 1: facingTargetZ = value; break;
    case 2: facingClearanceX = value; break;
    case 3: facingStartRadius = value; break;
    case 4:
        facingEndRadius = value * from_input_to_raggio();
        break;
    case 5: facingClearanceZ = value; break;
    case 6: facingChamferEnable = (value > 0); break;
    case 7: facingBilletEnable = (value > 0); break;
    case 8: facingAddProfileDistance = value; break;
    case 9: facingPassDepth = value; break;
    case 10: facingFeedRate = (value < 0.01f) ? 0.01f : value; break;
    case 11: facingLastPassDepth = value; break;
    case 12: facingLastFeedRate = value; break;
    case 13: facingSpringPasses = ((int)value < 0) ? 0 : (int)value; break;
    case 14: facingUseCSS = (value > 0); break;
    case 15:
        if (facingUseCSS) facingSpindleMmin = value;
        else facingSpindleRPM = value;
        break;
    case 16: facingSpindleCW = (value > 0); break;
    }
}

// -------------------------------------------------------------------
// Inizializzazione valori di default
// -------------------------------------------------------------------
static void initFacingDefaults()
{
    facingStartZ = 1.5f;
    facingTargetZ = -0.1f;
    facingClearanceX = 0.5f;
    facingStartRadius = 5.0f;
    facingEndRadius = 0.0f;
    facingClearanceZ = 0.5f;
    facingChamferEnable = false;
    facingBilletEnable = false;
    facingAddProfileDistance = 0.0f;
    facingPassDepth = 0.1f;
    facingLastPassDepth = 0.02f;
    facingFeedRate = 200.0f;
    facingLastFeedRate = 100.0f;
    facingSpringPasses = 2;
    facingUseCSS = false;
    facingSpindleMmin = 75.0f;
    facingSpindleRPM = 500.0f;
    facingSpindleCW = true;
    facingAddProfileType = PROFILE_NONE;
    ESP_LOGI(TAG, "Facing parameters reset to default.");
}

// -------------------------------------------------------------------
// Messaggi di warning (centrati)
// -------------------------------------------------------------------
static void drawFacingWarning(const char *msg)
{
    tft_fill_rect(0, 260, 480, 60, TFT_RED);
    drawCenteredTextAt(msg, 240, 290, 2, TFT_WHITE);
    vTaskDelay(pdMS_TO_TICKS(2500));
    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
    drawMachiningButtons();
}

// -------------------------------------------------------------------
// Validazione parametri
// -------------------------------------------------------------------
static bool validateFacingParams()
{
    if (facingStartZ <= facingTargetZ)
    {
        drawFacingWarning("Start Z deve essere > Target Z");
        return false;
    }
    if (facingStartRadius <= facingEndRadius)
    {
        drawFacingWarning("Raggio iniziale deve essere > raggio finale");
        return false;
    }
    if (facingChamferEnable && facingAddProfileDistance <= 0.0f)
    {
        drawFacingWarning("Chamfer Distance deve essere > 0");
        return false;
    }
    if (facingBilletEnable && facingEndRadius <= 0.0f)
    {
        drawFacingWarning("Billet attivo: Raggio finale deve essere > 0");
        return false;
    }
    if (facingPassDepth <= 0.0f)
    {
        drawFacingWarning("Pass Depth deve essere > 0");
        return false;
    }
    if (facingFeedRate <= 0.0f)
    {
        drawFacingWarning("Feed Rate deve essere > 0");
        return false;
    }
    if (facingUseCSS)
    {
        if (facingSpindleMmin <= 0.0f)
        {
            drawFacingWarning("m/min deve essere > 0");
            return false;
        }
        float rpm = (facingSpindleMmin * 1000.0f) / (3.14159f * 2.0f * facingStartRadius);
        if (rpm > maxSpindleRPM)
        {
            drawFacingWarning("m/min troppo alto per il raggio iniziale");
            return false;
        }
    }
    else
    {
        if (facingSpindleRPM <= 0.0f)
        {
            drawFacingWarning("RPM deve essere > 0");
            return false;
        }
    }
    return true;
}

// -------------------------------------------------------------------
// Input numerico (stessa logica di turning)
// -------------------------------------------------------------------
static void drawInputPreview(int index, const std::string &buffer, bool blinkBorder)
{
    int cx, cy, w, h, offsetX, decimals;
    getFacingFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getFacingFieldState(index, facingSelectedField);
    uint16_t fg, bg, border;
    if (state == 0) { fg = TFT_CYAN; bg = TFT_BLACK; border = TFT_YELLOW; }
    else if (state == 1) { fg = TFT_WHITE; bg = TFT_BLACK; border = TFT_YELLOW; }
    else { fg = TFT_RED; bg = TFT_BLACK; border = TFT_YELLOW; }
    cy = cy - h / 2;
    drawInputPreviewBox(cx, cy, w, h, buffer, blinkBorder, bg, fg, border);
}

static void handleNumericInput(int index)
{
    if (index == 0 || index == 3) return; // DRO non modificabili
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
            setFacingField(index, newValue);
            drawFacingField(index, facingSelectedField);
            drawMachiningButtons();
            editing = false;
            break;
        }
        if (key == 'B') {
            drawFacingField(index, facingSelectedField);
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
            drawInputPreview(index, buffer, blinkState);
        }
        if (millis_idf() - lastBlink > 400) {
            lastBlink = millis_idf();
            blinkState = !blinkState;
            drawInputPreview(index, buffer, blinkState);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// -------------------------------------------------------------------
// Stampa parametri (debug seriale)
// -------------------------------------------------------------------
static void printFacingParams()
{
    ESP_LOGI(TAG, "Parametri Facing:");
    ESP_LOGI(TAG, "Start Z: %.3f, Target Z: %.3f", facingStartZ, facingTargetZ);
    ESP_LOGI(TAG, "Start Radius: %.3f, End Radius: %.3f", facingStartRadius, facingEndRadius);
    ESP_LOGI(TAG, "Clearance X: %.3f, Clearance Z: %.3f", facingClearanceX, facingClearanceZ);
    ESP_LOGI(TAG, "Chamfer: %s, Billet: %s, Distance: %.3f",
             facingChamferEnable ? "ON" : "OFF", facingBilletEnable ? "ON" : "OFF", facingAddProfileDistance);
    ESP_LOGI(TAG, "Pass Depth: %.3f, Last Pass: %.3f", facingPassDepth, facingLastPassDepth);
    ESP_LOGI(TAG, "Feed Rate: %.1f, Last Feed: %.1f", facingFeedRate, facingLastFeedRate);
    ESP_LOGI(TAG, "Spring Passes: %d", facingSpringPasses);
    ESP_LOGI(TAG, "CSS: %s, Spindle: %.0f %s", facingUseCSS ? "ON" : "OFF",
             facingUseCSS ? facingSpindleMmin : facingSpindleRPM, facingUseCSS ? "m/min" : "RPM");
    ESP_LOGI(TAG, "Direction: %s", facingSpindleCW ? "CW" : "CCW");
}

// -------------------------------------------------------------------
// Aggiornamento campi DRO in tempo reale
// -------------------------------------------------------------------
static void updateFacingRealtimeFields()
{
    static float lastZ = -9999;
    static float lastRadius = -9999;
    float newZ = DRO_Z;
    float newRadius = DRO_X;
    if (fabs(newZ - lastZ) > 0.001f)
    {
        facingStartZ = newZ;
        drawFacingField(0, facingSelectedField);
        lastZ = newZ;
    }
    if (fabs(newRadius - lastRadius) > 0.001f)
    {
        facingStartRadius = newRadius;
        drawFacingField(3, facingSelectedField);
        lastRadius = newRadius;
    }
}

// -------------------------------------------------------------------
// Stub generazione G-code
// -------------------------------------------------------------------
static void FacingGcodeGenerator()
{
    FacingParams fp;
    fp.startZ = facingStartZ;
    fp.targetZ = facingTargetZ;
    fp.clearanceX = facingClearanceX;
    fp.clearanceZ = facingClearanceZ;
    fp.startRadius = facingStartRadius;        // già in raggio
    fp.endRadius = facingEndRadius;            // già in raggio
    fp.addProfileType = (facingChamferEnable ? 1 : (facingBilletEnable ? -1 : 0));
    fp.addProfileDistance = facingAddProfileDistance;
    fp.passDepth = facingPassDepth;
    fp.lastPassDepth = facingLastPassDepth;
    fp.feedRate = facingFeedRate;
    fp.lastFeedRate = facingLastFeedRate;
    fp.springPasses = facingSpringPasses;
    fp.useCSS = facingUseCSS;
    fp.spindleMmin = facingSpindleMmin;
    fp.spindleRPM = facingSpindleRPM;
    fp.spindleCW = facingSpindleCW;

    GcodeBlock block = buildFacingBlock(fp, true);
    GcodeGenerator(block);
}

// -------------------------------------------------------------------
// Disegno della scena Facing (sfondo, titolo, linee, campi)
// -------------------------------------------------------------------
void drawFacingScene()
{
    char *stopSpindle = strdup("M5\n");
    if (stopSpindle && xQueueSend(gcodeQueue, &stopSpindle, 0) != pdTRUE)
    {
        free(stopSpindle);
        ESP_LOGE(TAG, "Coda piena per M5");
    }
    initFacingDefaults();
    facingStartZ = DRO_Z;
    facingStartRadius = DRO_X;
    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    tft_set_text_color(TFT_YELLOW);
    tft_set_text_size(2);
    tft_set_text_datum(MC_DATUM);
    drawCenteredTextAt("FACING", 240, 15, 2, TFT_YELLOW);
    tft_fill_rect(20, 30, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 72, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 144, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 216, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 258, 440, 1, TFT_WHITE);
    resetTextStyle();
    for (int i = 0; i < facingTotalFields; ++i)
    {
        drawFacingField(i, facingSelectedField);
    }
    drawMachiningButtons();
}

// -------------------------------------------------------------------
// Loop di modifica parametri (come in turning)
// -------------------------------------------------------------------
void drawFacingParams()
{
    // Imposta il primo campo modificabile (Z target, indice 1)
    facingSelectedField = 1;
    facingNeedsRedraw = true;
    while (true)
    {
        updateFacingRealtimeFields();
        if (facingNeedsRedraw)
        {
            drawFacingField(facingSelectedField, facingSelectedField);
            facingNeedsRedraw = false;
        }
        char key = readKeypad();
        if (key == 'D')
        {
            resetTextStyle();
            //DisplayManager();
            return;
        }
        if (key == 'A')
        {
            if (validateFacingParams())
            {
                bool useCoolant = askCoolant();
                bool oldCoolant = coolantEnabled;
                coolantEnabled = useCoolant;
                FacingGcodeGenerator();
                return;
            }
            continue;
        }
        if (key == 'B')
        {
            printFacingParams();
            continue;
        }
        if (key == '1')
        {
            drawFacingField(facingSelectedField, -1);
            int next = facingSelectedField;
            do
            {
                next = (next - 1 + facingTotalFields) % facingTotalFields;
            } while (getFacingFieldState(next, next) < 1);
            facingSelectedField = next;
            facingNeedsRedraw = true;
            continue;
        }
        if (key == '2')
        {
            drawFacingField(facingSelectedField, -1);
            int next = facingSelectedField;
            do
            {
                next = (next + 1) % facingTotalFields;
            } while (getFacingFieldState(next, next) < 1);
            facingSelectedField = next;
            facingNeedsRedraw = true;
            continue;
        }
        if (key == 'C')
        {
            int state = getFacingFieldState(facingSelectedField, facingSelectedField);
            if (state < 0)
            {
                drawFacingWarning("Campo non modificabile");
                continue;
            }
            switch (facingSelectedField)
            {
            case 6: // Chamfer checkbox
                facingChamferEnable = !facingChamferEnable;
                if (facingChamferEnable)
                {
                    facingBilletEnable = false;
                    facingAddProfileType = PROFILE_CHAMFER;
                }
                else
                {
                    facingAddProfileType = PROFILE_NONE;
                }
                drawFacingField(6, facingSelectedField);
                drawFacingField(7, -1);
                drawFacingField(8, -1);
                facingNeedsRedraw = true;
                break;
            case 7: // Billet checkbox
                facingBilletEnable = !facingBilletEnable;
                if (facingBilletEnable)
                {
                    facingChamferEnable = false;
                    facingAddProfileType = PROFILE_BILLET;
                }
                else
                {
                    facingAddProfileType = PROFILE_NONE;
                }
                drawFacingField(7, facingSelectedField);
                drawFacingField(6, -1);
                drawFacingField(8, -1);
                facingNeedsRedraw = true;
                break;
            case 14: // CSS checkbox
                facingUseCSS = !facingUseCSS;
                tft_fill_rect(140, 217, 100, 40, TFT_NAVY);
                drawFacingField(14, -1);
                drawFacingField(15, -1);
                facingNeedsRedraw = true;
                break;
            case 16: // Direction checkbox
                facingSpindleCW = !facingSpindleCW;
                facingNeedsRedraw = true;
                break;
            default:
                handleNumericInput(facingSelectedField);
                facingNeedsRedraw = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// -------------------------------------------------------------------
// Funzione di ingresso (chiamata da main_scene)
// -------------------------------------------------------------------
void facing_scene_enter(void)
{
    drawFacingScene();
    drawFacingParams();
}

FacingParams getCurrentFacingParams(void) {
    FacingParams fp;
    fp.startZ = facingStartZ;
    fp.targetZ = facingTargetZ;
    fp.clearanceX = facingClearanceX;
    fp.clearanceZ = facingClearanceZ;
    fp.startRadius = facingStartRadius;
    fp.endRadius = facingEndRadius;
    fp.addProfileType = (facingChamferEnable ? 1 : (facingBilletEnable ? -1 : 0));
    fp.addProfileDistance = facingAddProfileDistance;
    fp.passDepth = facingPassDepth;
    fp.lastPassDepth = facingLastPassDepth;
    fp.feedRate = facingFeedRate;
    fp.lastFeedRate = facingLastFeedRate;
    fp.springPasses = facingSpringPasses;
    fp.useCSS = facingUseCSS;
    fp.spindleMmin = facingSpindleMmin;
    fp.spindleRPM = facingSpindleRPM;
    fp.spindleCW = facingSpindleCW;
    return fp;
}