#include "tool_manager_scene.h"
#include "tool_database.h"
#include "tool_catalog_scene.h"
#include "probe_scene.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_vars.h"
#include "graphics_lgfx.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "msgbox.h"
#include "keypad.h"
#include "colors.h"
#include "main_scene.h"
#include <cstdio>
#include <cstring>

static int selectedTool = 0;
static bool needsRedraw = true;
static ToolData currentToolData;

static unsigned long lastBlinkTime = 0;
static bool blinkState = true;
static const unsigned long BLINK_INTERVAL_MS = 500;

extern int pending_tool_for_probe;

static uint8_t *currentImageBuffer = nullptr;
static size_t currentImageSize = 0;

static void freeToolImage()
{
    if (currentImageBuffer)
    {
        free(currentImageBuffer);
        currentImageBuffer = nullptr;
        currentImageSize = 0;
    }
}

static void drawToolImageInDetails(int x, int y, int w, int h, const char *filename)
{
    freeToolImage();
    if (!filename || filename[0] == '\0')
    {
        tft_fill_rect(x, y, w, h, TFT_DARKGREY);
        tft_draw_rect(x, y, w, h, TFT_WHITE);
        drawCenteredTextAt("NO IMAGE", x + w / 2, y + h / 2, 1, TFT_RED);
        return;
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

static uint16_t getToolButtonColor(int tool)
{
    if (!tool_is_associated(tool))
        return TFT_RED;
    if (!tool_is_calibrated(tool))
        return TFT_YELLOW;
    return TFT_GREEN;
}

static void updateBlinkingCircle()
{
    if (selectedTool == 0)
        return;
    int btnX = 20, btnW = 70, btnH = 28, startY = 50, spacing = 4;
    int idx = selectedTool - 1;
    int y = startY + idx * (btnH + spacing);
    uint16_t stateColor = getToolButtonColor(selectedTool);
    int circleX = btnX + btnW - 8;
    int circleY = y + 8;
    if (blinkState)
    {
        tft_fill_circle(circleX, circleY, 5, stateColor);
        tft_draw_circle(circleX, circleY, 5, TFT_BLACK);
    }
    else
    {
        tft_fill_circle(circleX, circleY, 5, TFT_NAVY);
        tft_draw_circle(circleX, circleY, 5, TFT_NAVY);
    }
}

static void drawToolButtons()
{
    resetTextStyle();
    const int btnX = 20, btnW = 70, btnH = 28, startY = 50, spacing = 4;
    for (int i = 1; i <= MAX_TOOLS; ++i)
    {
        int y = startY + (i - 1) * (btnH + spacing);
        uint16_t stateColor = getToolButtonColor(i);
        tft_fill_round_rect(btnX, y, btnW, btnH, 8, TFT_BLACK);
        tft_draw_round_rect(btnX, y, btnW, btnH, 8, TFT_WHITE);
        char label[4];
        snprintf(label, sizeof(label), "T%d", i);
        drawCenteredTextAt(label, btnX + btnW / 2, y + btnH / 2, 2, TFT_WHITE);
        int circleX = btnX + btnW - 8;
        int circleY = y + 8;
        tft_fill_circle(circleX, circleY, 5, stateColor);
        tft_draw_circle(circleX, circleY, 5, TFT_BLACK);
    }
}

static void drawToolDetails()
{
    resetTextStyle();
    int x = 116, y = 38, w = 344, h = 212;
    tft_fill_rect(x, y, w, h, TFT_BLACK);
    tft_draw_rect(x, y, w, h, TFT_WHITE);

    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_text_color(TFT_WHITE);

    if (selectedTool == 0)
    {
        int line = y + 15;
        tft_set_cursor(x + 15, line);
        tft_print("Color code:");
        line += 20;
        tft_set_cursor(x + 20, line);
        tft_set_text_color(TFT_RED);
        tft_print("RED");
        tft_set_text_color(TFT_WHITE);
        tft_print("     = Not associated");
        line += 18;

        tft_set_cursor(x + 20, line);
        tft_set_text_color(TFT_YELLOW);
        tft_print("YELLOW");
        tft_set_text_color(TFT_WHITE);
        tft_print(" = Associated, not calibrated");
        line += 18;

        tft_set_cursor(x + 20, line);
        tft_set_text_color(TFT_GREEN);
        tft_print("GREEN");
        tft_set_text_color(TFT_WHITE);
        tft_print("  = Associated & calibrated");
        line += 25;

        tft_set_cursor(x + 15, line);
        tft_print("Press number (1..6) to select tool.");
        line += 20;
        tft_set_cursor(x + 15, line);
        tft_print("If not associated, catalog opens.");
        line += 20;
        tft_set_cursor(x + 15, line);
        tft_print("Then press B to calibrate.");
        line += 20;
        tft_set_cursor(x + 15, line);
        tft_print("Once calibrated, tool is ready.");
        return;
    }

    if (!tool_is_associated(selectedTool))
    {
        drawCenteredTextAt("No tool associated. Press number to select from catalog.", x + w / 2, y + h / 2, 1, TFT_ORANGE);
        return;
    }

    currentToolData = tool_get_data(selectedTool);

    int imgX = x + 10, imgY = y + 10, imgW = 120, imgH = 120;
    tft_fill_rect(imgX - 2, imgY - 2, imgW + 4, imgH + 4, TFT_WHITE);
    drawToolImageInDetails(imgX, imgY, imgW, imgH, currentToolData.image_name);

    int nameCenterX = imgX + imgW / 2;
    int nameY = imgY + imgH + 24;
    tft_set_font(&fonts::Font2);
    tft_set_text_size(2);
    tft_set_text_color(TFT_CYAN);
    drawCenteredTextAt(currentToolData.tool_name, nameCenterX, nameY, 2, TFT_CYAN);

    int textX = imgX + imgW + 15;
    int lineY = y + 28;
    char buf[64];

    tft_set_font(&fonts::Font2);
    tft_set_text_size(1);
    tft_set_text_color(TFT_WHITE);

    tft_set_cursor(textX, lineY);
    tft_print("Offset X:");
    tft_set_cursor(textX + 90, lineY);
    tft_set_text_color(currentToolData.calibrated ? TFT_GREEN : TFT_YELLOW);
    snprintf(buf, sizeof(buf), "%.3f mm", currentToolData.offset_x);
    tft_print(buf);
    tft_set_text_color(TFT_WHITE);
    lineY += 20;

    tft_set_cursor(textX, lineY);
    tft_print("Offset Z:");
    tft_set_cursor(textX + 90, lineY);
    tft_set_text_color(currentToolData.calibrated ? TFT_GREEN : TFT_YELLOW);
    snprintf(buf, sizeof(buf), "%.3f mm", currentToolData.offset_z);
    tft_print(buf);
    tft_set_text_color(TFT_WHITE);
    lineY += 20;

    tft_set_cursor(textX, lineY);
    tft_print("Type:");
    const char *typeStr = "External";
    if (currentToolData.type == 1)
        typeStr = "Internal";
    else if (currentToolData.type == 2)
        typeStr = "Drill";
    else if (currentToolData.type == 3)
        typeStr = "Thread";
    else if (currentToolData.type == 4)
        typeStr = "Groove";
    tft_set_cursor(textX + 90, lineY);
    tft_print(typeStr);
    lineY += 20;

    tft_set_cursor(textX, lineY);
    tft_print("Hand:");
    tft_set_cursor(textX + 90, lineY);
    tft_print(currentToolData.hand == TOOL_HAND_RIGHT ? "RIGHT" : "LEFT");
    lineY += 20;

    tft_set_cursor(textX, lineY);
    tft_print("Width:");
    snprintf(buf, sizeof(buf), "%.3f mm", currentToolData.width);
    tft_set_cursor(textX + 90, lineY);
    tft_print(buf);
    lineY += 20;

    tft_set_cursor(textX, lineY);
    tft_print("Feed:");
    snprintf(buf, sizeof(buf), "%.1f mm/min", currentToolData.feed);
    tft_set_cursor(textX + 90, lineY);
    tft_print(buf);
    lineY += 20;

    tft_set_cursor(textX, lineY);
    tft_print("RPM:");
    snprintf(buf, sizeof(buf), "%d", currentToolData.rpm);
    tft_set_cursor(textX + 90, lineY);
    tft_print(buf);
    lineY += 20;

    tft_set_cursor(textX, lineY);
    tft_print("Material:");
    const char *matStr = "Steel";
    if (currentToolData.material == 1)
        matStr = "Aluminum";
    else if (currentToolData.material == 2)
        matStr = "Brass";
    tft_set_cursor(textX + 90, lineY);
    tft_print(matStr);
    lineY += 20;

    tft_set_cursor(textX, lineY);
    tft_print("Calibrated:");
    tft_set_cursor(textX + 90, lineY);
    tft_set_text_color(currentToolData.calibrated ? TFT_GREEN : TFT_YELLOW);
    tft_print(currentToolData.calibrated ? "YES" : "NO");
    tft_set_text_color(TFT_WHITE);
}

static void drawActionButtons()
{
    resetTextStyle();
    const int btnW = 90, btnH = 30, btnY = 270;
    const int spacing = 15;
    const int totalWidth = btnW * 3 + spacing * 2;
    const int startX = (480 - totalWidth) / 2;
    drawButton(startX, btnY, btnW, btnH, TFT_BLACK, 'B', 2, TFT_WHITE, "CALIBRATE TOOL", 1, TFT_WHITE);
    drawButton(startX + btnW + spacing, btnY, btnW, btnH, TFT_BLACK, 'C', 2, TFT_WHITE, "DELETE TOOL", 1, TFT_WHITE);
    drawButton(startX + 2 * (btnW + spacing), btnY, btnW, btnH, TFT_RED, 'D', 2, TFT_WHITE, "EXIT", 1, TFT_WHITE);
}

void tool_manager_scene_enter(void)
{
    selectedTool = 0;
    needsRedraw = true;
    lastBlinkTime = 0;
    blinkState = true;
    freeToolImage();

    while (true)
    {
        unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
        if (now - lastBlinkTime > BLINK_INTERVAL_MS)
        {
            lastBlinkTime = now;
            blinkState = !blinkState;
            if (selectedTool != 0)
            {
                updateBlinkingCircle();
            }
        }

        if (needsRedraw)
        {
            resetTextStyle();
            tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
            drawCenteredTextAt("TOOLS MANAGER", 240, 15, 2, TFT_YELLOW);
            tft_draw_line(20, 30, 460, 30, TFT_WHITE);
            tft_draw_line(20, 258, 460, 258, TFT_WHITE);
            drawToolButtons();
            drawToolDetails();
            drawActionButtons();
            needsRedraw = false;
        }

        char key = readKeypad();
        if (key == 'D')
        {
            freeToolImage();
            resetTextStyle();
            return;
        }

        if (key >= '1' && key <= '0' + MAX_TOOLS)
        {
            int tool = key - '0';
            if (tool >= 1 && tool <= MAX_TOOLS)
            {
                if (!tool_is_associated(tool))
                {
                    ToolData newTool;
                    bool selected = false;
                    tool_catalog_scene_enter(&newTool, &selected);
                    if (selected)
                    {
                        tool_associate(tool, &newTool);
                        selectedTool = tool;
                        needsRedraw = true;
                    }
                    else
                    {
                        needsRedraw = true;
                    }
                }
                else
                {
                    selectedTool = tool;
                    needsRedraw = true;
                }
            }
            continue;
        }

        if (selectedTool != 0)
        {
            if (key == 'B')
            {
                if (!tool_is_associated(selectedTool))
                {
                    drawCenteredTextAt("Associate a tool first!", 240, 200, 1, TFT_RED);
                    vTaskDelay(pdMS_TO_TICKS(1500));
                    needsRedraw = true;
                    continue;
                }
                pending_tool_for_probe = selectedTool;
                resetTextStyle();
                freeToolImage();
                probe_scene_enter();
                needsRedraw = true;
                continue;
            }
            if (key == 'C')
            {
                if (!tool_is_associated(selectedTool))
                {
                    drawCenteredTextAt("No tool to delete", 240, 200, 1, TFT_RED);
                    vTaskDelay(pdMS_TO_TICKS(1500));
                    needsRedraw = true;
                    continue;
                }
                int x = 116, y = 38, w = 344, h = 212;
                tft_fill_rect(x, y, w, h, TFT_BLACK);
                tft_draw_rect(x, y, w, h, TFT_WHITE);
                resetTextStyle();
                char confirmMsg[32];
                char deleteMsg[48];
                snprintf(confirmMsg, sizeof(confirmMsg), "CONFIRM DELETE T%d?", selectedTool);
                snprintf(deleteMsg, sizeof(deleteMsg), "Delete all data for T%d?", selectedTool);
                drawCenteredTextAt(confirmMsg, x + w / 2, y + 60, 2, TFT_YELLOW);
                drawCenteredTextAt(deleteMsg, x + w / 2, y + 100, 1, TFT_WHITE);
                drawCenteredTextAt("Press C to confirm, A to abort", x + w / 2, y + 150, 1, TFT_CYAN);
                ;
                bool waiting = true;
                bool deleted = false;
                while (waiting)
                {
                    char k = readKeypad();
                    if (k == 'C')
                    {
                        tool_reset(selectedTool);
                        selectedTool = 0;
                        deleted = true;
                        waiting = false;
                    }
                    else if (k == 'A')
                    {
                        waiting = false;
                    }
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                if (deleted)
                {
                    needsRedraw = true;
                }
                else
                {
                    drawToolDetails();
                    if (selectedTool != 0)
                    {
                        updateBlinkingCircle();
                    }
                }
                continue;
            }
        }
        else
        {
            // Nessun utensile selezionato: se l'utente preme B o C, mostra avviso
            if (key == 'B' || key == 'C')
            {
                int x = 116, y = 38, w = 344, h = 212;
                tft_fill_rect(x, y, w, h, TFT_BLACK);
                tft_draw_rect(x, y, w, h, TFT_WHITE);
                resetTextStyle();
                drawCenteredTextAt("Select a tool first", x + w / 2, y + h / 2 - 10, 2, TFT_YELLOW);
                drawCenteredTextAt("using numeric keys 1..6", x + w / 2, y + h / 2 + 10, 2, TFT_YELLOW);
                vTaskDelay(pdMS_TO_TICKS(2500));
                // Ridisegna solo l'area dettagli (senza rifare tutta la scena)
                drawToolDetails();
                continue;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}