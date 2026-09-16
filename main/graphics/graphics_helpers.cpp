#include "graphics_lgfx.h"
#include "graphics_helpers.h"
#include "graphics_primitive.h"
#include "cnc_manager.h"
#include "global_vars.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <cstring>
#include <cmath>

static const char *TAG = "GFX_HELPERS";

// Variabili globali utilizzate (definite altrove)
extern std::string activeGcodeType;
extern float DRO_X, DRO_Z, DRO_A, DRO_RPM;
extern float last_DRO_X, last_DRO_Z, last_DRO_A, last_DRO_RPM;
extern bool R_D;
extern float k_XDRO;
static std::string last_GRBL_State = "";
static float last_angleDeg = -9999;

// ========== Buffer per immagini ==========
uint8_t *logoBuffer = nullptr;
size_t logoSize = 0;

uint8_t *miniLogoBuffer = nullptr;
size_t miniLogoSize = 0;

uint8_t *madreviteBuffer = nullptr;
size_t madreviteSize = 0;
uint8_t *viteBuffer = nullptr;
size_t viteSize = 0;

uint8_t *morseTaperBuffer = nullptr;
size_t morseTaperSize = 0;
uint8_t *mtABBuffer = nullptr;
size_t mtABSize = 0;

// =============================================
// Funzioni di base (stile, messaggi)
// =============================================
void resetTextStyle()
{
    tft_set_font(nullptr);
    tft_set_text_size(2);
    tft_set_text_color_bg(TFT_WHITE, TFT_BLACK);
    tft_set_text_datum(0); // TL_DATUM
}

void drawCenteredTextAt(const char *text, int centerX, int centerY, uint8_t textSize, uint16_t textColor)
{
    tft_set_text_size(textSize);
    tft_set_text_color(textColor);
    tft_set_text_datum(4);
    int16_t textWidth = tft_text_width(text);
    int16_t x = centerX - (textWidth / 2);
    tft_set_cursor(x, centerY);
    tft_print(text);
}

void drawTitolo(const char *titolo, uint8_t textSize)
{
    tft_set_text_color(TFT_YELLOW);
    tft_set_text_size(textSize);
    tft_set_text_datum(4);
    int16_t textWidth = tft_text_width(titolo);
    int16_t x = 240 - (textWidth / 2);
    tft_set_cursor(x, 15);
    tft_print(titolo);
}

void drawMessaggio(const char *msg, int y, uint16_t coloreTesto, uint16_t coloreSfondo)
{
    tft_set_text_color_bg(coloreTesto, coloreSfondo);
    tft_set_text_size(2);
    int16_t x = (480 - tft_text_width(msg)) / 2;
    tft_set_cursor(x, y);
    tft_print(msg);
}

// =============================================
// Box e valori
// =============================================
void drawBox(const char *label, int x, int y, int w, int h,
             uint16_t coloreBordo, uint16_t coloreTesto)
{
    tft_draw_rect(x, y, w, h, coloreBordo);
    tft_set_text_color(coloreTesto);
    tft_set_text_size(2);
    int16_t tx = x + (w - tft_text_width(label)) / 2;
    int16_t ty = y + (h - 16) / 2;
    tft_set_cursor(tx, ty);
    tft_print(label);
}

void drawValore(const char *etichetta, float valore, int x, int y,
                uint16_t coloreSfondo, uint16_t coloreTesto)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s: %.2f", etichetta, valore);
    tft_fill_rect(x, y, 120, 30, coloreSfondo);
    tft_set_text_color(coloreTesto);
    tft_set_text_size(2);
    tft_set_cursor(x + 5, y + 5);
    tft_print(buf);
}

