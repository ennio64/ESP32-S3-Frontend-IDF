#include "DRO_scene.h"
#include "graphics_helpers.h"
#include "global_vars.h"
#include "global_settings.h"
#include "keypad.h"
#include "cnc_manager.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <string>

static unsigned long millis_idf() {
    return (unsigned long)(esp_timer_get_time() / 1000);
}

extern float DRO_RPM, DRO_X, DRO_Z;
extern float k_XDRO;

// Helper per disegnare un box DRO
// label: etichetta in alto a sinistra (se nullptr, non viene disegnata)
// bottomLabel: etichetta in basso al centro (se nullptr, non viene disegnata)
static void drawDroBox(int x, int y, int w, int h, const char* value, const char* label, const char* bottomLabel = nullptr) {
    tft_fill_round_rect(x, y, w, h, 8, TFT_BLACK);
    tft_draw_round_rect(x, y, w, h, 8, TFT_YELLOW);

    // Etichetta in alto a sinistra (solo se fornita)
    if (label != nullptr) {
        resetTextStyle();
        tft_set_text_size(2);
        tft_set_text_color(TFT_CYAN);   // colore ciano per le etichette
        tft_set_cursor(x + 10, y + 5);
        tft_print(label);
    }

    // Valore centrato (stesso per tutti i box)
    resetTextStyle();
    tft_set_text_size(3);
    tft_set_text_color(TFT_WHITE);      // valori in bianco
    int16_t tw = tft_text_width(value);
    int16_t th = tft_font_height();
    int16_t cx = x + (w - tw) / 2;
    int16_t cy = y + (h - th) / 2;
    if (cx < x + 10) cx = x + 10; // protezione
    tft_set_cursor(cx, cy);
    tft_print(value);

    // Etichetta in basso (se fornita)
    if (bottomLabel != nullptr) {
        resetTextStyle();
        tft_set_text_size(1);
        tft_set_text_color(TFT_CYAN);
        int16_t mw = tft_text_width(bottomLabel);
        int16_t mx = x + (w - mw) / 2;
        int16_t my = y + h - 12; // posizionato in basso
        tft_set_cursor(mx, my);
        tft_print(bottomLabel);
    }
}

void dro_scene_enter() {
    tft_set_rotation(3);
    tft_fill_screen(TFT_NAVY);
    tft_fill_rect(0, 0, 480, 50, TFT_BLACK);

    // Titolo
    resetTextStyle();
    tft_set_text_size(3);
    tft_set_text_color(TFT_YELLOW);
    int16_t tw = tft_text_width("MX180D lathe");
    tft_set_cursor((480 - tw) / 2, 15);
    tft_print("MX180D lathe");

    // Layout 4 box: 2×2
    const int w = 200, h = 80;
    const int xLeft = 20, xRight = 260;
    const int yTop = 70, yBottom = 180;

    // Modalità per X (Diameter/Radius)
    bool isDiameter = (settings_get_xmode() == 0);
    const char* modeStr = isDiameter ? "Diameter" : "Radius";

    // Box iniziali
    {
        char buf[32];
        float xVal = k_XDRO * DRO_X;
        snprintf(buf, sizeof(buf), "%.3f", xVal);
        drawDroBox(xLeft, yTop, w, h, buf, "X", modeStr);          // X a sinistra
        snprintf(buf, sizeof(buf), "%.0f", DRO_RPM);
        drawDroBox(xRight, yTop, w, h, buf, "RPM");                // RPM a destra
        snprintf(buf, sizeof(buf), "%.3f", DRO_Z);
        drawDroBox(xLeft, yBottom, w, h, buf, "Z");                // Z a sinistra
        drawDroBox(xRight, yBottom, w, h, cnc.getStatus().c_str(), nullptr, "Status Controller"); // Status a destra
    }

    // Istruzione uscita
    resetTextStyle();
    tft_set_text_size(2);
    tft_set_text_color(TFT_BLACK);
    const char* msg = "PREMI 'D' PER USCIRE";
    int16_t msgWidth = tft_text_width(msg);
    tft_set_cursor((480 - msgWidth) / 2, 300);
    tft_print(msg);

    unsigned long lastUpdate = 0;
    float lastRPM = -9999, lastX = -9999, lastZ = -9999;
    std::string lastStatus = "";

    while (true) {
        char key = readKeypad();

        if (millis_idf() - lastUpdate >= 250) {
            bool needUpdate = false;
            char buf[32];

            // X a sinistra
            float xVal = k_XDRO * DRO_X;
            if (xVal != lastX) {
                snprintf(buf, sizeof(buf), "%.3f", xVal);
                drawDroBox(xLeft, yTop, w, h, buf, "X", modeStr);
                lastX = xVal;
                needUpdate = true;
            }

            // RPM a destra
            if (DRO_RPM != lastRPM) {
                snprintf(buf, sizeof(buf), "%.0f", DRO_RPM);
                drawDroBox(xRight, yTop, w, h, buf, "RPM");
                lastRPM = DRO_RPM;
                needUpdate = true;
            }

            // Z a sinistra
            if (DRO_Z != lastZ) {
                snprintf(buf, sizeof(buf), "%.3f", DRO_Z);
                drawDroBox(xLeft, yBottom, w, h, buf, "Z");
                lastZ = DRO_Z;
                needUpdate = true;
            }

            // Status a destra
            const char* status = cnc.getStatus().c_str();
            if (lastStatus != status) {
                drawDroBox(xRight, yBottom, w, h, status, nullptr, "Status Controller");
                lastStatus = status;
                needUpdate = true;
            }

            if (needUpdate) {
                lastUpdate = millis_idf();
            }
        }

        if (key == 'D') {
            resetTextStyle();
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}