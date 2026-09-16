// parting_scene.cpp (corretto)
#include "parting_scene.h"
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

static const char *TAG = "PARTING";

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
// Parametri di lavorazione (statici)
// cuttingStartX è il raggio (DRO), gli altri valori lineari sono in mm.
// -------------------------------------------------------------------
static int PartingSelectedField = 0;
static const int PartingTotalFields = 11;

static float cuttingStartZ = 0.0f;   // Z corrente (DRO)
static float cuttingStartX = 0.0f;   // X corrente (raggio, DRO)
static float cuttingRetractX = 0.5f; // ritrazione in X (mm lineare)
static float cuttingDepth = 0.0f;    // profondità di taglio (mm lineare)
static float cuttingFeedRate = 80.0f;

static float cuttingPulseEnable = 0.0f;
static float cuttingPulseAdvance = 0.1f;
static float cuttingPulsePause = 0.2f;

static float cuttingSpindleRPM = 1200.0f;
static float cuttingSpindleMmin = 75.0f;
static bool cuttingUseCSS = false;
static bool cuttingSpindleCW = true;

static bool cuttingNeedsRedraw = false;

// Array centralizzato delle label (base)
static const char *cuttingFieldLabels[] = {
    "Z current", // 0
    "X current", // 1
    "X Retract", // 2
    "Depth",     // 3
    "Feed Rate", // 4
    "Pulse Cut", // 5
    "Advance",   // 6
    "Pause(s)",  // 7
    "CSS",       // 8
    "RPM/m/min", // 9 (etichetta dinamica)
    "Direction"  // 10
};

