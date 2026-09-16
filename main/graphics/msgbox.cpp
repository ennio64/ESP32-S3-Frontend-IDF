#include "msgbox.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "keypad.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "MSGBOX";

bool showYesNoBox(const std::string& title, const std::string& message,
                  const std::string& btn1, char key1,
                  const std::string& btn2, char key2,
                  int boxW, int boxH)
{
    const int boxX = (480 - boxW) / 2;
    const int boxY = (320 - boxH) / 2;

    // Sfondo giallo
    tft_fill_rect(boxX, boxY, boxW, boxH, TFT_YELLOW);
    tft_draw_rect(boxX, boxY, boxW, boxH, TFT_BLACK);

    // Titolo (se presente)
    int lineY = boxY + 25;
    if (!title.empty()) {
        drawCenteredTextAt(title.c_str(), boxX + boxW/2, lineY, 2, TFT_BLACK);
        lineY += 25;
    }

    // Messaggio principale (multilinea)
    std::string msg = message;
    size_t pos = 0;
    while (pos < msg.length()) {
        size_t end = msg.find('\n', pos);
        if (end == std::string::npos) end = msg.length();
        std::string line = msg.substr(pos, end - pos);
        drawCenteredTextAt(line.c_str(), boxX + boxW/2, lineY, 1, TFT_BLACK);
        lineY += 18;
        pos = end + 1;
        // Protezione: se il testo esce dalla finestra, interrompi
        if (lineY + 20 > boxY + boxH - 40) break;
    }

    // Pulsanti
    int btnW = 80, btnH = 30;
    int btnSpacing = 20;
    int totalWidth = btnW * 2 + btnSpacing;
    int startX = boxX + (boxW - totalWidth) / 2;
    int btnY = boxY + boxH - btnH - 10;

    // Pulsante 1
    tft_fill_round_rect(startX, btnY, btnW, btnH, 5, TFT_BLACK);
    tft_draw_round_rect(startX, btnY, btnW, btnH, 5, TFT_WHITE);
    drawCenteredTextAt(btn1.c_str(), startX + btnW/2, btnY + btnH/2, 1, TFT_WHITE);

    // Pulsante 2
    tft_fill_round_rect(startX + btnW + btnSpacing, btnY, btnW, btnH, 5, TFT_BLACK);
    tft_draw_round_rect(startX + btnW + btnSpacing, btnY, btnW, btnH, 5, TFT_WHITE);
    drawCenteredTextAt(btn2.c_str(), startX + btnW + btnSpacing + btnW/2, btnY + btnH/2, 1, TFT_WHITE);

    ESP_LOGI(TAG, "In attesa di scelta: %c o %c", key1, key2);

    while (true) {
        char key = readKeypad();
        if (key == key1) {
            tft_fill_rect(boxX, boxY, boxW, boxH, TFT_NAVY);
            return true;
        }
        if (key == key2) {
            tft_fill_rect(boxX, boxY, boxW, boxH, TFT_NAVY);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void showNotImplementedMessage(const char *featureName) {
    tft_fill_screen(TFT_BLACK);
    tft_set_text_color(TFT_RED);
    tft_set_text_size(3);
    tft_set_cursor(20, 100);
    tft_printf("Scene not implemented:");
    tft_set_cursor(20, 140);
    tft_printf("%s", featureName);
    tft_set_text_size(2);
    tft_set_cursor(20, 200);
    tft_print("Returning to menu in 3s...");
    vTaskDelay(pdMS_TO_TICKS(3000));
}