void drawMachineStateBox(int x, int y, int w, int h)
{
    std::string stateLabel = getStateString(cnc.getMachineState());
    uint16_t bgColor;
    uint16_t fgColor = TFT_BLACK;
    switch (cnc.getMachineState())
    {
    case STATE_IDLE:
        bgColor = TFT_WHITE;
        break;
    case STATE_RUN:
        bgColor = TFT_GREEN;
        break;
    case STATE_HOLD:
        bgColor = TFT_ORANGE;
        break;
    case STATE_ALARM:
        bgColor = TFT_RED;
        break;
    default:
        bgColor = TFT_LIGHTGREY;
        break;
    }
    int rectX = x - w / 2;
    int rectY = y - h / 2;
    tft_fill_round_rect(rectX, rectY, w, h, 5, bgColor);
    tft_draw_round_rect(rectX, rectY, w, h, 5, TFT_YELLOW);

    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_text_datum(4);
    int16_t textWidth = tft_text_width(stateLabel.c_str());
    int16_t textHeight = 16; // Font2 ha altezza 16 pixel
    int16_t textX = x - textWidth / 2;
    int16_t textY = y;

    // Ombra nera (spostata di 1 pixel)
    tft_draw_string(stateLabel.c_str(), textX + 1, textY + 1, TFT_BLACK, 1);
    // Testo principale
    tft_draw_string(stateLabel.c_str(), textX, textY, fgColor, 1);
}

void drawParamBoxUnified(const char *label, float value, int labelX, int labelY,
                         int boxWidth, int state, int decimals,
                         int boxCenterX, int boxCenterY)
{
    uint16_t fg, bg, border, labelColor;
    switch (state)
    {
    case -2:
        fg = TFT_GREEN;
        bg = TFT_BLACK;
        border = TFT_YELLOW;
        labelColor = TFT_CYAN;
        break;
    case -1:
        fg = TFT_WHITE;
        bg = TFT_LIGHTGREY;
        border = TFT_WHITE;
        labelColor = TFT_LIGHTGREY;
        break;
    case 0:
        fg = TFT_CYAN;
        bg = TFT_BLACK;
        border = TFT_YELLOW;
        labelColor = TFT_CYAN;
        break;
    case 1:
        fg = TFT_WHITE;
        bg = TFT_BLACK;
        border = TFT_YELLOW;
        labelColor = TFT_WHITE;
        break;
    default:
        fg = TFT_RED;
        bg = TFT_BLACK;
        border = TFT_YELLOW;
        labelColor = TFT_WHITE;
        break;
    }
    drawFloatValueBox(value, decimals, boxCenterX, boxCenterY, boxWidth, 24, bg, fg, border);
    tft_set_text_color(labelColor);
    tft_set_text_size(1);
    tft_set_cursor(labelX, labelY);
    tft_print(label);
}

void drawFloatValueBox(float value, int decimals, int centerX, int centerY,
                       int w, int h, uint16_t bg, uint16_t fg, uint16_t borderColor)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    int x = centerX - w / 2;
    int y = centerY - h / 2;
    tft_fill_round_rect(x, y, w, h, 8, bg);
    tft_draw_round_rect(x, y, w, h, 8, borderColor);

    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    int16_t textWidth = tft_text_width(buffer); // larghezza in pixel
    int16_t textHeight = tft_font_height();     // altezza in pixel
    int16_t textX = centerX - textWidth / 2;
    int16_t textY = centerY - textHeight / 2;
    tft_set_text_color(fg);
    tft_draw_string(buffer, textX, textY, fg, 1);
}

// =============================================
// Testo centrato
// =============================================
void drawCenteredText2InRoundRect(const std::string &text, int16_t centerX, int16_t centerY,
                                  int16_t rectW, int16_t rectH, uint16_t rectColor, uint16_t textColor)
{
    int16_t x = centerX - rectW / 2;
    int16_t y = centerY - rectH / 2;
    tft_fill_round_rect(x, y, rectW, rectH, 10, rectColor);
    tft_draw_round_rect(x, y, rectW, rectH, 10, TFT_WHITE);
    tft_set_text_color(textColor);
    tft_set_text_size(2);
    int16_t tx = centerX - (text.length() * 6);
    int16_t ty = centerY - 7;
    tft_set_cursor(tx, ty);
    tft_print(text.c_str());
}

void drawCenteredValue4InRoundRect(float value, int decimalPlaces,
                                   int16_t centerX, int16_t centerY,
                                   int16_t rectW, int16_t rectH,
                                   uint16_t rectColor, uint16_t textColor)
{
    int16_t x = centerX - rectW / 2;
    int16_t y = centerY - rectH / 2;
    tft_fill_round_rect(x, y, rectW, rectH, 10, rectColor);
    tft_draw_round_rect(x, y, rectW, rectH, 10, textColor);
    char valueStr[20];
    snprintf(valueStr, sizeof(valueStr), "%.*f", decimalPlaces, value);
    if (decimalPlaces == 0)
    {
        char *p = strchr(valueStr, '.');
        if (p)
            *p = '\0';
    }
    tft_set_text_size(4);
    tft_set_text_datum(4);
    tft_set_text_color(textColor);
    tft_draw_string(valueStr, centerX, centerY, textColor, 4);
}

