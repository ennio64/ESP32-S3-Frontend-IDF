#include "tool_catalog_scene.h"
#include "graphics_helpers.h"
#include "graphics_primitive.h"
#include "graphics_lgfx.h"
#include "image_manager.h"
#include "keypad.h"
#include "colors.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

enum ToolFamily
{
    FAMILY_EXTERNAL,
    FAMILY_INTERNAL,
    FAMILY_MICRO
};

static ToolFamily currentFamily = FAMILY_EXTERNAL;
static int currentIndex = 0;
static bool needsRedraw = true;
static uint8_t *currentImageBuffer = nullptr;
static size_t currentImageSize = 0;
static bool inFamilySelection = true;

struct CatalogItem
{
    const char *name;
    const char *image_file;
    int type;
    float width;
    float feed;
    int rpm;
    int material;
    const char *description; // "Insert XYZ - operation"
    ProbeType probe_type;
    ToolHand hand;
    float degree;
};

// ========== EXTERNAL TOOLS (custom order) ==========
static const CatalogItem externalTools[] = {
    // Primi 8 (ordine originale)
    {"SCLCL", "E_SCLCL.png", TOOL_TYPE_EXTERNAL, 8.0f, 180.0f, 1400, TOOL_MATERIAL_ALUMINUM,
     "Insert CCMT - turning left", PROBE_STANDARD, TOOL_HAND_LEFT, 95.0f},
    {"SCLCR", "E_SCLCR.png", TOOL_TYPE_EXTERNAL, 8.0f, 180.0f, 1400, TOOL_MATERIAL_STEEL,
     "Insert CCMT - turning right", PROBE_STANDARD, TOOL_HAND_RIGHT, 95.0f},
    {"SCMCN100", "E_SCMCN100.png", TOOL_TYPE_EXTERNAL, 12.0f, 250.0f, 900, TOOL_MATERIAL_STEEL,
     "Insert CCMT - roughing", PROBE_STANDARD, TOOL_HAND_RIGHT, 45.0f},
    {"SDJCL", "E_SDJCL.png", TOOL_TYPE_EXTERNAL, 10.0f, 200.0f, 1100, TOOL_MATERIAL_STEEL,
     "Insert DCMT - turning left", PROBE_STANDARD, TOOL_HAND_LEFT, 93.0f},
    {"SDJCR", "E_SDJCR.png", TOOL_TYPE_EXTERNAL, 10.0f, 200.0f, 1100, TOOL_MATERIAL_STEEL,
     "Insert DCMT - turning right", PROBE_STANDARD, TOOL_HAND_RIGHT, 93.0f},
    {"SDNCN", "E_SDNCN.png", TOOL_TYPE_EXTERNAL, 10.0f, 180.0f, 1200, TOOL_MATERIAL_STEEL,
     "Insert DCMT - neutral turning", PROBE_STANDARD, TOOL_HAND_RIGHT, 62.5f},
    {"SER", "E_SER.png", TOOL_TYPE_EXTERNAL, 8.0f, 120.0f, 600, TOOL_MATERIAL_STEEL,
     "Insert TN - threading", PROBE_STANDARD, TOOL_HAND_RIGHT, 45.0f},
    {"STECR", "E_STECR.png", TOOL_TYPE_EXTERNAL, 10.0f, 160.0f, 800, TOOL_MATERIAL_STEEL,
     "Insert TCMT - parting 90°", PROBE_STANDARD, TOOL_HAND_RIGHT, 90.0f},
    {"MGEHR", "E_MGEHR.png", TOOL_TYPE_EXTERNAL, 12.0f, 100.0f, 800, TOOL_MATERIAL_STEEL,
     "Insert MGMN - grooving / cave", PROBE_STANDARD, TOOL_HAND_RIGHT, 90.0f},
    {"SRDPN", "E_SRDPN.png", TOOL_TYPE_EXTERNAL, 8.0f, 150.0f, 1200, TOOL_MATERIAL_STEEL,
     "Insert RPMT - rolling / rullatura", PROBE_STANDARD, TOOL_HAND_RIGHT, 0.0f},
    {"SPB", "E_SPB.png", TOOL_TYPE_EXTERNAL, 4.0f, 100.0f, 2000, TOOL_MATERIAL_BRASS,
     "Insert SPB - parting / troncatura", PROBE_STANDARD, TOOL_HAND_RIGHT, 90.0f},
    {"KNR", "E_KNR.png", TOOL_TYPE_EXTERNAL, 8.0f, 80.0f, 600, TOOL_MATERIAL_STEEL,
     "Knurling wheel - zigrinatura", PROBE_STANDARD, TOOL_HAND_RIGHT, 45.0f}};
