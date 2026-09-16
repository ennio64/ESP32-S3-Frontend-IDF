// realtime_command_scene.cpp
#include "realtime_command_scene.h"
#include "scene_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_vars.h"
#include "graphics_lgfx.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "keypad.h"
#include "cnc_manager.h"
#include "colors.h"
#include "main_scene.h"
#include <cstdio>
#include <string>

static const char *TAG = "RT_CMD";

static unsigned long millis_idf()
{
    return (unsigned long)(esp_timer_get_time() / 1000);
}

// Gestisce un singolo comando real-time o testuale
static void handleRTCommand(char key)
{
    switch (key)
    {
    case 'A':
        cnc.sendRealtime(0x82);  // Feed Hold
        ESP_LOGI(TAG, "FeedHold inviato");
        break;
    case 'B':
        cnc.sendRealtime(0x81);  // Cycle Start
        ESP_LOGI(TAG, "CycleStart inviato");
        break;
    case 'C':
        cnc.sendRealtime(0x18);  // Soft Reset (Ctrl-X)
        ESP_LOGI(TAG, "Reset inviato");
        break;

    case '1':
        cnc.sendCommand("$X");
        ESP_LOGI(TAG, "Unlock ($X)");
        break;
    case '2':
        cnc.sendCommand("$H");
        ESP_LOGI(TAG, "Homing ($H)");
        break;
    case '3':
        cnc.sendCommand("M3");
        ESP_LOGI(TAG, "Spindle CW (M3)");
        break;
    case '4':
        cnc.sendCommand("M4");
        ESP_LOGI(TAG, "Spindle CCW (M4)");
        break;
    case '5':
        cnc.sendCommand("M5");
        ESP_LOGI(TAG, "Spindle Stop (M5)");
        break;
    case '6':
        cnc.sendCommand("$I");
        ESP_LOGI(TAG, "Info firmware ($I)");
        break;
    case '7':
        cnc.sendCommand("M7");
        ESP_LOGI(TAG, "Mist ON (M7)");
        break;
    case '8':
        cnc.sendCommand("M8");
        ESP_LOGI(TAG, "Flood ON (M8)");
        break;
    case '9':
        cnc.sendCommand("M9");
        ESP_LOGI(TAG, "Coolant OFF (M9)");
        break;
    case '0':
        cnc.sendCommand("$G");
        ESP_LOGI(TAG, "Modal state ($G)");
        break;
    case '*':
        cnc.sendCommand("$EE");
        ESP_LOGI(TAG, "Error map ($EE)");
        break;
    case '#':
        cnc.sendCommand("$EA");
        ESP_LOGI(TAG, "Alarm map ($EA)");
        break;

    default:
        break;
    }
}

// Disegna il log di autoreport (area in basso)
static void drawAutoReportLog(const std::string &status)
{
    const int logX = 20;
    const int logY = 260;
    const int logW = 440;
    const int logH = 55;

    tft_fill_rect(logX + 2, logY + 2, logW - 4, logH - 4, TFT_BLACK);
    tft_set_text_color(TFT_YELLOW);
    tft_set_text_size(1);
    tft_set_text_datum(TL_DATUM);

    tft_set_cursor(logX + 6, logY + 6);
    tft_print("AutoReport:");

    // Tronca se troppo lunga
    std::string s = status;
    if (s.length() > 72)
        s = s.substr(0, 72);

    tft_set_cursor(logX + 6, logY + 26);
    tft_print(s.c_str());
}

// Scena principale
void drawRTCommandScene()
{
    // Parametri pulsanti
    const int buttonW = 60;
    const int buttonH = 30;
    const int colSpacing = 20;
    const int rowSpacing = 20;
    const int abcSpacing = 20;

    const int group1OffsetX = 50;   // griglia numerica
    const int group2OffsetX = 340;  // colonna A–D
    const int baseY_group1 = 50;
    const int baseY_group2 = 50;

    // Griglia numerica 4x3
    const char keys[4][3] = {
        {'1', '2', '3'},
        {'4', '5', '6'},
        {'7', '8', '9'},
        {'*', '0', '#'}};
    const char *labels[4][3] = {
        {"$X", "$H", "M3"},
        {"M4", "M5", "$I"},
        {"M7", "M8", "M9"},
        {"$EE", "$G", "$EA"}};

    // Pulsanti A–D
    const char abcKeys[4] = {'A', 'B', 'C', 'D'};
    const char *abcLabels[4] = {"FeedHold", "CycleStart", "Reset", "EXIT"};
    uint16_t abcColors[4] = {TFT_ORANGE, TFT_DARKGREEN, TFT_RED, TFT_DARKCYAN};

    // Sfondo e intestazione
    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("Real Time Command", 240, 15, 2, TFT_YELLOW);
    tft_draw_line(20, 30, 460, 30, TFT_WHITE);
    tft_draw_line(20, 258, 460, 258, TFT_WHITE);

    // Disegna griglia numerica
    resetTextStyle();
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            int x = group1OffsetX + c * (buttonW + colSpacing);
            int y = baseY_group1 + r * (buttonH + rowSpacing);
            drawButton(x, y, buttonW, buttonH, TFT_BLUE, keys[r][c], 2, TFT_WHITE,
                       labels[r][c], 1, TFT_WHITE);
        }
    }

    // Disegna colonna A–D
    resetTextStyle();
    for (int i = 0; i < 4; ++i)
    {
        int y = baseY_group2 + i * (buttonH + abcSpacing);
        drawButton(group2OffsetX, y, 70, buttonH, abcColors[i], abcKeys[i], 2, TFT_WHITE,
                   abcLabels[i], 1, TFT_WHITE);
    }

    // Area log
    const int logX = 20, logY = 260, logW = 440, logH = 55;
    tft_fill_rect(logX, logY, logW, logH, TFT_BLACK);
    tft_draw_rect(logX, logY, logW, logH, TFT_WHITE);

    ESP_LOGI(TAG, "Real Time Command scene avviata");

    unsigned long lastLogUpdate = 0;
    while (true)
    {
        unsigned long now = millis_idf();

        // Aggiorna il log ogni 1000 ms
        if (now - lastLogUpdate >= 1000)
        {
            auto st = cnc.getParsedStatus();
            drawAutoReportLog(st.lastStatusLine);
            lastLogUpdate = now;
        }

        char key = readKeypad();
        if (key == 'D')
        {
            resetTextStyle();
            return;
        }

        if (key == '\0' || key == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        handleRTCommand(key);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void realtime_command_scene_enter(void)
{
    drawRTCommandScene();
}