void draw_X_DRO_Text4InRoundRect(float value, int decimalPlaces,
                                 int16_t centerX, int16_t centerY,
                                 int16_t rectW, int16_t rectH,
                                 uint16_t rectColor, uint16_t textColor, bool R_D)
{
    int16_t x = centerX - rectW / 2;
    int16_t y = centerY - rectH / 2;
    tft_fill_round_rect(x, y, rectW, rectH, 10, rectColor);
    tft_draw_round_rect(x, y, rectW, rectH, 10, textColor);
    char valueStr[20];
    snprintf(valueStr, sizeof(valueStr), "%.*f", decimalPlaces, value);
    tft_set_text_size(4);
    tft_set_text_datum(4);
    tft_set_text_color(textColor);
    int16_t valueWidth = tft_text_width(valueStr);
    tft_draw_string(valueStr, centerX - 8, centerY, textColor, 4);
    tft_set_text_color(TFT_LTBLUE);
    tft_draw_string(R_D ? "D" : "R", centerX + valueWidth / 2 + 4, centerY, TFT_LTBLUE, 4);
}

void drawCenteredFloat2InRoundRect(float value, int decimals, int centerX, int centerY, int w, int h, uint16_t bg, uint16_t fg)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    int x = centerX - w / 2;
    int y = centerY - h / 2;
    tft_fill_round_rect(x, y, w, h, 8, bg);
    tft_draw_round_rect(x, y, w, h, 8, TFT_YELLOW);

    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_text_datum(4);
    int16_t textWidth = tft_text_width(buffer);
    int16_t textHeight = tft_font_height(); // altezza reale del font
    int16_t textX = centerX - textWidth / 2;
    int16_t textY = centerY;
    tft_set_text_color(fg);
    tft_draw_string(buffer, textX, textY, fg, 1);
}

void draw_X_DRO_Text2InRoundRect(float value, int decimals, int centerX, int centerY, int w, int h, uint16_t bg, uint16_t fg, bool reverse)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    char text[32];
    if (reverse)
        snprintf(text, sizeof(text), "-%s", buffer);
    else
        snprintf(text, sizeof(text), "%s", buffer);
    int x = centerX - w / 2;
    int y = centerY - h / 2;
    tft_fill_round_rect(x, y, w, h, 8, bg);
    tft_draw_round_rect(x, y, w, h, 8, TFT_YELLOW);

    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_text_datum(4);
    int16_t textWidth = tft_text_width(text);
    int16_t textHeight = tft_font_height(); // altezza reale del font
    int16_t textX = centerX - textWidth / 2;
    int16_t textY = centerY;
    tft_set_text_color(fg);
    tft_draw_string(text, textX, textY, fg, 1);
}

void drawCenterInteraction(const std::string &text, uint16_t color) {}

// =============================================
// Pulsanti
// =============================================
void drawButton(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t buttonColor, char letter, uint8_t textSizeLetter,
                uint16_t letterColor, const std::string &desc,
                uint8_t textSizeDesc, uint16_t descColor)
{
    // Disegna il bottone (rettangolo arrotondato)
    tft_fill_round_rect(x, y, w, h, 10, buttonColor);
    tft_draw_round_rect(x, y, w, h, 10, TFT_WHITE);

    // ---- Lettera centrale ----
    char letterStr[2] = {letter, '\0'};
    tft_set_text_size(textSizeLetter);
    int16_t letterWidth = tft_text_width(letterStr);
    int16_t letterHeight = tft_font_height(); // altezza del font corrente
    int16_t letterX = x + (w - letterWidth) / 2;
    int16_t letterY = y + (h - letterHeight) / 2;
    tft_draw_string(letterStr, letterX, letterY, letterColor, textSizeLetter);

    // ---- Descrizione sotto il bottone ----
    tft_set_text_size(textSizeDesc);
    int16_t descWidth = tft_text_width(desc.c_str());
    int16_t descHeight = tft_font_height();
    int16_t descX = x + (w - descWidth) / 2;
    int16_t descY = y + h + 4; // 4 pixel di margine dal bordo inferiore
    tft_draw_string(desc.c_str(), descX, descY, descColor, textSizeDesc);
}

