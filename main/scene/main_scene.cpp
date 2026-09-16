#include "main_scene.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "msgbox.h"
#include "global_vars.h"
#include "global_settings.h"
#include "cnc_manager.h"
#include "keypad.h"
#include "Colors.h"
#include <cstdio>
#include <string>
#include "esp_log.h"
#include "esp_timer.h"
#include "BrakeAlert.h"
#include "OTA_Manager.h"
#include "scenes.h"
#include "DRO_scene.h"          // ⭐ Aggiunto per il DRO automatico

// Variabili globali per il menu
int selectedMenuItem = 0;
std::string activeGcodeType = "";
int numMenuItems = 14;
int menuIndex = 0;
int menuStartIndex = 0;

// Funzioni stub (se non ancora implementate)
void PreloadImage(const char *) {}
void UnloadImage() {}
void DROmenu() {}

// ⏱️ millis per ESP‑IDF
static unsigned long millis_idf()
{
    return (unsigned long)(esp_timer_get_time() / 1000);
}

// ============================================================
//  Disegna gli elementi statici dello sfondo (titolo, logo, pulsanti, separatori)
//  Viene chiamato una sola volta all'ingresso del menu
// ============================================================
void drawStaticBackground()
{
    tft_set_rotation(3);
    tft_fill_screen(TFT_NAVY);
    tft_fill_rect(0, 0, 480, 320, TFT_BLACK);

    // Titolo
    tft_set_text_color(TFT_YELLOW);
    tft_set_text_size(3);
    int16_t tw = tft_text_width("SELECT OPTIONS");
    tft_set_cursor((480 - 75 - tw) / 2, 15);
    tft_print("SELECT OPTIONS");

    activeGcodeType = "";

    // Logo in alto a destra
    draw_miniLogoTopRight();

    // Separatori orizzontali e verticali
    tft_fill_rect(20, 50, 370, 1, TFT_WHITE);
    tft_fill_rect(20, 315, 370, 1, TFT_WHITE);
    tft_fill_rect(5, 65, 1, 235, TFT_WHITE);
    tft_fill_rect(400, 65, 1, 235, TFT_WHITE);
    tft_fill_rect(6, 51, 394, 263, TFT_NAVY);

    // Triangoli smussati agli angoli
    int s = 15;
    tft_fill_triangle(5, 50, 5 + s, 50, 5, 50 + s, TFT_BLACK);
    tft_fill_triangle(400, 50, 400 - s, 50, 400, 50 + s, TFT_BLACK);
    tft_fill_triangle(5, 315, 5 + s, 315, 5, 315 - s, TFT_BLACK);
    tft_fill_triangle(400, 315, 400 - s, 315, 400, 315 - s, TFT_BLACK);

    // Pulsanti laterali (NEXT, PREV, ENTER, JOG MODE)
    drawButton(426, 65, 40, 40, TFT_BLACK, 'A', 2, TFT_WHITE, "NEXT", 1, TFT_WHITE);
    drawButton(426, 130, 40, 40, TFT_BLACK, 'B', 2, TFT_WHITE, "PREV", 1, TFT_WHITE);
    drawButton(426, 195, 40, 40, TFT_BLACK, 'C', 2, TFT_WHITE, "ENTER", 1, TFT_WHITE);
    drawButton(426, 260, 40, 40, TFT_ORANGE, 'D', 2, TFT_WHITE, "JOG MODE", 1, TFT_WHITE);
}

