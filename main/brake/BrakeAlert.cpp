#include "BrakeAlert.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "global_vars.h"   // per brakeActive
#include "keypad.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BRAKE";

#ifndef MC_DATUM
#define MC_DATUM 4   // 4 = middle center (LovyanGFX)
#endif

// ======================================================
//  Legge stato attuale del freno
// ======================================================
bool isBrakeActive(void)
{
    return brakeActive;
}

// ======================================================
//  Mostra alert a schermo intero e aspetta pressione D o rilascio freno
//  Restituisce:
//    true  → freno rilasciato, procedi con lavorazione
//    false → utente ha premuto D, torna al menu principale
// ======================================================
bool showBrakeAlertAndWait(void)
{
    tft_fill_screen(TFT_RED);
    
    // Usa drawMessaggio per centrare automaticamente (ma supporta solo text size 2)
    // Quindi per il testo grande usiamo un approccio manuale ma con calcolo corretto
    tft_set_text_size(4);
    tft_set_text_color(TFT_WHITE);
    const char* mainText = "FRENO BLOCCATO!";
    // Calcola larghezza in pixel: ogni carattere a size 4 occupa circa 24 pixel (se font a larghezza fissa)
    // Oppure usa tft_text_width * 6 * size (dove 6 è pixel per carattere a size 1)
    int16_t textWidth = strlen(mainText) * 6 * 4;   // 6 pixel per carattere a size1, * size
    int16_t x = (480 - textWidth) / 2;
    if (x < 0) x = 10;
    tft_set_cursor(x, 120);
    tft_print(mainText);
    
    // Sottotesto 1 (size 2)
    const char* sub1 = "Sbloccare il freno o premere D";
    tft_set_text_size(2);
    textWidth = strlen(sub1) * 6 * 2;
    x = (480 - textWidth) / 2;
    tft_set_cursor(x, 200);
    tft_print(sub1);
    
    // Sottotesto 2 (size 2)
    const char* sub2 = "Premi D per tornare al menu";
    textWidth = strlen(sub2) * 6 * 2;
    x = (480 - textWidth) / 2;
    tft_set_cursor(x, 240);
    tft_print(sub2);
    
    // Attesa freno o tasto D
    while (true) {
        if (!brakeActive) {
            tft_fill_screen(TFT_BLACK);
            return true;
        }
        char key = readKeypad();
        if (key == 'D') {
            tft_fill_screen(TFT_BLACK);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/*bool showBrakeAlertAndWait(void)
{
    resetTextStyle();
    tft_fill_screen(TFT_RED);
    
    tft_set_text_color(TFT_WHITE);
    tft_set_text_size(4);
    tft_set_text_datum(MC_DATUM);
    tft_draw_string("FRENO BLOCCATO!", 240, 140, TFT_WHITE, 4);
    
    tft_set_text_size(2);
    tft_draw_string("Sbloccare il freno o premere D", 240, 220, TFT_WHITE, 2);
    tft_draw_string("Premi D per tornare al menu", 240, 260, TFT_WHITE, 2);
    
    resetTextStyle();
    
    // Attesa: esce se freno rilasciato OPPURE se preme D
    while (true) {
        if (!brakeActive) {
            ESP_LOGI(TAG, "Freno rilasciato - Continuo con la lavorazione");
            tft_fill_screen(TFT_BLACK);
            resetTextStyle();
            return true;
        }
        
        char key = readKeypad();
        if (key == 'D') {
            ESP_LOGI(TAG, "Uscita forzata - Torno al menu principale");
            tft_fill_screen(TFT_BLACK);
            resetTextStyle();
            return false;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}*/

// ======================================================
//  Verifica freno prima di entrare in una lavorazione
//  Restituisce:
//    true  → si può entrare in lavorazione
//    false → blocca e torna al menu principale
// ======================================================
bool checkBrakeBeforeMachining(void)
{
    if (brakeActive) {
        ESP_LOGW(TAG, "Freno bloccato - Lavorazione impedita");
        return showBrakeAlertAndWait();
    }
    return true;
}