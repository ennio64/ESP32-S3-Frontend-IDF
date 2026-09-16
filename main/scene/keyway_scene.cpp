// keyway_scene.cpp
#include "keyway_scene.h"
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

static const char *TAG = "KEYWAY";

static unsigned long millis_idf()
{
    return (unsigned long)(esp_timer_get_time() / 1000);
}

// Conversioni diametro/raggio (solo per visualizzazione X current)
static inline float to_display()
{
    return (settings_get_xmode() == 0) ? 2.0f : 1.0f;
}
static inline float from_input_to_raggio()
{
    return (settings_get_xmode() == 0) ? 0.5f : 1.0f;
}

// ------------------------------------------------------------
//  Parametri statici (tutti in raggi per X)
// ------------------------------------------------------------
static int keywaySelectedField = 0;
static const int keywayTotalFields = 13;

static float keywayStartZ = 10.0f;      // Posizione Z iniziale (DRO)
static float keywayStartX = 10.0f;      // Posizione X iniziale (raggio, DRO)
static float keywayClearance = 0.5f;    // R (Z retract)
static float keywayLength = 20.0f;      // Q (lunghezza in Z)
static float keywayDepth = 2.0f;        // D (profondità finale in X)
static float keywayStepOver = 0.1f;     // P (incremento per pass)
static float keywayToolWidth = 3.0f;    // S (tool width per compensazione)
static int keywayRepetitions = 1;       // L (repetitions per depth level)
static float keywayReturnH = 1.0f;      // H (return to start: 0=no, 1=yes)
static float keywayFeedRate = 100.0f;   // F (feedrate)
static bool keywayMultiple = false;     // Multiple keyway
static int keywayCount = 1;             // Numero chiavette
static float keywayAngle = 0.0f;        // Angolo tra chiavette

static bool needsRedraw = false;

static const char *fieldLabels[] = {
    "Z Start",          // 0 (DRO)
    "X Start",          // 1 (DRO)
    "Retract(R)",      // 2
    "Length(Q)",       // 3
    "Depth(D)",        // 4
    "Step(P)",         // 5
    "ToolWidth(S)",   // 6
    "Repetitions(L)",  // 7
    "Return(H)",       // 8
    "Multiple Keyway",  // 9
    "Keyway Count",     // 10
    "Angle Step",       // 11
    "Feed(F)"          // 12
};

// ------------------------------------------------------------
// Geometria dei campi
// ------------------------------------------------------------
static void getKeywayFieldGeometry(int index, int &centerX, int &centerY, int &w, int &h, int &offsetX, int &decimals)
{
    const int colX[] = {128, 278, 425};
    const int rowY[] = {57, 87, 117, 156, 177, 207};

    w = (index == 7 || index == 8 || index == 10) ? 36 : 70;
    h = 24;
    offsetX = 100;
    decimals = 3;

    switch (index)
    {
    case 0: // Z Start
        centerX = colX[0];
        centerY = rowY[0] + 7;
        offsetX = 86;
        break;
    case 1: // X Start
        centerX = colX[1];
        centerY = rowY[0] + 7;
        offsetX = 86;
        break;
    case 2: // Retract (R)
        centerX = colX[2];
        centerY = rowY[0] + 7;
        offsetX = 104;
        break;
    case 3: // Length (Q)
        centerX = colX[0];
        centerY = rowY[1] + 19;
        offsetX = 102;
        break;
    case 4: // Depth (D)
        centerX = colX[1];
        centerY = rowY[1] + 19;
        offsetX = 96;
        break;
    case 5: // Step (P)
        centerX = colX[2];
        centerY = rowY[1] + 19;
        offsetX = 90;
        break;
    case 6: // Tool Width (S)
        centerX = colX[0];
        centerY = rowY[2] + 30;
        offsetX = 124;
        break;
    case 7: // Repetitions (L)
        centerX = colX[1] + 15;
        centerY = rowY[2] + 30;
        offsetX = 112;
        decimals = 0;
        break;
    case 8: // Return (H)
        centerX = colX[2] + 15;
        centerY = rowY[2] + 30;
        offsetX = 86;
        decimals = 0;
        break;
    case 9: // Multiple Keyway
        centerX = colX[0] + 40;
        centerY = rowY[3] + 40;
        offsetX = 124;
        decimals = 0;
        break;
    case 10: // Keyway Count
        centerX = colX[1] + 15;
        centerY = rowY[3] + 40;
        offsetX = 110;
        decimals = 0;
        break;
    case 11: // Angle Step
        centerX = colX[2];
        centerY = rowY[3] + 40;
        offsetX = 106;
        decimals = 1;
        break;
    case 12: // Feed (F)
        centerX = colX[0];
        centerY = rowY[5] + 40;
        offsetX = 92;
        decimals = 1;
        break;
    default:
        centerX = -999;
        centerY = -999;
        break;
    }
}

