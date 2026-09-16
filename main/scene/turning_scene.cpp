// turning_scene.cpp
#include "turning_scene.h"
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

static const char *TAG = "TURNING";

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
// Parametri di lavorazione (statici) – TUTTI IN RAGGIO
// -------------------------------------------------------------------
static int TurningSelectedField = 0;
static const int TurningTotalFields = 16;

static float turningStartZ = 10.0f;
static float turningLength = 10.0f;
static float turningOldLength = 0.0f;
static float turningClearanceX = 0.5f;    // raggio di sicurezza
static float turningActualRadius = 10.0f; // raggio attuale (DRO_X)
static float turningTargetRadius = 8.0f;  // raggio target
static float turningPassDepth = 0.1f;     // profondità di passata in raggio
static float turningLastPassDepth = 0.02f;
static float turningFeedRate = 200.0f;
static float turningLastFeedRate = 100.0f;
static float turningClearanceZ = 0.5f;
static float turningSpringPasses = 2.0f;
static float turningTaperEnable = 0.0f;
static float turningAngle = 0.0f;
static float turningSpindleRPM = 1200.0f;
static float turningSpindleMmin = 75.0f;
static bool turningUseCSS = false;
static bool turningSpindleCW = true;
static bool needsRedraw = false;

// Array centralizzato delle label (base)
static const char *fieldLabels[] = {
    "Z actual",      // 0
    "Z Length",      // 1
    "X Retract",     // 2
    "X actual",      // 3
    "X target",      // 4
    "Z Retract",     // 5
    "Taper",         // 6
    "Angle",         // 7
    "Pass Depth",    // 8
    "Feed Rate",     // 9
    "Last Pass",     // 10
    "Last Feed",     // 11
    "Spring Passes", // 12
    "CSS",           // 13
    "RPM",           // 14 (etichetta statica, poi dinamicamente diventa "m/min" se CSS)
    "m/min",         // 15 (etichetta alternativa per CSS)
    "Direction"      // 16
};