void drawButton_h30(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint16_t buttonColor, char letter, uint8_t textSizeLetter,
                    uint16_t letterColor, const std::string &desc,
                    uint8_t textSizeDesc, uint16_t descColor)
{
    tft_fill_round_rect(x, y, w, h, 10, buttonColor);
    tft_draw_round_rect(x, y, w, h, 10, TFT_WHITE);

    // ---- Lettera centrale (calcolo verticale specifico per h=30) ----
    char letterStr[2] = {letter, '\0'};
    tft_set_text_size(textSizeLetter);
    int16_t letterWidth = tft_text_width(letterStr);
    // Altezza lettera: 16 pixel per textSizeLetter=2 (Font2)
    int16_t letterHeight = 16; // fissa perché textSizeLetter=2 in Jog
    int16_t letterX = x + (w - letterWidth) / 2;
    int16_t letterY = y + (h - letterHeight) + 2;
    tft_draw_string(letterStr, letterX, letterY, letterColor, textSizeLetter);

    // ---- Descrizione sotto il bottone (identica alla versione normale) ----
    tft_set_text_size(textSizeDesc);
    int16_t descWidth = tft_text_width(desc.c_str());
    int16_t descX = x + (w - descWidth) / 2;
    int16_t descY = y + h + 7;
    tft_draw_string(desc.c_str(), descX, descY, descColor, textSizeDesc);
}

void drawBottomButtons(const ButtonConfig buttons[], int numButtons)
{
    const int startX = 73;
    const int startY = 260;
    const int buttonW = 40;
    const int buttonH = 40;
    const int spacing = 10;
    for (int i = 0; i < numButtons; i++)
    {
        int x = startX + i * (buttonW + spacing);
        drawButton(x, startY, buttonW, buttonH,
                   buttons[i].bgColor,
                   buttons[i].key, 2, buttons[i].letterColor,
                   buttons[i].label, 1, buttons[i].descColor);
    }
}

void drawMachiningButtons()
{
    resetTextStyle();
    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);

    const int btnW = 70; // larghezza ridotta
    const int btnH = 30;
    const int btnY = 270;
    const int spacing = 8; // spazio tra pulsanti
    const int numButtons = 5;
    const int totalWidth = numButtons * btnW + (numButtons - 1) * spacing;
    const int startX = (480 - totalWidth) / 2;

    drawButton(startX + 0 * (btnW + spacing), btnY, btnW, btnH, TFT_DARKGREEN, 'A', 2, TFT_WHITE, "GENERATE", 1, TFT_WHITE);
    drawButton(startX + 1 * (btnW + spacing), btnY, btnW, btnH, TFT_DARKGREY, '1', 2, TFT_WHITE, "PREV", 1, TFT_WHITE);
    drawButton(startX + 2 * (btnW + spacing), btnY, btnW, btnH, TFT_DARKGREY, '2', 2, TFT_WHITE, "NEXT", 1, TFT_WHITE);
    drawButton(startX + 3 * (btnW + spacing), btnY, btnW, btnH, TFT_DARKGREY, 'C', 2, TFT_WHITE, "EDIT", 1, TFT_WHITE);
    drawButton(startX + 4 * (btnW + spacing), btnY, btnW, btnH, TFT_RED, 'D', 2, TFT_WHITE, "EXIT", 1, TFT_WHITE);
}

void drawMainButtons()
{
    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
    drawButton(30, 270, 100, 30, TFT_DARKGREY, '1', 2, TFT_WHITE, "PREV", 1, TFT_WHITE);
    drawButton(140, 270, 100, 30, TFT_DARKGREY, '2', 2, TFT_WHITE, "NEXT", 1, TFT_WHITE);
    drawButton(250, 270, 100, 30, TFT_DARKGREY, 'C', 2, TFT_WHITE, "INPUT", 1, TFT_WHITE);
    drawButton(360, 270, 100, 30, TFT_DARKGREY, 'D', 2, TFT_WHITE, "EXIT", 1, TFT_WHITE);
}