// ------------------------------------------------------------
// Stato del campo
// ------------------------------------------------------------
static int getKeywayFieldState(int index, int selectedIndex)
{
    if (index == 0 || index == 1) return -2; // DRO non modificabili
    if (index == 9) return (selectedIndex == index) ? 1 : 0;
    if (index == 10 || index == 11) {
        if (!keywayMultiple) return -1;
        return (selectedIndex == index) ? 1 : 0;
    }
    if (selectedIndex == index) return 1;
    return 0;
}

// ------------------------------------------------------------
// Disegno di un singolo campo
// ------------------------------------------------------------
static void drawKeywayField(int index, int selectedIndex)
{
    resetTextStyle();
    int cx, cy, w, h, offsetX, decimals;
    getKeywayFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getKeywayFieldState(index, selectedIndex);
    int labelX = cx - offsetX;
    int labelY = cy - tft_font_height() - 4;
    int boxCenterY = cy - h / 2;

    switch (index)
    {
    case 0:
        drawParamBoxUnified(fieldLabels[0], keywayStartZ, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 1:
    {
        float displayValue = keywayStartX * to_display();
        char numBuf[16];
        snprintf(numBuf, sizeof(numBuf), "%.3f", displayValue);
        const char *suffix = (settings_get_xmode() == 0) ? "D" : "R";
        int boxX = cx - w/2, boxY = cy - h;
        tft_fill_round_rect(boxX, boxY, w, h, 8, TFT_BLACK);
        tft_draw_round_rect(boxX, boxY, w, h, 8, TFT_YELLOW);
        tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
        tft_set_font(&fonts::Font2); tft_set_text_size(1);
        tft_set_cursor(labelX, labelY); tft_print(fieldLabels[1]);
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
        drawParamBoxUnified(fieldLabels[2], keywayClearance, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 3:
        drawParamBoxUnified(fieldLabels[3], keywayLength, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 4:
        drawParamBoxUnified(fieldLabels[4], keywayDepth, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 5:
        drawParamBoxUnified(fieldLabels[5], keywayStepOver, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 6:
        drawParamBoxUnified(fieldLabels[6], keywayToolWidth, labelX, labelY, w, state, 3, cx, boxCenterY);
        break;
    case 7:
        drawParamBoxUnified(fieldLabels[7], (float)keywayRepetitions, labelX, labelY, w, state, 0, cx, boxCenterY);
        break;
    case 8:
        drawParamBoxUnified(fieldLabels[8], keywayReturnH, labelX, labelY, w, state, 0, cx, boxCenterY);
        break;
    case 9:
        drawCheckbox(fieldLabels[9], keywayMultiple, labelX, labelY, selectedIndex == index, state);
        break;
    case 10:
        drawParamBoxUnified(fieldLabels[10], (float)keywayCount, labelX, labelY, w, state, 0, cx, boxCenterY);
        break;
    case 11:
        drawParamBoxUnified(fieldLabels[11], keywayAngle, labelX, labelY, w, state, 1, cx, boxCenterY);
        break;
    case 12:
        drawParamBoxUnified(fieldLabels[12], keywayFeedRate, labelX, labelY, w, state, 1, cx, boxCenterY);
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------
// Setter dei campi
// ------------------------------------------------------------
static void setKeywayField(int index, float value)
{
    switch (index)
    {
    case 0: keywayStartZ = value; break;
    case 1: keywayStartX = value * from_input_to_raggio(); break;
    case 2: keywayClearance = (value >= 0) ? value : 0; break;
    case 3: keywayLength = (value > 0) ? value : 0.001f; break;
    case 4: keywayDepth = (value > 0) ? value : 0.001f; break;
    case 5: keywayStepOver = (value < 0.001f) ? 0.001f : value; break;
    case 6: keywayToolWidth = (value < 0.01f) ? 0.01f : value; break;
    case 7: keywayRepetitions = (value < 1) ? 1 : (int)value; break;
    case 8: keywayReturnH = (value > 0.5f) ? 1.0f : 0.0f; break;
    case 10: keywayCount = (value < 1) ? 1 : (int)value; break;
    case 11: keywayAngle = value; break;
    case 12: keywayFeedRate = (value < 0.01f) ? 0.01f : value; break;
    default: break;
    }
}

// ------------------------------------------------------------
// Inizializzazione valori di default (sincronizzati con DRO)
// ------------------------------------------------------------
static void initKeywayDefaults()
{
    keywayStartZ = DRO_Z;
    keywayStartX = DRO_X;
    keywayClearance = 0.5f;
    keywayLength = 20.0f;
    keywayDepth = 2.0f;
    keywayStepOver = 0.1f;
    keywayToolWidth = 3.0f;
    keywayRepetitions = 1;
    keywayReturnH = 1.0f;
    keywayFeedRate = 100.0f;
    keywayMultiple = false;
    keywayCount = 1;
    keywayAngle = 0.0f;
    ESP_LOGI(TAG, "Keyway parameters reset to default");
}

// ------------------------------------------------------------
// Messaggio di warning
// ------------------------------------------------------------
static void drawKeywayWarning(const char *msg)
{
    tft_fill_rect(0, 260, 480, 60, TFT_RED);
    drawCenteredTextAt(msg, 240, 290, 2, TFT_WHITE);
    vTaskDelay(pdMS_TO_TICKS(2500));
    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
    drawMachiningButtons();
}

// ------------------------------------------------------------
// Validazione parametri
// ------------------------------------------------------------
static bool validateKeywayParams()
{
    if (keywayLength <= 0) { drawKeywayWarning("Length (Q) deve essere > 0"); return false; }
    if (keywayDepth <= 0) { drawKeywayWarning("Depth (D) deve essere > 0"); return false; }
    if (keywayStepOver <= 0) { drawKeywayWarning("Step (P) deve essere > 0"); return false; }
    if (keywayToolWidth <= 0) { drawKeywayWarning("Tool Width (S) deve essere > 0"); return false; }
    if (keywayClearance < 0) { drawKeywayWarning("Retract (R) deve essere >= 0"); return false; }
    if (keywayFeedRate <= 0) { drawKeywayWarning("Feed (F) deve essere > 0"); return false; }
    if (keywayRepetitions < 1) { drawKeywayWarning("Repetitions (L) deve essere >= 1"); return false; }
    if (keywayStepOver >= keywayDepth) { drawKeywayWarning("Step (P) deve essere < Depth (D)"); return false; }
    if (keywayReturnH != 0.0f && keywayReturnH != 1.0f) { drawKeywayWarning("Return (H) deve essere 0 o 1"); return false; }

    if (keywayMultiple) {
        if (keywayCount < 1) { drawKeywayWarning("Keyway Count deve essere >= 1"); return false; }
        if (keywayCount > 100) { drawKeywayWarning("Keyway Count troppo alto (max 100)"); return false; }
        if (keywayAngle <= 0) { drawKeywayWarning("Angle Step deve essere > 0"); return false; }
        if (keywayAngle > 360) { drawKeywayWarning("Angle Step deve essere ≤ 360°"); return false; }
        float totalAngle = keywayAngle * (keywayCount - 1);
        if (totalAngle >= 360.0f) { drawKeywayWarning("Angolo totale ≥ 360°"); return false; }
    }
    return true;
}

// ------------------------------------------------------------
// Preview input (box lampeggiante)
// ------------------------------------------------------------
static void drawKeywayInputPreview(int index, const std::string &buffer, bool blinkBorder)
{
    int cx, cy, w, h, offsetX, decimals;
    getKeywayFieldGeometry(index, cx, cy, w, h, offsetX, decimals);
    int state = getKeywayFieldState(index, keywaySelectedField);
    uint16_t fg, bg, border;
    if (state == 0) { fg = TFT_CYAN; bg = TFT_BLACK; border = TFT_YELLOW; }
    else if (state == 1) { fg = TFT_WHITE; bg = TFT_BLACK; border = TFT_YELLOW; }
    else { fg = TFT_RED; bg = TFT_BLACK; border = TFT_YELLOW; }
    cy = cy - h/2;
    drawInputPreviewBox(cx, cy, w, h, buffer, blinkBorder, bg, fg, border);
}

// ------------------------------------------------------------
// Gestione input numerico
// ------------------------------------------------------------
static void handleKeywayNumericInput(int index)
{
    // Return (H) è toggle, gestito separatamente
    if (index == 8) {
        keywayReturnH = (keywayReturnH > 0.5f) ? 0.0f : 1.0f;
        drawKeywayField(index, keywaySelectedField);
        drawMachiningButtons();
        return;
    }

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
            // Validazioni immediate
            switch (index) {
            case 3: if (newValue <= 0) { drawKeywayWarning("Length > 0"); continue; } break;
            case 4: if (newValue <= 0) { drawKeywayWarning("Depth > 0"); continue; }
                    if (newValue <= keywayStepOver) { drawKeywayWarning("Depth deve essere > Step"); continue; } break;
            case 5: if (newValue <= 0) { drawKeywayWarning("Step > 0"); continue; }
                    if (newValue >= keywayDepth) { drawKeywayWarning("Step deve essere < Depth"); continue; } break;
            case 6: if (newValue <= 0) { drawKeywayWarning("Tool Width > 0"); continue; } break;
            case 7: if (newValue < 1) { drawKeywayWarning("Repetitions >= 1"); continue; } break;
            case 2: if (newValue < 0) { drawKeywayWarning("Retract >= 0"); continue; } break;
            case 10: if (newValue < 1) { drawKeywayWarning("Count >= 1"); continue; }
                     if (newValue > 100) { drawKeywayWarning("Count max 100"); continue; } break;
            case 11: if (newValue <= 0) { drawKeywayWarning("Angle > 0"); continue; }
                     if (newValue > 360) { drawKeywayWarning("Angle ≤ 360°"); continue; }
                     if (keywayMultiple && keywayCount > 1) {
                         float totalAngle = newValue * (keywayCount - 1);
                         if (totalAngle >= 360.0f) { drawKeywayWarning("Angolo totale ≥ 360°"); continue; }
                     } break;
            case 12: if (newValue <= 0) { drawKeywayWarning("Feed > 0"); continue; } break;
            default: break;
            }
            setKeywayField(index, newValue);
            drawKeywayField(index, keywaySelectedField);
            drawMachiningButtons();
            editing = false;
            break;
        }
        if (key == 'B') {
            drawKeywayField(index, keywaySelectedField);
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
            drawKeywayInputPreview(index, buffer, blinkState);
        }
        if (millis_idf() - lastBlink > 400) {
            lastBlink = millis_idf();
            blinkState = !blinkState;
            drawKeywayInputPreview(index, buffer, blinkState);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ------------------------------------------------------------
// Stampa parametri su seriale (debug)
// ------------------------------------------------------------
static void printKeywayParams()
{
    ESP_LOGI(TAG, "KEYWAY CYCLE (M800) PARAMS");
    ESP_LOGI(TAG, "Start Z: %.3f", keywayStartZ);
    ESP_LOGI(TAG, "Start X (radius): %.3f", keywayStartX);
    ESP_LOGI(TAG, "Length (Q): %.3f", keywayLength);
    ESP_LOGI(TAG, "Depth (D): %.3f", keywayDepth);
    ESP_LOGI(TAG, "Step (P): %.3f", keywayStepOver);
    ESP_LOGI(TAG, "Tool Width (S): %.3f", keywayToolWidth);
    ESP_LOGI(TAG, "Retract (R): %.3f", keywayClearance);
    ESP_LOGI(TAG, "Feed (F): %.1f", keywayFeedRate);
    ESP_LOGI(TAG, "Repetitions (L): %d", keywayRepetitions);
    ESP_LOGI(TAG, "Return (H): %.0f", keywayReturnH);
    ESP_LOGI(TAG, "Multiple: %s", keywayMultiple ? "YES" : "NO");
    if (keywayMultiple) {
        ESP_LOGI(TAG, "Keyway Count: %d", keywayCount);
        ESP_LOGI(TAG, "Angle Step: %.1f", keywayAngle);
    }
}

// ------------------------------------------------------------
// Aggiornamento campi in tempo reale (DRO)
// ------------------------------------------------------------
static void updateKeywayRealtimeFields()
{
    static float lastZ = -9999, lastX = -9999;
    float newZ = DRO_Z;
    float newX = DRO_X;
    if (fabs(newZ - lastZ) > 0.001f) {
        keywayStartZ = newZ;
        drawKeywayField(0, keywaySelectedField);
        lastZ = newZ;
    }
    if (fabs(newX - lastX) > 0.001f) {
        keywayStartX = newX;
        drawKeywayField(1, keywaySelectedField);
        lastX = newX;
    }
}

// ------------------------------------------------------------
// Generatore G‑code (usa buildKeywayBlock)
// ------------------------------------------------------------
static void generateKeywayGcode()
{
    KeywayParams kp;
    kp.startZ = keywayStartZ;
    kp.startX = keywayStartX;
    kp.length = keywayLength;
    kp.depth = keywayDepth;
    kp.step = keywayStepOver;
    kp.toolWidth = keywayToolWidth;
    kp.retract = keywayClearance;
    kp.feed = keywayFeedRate;
    kp.repetitions = keywayRepetitions;
    kp.returnToStart = (keywayReturnH > 0.5f);
    kp.multiple = keywayMultiple;
    kp.count = keywayCount;
    kp.angleStep = keywayAngle;

    GcodeBlock block = buildKeywayBlock(kp, true);
    GcodeGenerator(block);
}

// ------------------------------------------------------------
// Disegno scena
// ------------------------------------------------------------
static void drawKeywayScene()
{
    // Arresta mandrino
    char *stopSpindle = strdup("M5\n");
    if (stopSpindle && xQueueSend(gcodeQueue, &stopSpindle, 0) != pdTRUE) {
        free(stopSpindle);
        ESP_LOGE(TAG, "Coda piena per M5");
    }

    initKeywayDefaults();

    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("KEYWAY (M800 Cycle)", 240, 15, 2, TFT_YELLOW);

    tft_fill_rect(20, 30, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 72, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 156, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 216, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 258, 440, 1, TFT_WHITE);

    // Messaggio di avviso: mandrino fermo
    {
        int yTop = 216, yBottom = 258;
        int cy = (yTop + yBottom) / 2;
        int cx = 230;
        int triSize = 16;

        tft_fill_triangle(cx, cy - triSize - 2,
                          cx - (triSize + 2), cy + triSize,
                          cx + (triSize + 2), cy + triSize,
                          TFT_YELLOW);
        tft_draw_triangle(cx, cy - triSize - 2,
                          cx - (triSize + 2), cy + triSize,
                          cx + (triSize + 2), cy + triSize,
                          TFT_BLACK);
        tft_set_text_color(TFT_BLACK);
        tft_set_text_size(2);
        tft_set_text_datum(MC_DATUM);
        drawCenteredTextAt("i", cx + 1, cy + 4, 2, TFT_BLACK);
        tft_set_text_color(TFT_WHITE);
        tft_set_text_size(1);
        tft_set_text_datum(ML_DATUM);
        tft_set_cursor(cx + triSize + 18, cy - 6);
        tft_print("Spindle must be stopped");
        tft_set_cursor(cx + triSize + 18, cy + 6);
        tft_print("during keyway cycle");
    }

    resetTextStyle();
    for (int i = 0; i < keywayTotalFields; ++i)
        drawKeywayField(i, keywaySelectedField);

    drawMachiningButtons();
}

// ------------------------------------------------------------
// Loop di editing parametri
// ------------------------------------------------------------
static void editKeywayParams()
{
    keywaySelectedField = 2; // Partiamo da Retract (R)
    needsRedraw = true;

    while (true) {
        updateKeywayRealtimeFields();
        if (needsRedraw) {
            for (int i = 0; i < keywayTotalFields; ++i)
                drawKeywayField(i, keywaySelectedField);
            needsRedraw = false;
        }

        char key = readKeypad();
        if (key == 'D') {
            resetTextStyle();
            return;
        }
        if (key == 'A') {
            if (validateKeywayParams()) {
                bool useCoolant = askCoolant();
                coolantEnabled = useCoolant;
                generateKeywayGcode();
                return;
            }
            continue;
        }
        if (key == 'B') {
            printKeywayParams();
            continue;
        }
        if (key == '1') {
            int next = keywaySelectedField;
            do {
                next = (next - 1 + keywayTotalFields) % keywayTotalFields;
            } while (getKeywayFieldState(next, next) < 0);
            keywaySelectedField = next;
            needsRedraw = true;
            continue;
        }
        if (key == '2') {
            int next = keywaySelectedField;
            do {
                next = (next + 1) % keywayTotalFields;
            } while (getKeywayFieldState(next, next) < 0);
            keywaySelectedField = next;
            needsRedraw = true;
            continue;
        }
        if (key == 'C') {
            int state = getKeywayFieldState(keywaySelectedField, keywaySelectedField);
            if (state < 0) {
                drawKeywayWarning("Campo non modificabile");
                continue;
            }
            if (keywaySelectedField == 9) { // Multiple Keyway toggle
                keywayMultiple = !keywayMultiple;
                needsRedraw = true;
                continue;
            }
            handleKeywayNumericInput(keywaySelectedField);
            needsRedraw = true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ------------------------------------------------------------
// Entry point
// ------------------------------------------------------------
void keyway_scene_enter(void)
{
    drawKeywayScene();
    editKeywayParams();
}

// ------------------------------------------------------------
// Getter per i parametri correnti (pubblico)
// ------------------------------------------------------------
KeywayParams getCurrentKeywayParams(void)
{
    KeywayParams kp;
    kp.startZ = keywayStartZ;
    kp.startX = keywayStartX;
    kp.length = keywayLength;
    kp.depth = keywayDepth;
    kp.step = keywayStepOver;
    kp.toolWidth = keywayToolWidth;
    kp.retract = keywayClearance;
    kp.feed = keywayFeedRate;
    kp.repetitions = keywayRepetitions;
    kp.returnToStart = (keywayReturnH > 0.5f);
    kp.multiple = keywayMultiple;
    kp.count = keywayCount;
    kp.angleStep = keywayAngle;
    return kp;
}