// -------------------------------------------------------------------
// Conversioni UI (diametro/raggio) – solo per visualizzazione e input
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
static void getTurningFieldBoxGeometry(int index, int &centerX, int &centerY, int &w, int &h, int &offsetX, int &decimals)
{
    const int colX[] = {128, 278, 425};
    const int rowY[] = {57, 87, 117, 147, 177, 207};
    w = (index == 12 || index == 15) ? 36 : 70;
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
        break;
    case 7:
        centerX = colX[1];
        centerY = rowY[2] + 19;
        break;
    case 8:
        centerX = colX[0];
        centerY = rowY[3] + 30;
        offsetX = 108;
        break;
    case 9:
        centerX = colX[1];
        centerY = rowY[3] + 30;
        break;
    case 10:
        centerX = colX[0];
        centerY = rowY[4] + 30;
        offsetX = 108;
        break;
    case 11:
        centerX = colX[1];
        centerY = rowY[4] + 30;
        break;
    case 12:
        centerX = colX[2] + 15;
        centerY = rowY[4] + 30;
        offsetX = 110;
        decimals = 0;
        break;
    case 13:
        centerX = colX[0] + 80;
        centerY = rowY[5] + 42;
        break;
    case 14:
        centerX = colX[1];
        centerY = rowY[5] + 42;
        offsetX = 80;
        break;
    case 15:
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
static int getTurningFieldState(int index, int selectedIndex)
{
    if (index == 0)
        return -2; // Z actual (DRO)
    if (index == 3)
        return -2; // X actual (DRO)
    if (index == 1 && turningTaperEnable > 0)
        return -2; // Length disabilitato durante taper
    if (index == 7 && turningTaperEnable == 0)
        return -1; // Angle disabilitato se taper off
    if (selectedIndex == index)
        return 1;
    return 0;
}

// -------------------------------------------------------------------
// Disegno di un singolo campo
// -------------------------------------------------------------------
static void drawTurningField(int index, int selectedIndex)
{
    resetTextStyle();
    int cx, cy, w, h, offsetX, decimals;
    getTurningFieldBoxGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getTurningFieldState(index, selectedIndex);
    int labelX = cx - offsetX;
    int labelY = cy - tft_font_height() - 4;
    int boxCenterY = cy - h / 2;

    switch (index)
    {
    case 0:
        drawParamBoxUnified(fieldLabels[0], turningStartZ, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 1:
        drawParamBoxUnified(fieldLabels[1], turningLength, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 2:
        drawParamBoxUnified(fieldLabels[2], turningClearanceX, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 3:
    {
        // Box personalizzato per X actual con suffisso D/R
        int boxX = cx - w / 2;
        int boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2);
        tft_set_text_size(1);
        tft_set_cursor(labelX, labelY);
        tft_print(fieldLabels[3]);

        float displayValue = turningActualRadius * to_display();
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
        tft_print(fieldLabels[4]);

        float displayValue = turningTargetRadius * to_display();
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
        drawParamBoxUnified(fieldLabels[5], turningClearanceZ, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 6:
        drawCheckbox(fieldLabels[6], turningTaperEnable > 0, labelX, labelY, selectedIndex == index, state);
        break;
    case 7:
        drawParamBoxUnified(fieldLabels[7], turningAngle, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 8:
        drawParamBoxUnified(fieldLabels[8], turningPassDepth, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 9:
        drawParamBoxUnified(fieldLabels[9], turningFeedRate, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 10:
        drawParamBoxUnified(fieldLabels[10], turningLastPassDepth, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 11:
        drawParamBoxUnified(fieldLabels[11], turningLastFeedRate, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 12:
        drawParamBoxUnified(fieldLabels[12], turningSpringPasses, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 13:
        drawCheckbox(fieldLabels[13], turningUseCSS, labelX, labelY, selectedIndex == index, state);
        break;
    case 14:
    {
        const char *rpmLabel = turningUseCSS ? fieldLabels[15] : fieldLabels[14];
        float rpmValue = turningUseCSS ? turningSpindleMmin : turningSpindleRPM;
        drawParamBoxUnified(rpmLabel, rpmValue, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    }
    case 15:
    {
        // Direction (CW/CCW) – box manuale
        int boxX = cx - w / 2;
        int boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2);
        tft_set_text_size(1);
        tft_set_cursor(labelX, labelY);
        tft_print(fieldLabels[16]);

        const char *dirText = turningSpindleCW ? "CW" : "CCW";
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
// Setter dei campi (input utente)
// -------------------------------------------------------------------
static void setTurningField(int index, float value)
{
    switch (index)
    {
    case 0:
        turningStartZ = value;
        break;
    case 1:
        turningLength = value;
        break;
    case 2:
        turningClearanceX = value;
        break;
    case 3:
        turningActualRadius = value;
        break;
    case 4:
        // Input in diametro o raggio → converti in raggio interno
        turningTargetRadius = value * from_input_to_raggio();
        break;
    case 5:
        turningClearanceZ = value;
        break;
    case 6:
        turningTaperEnable = (value > 0) ? value : 0;
        break;
    case 7:
        turningAngle = value;
        break;
    case 8:
        turningPassDepth = value;
        break;
    case 9:
        turningFeedRate = (value < 0.01f) ? 0.01f : value;
        break;
    case 10:
        turningLastPassDepth = value;
        break;
    case 11:
        turningLastFeedRate = value;
        break;
    case 12:
        turningSpringPasses = ((int)value < 0) ? 0 : (int)value;
        break;
    case 13:
        turningUseCSS = (value > 0);
        break;
    case 14:
        if (turningUseCSS)
            turningSpindleMmin = value;
        else
            turningSpindleRPM = value;
        break;
    case 15:
        turningSpindleCW = (value > 0);
        break;
    }
}

// -------------------------------------------------------------------
// Inizializzazione valori di default
// -------------------------------------------------------------------
static void initTurningDefaults()
{
    turningStartZ = 10.0f;
    turningLength = 10.0f;
    turningClearanceX = 0.5f;
    turningActualRadius = 10.0f;
    turningTargetRadius = 8.0f;
    turningPassDepth = 0.1f;
    turningLastPassDepth = 0.02f;
    turningFeedRate = 200.0f;
    turningLastFeedRate = 100.0f;
    turningClearanceZ = 0.5f;
    turningSpringPasses = 2.0f;
    turningTaperEnable = 0.0f;
    turningAngle = 0.0f;
    turningSpindleRPM = 1200.0f;
    turningSpindleMmin = 75.0f;
    turningUseCSS = false;
    turningSpindleCW = true;
    ESP_LOGI(TAG, "Turning parameters reset to default.");
}

// -------------------------------------------------------------------
// Messaggi di warning
// -------------------------------------------------------------------
static void drawTurningWarning(const char *msg)
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
static bool validateTurningParams()
{
    if (turningLength <= 0)
    {
        drawTurningWarning("Length deve essere > 0");
        return false;
    }
    if (turningClearanceX < 0)
    {
        drawTurningWarning("Retract X deve essere ≥ 0");
        return false;
    }
    if (fabs(turningActualRadius - turningTargetRadius) < 0.001f)
    {
        drawTurningWarning("Raggi identici: nessuna lavorazione");
        return false;
    }
    if (turningPassDepth <= 0)
    {
        drawTurningWarning("Pass Depth deve essere > 0");
        return false;
    }
    if (turningFeedRate <= 0)
    {
        drawTurningWarning("Feed Rate deve essere > 0");
        return false;
    }
    if (turningUseCSS)
    {
        if (turningSpindleMmin <= 0)
        {
            drawTurningWarning("Velocità superficiale (m/min) deve essere > 0");
            return false;
        }
        float raggioCritico = std::min(turningActualRadius, turningTargetRadius);
        float diametroCritico = raggioCritico * 2.0f;
        float rpm = (turningSpindleMmin * 1000.0f) / (M_PI * diametroCritico);
        if (rpm > maxSpindleRPM)
        {
            float cssMax = (maxSpindleRPM * M_PI * diametroCritico) / 1000.0f;
            char buf[80];
            snprintf(buf, sizeof(buf), "m/min troppo alto: limite ≈ %.1f", cssMax);
            drawTurningWarning(buf);
            return false;
        }
    }
    else
    {
        if (turningSpindleRPM <= 0)
        {
            drawTurningWarning("Spindle RPM deve essere > 0");
            return false;
        }
    }
    if (turningTaperEnable > 0 && turningAngle < 0.1f)
    {
        drawTurningWarning("Angolo conicità troppo basso");
        return false;
    }
    return true;
}

// -------------------------------------------------------------------
// Preview input (box lampeggiante)
// -------------------------------------------------------------------
static void drawInputPreview(int index, const std::string &buffer, bool blinkBorder)
{
    int cx, cy, w, h, offsetX, decimals;
    getTurningFieldBoxGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getTurningFieldState(index, TurningSelectedField);
    uint16_t fg, bg, border;
    if (state == 0) { fg = TFT_CYAN; bg = TFT_BLACK; border = TFT_YELLOW; }
    else if (state == 1) { fg = TFT_WHITE; bg = TFT_BLACK; border = TFT_YELLOW; }
    else { fg = TFT_RED; bg = TFT_BLACK; border = TFT_YELLOW; }
    cy = cy - h / 2;
    drawInputPreviewBox(cx, cy, w, h, buffer, blinkBorder, bg, fg, border);
}

// -------------------------------------------------------------------
// Gestione input numerico
// -------------------------------------------------------------------
static void handleNumericInput(int index)
{
    if (index == 0) return; // DRO non modificabile
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
            setTurningField(index, newValue);
            drawTurningField(index, TurningSelectedField);
            drawMachiningButtons();
            editing = false;
            break;
        }
        if (key == 'B') {
            drawTurningField(index, TurningSelectedField);
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
// Stampa parametri su seriale (debug)
// -------------------------------------------------------------------
static void printTurningParams()
{
    ESP_LOGI(TAG, "Parametri Turning:");
    ESP_LOGI(TAG, "Length: %.3f", turningLength);
    ESP_LOGI(TAG, "Retract X: %.3f", turningClearanceX);
    ESP_LOGI(TAG, "Actual Radius: %.3f", turningActualRadius);
    ESP_LOGI(TAG, "Target Radius: %.3f", turningTargetRadius);
    ESP_LOGI(TAG, "Retract Z: %.3f", turningClearanceZ);
    ESP_LOGI(TAG, "Taper angle: %.3f", turningAngle);
    ESP_LOGI(TAG, "Pass depth: %.3f", turningPassDepth);
    ESP_LOGI(TAG, "Feed rate: %.3f", turningFeedRate);
    ESP_LOGI(TAG, "Last pass: %.3f", turningLastPassDepth);
    ESP_LOGI(TAG, "Last feed: %.3f", turningLastFeedRate);
    ESP_LOGI(TAG, "Spring passes: %.0f", turningSpringPasses);
    ESP_LOGI(TAG, "CSS: %s", turningUseCSS ? "ON" : "OFF");
    ESP_LOGI(TAG, "Spindle: %.0f %s", turningUseCSS ? turningSpindleMmin : turningSpindleRPM, turningUseCSS ? "m/min" : "RPM");
    ESP_LOGI(TAG, "Direction: %s", turningSpindleCW ? "CW" : "CCW");
}

// -------------------------------------------------------------------
// Aggiornamento campi in tempo reale (DRO)
// -------------------------------------------------------------------
static void updateTurningRealtimeFields()
{
    static float lastZ = -9999;
    static float lastRadius = -9999;
    float newZ = DRO_Z;
    float newRadius = DRO_X; // DRO_X è già il raggio
    if (fabs(newZ - lastZ) > 0.001f)
    {
        turningStartZ = newZ;
        drawTurningField(0, TurningSelectedField);
        lastZ = newZ;
    }
    if (fabs(newRadius - lastRadius) > 0.001f)
    {
        turningActualRadius = newRadius;
        drawTurningField(3, TurningSelectedField);
        lastRadius = newRadius;
    }
}

// -------------------------------------------------------------------
// Generatore G‑code (usando il modulo GenerateGcode)
// -------------------------------------------------------------------
static void TurningGcodeGenerator()
{
    // Costruisce i parametri (già in raggi)
    TurningParams tp;
    tp.actualRadius = turningActualRadius;
    tp.targetRadius = turningTargetRadius;
    tp.clearanceX = turningClearanceX;
    tp.clearanceZ = turningClearanceZ;
    tp.FeedRate = turningFeedRate;
    tp.lastFeedRate = turningLastFeedRate;
    tp.passDepth = turningPassDepth;
    tp.lastPassDepth = turningLastPassDepth;
    tp.Length = turningLength;
    tp.springPasses = (int)turningSpringPasses;
    tp.startZ = turningStartZ;
    tp.angle = turningAngle;
    tp.useCSS = turningUseCSS;
    tp.spindleCW = turningSpindleCW;
    tp.spindleRPM = turningSpindleRPM;
    tp.spindleMmin = turningSpindleMmin;

    GcodeBlock block = buildTurningBlock(tp, true);
    GcodeGenerator(block);
}

// -------------------------------------------------------------------
// Disegno scena
// -------------------------------------------------------------------
void drawTurningScene()
{
    // Arresta mandrino
    char *stopSpindle = strdup("M5\n");
    if (stopSpindle && xQueueSend(gcodeQueue, &stopSpindle, 0) != pdTRUE)
    {
        free(stopSpindle);
        ESP_LOGE(TAG, "Coda piena per M5");
    }
    initTurningDefaults();
    turningStartZ = DRO_Z;
    turningActualRadius = DRO_X;

    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    tft_set_text_color(TFT_YELLOW);
    tft_set_text_size(2);
    tft_set_text_datum(MC_DATUM);
    drawCenteredTextAt("TURNING", 240, 15, 2, TFT_YELLOW);
    tft_fill_rect(20, 30, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 72, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 144, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 216, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 258, 440, 1, TFT_WHITE);
    resetTextStyle();
    for (int i = 0; i < TurningTotalFields; ++i)
    {
        drawTurningField(i, TurningSelectedField);
    }
    drawMachiningButtons();
}

// -------------------------------------------------------------------
// Loop di editing parametri
// -------------------------------------------------------------------
void drawTurningParams()
{
    // Imposta il primo campo modificabile (Length, indice 1)
    TurningSelectedField = 1;
    needsRedraw = true;
    while (true)
    {
        updateTurningRealtimeFields();
        if (needsRedraw)
        {
            drawTurningField(TurningSelectedField, TurningSelectedField);
            needsRedraw = false;
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
            if (validateTurningParams())
            {
                bool useCoolant = askCoolant();
                bool oldCoolant = coolantEnabled;
                coolantEnabled = useCoolant;
                TurningGcodeGenerator();
                return;
            }
            continue;
        }
        if (key == 'B')
        {
            printTurningParams();
            continue;
        }
        if (key == '1')
        {
            drawTurningField(TurningSelectedField, -1);
            int next = TurningSelectedField;
            do
            {
                next = (next - 1 + TurningTotalFields) % TurningTotalFields;
            } while (getTurningFieldState(next, next) < 1);
            TurningSelectedField = next;
            needsRedraw = true;
            continue;
        }
        if (key == '2')
        {
            drawTurningField(TurningSelectedField, -1);
            int next = TurningSelectedField;
            do
            {
                next = (next + 1) % TurningTotalFields;
            } while (getTurningFieldState(next, next) < 1);
            TurningSelectedField = next;
            needsRedraw = true;
            continue;
        }
        if (key == 'C')
        {
            int state = getTurningFieldState(TurningSelectedField, TurningSelectedField);
            if (state < 0)
            {
                drawTurningWarning("Campo non modificabile");
                continue;
            }
            switch (TurningSelectedField)
            {
            case 6: // Taper checkbox
                if (turningTaperEnable > 0)
                {
                    turningTaperEnable = 0;
                    turningLength = turningOldLength;
                }
                else
                {
                    turningTaperEnable = 2;
                    turningOldLength = turningLength;
                    float rad = turningAngle * M_PI / 180.0f;
                    turningLength = fabs((turningActualRadius - turningTargetRadius) / (2.0f * tan(rad)));
                }
                drawTurningField(7, -1);
                drawTurningField(1, -1);
                needsRedraw = true;
                break;
            case 13: // CSS checkbox
                turningUseCSS = !turningUseCSS;
                tft_fill_rect(140, 217, 100, 40, TFT_NAVY);
                drawTurningField(14, -1);
                needsRedraw = true;
                break;
            case 15: // Direction checkbox
                turningSpindleCW = !turningSpindleCW;
                needsRedraw = true;
                break;
            default:
                handleNumericInput(TurningSelectedField);
                needsRedraw = true;
                if (TurningSelectedField == 7 && turningTaperEnable > 0)
                {
                    float rad = turningAngle * M_PI / 180.0f;
                    turningLength = fabs((turningActualRadius - turningTargetRadius) / (2.0f * tan(rad)));
                    drawTurningField(1, -1);
                }
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// -------------------------------------------------------------------
// Entry point della scena
// -------------------------------------------------------------------
void turning_scene_enter(void)
{
    drawTurningScene();
    drawTurningParams();
}

// ------------------------------------------------------------
// Getter per i parametri correnti (pubblico)
// ------------------------------------------------------------
TurningParams getCurrentTurningParams(void) {
    TurningParams tp;
    tp.actualRadius = turningActualRadius;
    tp.targetRadius = turningTargetRadius;
    tp.clearanceX = turningClearanceX;
    tp.clearanceZ = turningClearanceZ;
    tp.FeedRate = turningFeedRate;
    tp.lastFeedRate = turningLastFeedRate;
    tp.passDepth = turningPassDepth;
    tp.lastPassDepth = turningLastPassDepth;
    tp.Length = turningLength;
    tp.springPasses = (int)turningSpringPasses;
    tp.startZ = turningStartZ;
    tp.angle = turningAngle;
    tp.useCSS = turningUseCSS;
    tp.spindleCW = turningSpindleCW;
    tp.spindleRPM = turningSpindleRPM;
    tp.spindleMmin = turningSpindleMmin;
    return tp;
}