void drawInputButtons()
{
    resetTextStyle();
    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
    drawButton(25, 270, 100, 30, TFT_BLACK, 'C', 2, TFT_WHITE, "CONFIRM", 1, TFT_WHITE);
    drawButton(145, 270, 100, 30, TFT_BLACK, 'B', 2, TFT_WHITE, "CANCEL", 1, TFT_WHITE);
    drawButton(260, 270, 40, 30, TFT_BLACK, '*', 2, TFT_WHITE, "(.)", 1, TFT_WHITE);
    drawButton(320, 270, 40, 30, TFT_BLACK, '#', 2, TFT_WHITE, "(-)", 1, TFT_WHITE);
    drawCenteredText2InRoundRect("0 - 9", 380 + 40, 270 + 15, 80, 30, TFT_BLACK, TFT_WHITE);
}

// =============================================
// Utility
// =============================================
void drawCheckbox(const char *label, bool checked, int x, int y, bool selected, int state)
{
    resetTextStyle();
    uint16_t fg = (state < 0) ? TFT_DARKGREY : (selected ? TFT_WHITE : TFT_CYAN);
    uint16_t textColor = (state < 0) ? TFT_LIGHTGREY : fg;
    tft_set_text_color(textColor);
    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_cursor(x, y);
    tft_print(label);
    int boxX = x + tft_text_width(label) + 5;
    // int boxY = y - 6;
    int boxY = y + 2;
    tft_fill_rect(boxX, boxY, 12, 12, TFT_BLACK);
    tft_draw_rect(boxX, boxY, 12, 12, fg);
    if (checked && state >= 0)
    {
        tft_draw_line(boxX + 2, boxY + 2, boxX + 10, boxY + 10, fg);
        tft_draw_line(boxX + 10, boxY + 2, boxX + 2, boxY + 10, fg);
    }
}

bool isValidNumericKey(char key, const std::string &buffer, bool hasDecimal)
{
    if (key >= '0' && key <= '9')
        return true;
    if (key == '#' && buffer.empty())
        return true;
    if (key == '*' && !hasDecimal)
        return true;
    return false;
}

void drawInputPreviewBox(int cx, int cy, int w, int h, const std::string &buffer,
                         bool blinkBorder, uint16_t bg, uint16_t fg, uint16_t border)
{
    int boxX = cx - w/2;
    int boxY = cy - h/2;
    
    // Sfondo
    tft_fill_round_rect(boxX, boxY, w, h, 8, bg);
    // Bordo (lampeggiante o normale)
    uint16_t actualBorder = blinkBorder ? TFT_YELLOW : border;
    tft_draw_round_rect(boxX, boxY, w, h, 8, actualBorder);
    
    // Testo solo se buffer non vuoto
    if (!buffer.empty()) {
        tft_set_text_color(fg);
        tft_set_font(&fonts::Font2);
        tft_set_text_size(1);
        int16_t textWidth = tft_text_width(buffer.c_str());
        int16_t textX = cx - textWidth / 2;
        int16_t textY = cy - tft_font_height() / 2;
        tft_draw_string(buffer.c_str(), textX, textY, fg, 1);
    }
}

// =============================================
// DRO e sidebar
// =============================================
static float calculateKeywayAngleDeg()
{
    float a = DRO_A;
    if (a < 0.0f)
        a = 0.0f;
    if (a > 1.0f)
        a = 1.0f;
    return a * 360.0f;
}