// -------------------------------------------------------------------
// Conversioni diametro/raggio (solo per visualizzazione X current)
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
// Geometria dei campi
// -------------------------------------------------------------------
static void getPartingFieldGeometry(int index, int &centerX, int &centerY, int &w, int &h, int &offsetX, int &decimals)
{
    const int colX[] = {128, 278, 425};
    const int rowY[] = {57, 87, 117, 147, 177, 207};

    w = 70;
    h = 24;
    offsetX = 100;
    decimals = 3;

    switch (index)
    {
    case 0: // Z current
        centerX = colX[0];
        centerY = rowY[0] + 7;
        offsetX = 108;
        break;
    case 1: // X current
        centerX = colX[1];
        centerY = rowY[0] + 7;
        break;
    case 2: // X Retract
        centerX = colX[2];
        centerY = rowY[0] + 7;
        offsetX = 100;
        break;
    case 3: // Depth
        centerX = colX[0];
        centerY = rowY[1] + 19;
        offsetX = 108;
        break;
    case 4: // Feed Rate
        centerX = colX[1];
        centerY = rowY[1] + 19;
        break;
    case 5: // Pulse Cut (checkbox)
        centerX = colX[0] + 40;
        centerY = rowY[2] + 19;
        w = 36;
        break;
    case 6: // Advance
        centerX = colX[1];
        centerY = rowY[2] + 19;
        break;
    case 7: // Pause
        centerX = colX[2];
        centerY = rowY[2] + 19;
        offsetX = 100;
        break;
    case 8: // CSS (checkbox)
        centerX = colX[0] + 40;
        centerY = rowY[5] + 42;
        w = 36;
        break;
    case 9: // RPM/m/min
        centerX = colX[1];
        centerY = rowY[5] + 42;
        offsetX = 80;
        decimals = cuttingUseCSS ? 1 : 0;
        break;
    case 10: // Direction
        centerX = colX[2] + 15;
        centerY = rowY[5] + 42;
        w = 36;
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
// Stato dei campi
// -------------------------------------------------------------------
static int getPartingFieldState(int index, int selectedIndex)
{
    if (index == 0 || index == 1)
        return -2; // DRO (Z current, X current)
    if ((index == 6 || index == 7) && cuttingPulseEnable == 0)
        return -1; // disabilitati se pulse off
    if (selectedIndex == index)
        return 1;
    return 0;
}

// -------------------------------------------------------------------
// Disegno di un singolo campo
// -------------------------------------------------------------------
static void drawPartingField(int index, int selectedIndex)
{
    resetTextStyle();
    int cx, cy, w, h, offsetX, decimals;
    getPartingFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getPartingFieldState(index, selectedIndex);
    int labelX = cx - offsetX;
    int labelY = cy - tft_font_height() - 4;
    int boxCenterY = cy - h / 2;

    switch (index)
    {
    case 0: // Z current (lineare)
        drawParamBoxUnified(cuttingFieldLabels[0], cuttingStartZ, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 1: // X current
    {
        int boxX = cx - w / 2;
        int boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);

        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2);
        tft_set_text_size(1);
        tft_set_cursor(labelX, labelY);
        tft_print(cuttingFieldLabels[1]);

        float displayValue = cuttingStartX * to_display();
        char numBuf[16];
        snprintf(numBuf, sizeof(numBuf), "%.3f", displayValue);
        const char *suffix = (settings_get_xmode() == 0) ? "D" : "R";
        int16_t numWidth = tft_text_width(numBuf);
        int16_t suffixWidth = tft_text_width(suffix);
        int16_t totalWidth = numWidth + 2 + suffixWidth; // spazio 2 pixel
        int16_t startX = cx - totalWidth / 2;
        int16_t textY = cy - 20; 
        uint16_t numColor = (state == -2) ? TFT_GREEN : ((state == 1) ? TFT_WHITE : TFT_CYAN);
        tft_set_text_color(numColor);
        tft_draw_string(numBuf, startX, textY, numColor, 1);
        tft_set_text_color(TFT_ORANGE);
        tft_draw_string(suffix, startX + numWidth + 2, textY, TFT_ORANGE, 1);
        break;
    }
    case 2: // X Retract (lineare)
        drawParamBoxUnified(cuttingFieldLabels[2], cuttingRetractX, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 3: // Depth (lineare)
        drawParamBoxUnified(cuttingFieldLabels[3], cuttingDepth, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 4: // Feed Rate
        drawParamBoxUnified(cuttingFieldLabels[4], cuttingFeedRate, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    case 5: // Pulse Cut checkbox
        drawCheckbox(cuttingFieldLabels[5], cuttingPulseEnable > 0, labelX, labelY, selectedIndex == index, state);
        break;
    case 6: // Advance
        if (cuttingPulseEnable > 0)
            drawParamBoxUnified(cuttingFieldLabels[6], cuttingPulseAdvance, labelX, labelY, w, state, decimals, cx, boxCenterY);
        else
            drawParamBoxUnified(cuttingFieldLabels[6], 0.0f, labelX, labelY, w, -1, decimals, cx, boxCenterY);
        break;
    case 7: // Pause
        if (cuttingPulseEnable > 0)
            drawParamBoxUnified(cuttingFieldLabels[7], cuttingPulsePause, labelX, labelY, w, state, decimals, cx, boxCenterY);
        else
            drawParamBoxUnified(cuttingFieldLabels[7], 0.0f, labelX, labelY, w, -1, decimals, cx, boxCenterY);
        break;
    case 8: // CSS checkbox
        drawCheckbox(cuttingFieldLabels[8], cuttingUseCSS, labelX, labelY, selectedIndex == index, state);
        break;
    case 9: // RPM/m/min (etichetta dinamica)
    {
        const char *rpmLabel = cuttingUseCSS ? "m/min" : "RPM";
        float rpmValue = cuttingUseCSS ? cuttingSpindleMmin : cuttingSpindleRPM;
        drawParamBoxUnified(rpmLabel, rpmValue, labelX, labelY, w, state, decimals, cx, boxCenterY);
        break;
    }
    case 10: // Direction (CW/CCW) – box manuale
    {
        int boxX = cx - w / 2;
        int boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2);
        tft_set_text_size(1);
        tft_set_cursor(labelX, labelY);
        tft_print(cuttingFieldLabels[10]);

        const char *dirText = cuttingSpindleCW ? "CW" : "CCW";
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
static void setPartingField(int index, float value)
{
    switch (index)
    {
    case 2:
        cuttingRetractX = (value >= 0) ? value : 0;
        break;
    case 3:
    {
        if (value <= 0)
            cuttingDepth = 0.1f;
        else
        {
            float maxDepth = cuttingStartX; // non può superare il raggio corrente
            cuttingDepth = (value <= maxDepth) ? value : maxDepth;
        }
        break;
    }
    case 4:
        cuttingFeedRate = (value > 0) ? value : 1.0f;
        break;
    case 5:
        cuttingPulseEnable = (value > 0) ? 1.0f : 0.0f;
        break;
    case 6:
        if (value < 0.1f)
            cuttingPulseAdvance = 0.1f;
        else if (value > 1.0f)
            cuttingPulseAdvance = 1.0f;
        else
            cuttingPulseAdvance = value;
        break;
    case 7:
        if (value < 0.2f)
            cuttingPulsePause = 0.2f;
        else if (value > 1.0f)
            cuttingPulsePause = 1.0f;
        else
            cuttingPulsePause = value;
        break;
    case 8:
        cuttingUseCSS = (value > 0);
        break;
    case 9:
        if (cuttingUseCSS)
            cuttingSpindleMmin = (value > 0) ? value : 1.0f;
        else
            cuttingSpindleRPM = (value > 0) ? value : 1.0f;
        break;
    case 10:
        cuttingSpindleCW = (value > 0);
        break;
    }
}

// -------------------------------------------------------------------
// Inizializzazione valori di default
// -------------------------------------------------------------------
static void initPartingDefaults()
{
    cuttingStartZ = DRO_Z;
    cuttingStartX = DRO_X;
    cuttingRetractX = 0.5f;
    cuttingDepth = DRO_X; // default = raggio attuale
    cuttingFeedRate = 80.0f;

    cuttingPulseEnable = 0.0f;
    cuttingPulseAdvance = 0.1f;
    cuttingPulsePause = 0.2f;

    cuttingSpindleRPM = 1200.0f;
    cuttingSpindleMmin = 75.0f;
    cuttingUseCSS = false;
    cuttingSpindleCW = true;

    ESP_LOGI(TAG, "Parting parameters reset to default.");
}

// -------------------------------------------------------------------
// Messaggi di warning
// -------------------------------------------------------------------
static void drawPartingWarning(const char *msg)
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
static bool validatePartingParams()
{
    if (cuttingDepth <= 0)
    {
        drawPartingWarning("Depth deve essere > 0");
        return false;
    }
    if (cuttingFeedRate <= 0)
    {
        drawPartingWarning("Feed Rate deve essere > 0");
        return false;
    }
    if (cuttingRetractX < 0)
    {
        drawPartingWarning("Retract X deve essere ≥ 0");
        return false;
    }
    if (cuttingPulseEnable > 0)
    {
        if (cuttingPulseAdvance <= 0)
        {
            drawPartingWarning("Advance deve essere > 0");
            return false;
        }
        if (cuttingPulsePause < 0)
        {
            drawPartingWarning("Pause deve essere ≥ 0");
            return false;
        }
    }
    if (cuttingUseCSS)
    {
        if (cuttingSpindleMmin <= 0)
        {
            drawPartingWarning("m/min deve essere > 0");
            return false;
        }
    }
    else
    {
        if (cuttingSpindleRPM <= 0)
        {
            drawPartingWarning("RPM deve essere > 0");
            return false;
        }
    }
    return true;
}

// -------------------------------------------------------------------
// Preview input
// -------------------------------------------------------------------
static void drawInputPreview(int index, const std::string &buffer, bool blinkBorder)
{
    int cx, cy, w, h, offsetX, decimals;
    getPartingFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getPartingFieldState(index, PartingSelectedField);
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
    if (index == 0 || index == 1) return; // DRO non modificabili
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
            setPartingField(index, newValue);
            drawPartingField(index, PartingSelectedField);
            drawMachiningButtons();
            editing = false;
            break;
        }
        if (key == 'B') {
            drawPartingField(index, PartingSelectedField);
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
// Stampa parametri su seriale
// -------------------------------------------------------------------
static void printPartingParams()
{
    ESP_LOGI(TAG, "Parting parameters:");
    ESP_LOGI(TAG, "Z current: %.3f", cuttingStartZ);
    ESP_LOGI(TAG, "X current: %.3f", cuttingStartX);
    ESP_LOGI(TAG, "Retract X: %.3f", cuttingRetractX);
    ESP_LOGI(TAG, "Depth: %.3f", cuttingDepth);
    ESP_LOGI(TAG, "Feed Rate: %.1f", cuttingFeedRate);
    ESP_LOGI(TAG, "Pulse Cut: %s", cuttingPulseEnable > 0 ? "ON" : "OFF");
    if (cuttingPulseEnable > 0)
    {
        ESP_LOGI(TAG, "Advance: %.3f", cuttingPulseAdvance);
        ESP_LOGI(TAG, "Pause: %.3f", cuttingPulsePause);
    }
    ESP_LOGI(TAG, "CSS: %s", cuttingUseCSS ? "ON" : "OFF");
    ESP_LOGI(TAG, "Spindle: %.0f %s", cuttingUseCSS ? cuttingSpindleMmin : cuttingSpindleRPM, cuttingUseCSS ? "m/min" : "RPM");
    ESP_LOGI(TAG, "Direction: %s", cuttingSpindleCW ? "CW" : "CCW");
}

// -------------------------------------------------------------------
// Aggiornamento campi in tempo reale (DRO)
// -------------------------------------------------------------------
static void updatePartingRealtimeFields()
{
    static float lastZ = -9999;
    static float lastX = -9999;
    float newZ = DRO_Z;
    float newX = DRO_X;
    if (fabs(newZ - lastZ) > 0.001f)
    {
        cuttingStartZ = newZ;
        drawPartingField(0, PartingSelectedField);
        lastZ = newZ;
    }
    if (fabs(newX - lastX) > 0.001f)
    {
        cuttingStartX = newX;
        drawPartingField(1, PartingSelectedField);
        lastX = newX;
        // Limita Depth se supera il nuovo raggio
        if (cuttingDepth > cuttingStartX)
        {
            cuttingDepth = cuttingStartX;
            drawPartingField(3, PartingSelectedField);
        }
    }
}

// -------------------------------------------------------------------
// Generatore G‑code (stub)
// -------------------------------------------------------------------
static void PartingGcodeGenerator()
{
    PartingParams cp;
    cp.startZ = cuttingStartZ;
    cp.startX = cuttingStartX;            // raggio
    cp.retractX = cuttingRetractX;        // mm lineare (giusto)
    cp.depth = cuttingDepth;              // mm lineare (giusto)
    cp.feedRate = cuttingFeedRate;
    cp.pulseEnable = (cuttingPulseEnable > 0);
    cp.pulseAdvance = cuttingPulseAdvance;
    cp.pulsePause = cuttingPulsePause;
    cp.useCSS = cuttingUseCSS;
    cp.spindleMmin = cuttingSpindleMmin;
    cp.spindleRPM = cuttingSpindleRPM;
    cp.spindleCW = cuttingSpindleCW;

    GcodeBlock block = buildPartingBlock(cp, true);
    GcodeGenerator(block);
}

// -------------------------------------------------------------------
// Disegno scena
// -------------------------------------------------------------------
void drawPartingScene()
{
    char *stopSpindle = strdup("M5\n");
    if (stopSpindle && xQueueSend(gcodeQueue, &stopSpindle, 0) != pdTRUE)
    {
        free(stopSpindle);
        ESP_LOGE(TAG, "Coda piena per M5");
    }
    initPartingDefaults();

    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    tft_set_text_color(TFT_YELLOW);
    tft_set_text_size(2);
    tft_set_text_datum(MC_DATUM);
    drawCenteredTextAt("PARTING", 240, 15, 2, TFT_YELLOW);
    tft_fill_rect(20, 30, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 72, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 144, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 216, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 258, 440, 1, TFT_WHITE);
    resetTextStyle();
    for (int i = 0; i < PartingTotalFields; ++i)
    {
        drawPartingField(i, PartingSelectedField);
    }
    drawMachiningButtons();
}

// -------------------------------------------------------------------
// Loop di editing parametri
// -------------------------------------------------------------------
void drawPartingParams()
{
    // Imposta il primo campo modificabile (Retract X, indice 2)
    PartingSelectedField = 2;
    cuttingNeedsRedraw = true;
    while (true)
    {
        updatePartingRealtimeFields();
        if (cuttingNeedsRedraw)
        {
            drawPartingField(PartingSelectedField, PartingSelectedField);
            cuttingNeedsRedraw = false;
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
            if (validatePartingParams())
            {
                bool useCoolant = askCoolant();
                bool oldCoolant = coolantEnabled;
                coolantEnabled = useCoolant;
                PartingGcodeGenerator();
                return;
            }
            continue;
        }
        if (key == 'B')
        {
            printPartingParams();
            continue;
        }
        if (key == '1')
        {
            drawPartingField(PartingSelectedField, -1);
            int next = PartingSelectedField;
            do
            {
                next = (next - 1 + PartingTotalFields) % PartingTotalFields;
            } while (getPartingFieldState(next, next) < 1);
            PartingSelectedField = next;
            cuttingNeedsRedraw = true;
            continue;
        }
        if (key == '2')
        {
            drawPartingField(PartingSelectedField, -1);
            int next = PartingSelectedField;
            do
            {
                next = (next + 1) % PartingTotalFields;
            } while (getPartingFieldState(next, next) < 1);
            PartingSelectedField = next;
            cuttingNeedsRedraw = true;
            continue;
        }
        if (key == 'C')
        {
            int state = getPartingFieldState(PartingSelectedField, PartingSelectedField);
            if (state < 0)
            {
                drawPartingWarning("Campo non modificabile");
                continue;
            }
            switch (PartingSelectedField)
            {
            case 5: // Pulse Cut toggle
                cuttingPulseEnable = (cuttingPulseEnable > 0) ? 0.0f : 1.0f;
                drawPartingField(6, -1);
                drawPartingField(7, -1);
                cuttingNeedsRedraw = true;
                break;
            case 8: // CSS toggle
                cuttingUseCSS = !cuttingUseCSS;
                tft_fill_rect(140, 217, 100, 40, TFT_NAVY);
                drawPartingField(9, -1);
                cuttingNeedsRedraw = true;
                break;
            case 10: // Direction toggle
                cuttingSpindleCW = !cuttingSpindleCW;
                cuttingNeedsRedraw = true;
                break;
            default:
                handleNumericInput(PartingSelectedField);
                cuttingNeedsRedraw = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// -------------------------------------------------------------------
// Entry point della scena
// -------------------------------------------------------------------
void parting_scene_enter(void)
{
    drawPartingScene();
    drawPartingParams();
}

// ------------------------------------------------------------
// Getter per i parametri correnti (pubblico)
// ------------------------------------------------------------
PartingParams getCurrentPartingParams(void) {
    PartingParams pp;
    pp.startZ = cuttingStartZ;
    pp.startX = cuttingStartX;
    pp.retractX = cuttingRetractX;
    pp.depth = cuttingDepth;
    pp.feedRate = cuttingFeedRate;
    pp.pulseEnable = (cuttingPulseEnable > 0);
    pp.pulseAdvance = cuttingPulseAdvance;
    pp.pulsePause = cuttingPulsePause;
    pp.useCSS = cuttingUseCSS;
    pp.spindleMmin = cuttingSpindleMmin;
    pp.spindleRPM = cuttingSpindleRPM;
    pp.spindleCW = cuttingSpindleCW;
    return pp;
}