// ============================================================
//  Disegna SOLO le voci del menu (6 righe) e le frecce di selezione.
//  Non tocca lo sfondo, il titolo, i pulsanti, ecc.
// ============================================================
void drawMenu()
{
    tft_set_text_size(2);

    // Calcola l'indice di inizio delle voci visibili
    int visibleItems = 6;
    if (menuIndex < menuStartIndex)
        menuStartIndex = menuIndex;
    if (menuIndex >= menuStartIndex + visibleItems)
        menuStartIndex = menuIndex - visibleItems + 1;

    const char *menuItems[] = {
        "TURNING", "FACING", "PARTING", "CHAMFER & BILLET",
        "HEMISPHERICAL PROFILING", "METRIC THREADING", "KEYWAY",
        "SPINDLE POSITION", "REALTIME COMMAND", "GLOBAL SETTINGS",
        "MORSE TAPER (TAB)", "THREAD ISO METRIC (TAB)",
        "TOOLS MANAGER", "Free work in progress"};

    for (int i = 0; i < visibleItems; i++)
    {
        int itemIdx = menuStartIndex + i;
        if (itemIdx >= numMenuItems)
            break;

        int y = 80 + i * 40;
        const char *label = menuItems[itemIdx];
        uint16_t color = (itemIdx == menuIndex) ? TFT_WHITE : TFT_CYAN;
        int16_t textW = tft_text_width(label);
        int16_t centerX = (480 - 75 - textW) / 2;

        // Pulisci l'area della riga (solo il rettangolo della voce)
        tft_fill_rect(15, y - 6, 370, 30, TFT_NAVY);

        if (itemIdx == menuIndex)
        {
            // Freccia sinistra
            tft_fill_triangle(35, y + 3, 25, y - 2, 25, y + 8, TFT_WHITE);
            // Freccia destra
            tft_fill_triangle(480 - 80 - 30, y + 3, 480 - 80 - 20, y - 2, 480 - 80 - 20, y + 8, TFT_WHITE);
        }

        tft_set_text_color(color);
        tft_set_cursor(centerX, y - 4);
        tft_print(label);
    }
}