void drawDROBoxes()
{
    const int boxW = 100;
    const int boxH = 30;
    const int centerX = 420;
    const int labelX = 320;
    if (activeGcodeType == "Keyway")
    {
        float angleDeg = calculateKeywayAngleDeg();
        drawCenteredFloat2InRoundRect(angleDeg, 0, centerX, 74, boxW, boxH, TFT_BLACK, TFT_YELLOW);
        tft_set_text_color(TFT_YELLOW);
        tft_set_cursor(labelX, 74);
        tft_print("ANGLE");
    }
    else
    {
        drawCenteredFloat2InRoundRect(DRO_RPM, 0, centerX, 74, boxW, boxH, TFT_BLACK, TFT_YELLOW);
        tft_set_text_color(TFT_YELLOW);
        tft_set_cursor(labelX, 74);
        tft_print("RPM");
    }
    draw_X_DRO_Text2InRoundRect(k_XDRO * DRO_X, 3, centerX, 110, boxW, boxH, TFT_BLACK, TFT_YELLOW, R_D);
    tft_set_text_color(TFT_YELLOW);
    tft_set_cursor(labelX, 110);
    tft_print("DRO X");
    drawCenteredFloat2InRoundRect(DRO_Z, 3, centerX, 146, boxW, boxH, TFT_BLACK, TFT_YELLOW);
    tft_set_text_color(TFT_YELLOW);
    tft_set_cursor(labelX, 146);
    tft_print("DRO Z");
    drawMachineStateBox(420, 218, 100, 30);
    tft_set_text_color(TFT_YELLOW);
    tft_set_cursor(labelX - 5, 218);
    tft_print("STATUS");
}

void updateDROBoxesIfChanged()
{
    const int boxW = 100;
    const int boxH = 30;
    const int centerX = 420;
    const int labelX = 320;
    if (activeGcodeType == "Keyway")
    {
        float angleDeg = calculateKeywayAngleDeg();
        if (fabs(angleDeg - last_angleDeg) > 0.01f)
        {
            drawCenteredFloat2InRoundRect(angleDeg, 0, centerX, 74, boxW, boxH, TFT_BLACK, TFT_YELLOW);
            last_angleDeg = angleDeg;
        }
    }
    else
    {
        if (fabs(DRO_RPM - last_DRO_RPM) > 0.1f)
        {
            drawCenteredFloat2InRoundRect(DRO_RPM, 0, centerX, 74, boxW, boxH, TFT_BLACK, TFT_YELLOW);
            last_DRO_RPM = DRO_RPM;
        }
    }
    float xVal = k_XDRO * DRO_X;
    if (fabs(xVal - last_DRO_X) > 0.01f)
    {
        draw_X_DRO_Text2InRoundRect(k_XDRO * DRO_X, 3, centerX, 110, boxW, boxH, TFT_BLACK, TFT_YELLOW, R_D);
        last_DRO_X = xVal;
    }
    if (fabs(DRO_Z - last_DRO_Z) > 0.01f)
    {
        drawCenteredFloat2InRoundRect(DRO_Z, 3, centerX, 146, boxW, boxH, TFT_BLACK, TFT_YELLOW);
        last_DRO_Z = DRO_Z;
    }
    std::string currentState = getStateString(cnc.getMachineState());
    if (currentState != last_GRBL_State)
    {
        drawMachineStateBox(420, 218, 100, 30);
        last_GRBL_State = currentState;
    }
}

static float last_cal_offset_x = 0.0f;
static float last_cal_offset_z = 0.0f;
static float last_dro_x = 0.0f;
static float last_dro_z = 0.0f;
static std::string last_state = "";

static const int DRO_BOX_W = 100;
static const int DRO_BOX_H = 30;
static const int DRO_BOX_CX = 420;
static const int DRO_LABEL_X = 325;

// Y positions
static const int Y_DRO_X    = 74; 
static const int Y_DRO_Z    = 110;
static const int Y_OFFSET_X = 146; 
static const int Y_OFFSET_Z = 182; 
static const int Y_STATUS   = 218;

