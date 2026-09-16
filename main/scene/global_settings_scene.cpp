// global_settings_scene.cpp
#include "global_settings_scene.h"
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
#include "colors.h"
#include "main_scene.h"
#include <cmath>
#include <cstring>
#include <string>
#include <cstdio>

static const char *TAG = "GLOBAL_SETTINGS";

static unsigned long millis_idf()
{
    return (unsigned long)(esp_timer_get_time() / 1000);
}

// Indici dei campi
enum SettingsField
{
    FIELD_JOYSTICK_TYPE,
    FIELD_X_MODE,
    FIELD_SPINDLE_UNLOCK,
    FIELD_SPINDLE_LOCK,
    FIELD_SPINDLE_DIR,
    FIELD_ENCODER_DIR,
    FIELD_EXT_DEPTH,
    FIELD_INT_DEPTH,
    TOTAL_FIELDS
};

static int selectedField = 0;
static bool needsRedraw = false;

// Etichette
static const char *fieldLabels[] = {
    "Joystick Type",
    "X Mode",
    "Spindle Unlock",
    "Spindle Lock",
    "Spindle Dir (Display)",
    "Encoder Dir",
    "Ext Depth Coeff",
    "Int Depth Coeff"};

// Valori correnti (caricati all'avvio e aggiornati dopo ogni modifica)
static int joyType, xMode, spindleUnlock, spindleLock, spindleDir, encoderDir;
static float extDepth, intDepth;

// Helper per ottenere testo descrittivo per i campi booleani
static const char* getJoyTypeText() { return (joyType == 0) ? "Analogic" : "Digital"; }
static const char* getXModeText()  { return (xMode == 0) ? "Diameter" : "Radius"; }
static const char* getSpindleDirText() { return (spindleDir == 0) ? "CW" : "CCW"; }
static const char* getEncoderDirText() { return (encoderDir == 0) ? "Normal" : "Reversed"; }

// Carica tutti i parametri da NVS
static void loadSettings()
{
    joyType = settings_get_joystick_type();
    xMode = settings_get_xmode();
    spindleUnlock = settings_get_spindle_unlock_param();
    spindleLock = settings_get_spindle_lock_param();
    spindleDir = settings_get_spindle_dir();
    encoderDir = settings_get_encoder_dir();
    extDepth = settings_get_ext_depth_coeff();
    intDepth = settings_get_int_depth_coeff();
    ESP_LOGI(TAG, "Settings loaded");
}

// Salva tutti i parametri in NVS
static void saveSettings()
{
    settings_set_joystick_type(joyType);
    settings_set_xmode(xMode);
    settings_set_spindle_unlock_param(spindleUnlock);
    settings_set_spindle_lock_param(spindleLock);
    settings_set_spindle_dir(spindleDir);
    settings_set_encoder_dir(encoderDir);
    settings_set_ext_depth_coeff(extDepth);
    settings_set_int_depth_coeff(intDepth);
    settings_commit();
    ESP_LOGI(TAG, "Settings saved");
}

// Aggiorna un campo specifico
static void setField(int index, float value)
{
    switch (index)
    {
    case FIELD_JOYSTICK_TYPE:
        joyType = (int)value;
        if (joyType < 0) joyType = 0;
        if (joyType > 1) joyType = 1;
        break;
    case FIELD_X_MODE:
        xMode = (int)value;
        if (xMode < 0) xMode = 0;
        if (xMode > 1) xMode = 1;
        break;
    case FIELD_SPINDLE_UNLOCK:
        spindleUnlock = (int)value;
        if (spindleUnlock < 0) spindleUnlock = 0;
        break;
    case FIELD_SPINDLE_LOCK:
        spindleLock = (int)value;
        if (spindleLock < 0) spindleLock = 0;
        break;
    case FIELD_SPINDLE_DIR:
        spindleDir = (int)value;
        if (spindleDir < 0) spindleDir = 0;
        if (spindleDir > 1) spindleDir = 1;
        break;
    case FIELD_ENCODER_DIR:
        encoderDir = (int)value;
        if (encoderDir < 0) encoderDir = 0;
        if (encoderDir > 1) encoderDir = 1;
        break;
    case FIELD_EXT_DEPTH:
        extDepth = value;
        if (extDepth < 0.5f) extDepth = 0.5f;
        if (extDepth > 2.0f) extDepth = 2.0f;
        break;
    case FIELD_INT_DEPTH:
        intDepth = value;
        if (intDepth < 0.5f) intDepth = 0.5f;
        if (intDepth > 2.0f) intDepth = 2.0f;
        break;
    default: break;
    }
}