// ============================================================
//  Schermata di startup (originale)
// ============================================================
void showStartupScreen()
{
    keypad_reset();
    vTaskDelay(pdMS_TO_TICKS(100)); // attendi stabilizzazione

    tft_fill_screen(TFT_BLACK);
    tft_set_text_color(TFT_CYAN);
    tft_set_text_size(3);
    int16_t tw = tft_text_width("GRBL CONTROLLER");
    tft_set_cursor((480 - tw) / 2, 15);
    tft_print("GRBL CONTROLLER");
    tft_fill_rect(40, 60, 400, 2, TFT_DARKGREY);

    tft_set_text_color(TFT_YELLOW);
    tft_set_text_size(2);
    int y = 75;
    tft_set_cursor(40, y);
    tft_printf("State: %s", cnc.getStatus().c_str());
    y += 30;
    tft_set_cursor(40, y);
    tft_printf("Max RPM: %.1f", maxSpindleRPM);
    y += 30;
    tft_set_cursor(40, y);
    tft_printf("maxFeed X: %.1f", maxFeedX);
    y += 30;
    tft_set_cursor(40, y);
    tft_printf("maxFeed Z: %.1f", maxFeedZ);
    y += 30;
    tft_set_cursor(40, y);
    tft_printf("AutoReport: %s", AutoReportStatus ? "ON" : "OFF");

    tft_fill_rect(40, 250, 400, 2, TFT_DARKGREY);

    // Pulsanti di startup
    int btnW = 80, btnH = 40, btnY = 265;
    int margin = (480 - (btnW * 4)) / 5;
    int xA = margin, xB = margin * 2 + btnW, xC = margin * 3 + btnW * 2, xD = margin * 4 + btnW * 3;
    drawButton(xA, btnY, btnW, btnH, TFT_DARKGREEN, 'A', 2, TFT_WHITE, "START", 1, TFT_WHITE);
    drawButton(xB, btnY, btnW, btnH, TFT_BLUE, 'B', 2, TFT_WHITE, "OTA", 1, TFT_WHITE);
    drawButton(xC, btnY, btnW, btnH, TFT_PURPLE, 'C', 2, TFT_WHITE, "INVERT COLORS", 1, TFT_WHITE);
    drawButton(xD, btnY, btnW, btnH, TFT_RED, 'D', 2, TFT_WHITE, "REBOOT", 1, TFT_WHITE);

    ESP_LOGI("STARTUP", "➡️  Entrato nella STARTUP SCREEN");

    // Attesa interattiva (keypad reale)
    while (true)
    {
        char key = readKeypad();
        if (key == 'A')
            return; // START
        if (key == 'B')
        {
            openOTAManager();
            return;
        }
        if (key == 'D')
        {
            esp_restart();
        }
        if (key == 'C')
        {
            settings_toggle_and_restart("display_invert");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================
//  DisplayManager principale (loop menu) con TIMEOUT DRO
// ============================================================
void DisplayManager()
{
    static bool startupShown = false;
    if (!startupShown)
    {
        while (!grblReady)
            vTaskDelay(pdMS_TO_TICKS(10));
        showStartupScreen();
        startupShown = true;
    }

    ESP_LOGI("MAIN_MENU", "➡️  Entrato nel MAIN MENU");

    // Disegna lo sfondo statico una sola volta
    drawStaticBackground();
    drawMenu();

    // ⏱️ Avvia il timer per il DRO
    unsigned long startTime = millis_idf();

    while (true)
    {
        char key = readKeypad();

        // 🔄 Reset del timer a ogni pressione di tasto
        if (key != 0)
        {
            startTime = millis_idf();
        }

        // ⏱️ Controllo timeout (60 secondi di inattività)
        if (millis_idf() - startTime > 60000)
        {
            ESP_LOGI("MAIN_MENU", "Timeout: entro in DRO");
            dro_scene_enter();   // ← schermata DRO
            selectedMenuItem = 13; // ⭐ IMPORTANTE: dopo il DRO torna al menu
            return;              // esce da DisplayManager, task_graphics la richiamerà
        }

        if (key == 'A')
        { // NEXT
            menuIndex = (menuIndex > 0) ? menuIndex - 1 : numMenuItems - 1;
            if (menuIndex < menuStartIndex)
                menuStartIndex = menuIndex;
            drawMenu(); // ridisegna solo le voci
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        else if (key == 'B')
        { // PREV
            menuIndex = (menuIndex < numMenuItems - 1) ? menuIndex + 1 : 0;
            if (menuIndex >= menuStartIndex + 6)
                menuStartIndex = menuIndex - 5;
            drawMenu(); // ridisegna solo le voci
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        else if (key == 'C')
        { // ENTER
            selectedMenuItem = menuIndex;
            break;
        }
        else if (key == 'D')
        {
            jog_scene_enter();
            selectedMenuItem = 13;   // torna al menu dopo Jog
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================
//  Task grafico
// ============================================================
void task_graphics(void *pvParameters)
{
    while (!displayReady)
        vTaskDelay(pdMS_TO_TICKS(100));
    while (!grblReady)
        vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI("GRAPHICS", "Task grafico avviato su core 1");

    bool returningFromParams = false;

    while (true)
    {
        if (!returningFromParams)
        {
            DisplayManager(); // menu principale
        }

        switch (selectedMenuItem)
        {
        case 0: // TURNING
            if (!checkBrakeBeforeMachining())
                break;
            turning_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 1: // FACING
            if (!checkBrakeBeforeMachining())
                break;
            facing_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 2: // PARTING
            if (!checkBrakeBeforeMachining())
                break;
            parting_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 3: // CHAMFER & BILLET
            if (!checkBrakeBeforeMachining())
                break;
            chamfer_billet_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 4: // HEMISPHERICAL PROFILING
            if (!checkBrakeBeforeMachining())
                break;
            hemispherical_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 5: // THREADING
            if (!checkBrakeBeforeMachining())
                break;
            threading_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 6: // KEYWAY
            keyway_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 7: // SPINDLE POSITION
            spindle_position_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 8: // REALTIME COMMAND
            realtime_command_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 9: // GLOBAL SETTINGS
            global_settings_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 10: // MORSE TAPER (TAB)
            taper_morse_scene_enter();
            returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 11: // THREAD ISO M (TAB)
            thread_iso_scene_enter();
            // returningFromParams = false;
            selectedMenuItem = 13;
            break;
        case 12: // TOOL MANAGER
            tool_manager_scene_enter();
            selectedMenuItem = 13;
            break;
        case 13:
            returningFromParams = false;
            break;
        default:
            returningFromParams = false;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}