#ifndef GRAPHICS_HELPERS_H
#define GRAPHICS_HELPERS_H

#include <string>
#include <vector>
#include "global_vars.h"
#include "graphics_primitive.h"
#include "image_manager.h"

struct ButtonConfig
{
    char key;
    const char *label;
    uint16_t bgColor;
    uint16_t letterColor;
    uint16_t descColor;
};

class CommandTracker
{
public:
    void reset() { activeIndex = 0; }
    void advance() { activeIndex++; }
    int getActiveIndex() const { return activeIndex; }
    void setMaxLines(int maxLines) { maxIndex = maxLines; }
    bool isComplete() const { return activeIndex >= maxIndex; }

private:
    int activeIndex = 0;
    int maxIndex = 0;
};

// ===== Stile e messaggi =====
void resetTextStyle();
void drawCenteredTextAt(const char* text, int centerX, int centerY, uint8_t textSize, uint16_t textColor);
void drawTitolo(const char *titolo, uint8_t textSize);
void drawMessaggio(const char *msg, int y, uint16_t coloreTesto, uint16_t coloreSfondo);

// ===== Box e valori =====
void drawBox(const char *label, int x, int y, int w, int h, uint16_t coloreBordo, uint16_t coloreTesto);
void drawValore(const char *etichetta, float valore, int x, int y, uint16_t coloreSfondo, uint16_t coloreTesto);
void drawMachineStateBox(int x, int y, int w, int h);
void drawParamBoxUnified(const char *label, float value, int labelX, int labelY, int boxWidth,
                         int state, int decimals, int boxCenterX, int boxCenterY);
void drawFloatValueBox(float value, int decimals, int centerX, int centerY, int w, int h,
                       uint16_t bg, uint16_t fg, uint16_t borderColor);

// ===== Testo centrato =====
void drawCenteredText2InRoundRect(const std::string &text, int16_t centerX, int16_t centerY,
                                  int16_t rectW, int16_t rectH, uint16_t rectColor, uint16_t textColor);
void drawCenteredValue4InRoundRect(float value, int decimalPlaces, int16_t centerX, int16_t centerY,
                                   int16_t rectW, int16_t rectH, uint16_t rectColor, uint16_t textColor);
void draw_X_DRO_Text4InRoundRect(float value, int decimalPlaces, int16_t centerX, int16_t centerY,
                                 int16_t rectW, int16_t rectH, uint16_t rectColor, uint16_t textColor, bool R_D);
void drawCenteredFloat2InRoundRect(float value, int decimals, int16_t centerX, int16_t centerY,
                                   int16_t rectW, int16_t rectH, uint16_t rectColor, uint16_t textColor);
void draw_X_DRO_Text2InRoundRect(float value, int decimals, int16_t centerX, int16_t centerY,
                                 int16_t rectW, int16_t rectH, uint16_t rectColor, uint16_t textColor, bool R_D);
void drawCenterInteraction(const std::string &text, uint16_t color);

// ===== Pulsanti =====
void drawButton(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t buttonColor, char letter, uint8_t textSizeLetter,
                uint16_t letterColor, const std::string &desc,
                uint8_t textSizeDesc, uint16_t descColor);
void drawButton_h30(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint16_t buttonColor, char letter, uint8_t textSizeLetter,
                    uint16_t letterColor, const std::string &desc,
                    uint8_t textSizeDesc, uint16_t descColor);
void drawBottomButtons(const ButtonConfig buttons[], int numButtons);
void drawMachiningButtons();
void drawMainButtons();
void drawInputButtons();

// ===== Utility =====
void drawCheckbox(const char *label, bool checked, int x, int y, bool selected, int state = 0);
bool isValidNumericKey(char key, const std::string &buffer, bool hasDecimal);
void drawInputPreviewBox(int cx, int cy, int w, int h, const std::string &buffer,
                         bool blinkBorder, uint16_t bg, uint16_t fg, uint16_t border);

// ===== DRO e sidebar =====
void drawDROBoxes();
void updateDROBoxesIfChanged();
void draw_dro_cal(float offset_x, float offset_z);
void update_dro_cal(float offset_x, float offset_z);
void drawMonitorSidebarPreview();
void drawMonitorSidebarGcode();
void drawMonitorSidebarM8Gcode();
void drawGcodeTypeBox(const std::string &gcodeType);

#endif