static const int externalCount = sizeof(externalTools) / sizeof(externalTools[0]);

// ========== INTERNAL TOOLS ==========
static const CatalogItem internalTools[] = {
    {"SCLCR", "I_SCLCR.png", TOOL_TYPE_INTERNAL, 6.0f, 120.0f, 1400, TOOL_MATERIAL_ALUMINUM,
     "Insert CCMT - boring / alesaggio", PROBE_INTERNAL, TOOL_HAND_RIGHT, 95.0f},
    {"SNGR", "I_SNGR.png", TOOL_TYPE_INTERNAL, 8.0f, 130.0f, 1200, TOOL_MATERIAL_STEEL,
     "Insert SNGR - internal grooving", PROBE_INTERNAL, TOOL_HAND_RIGHT, 90.0f},
    {"SNR", "I_SNR.png", TOOL_TYPE_INTERNAL, 6.0f, 110.0f, 800, TOOL_MATERIAL_STEEL,
     "Insert SNR - internal threading", PROBE_INTERNAL, TOOL_HAND_RIGHT, 45.0f},
    {"STFCR", "I_STFCR.png", TOOL_TYPE_INTERNAL, 6.0f, 120.0f, 1500, TOOL_MATERIAL_ALUMINUM,
     "Insert TCMT - internal finishing", PROBE_INTERNAL, TOOL_HAND_RIGHT, 90.0f},
    {"STWCR", "I_STWCR.png", TOOL_TYPE_INTERNAL, 6.0f, 120.0f, 1500, TOOL_MATERIAL_ALUMINUM,
     "Insert TCMT - deep holes 60°", PROBE_INTERNAL, TOOL_HAND_RIGHT, 60.0f}};
static const int internalCount = sizeof(internalTools) / sizeof(internalTools[0]);

// ========== MICRO TOOLS ==========
static const CatalogItem microTools[] = {
    {"MFR", "MFR.png", TOOL_TYPE_EXTERNAL, 2.0f, 50.0f, 5000, TOOL_MATERIAL_STEEL,
     "Micro insert - roughing", PROBE_STANDARD, TOOL_HAND_RIGHT, 45.0f},
    {"MGR", "MGR.png", TOOL_TYPE_EXTERNAL, 1.5f, 40.0f, 6000, TOOL_MATERIAL_ALUMINUM,
     "Micro insert - finishing", PROBE_STANDARD, TOOL_HAND_RIGHT, 45.0f},
    {"MIR", "MIR.png", TOOL_TYPE_INTERNAL, 1.0f, 30.0f, 8000, TOOL_MATERIAL_ALUMINUM,
     "Micro boring bar - alesaggio", PROBE_INTERNAL, TOOL_HAND_RIGHT, 90.0f},
    {"MKR", "MKR.png", TOOL_TYPE_EXTERNAL, 1.0f, 30.0f, 6000, TOOL_MATERIAL_BRASS,
     "Micro profiling", PROBE_STANDARD, TOOL_HAND_RIGHT, 45.0f},
    {"MNR", "MNR.png", TOOL_TYPE_EXTERNAL, 2.0f, 45.0f, 5000, TOOL_MATERIAL_STEEL,
     "Micro turning", PROBE_STANDARD, TOOL_HAND_RIGHT, 45.0f},
    {"MPR", "MPR.png", TOOL_TYPE_EXTERNAL, 1.5f, 35.0f, 6000, TOOL_MATERIAL_BRASS,
     "Micro rolling", PROBE_STANDARD, TOOL_HAND_RIGHT, 0.0f},
    {"MQR", "MQR.png", TOOL_TYPE_EXTERNAL, 2.0f, 50.0f, 5000, TOOL_MATERIAL_STEEL,
     "Micro profiling", PROBE_STANDARD, TOOL_HAND_RIGHT, 45.0f},
    {"MTR", "MTR.png", TOOL_TYPE_EXTERNAL, 2.0f, 50.0f, 5000, TOOL_MATERIAL_STEEL,
     "Micro turning", PROBE_STANDARD, TOOL_HAND_RIGHT, 45.0f},
    {"MUR", "MUR.png", TOOL_TYPE_EXTERNAL, 1.5f, 40.0f, 6000, TOOL_MATERIAL_STEEL,
     "Micro universal", PROBE_STANDARD, TOOL_HAND_RIGHT, 45.0f}};