void draw_dro_cal(float offset_x, float offset_z)
{
    // DRO X (giallo)
    drawCenteredFloat2InRoundRect(DRO_X, 3, DRO_BOX_CX, Y_DRO_X, DRO_BOX_W, DRO_BOX_H, TFT_BLACK, TFT_YELLOW);
    tft_set_text_color(TFT_YELLOW);
    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_cursor(DRO_LABEL_X, Y_DRO_X);
    tft_print("DRO X");
    
    // DRO Z (giallo)
    drawCenteredFloat2InRoundRect(DRO_Z, 3, DRO_BOX_CX, Y_DRO_Z, DRO_BOX_W, DRO_BOX_H, TFT_BLACK, TFT_YELLOW);
    tft_set_text_color(TFT_YELLOW);
    tft_set_cursor(DRO_LABEL_X, Y_DRO_Z);
    tft_print("DRO Z");
    
    // OFFSET X (verde)
    drawCenteredFloat2InRoundRect(offset_x, 3, DRO_BOX_CX, Y_OFFSET_X, DRO_BOX_W, DRO_BOX_H, TFT_BLACK, TFT_GREEN);
    tft_set_text_color(TFT_YELLOW);
    tft_set_cursor(DRO_LABEL_X-25, Y_OFFSET_X);
    tft_print("OFFSET X");
    
    // OFFSET Z (verde)
    drawCenteredFloat2InRoundRect(offset_z, 3, DRO_BOX_CX, Y_OFFSET_Z, DRO_BOX_W, DRO_BOX_H, TFT_BLACK, TFT_GREEN);
    tft_set_text_color(TFT_YELLOW);
    tft_set_cursor(DRO_LABEL_X-25, Y_OFFSET_Z);
    tft_print("OFFSET Z");
    
    // STATUS box (come in drawDROBoxes)
    drawMachineStateBox(DRO_BOX_CX, Y_STATUS, DRO_BOX_W, DRO_BOX_H);
    tft_set_text_color(TFT_YELLOW);
    tft_set_cursor(DRO_LABEL_X - 10, Y_STATUS);
    tft_print("STATUS");
    
    last_dro_x = DRO_X;
    last_dro_z = DRO_Z;
    last_cal_offset_x = offset_x;
    last_cal_offset_z = offset_z;
    last_state = getStateString(cnc.getMachineState());
}

void update_dro_cal(float offset_x, float offset_z)
{
    bool changed = false;
    if (fabs(DRO_X - last_dro_x) >= 0.001f) {
        drawCenteredFloat2InRoundRect(DRO_X, 3, DRO_BOX_CX, Y_DRO_X, DRO_BOX_W, DRO_BOX_H, TFT_BLACK, TFT_YELLOW);
        last_dro_x = DRO_X;
        changed = true;
    }
    if (fabs(DRO_Z - last_dro_z) >= 0.001f) {
        drawCenteredFloat2InRoundRect(DRO_Z, 3, DRO_BOX_CX, Y_DRO_Z, DRO_BOX_W, DRO_BOX_H, TFT_BLACK, TFT_YELLOW);
        last_dro_z = DRO_Z;
        changed = true;
    }
    if (fabs(offset_x - last_cal_offset_x) >= 0.001f) {
        drawCenteredFloat2InRoundRect(offset_x, 3, DRO_BOX_CX, Y_OFFSET_X, DRO_BOX_W, DRO_BOX_H, TFT_BLACK, TFT_GREEN);
        last_cal_offset_x = offset_x;
        changed = true;
    }
    if (fabs(offset_z - last_cal_offset_z) >= 0.001f) {
        drawCenteredFloat2InRoundRect(offset_z, 3, DRO_BOX_CX, Y_OFFSET_Z, DRO_BOX_W, DRO_BOX_H, TFT_BLACK, TFT_GREEN);
        last_cal_offset_z = offset_z;
        changed = true;
    }
    std::string currentState = getStateString(cnc.getMachineState());
    if (currentState != last_state) {
        drawMachineStateBox(DRO_BOX_CX, Y_STATUS, DRO_BOX_W, DRO_BOX_H);
        last_state = currentState;
        changed = true;
    }
    // Se nessun cambiamento, non fare nulla
}

/*void drawMonitorSidebarPreview() {}
void drawMonitorSidebarGcode() {}
void drawMonitorSidebarM8Gcode() {}
void drawGcodeTypeBox(const std::string &gcodeType)

{
    const int px = 310;
    const int py = 55;
    const int pw = 150;
    const int margin = 1;
    const int w = pw - (margin * 2);
    const int h = 20;
    int x = px + margin;
    int y = py + 1;
    tft_fill_rect(x, y, w, h, TFT_BLACK);
    tft_set_text_size(1);
    tft_set_text_datum(4);
    tft_set_text_color_bg(TFT_YELLOW, TFT_BLACK);
    std::string upper = gcodeType;
    for (auto &c : upper)
        c = toupper(c);
    tft_draw_string(upper.c_str(), x + w / 2, y + h / 2, TFT_YELLOW, 1);
}*/