// Disegna un campo (label + box)
static void drawField(int index, int selectedIndex)
{
    resetTextStyle();
    // Coordinate fisse: colonna label a sinistra, colonna valore a destra
    const int labelColX = 120;   // centro label
    const int valueColX = 380;   // centro valore
    const int startY = 60;
    const int rowHeight = 32;
    const int boxW = 100;
    const int boxH = 24;
    int row = index;
    int cy = startY + row * rowHeight;
    int yBox = cy - boxH/2;
    int labelX = labelColX - boxW/2;
    int valueX = valueColX - boxW/2;

    // Stato del campo: -2 = readonly (non usato), -1 = disabled, 0 = normale, 1 = selezionato
    int state = (selectedIndex == index) ? 1 : 0;

    // Label
    tft_fill_rect(labelX, yBox, boxW, boxH, TFT_BLACK);
    tft_draw_rect(labelX, yBox, boxW, boxH, TFT_YELLOW);
    tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);
    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_cursor(labelX + 5, cy - 8);
    tft_print(fieldLabels[index]);

    // Valore
    tft_fill_rect(valueX, yBox, boxW, boxH, TFT_BLACK);
    tft_draw_rect(valueX, yBox, boxW, boxH, TFT_YELLOW);
    tft_set_text_color(state == 1 ? TFT_WHITE : TFT_CYAN);

    char buf[32];
    switch (index)
    {
    case FIELD_JOYSTICK_TYPE:
        snprintf(buf, sizeof(buf), "%s", getJoyTypeText());
        break;
    case FIELD_X_MODE:
        snprintf(buf, sizeof(buf), "%s", getXModeText());
        break;
    case FIELD_SPINDLE_UNLOCK:
        snprintf(buf, sizeof(buf), "%d", spindleUnlock);
        break;
    case FIELD_SPINDLE_LOCK:
        snprintf(buf, sizeof(buf), "%d", spindleLock);
        break;
    case FIELD_SPINDLE_DIR:
        snprintf(buf, sizeof(buf), "%s", getSpindleDirText());
        break;
    case FIELD_ENCODER_DIR:
        snprintf(buf, sizeof(buf), "%s", getEncoderDirText());
        break;
    case FIELD_EXT_DEPTH:
        snprintf(buf, sizeof(buf), "%.3f", extDepth);
        break;
    case FIELD_INT_DEPTH:
        snprintf(buf, sizeof(buf), "%.3f", intDepth);
        break;
    default: buf[0] = '\0'; break;
    }
    drawCenteredTextAt(buf, valueX + boxW/2, cy, 1, state == 1 ? TFT_WHITE : TFT_CYAN);
}

// Preview input (box lampeggiante)
static void drawInputPreview(int index, const std::string &buffer, bool blinkBorder)
{
    const int valueColX = 380;
    const int startY = 60;
    const int rowHeight = 32;
    const int boxW = 100;
    const int boxH = 24;
    int row = index;
    int cy = startY + row * rowHeight;
    int yBox = cy - boxH/2;
    int valueX = valueColX - boxW/2;

    uint16_t fg = TFT_WHITE, bg = TFT_BLACK, border = TFT_YELLOW;
    cy = yBox + boxH/2;
    drawInputPreviewBox(valueX + boxW/2, cy, boxW, boxH, buffer, blinkBorder, bg, fg, border);
}