static const int microCount = sizeof(microTools) / sizeof(microTools[0]);

static const CatalogItem *getCurrentArray(int &count)
{
    switch (currentFamily)
    {
    case FAMILY_EXTERNAL:
        count = externalCount;
        return externalTools;
    case FAMILY_INTERNAL:
        count = internalCount;
        return internalTools;
    case FAMILY_MICRO:
        count = microCount;
        return microTools;
    default:
        count = 0;
        return nullptr;
    }
}

static void drawToolImage(int x, int y, int w, int h, const char *filename)
{
    if (currentImageBuffer)
    {
        free(currentImageBuffer);
        currentImageBuffer = nullptr;
        currentImageSize = 0;
    }
    char path[64];
    snprintf(path, sizeof(path), "/spiffs/tools/%s", filename);
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        tft_fill_rect(x, y, w, h, TFT_DARKGREY);
        tft_draw_rect(x, y, w, h, TFT_WHITE);
        drawCenteredTextAt("NO IMAGE", x + w / 2, y + h / 2, 1, TFT_RED);
        return;
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    currentImageBuffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!currentImageBuffer)
        currentImageBuffer = (uint8_t *)malloc(size);
    if (currentImageBuffer)
    {
        fread(currentImageBuffer, 1, size, f);
        currentImageSize = size;
        get_tft().drawPng(currentImageBuffer, currentImageSize, x, y);
    }
    else
    {
        tft_fill_rect(x, y, w, h, TFT_DARKGREY);
        drawCenteredTextAt("OOM", x + w / 2, y + h / 2, 1, TFT_RED);
    }
    fclose(f);
}

// Family selection screen
static void drawFamilyButton(int x, int y, int w, int h, char letter, const char *text, uint16_t bgColor)
{
    resetTextStyle();
    // Rettangolo arrotondato di sfondo
    tft_fill_round_rect(x, y, w, h, 10, bgColor);
    tft_draw_round_rect(x, y, w, h, 10, TFT_WHITE);

    // Quadrato con angoli smussati per la lettera (a sinistra, margine 10px)
    int boxSize = h - 12; // leggermente più piccolo per margine
    int boxX = x + 10;
    int boxY = y + (h - boxSize) / 2;
    tft_fill_round_rect(boxX, boxY, boxSize, boxSize, 5, TFT_BLACK);
    tft_draw_round_rect(boxX, boxY, boxSize, boxSize, 5, TFT_WHITE);

    // Lettera centrata nel quadratino
    char letterStr[2] = {letter, '\0'};
    int letterCenterX = boxX + boxSize / 2;
    int letterCenterY = boxY + boxSize / 2;
    drawCenteredTextAt(letterStr, letterCenterX + 2, letterCenterY, 2, TFT_WHITE);

    // Testo centrato nell'area rimanente del pulsante (a destra del quadratino)
    int textAreaX = boxX + boxSize;
    int textAreaW = w - (textAreaX - x) - 10; // margine destro
    int textCenterX = textAreaX + textAreaW / 2;
    int textCenterY = y + h / 2;
    drawCenteredTextAt(text, textCenterX - 20, textCenterY, 2, TFT_WHITE);
}

// Schermata iniziale di selezione famiglia
static void drawFamilySelection()
{
    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("TOOLS CATALOG", 240, 15, 2, TFT_YELLOW);
    tft_draw_line(20, 30, 460, 30, TFT_WHITE);

    int btnW = 340, btnH = 50, spacing = 20;
    int totalHeight = btnH * 4 + spacing * 3;
    int startY = (320 - totalHeight) / 2 + 20;
    int btnX = (480 - btnW) / 2;

    // Pulsanti famiglia
    drawFamilyButton(btnX, startY, btnW, btnH, 'A', "EXTERNAL TOOLS", TFT_DARKGREEN);
    drawFamilyButton(btnX, startY + btnH + spacing, btnW, btnH, 'B', "INTERNAL TOOLS", TFT_DARKGREEN);
    drawFamilyButton(btnX, startY + 2 * (btnH + spacing), btnW, btnH, 'C', "MICRO TOOLS", TFT_DARKGREEN);

    // Pulsante EXIT (stesso stile, colore rosso)
    drawFamilyButton(btnX, startY + 3 * (btnH + spacing), btnW, btnH, 'D', "EXIT", TFT_RED);
}

// Tool browser screen
static void drawToolBrowser()
{
    int count = 0;
    const CatalogItem *arr = getCurrentArray(count);
    if (!arr || count == 0)
        return;
    const CatalogItem &item = arr[currentIndex];

    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("TOOLS CATALOG", 240, 15, 2, TFT_YELLOW);
    tft_draw_line(20, 30, 460, 30, TFT_WHITE);

    // ========== ADJUSTABLE IMAGE POSITION ==========
    int imgX = 40;  // X position (left/right)
    int imgY = 78;  // Y position (up/down)
    int imgW = 120; // width in pixels
    int imgH = 120; // height in pixels
    // ==============================================

    int textX = imgX + imgW + 20; // dynamic text starting X
    int lineY = 50;               // fixed Y for name and parameters

    tft_set_font(&fonts::Font2);
    tft_set_text_size(2);
    tft_set_text_color(TFT_CYAN);
    tft_set_cursor(textX, lineY);
    tft_print(item.name);
    lineY += 28;

    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_text_color(TFT_WHITE);

    tft_set_cursor(textX, lineY);
    tft_printf("Width: %.1f mm", item.width);
    lineY += 22;
    tft_set_cursor(textX, lineY);
    tft_printf("Feed: %.0f mm/min", item.feed);
    lineY += 22;
    tft_set_cursor(textX, lineY);
    tft_printf("RPM: %d", item.rpm);
    lineY += 22;
    const char *mat = (item.material == TOOL_MATERIAL_STEEL) ? "Steel" : (item.material == TOOL_MATERIAL_ALUMINUM) ? "Aluminum"
                                                                                                                   : "Brass";
    tft_set_cursor(textX, lineY);
    tft_printf("Material: %s", mat);
    lineY += 22;

    if (item.degree > 0.1f)
    {
        tft_set_cursor(textX, lineY);
        tft_printf("Angle: %.1f deg", item.degree);
    }
    else
    {
        tft_set_cursor(textX, lineY);
        tft_printf("Angle: N/A");
    }
    lineY += 22;

    tft_set_cursor(textX, lineY);
    tft_printf("Task: %s", item.description);
    lineY += 22;
    tft_set_cursor(textX, lineY);
    tft_printf("Hand: %s", item.hand == TOOL_HAND_RIGHT ? "RIGHT" : "LEFT");

    // Draw image with adjustable parameters
    tft_fill_rect(imgX - 2, imgY - 2, imgW + 4, imgH + 4, TFT_WHITE);
    drawToolImage(imgX, imgY, imgW, imgH, item.image_file);

    tft_draw_line(20, 258, 460, 258, TFT_WHITE);

    resetTextStyle();
    const int btnW = 80, btnH = 30, btnY = 270, spacing = 15;
    const int totalWidth = btnW * 4 + spacing * 3;
    const int startX = (480 - totalWidth) / 2;

    drawButton(startX, btnY, btnW, btnH, TFT_BLACK, 'B', 2, TFT_WHITE, "PREV", 1, TFT_WHITE);
    drawButton(startX + btnW + spacing, btnY, btnW, btnH, TFT_BLACK, 'A', 2, TFT_WHITE, "NEXT", 1, TFT_WHITE);
    drawButton(startX + 2 * (btnW + spacing), btnY, btnW, btnH, TFT_DARKGREEN, 'C', 2, TFT_WHITE, "SELECT", 1, TFT_WHITE);
    drawButton(startX + 3 * (btnW + spacing), btnY, btnW, btnH, TFT_RED, 'D', 2, TFT_WHITE, "BACK", 1, TFT_WHITE);

    char page[32];
    snprintf(page, sizeof(page), "%d / %d", currentIndex + 1, count);
    tft_set_text_color(TFT_YELLOW);
    tft_set_text_datum(TC_DATUM);
    drawCenteredTextAt(page, 240, 245, 1, TFT_YELLOW);
}