// Gestione input numerico generico
static void handleNumericInput(int index)
{
    std::string buffer = "";
    bool editing = true, hasDecimal = false;
    drawInputButtons();
    unsigned long lastBlink = millis_idf();
    bool blinkState = false;

    while (editing)
    {
        char key = readKeypad();
        if (key == 'A' || key == 'D') continue;
        if (key == 'C')
        {
            float newValue = safe_stof(buffer);
            setField(index, newValue);
            drawField(index, selectedField);
            drawMachiningButtons();
            editing = false;
            break;
        }
        if (key == 'B')
        {
            drawField(index, selectedField);
            drawMachiningButtons();
            editing = false;
            break;
        }
        if (isValidNumericKey(key, buffer, hasDecimal))
        {
            if (key == '#')
            {
                if (buffer.empty()) buffer = "-";
                else if (buffer[0] != '-') buffer.insert(0, 1, '-');
            }
            else if (key == '*')
            {
                if (buffer.empty()) buffer = "0.";
                else if (buffer == "-") buffer = "-0.";
                else if (buffer.find('.') == std::string::npos) buffer += '.';
                hasDecimal = true;
            }
            else
            {
                buffer += key;
            }
            drawInputPreview(index, buffer, blinkState);
        }
        if (millis_idf() - lastBlink > 400)
        {
            lastBlink = millis_idf();
            blinkState = !blinkState;
            drawInputPreview(index, buffer, blinkState);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Disegno sfondo e titolo
static void drawBackground()
{
    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("GLOBAL SETTINGS", 240, 15, 2, TFT_YELLOW);
    tft_draw_line(20, 30, 460, 30, TFT_WHITE);
    tft_draw_line(20, 258, 460, 258, TFT_WHITE);
}

// Pulsanti personalizzati per questa scena: A=Save & Exit, D=Exit without save, 1/2 per navigazione, C=Edit
static void drawCustomButtons()
{
    const int btnW = 75, btnH = 30, btnY = 270;
    const int spacing = 12;
    const int totalWidth = btnW * 5 + spacing * 4;
    const int startX = (480 - totalWidth) / 2;

    drawButton(startX, btnY, btnW, btnH, TFT_DARKGREEN, 'A', 2, TFT_WHITE, "SAVE", 1, TFT_WHITE);
    drawButton(startX + btnW + spacing, btnY, btnW, btnH, TFT_DARKGREY, '1', 2, TFT_WHITE, "PREV", 1, TFT_WHITE);
    drawButton(startX + 2*(btnW+spacing), btnY, btnW, btnH, TFT_DARKGREY, '2', 2, TFT_WHITE, "NEXT", 1, TFT_WHITE);
    drawButton(startX + 3*(btnW+spacing), btnY, btnW, btnH, TFT_DARKGREY, 'C', 2, TFT_WHITE, "EDIT", 1, TFT_WHITE);
    drawButton(startX + 4*(btnW+spacing), btnY, btnW, btnH, TFT_RED, 'D', 2, TFT_WHITE, "EXIT", 1, TFT_WHITE);
}

// Gestione dell'input per campi toggle (joystick, xmode, spindle dir, encoder dir)
static void handleToggleField(int index)
{
    switch (index)
    {
    case FIELD_JOYSTICK_TYPE:
        joyType = (joyType == 0) ? 1 : 0;
        break;
    case FIELD_X_MODE:
        xMode = (xMode == 0) ? 1 : 0;
        break;
    case FIELD_SPINDLE_DIR:
        spindleDir = (spindleDir == 0) ? 1 : 0;
        break;
    case FIELD_ENCODER_DIR:
        encoderDir = (encoderDir == 0) ? 1 : 0;
        break;
    default:
        return;
    }
    drawField(index, selectedField);
}

// Loop principale
static void editSettings()
{
    selectedField = 0;
    needsRedraw = true;
    while (true)
    {
        if (needsRedraw)
        {
            drawBackground();
            for (int i = 0; i < TOTAL_FIELDS; ++i)
                drawField(i, selectedField);
            drawCustomButtons();
            needsRedraw = false;
        }

        char key = readKeypad();
        if (key == 'A')
        {
            saveSettings();
            resetTextStyle();
            ESP_LOGI(TAG, "Settings saved, returning to menu");
            return;
        }
        if (key == 'D')
        {
            resetTextStyle();
            ESP_LOGI(TAG, "Settings NOT saved, returning to menu");
            return;
        }
        if (key == '1')
        {
            selectedField = (selectedField - 1 + TOTAL_FIELDS) % TOTAL_FIELDS;
            needsRedraw = true;
            continue;
        }
        if (key == '2')
        {
            selectedField = (selectedField + 1) % TOTAL_FIELDS;
            needsRedraw = true;
            continue;
        }
        if (key == 'C')
        {
            // Campi toggle: joystick, xmode, spindle dir, encoder dir
            if (selectedField == FIELD_JOYSTICK_TYPE ||
                selectedField == FIELD_X_MODE ||
                selectedField == FIELD_SPINDLE_DIR ||
                selectedField == FIELD_ENCODER_DIR)
            {
                handleToggleField(selectedField);
                // Aggiorna subito il display
                drawField(selectedField, selectedField);
            }
            else
            {
                // Campi numerici
                handleNumericInput(selectedField);
                // Dopo l'input, ridisegna il campo
                drawField(selectedField, selectedField);
            }
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void global_settings_scene_enter(void)
{
    loadSettings();
    editSettings();
}