void tool_catalog_scene_enter(ToolData *out_data, bool *selected)
{
    inFamilySelection = true;
    currentIndex = 0;
    needsRedraw = true;
    *selected = false;

    while (true)
    {
        if (needsRedraw)
        {
            if (inFamilySelection)
            {
                drawFamilySelection();
            }
            else
            {
                drawToolBrowser();
            }
            needsRedraw = false;
        }

        char key = readKeypad();
        if (inFamilySelection)
        {
            switch (key)
            {
            case 'A':
                currentFamily = FAMILY_EXTERNAL;
                currentIndex = 0;
                inFamilySelection = false;
                needsRedraw = true;
                break;
            case 'B':
                currentFamily = FAMILY_INTERNAL;
                currentIndex = 0;
                inFamilySelection = false;
                needsRedraw = true;
                break;
            case 'C':
                currentFamily = FAMILY_MICRO;
                currentIndex = 0;
                inFamilySelection = false;
                needsRedraw = true;
                break;
            case 'D':
                *selected = false;
                goto exit_catalog;
            default:
                break;
            }
        }
        else
        {
            switch (key)
            {
            case 'D':
                inFamilySelection = true;
                needsRedraw = true;
                break;
            case 'B':
            {
                int count = 0;
                getCurrentArray(count);
                if (count > 0)
                {
                    currentIndex = (currentIndex - 1 + count) % count;
                    needsRedraw = true;
                }
            }
            break;
            case 'A':
            {
                int count = 0;
                getCurrentArray(count);
                if (count > 0)
                {
                    currentIndex = (currentIndex + 1) % count;
                    needsRedraw = true;
                }
            }
            break;
            case 'C':
            {
                int count = 0;
                const CatalogItem *arr = getCurrentArray(count);
                if (arr && count > 0)
                {
                    const CatalogItem &item = arr[currentIndex];
                    out_data->type = item.type;
                    out_data->width = item.width;
                    out_data->feed = item.feed;
                    out_data->rpm = item.rpm;
                    out_data->material = item.material;
                    strncpy(out_data->image_name, item.image_file, sizeof(out_data->image_name) - 1);
                    out_data->image_name[sizeof(out_data->image_name) - 1] = '\0';
                    strncpy(out_data->tool_name, item.name, sizeof(out_data->tool_name) - 1);
                    out_data->tool_name[sizeof(out_data->tool_name) - 1] = '\0';
                    out_data->offset_x = 0.0f;
                    out_data->offset_z = 0.0f;
                    out_data->calibrated = false;
                    out_data->configured = true;
                    out_data->probe_type = item.probe_type;
                    out_data->hand = item.hand;
                    *selected = true;
                    goto exit_catalog;
                }
            }
            break;
            default:
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

exit_catalog:
    if (currentImageBuffer)
    {
        free(currentImageBuffer);
        currentImageBuffer = nullptr;
    }
}