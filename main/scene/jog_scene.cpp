    #include "jog_scene.h"
    #include "scene_utils.h"
    #include "esp_log.h"
    #include "esp_timer.h"
    #include "global_settings.h"
    #include "global_vars.h"
    #include "graphics_primitive.h"
    #include "graphics_helpers.h"
    #include "keypad.h"
    #include "cnc_manager.h"
    #include "grbl_commands.h"
    #include "ads1x15.h"
    #include "colors.h"
    #include <cmath>
    #include <cstring>
    #include <string>

    static const char *TAG = "JOG";
    extern ads1x15_t *ads; // definito in app_main

    // ---------- costanti grafiche (se mancanti) ----------
    #ifndef ML_DATUM
    #define ML_DATUM 3 // middle-left
    #endif
    #ifndef MC_DATUM
    #define MC_DATUM 4 // middle-center
    #endif

    // Utility RGB565
    static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
    {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    // ---------- millis per IDF ----------
    static unsigned long millis_idf()
    {
        return (unsigned long)(esp_timer_get_time() / 1000);
    }

    // ---------- mapping ADC raw ( -32768..32767 ) -> 0..4095 ----------
    static int map_ads(int16_t raw)
    {
        return (raw + 32768) * 4095 / 65535;
    }

    // ---------- verifica tipo joystick (0=analogico, 1=digitale) ----------
    static int getJoystickType() {
        return (int)settings_get_joystick_type();
    }

    // ---------- struttura calibrazione ----------
    static struct
    {
        bool valid;
        int xCenter, yCenter;
        int xMin, xMax, yMin, yMax;
        int deadzone;
    } cal;

    static void loadCalibration()
    {
        if (getJoystickType() == 1)
        {
            cal.valid = true;
            return;
        }
        cal.xCenter = settings_get_int("jog_xCenter", 2048);
        cal.yCenter = settings_get_int("jog_yCenter", 2048);
        cal.xMin = settings_get_int("jog_xMin", 0);
        cal.xMax = settings_get_int("jog_xMax", 4095);
        cal.yMin = settings_get_int("jog_yMin", 0);
        cal.yMax = settings_get_int("jog_yMax", 4095);
        cal.deadzone = settings_get_int("jog_deadzone", 100);
        cal.valid = true;
        ESP_LOGI(TAG, "Calibrazione caricata: xC=%d yC=%d", cal.xCenter, cal.yCenter);
    }

    static void saveCalibration()
    {
        if (getJoystickType() == 1)
            return;
        settings_set_int("jog_xCenter", cal.xCenter);
        settings_set_int("jog_yCenter", cal.yCenter);
        settings_set_int("jog_xMin", cal.xMin);
        settings_set_int("jog_xMax", cal.xMax);
        settings_set_int("jog_yMin", cal.yMin);
        settings_set_int("jog_yMax", cal.yMax);
        settings_set_int("jog_deadzone", cal.deadzone);
        settings_commit();
        ESP_LOGI(TAG, "Calibrazione salvata in NVS");
    }

    // ---------- lettura direzione ----------
    static uint8_t computeDirectionFromAnalog()
    {
        if (!cal.valid)
            return 0;
        int16_t x_raw = ads1x15_read_adc(ads, 2);
        int16_t y_raw = ads1x15_read_adc(ads, 3);
        int x_val = map_ads(x_raw);
        int y_val = map_ads(y_raw);

        bool raw_up = (x_val <= cal.xCenter - cal.deadzone);
        bool raw_down = (x_val >= cal.xCenter + cal.deadzone);
        bool raw_left = (y_val >= cal.yCenter + cal.deadzone);
        bool raw_right = (y_val <= cal.yCenter - cal.deadzone);

    #define INVERT_X 0
    #define INVERT_Z 1
        bool up = (INVERT_X ? raw_down : raw_up);
        bool down = (INVERT_X ? raw_up : raw_down);
        bool left = (INVERT_Z ? raw_right : raw_left);
        bool right = (INVERT_Z ? raw_left : raw_right);
        return (right << 3) | (left << 2) | (down << 1) | up;
    }

    static uint8_t readDigitalState()
    {
        if (getJoystickType() == 1)
        {
            // digitale: canali 0-3 (valori <1000 = premuto)
            bool down = (ads1x15_read_adc(ads, 0) < 1000);
            bool left = (ads1x15_read_adc(ads, 1) < 1000);
            bool up = (ads1x15_read_adc(ads, 2) < 1000);
            bool right = (ads1x15_read_adc(ads, 3) < 1000);
            return (right << 3) | (left << 2) | (down << 1) | up;
        }
        else
        {
            return computeDirectionFromAnalog();
        }
    }

    // ---------- invio comandi Jog ----------
    static void sendJogCommand(const std::string &cmd)
    {
        cnc.sendCommand(cmd);
        cnc.sendCommand("G4P0");
    }

    // ---------- modalità e variabili di stato ----------
    static int pulseMode = 0; // 0=continuo, 1..4=passi
    static bool waitingForRelease = false;
    static float jogFeed = 240.0f;
    static const float JOG_FEED_MIN = 20.0f;
    static const float JOG_FEED_MAX = 500.0f;
    static const float JOG_FEED_STEP = 20.0f;

    static void handlePulseMode(uint8_t state)
    {
        if (waitingForRelease)
        {
            if (state == 0)
                waitingForRelease = false;
            return;
        }
        if (state == 0)
            return;
        float step;
        switch (pulseMode)
        {
        case 1:
            step = 0.01f;
            break;
        case 2:
            step = 0.1f;
            break;
        case 3:
            step = 1.0f;
            break;
        case 4:
            step = 10.0f;
            break;
        default:
            step = 0.01f;
            break;
        }
        float F = 200.0f;
        std::string cmd;
        switch (state)
        {
        case 1:
            cmd = "$J=G91 X" + std::to_string(step) + " F" + std::to_string(F);
            break;
        case 2:
            cmd = "$J=G91 X-" + std::to_string(step) + " F" + std::to_string(F);
            break;
        case 4:
            cmd = "$J=G91 Z-" + std::to_string(step) + " F" + std::to_string(F);
            break;
        case 8:
            cmd = "$J=G91 Z" + std::to_string(step) + " F" + std::to_string(F);
            break;
        default:
            return;
        }
        sendJogCommand(cmd);
        waitingForRelease = true;
    }

    static void handleContinuousMode(uint8_t state, float feed)
    {
        static bool jogActive = false;
        const float dist = 1000.0f;
        if (state == 0)
        {
            if (jogActive)
            {
                cnc.sendCommand(CMD_JOG_CANCEL);
                cnc.sendCommand("G4P0");
                cnc.sendCommand(CMD_JOG_CANCEL);
            }
            jogActive = false;
            return;
        }
        if (jogActive)
            return;
        std::string cmd;
        switch (state)
        {
        case 1:
            cmd = "$J=G91 X" + std::to_string(dist) + " F" + std::to_string(feed);
            break;
        case 2:
            cmd = "$J=G91 X-" + std::to_string(dist) + " F" + std::to_string(feed);
            break;
        case 4:
            cmd = "$J=G91 Z-" + std::to_string(dist) + " F" + std::to_string(feed);
            break;
        case 8:
            cmd = "$J=G91 Z" + std::to_string(dist) + " F" + std::to_string(feed);
            break;
        default:
            return;
        }
        cnc.sendCommand(cmd);
        jogActive = true;
    }

    static void updateJog(uint8_t state)
    {
        if (pulseMode > 0)
            handlePulseMode(state);
        else
            handleContinuousMode(state, jogFeed);
    }

    // ---------- disegno frecce ----------
    static void drawArrow(int cx, int cy, int dir, bool active)
    {
        int s = 22;
        uint16_t color = active ? TFT_YELLOW : TFT_GREY;
        if (active)
        {
            float anim = (sin(millis_idf() * 0.02f) + 1.0f) * 0.5f;
            uint8_t glow = 200 + anim * 55;
            color = rgb565(255, glow, 0);
        }
        switch (dir)
        {
        case 0: // su
            tft_fill_triangle(cx, cy - s, cx - s, cy + s, cx + s, cy + s, color);
            break;
        case 1: // giù
            tft_fill_triangle(cx, cy + s, cx - s, cy - s, cx + s, cy - s, color);
            break;
        case 2: // sinistra
            tft_fill_triangle(cx - s, cy, cx + s, cy - s, cx + s, cy + s, color);
            break;
        case 3: // destra
            tft_fill_triangle(cx + s, cy, cx - s, cy - s, cx - s, cy + s, color);
            break;
        }
    }

    // ---------- barra feedrate ----------
    static void drawFeedrateBar(int x, int y, int w, int h)
    {
        static float lastFeed = -1;
        static unsigned long lastUpdate = 0;
        if (jogFeed == lastFeed && millis_idf() - lastUpdate < 1000)
            return;
        lastFeed = jogFeed;
        lastUpdate = millis_idf();
        const int squares = 24;
        int sqw = w / squares;
        float perc = (jogFeed - JOG_FEED_MIN) / (JOG_FEED_MAX - JOG_FEED_MIN);
        if (perc < 0)
            perc = 0;
        if (perc > 1)
            perc = 1;
        int active = perc * squares;
        tft_fill_rect(x, y, w, h, TFT_BLACK);
        for (int i = 0; i < squares; i++)
        {
            int sx = x + i * sqw;
            uint16_t col;
            if (i < 6)
                col = TFT_GREEN;
            else if (i < 12)
                col = TFT_YELLOW;
            else if (i < 18)
                col = rgb565(255, 165, 0);
            else
                col = TFT_RED;
            if (i < active)
                tft_fill_rect(sx, y, sqw - 1, h, col);
            else
                tft_draw_rect(sx, y, sqw - 1, h, TFT_DARKGREY);
        }
        // scritta F:xxx
        tft_fill_rect(x, y - 20, w, 18, TFT_BLACK);
        char buf[16];
        snprintf(buf, sizeof(buf), "F:%d", (int)jogFeed);
        resetTextStyle();
        tft_set_text_color(TFT_WHITE);
        tft_set_text_size(2);
        tft_set_text_datum(ML_DATUM);
        tft_draw_string(buf, x, y - 20, TFT_WHITE, 2);
    }

    // ---------- pulsanti standard ----------
    static void drawStandardButtons()
    {
        tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
        tft_fill_rect(0, 260, 480, 1, TFT_WHITE);
        drawButton_h30(20, 270, 70, 30, TFT_BLACK, '#', 2, TFT_WHITE, "Pulse+", 1, TFT_WHITE);
        drawButton_h30(95, 270, 70, 30, TFT_BLACK, '*', 2, TFT_WHITE, "Pulse-", 1, TFT_WHITE);
        drawButton_h30(170, 270, 70, 30, TFT_BLACK, 'A', 2, TFT_WHITE, "Feed+", 1, TFT_WHITE);
        drawButton_h30(245, 270, 70, 30, TFT_BLACK, 'B', 2, TFT_WHITE, "Feed-", 1, TFT_WHITE);
        drawButton_h30(320, 270, 70, 30, TFT_BLACK, 'C', 2, TFT_WHITE, "Set X", 1, TFT_WHITE);
        drawButton_h30(395, 270, 70, 30, TFT_BLACK, 'D', 2, TFT_WHITE, "Set Z", 1, TFT_WHITE);
    }

    // ---------- impostazione coordinate (Set X / Set Z) ----------
    static void setCoordinate(char axis)
    {
        std::string axisName = (axis == 'X') ? "X" : "Z";
        float currentValueFloat = (axis == 'X') ? cnc.getWorkPositionX() : cnc.getWorkPositionZ();
        std::string inputValue = "";
        bool editing = true, isNegative = false;
        int mx = 25, my = 40, mw = 280, mh = 208;
        int cx = mx + mw / 2;
        std::string lastDisplayValue = "", lastCurrentValue = "";
        bool firstDraw = true;

        // Area pulsanti
        tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
        tft_fill_rect(0, 260, 480, 1, TFT_WHITE);
        int btnW = 70, btnH = 30, btnY = 270, spacing = 10, btnW0_9 = 80;
        int totalWidth = btnW + spacing + btnW + spacing + btnW0_9 + spacing + btnW + spacing + btnW;
        int startX = (480 - totalWidth) / 2;
        int x1 = startX, x2 = x1 + btnW + spacing, x3 = x2 + btnW + spacing,
            x4 = x3 + btnW0_9 + spacing, x5 = x4 + btnW + spacing;
        drawButton(x1, btnY, btnW, btnH, TFT_BLACK, '*', 2, TFT_WHITE, "(.)", 1, TFT_WHITE);
        drawButton(x2, btnY, btnW, btnH, TFT_BLACK, '#', 2, TFT_WHITE, "(-)", 1, TFT_WHITE);
        drawCenteredText2InRoundRect("0-9", x3 + btnW0_9 / 2, btnY + btnH / 2, btnW0_9, btnH, TFT_BLACK, TFT_WHITE);
        drawButton(x4, btnY, btnW, btnH, TFT_BLACK, 'C', 2, TFT_WHITE, "CONFIRM", 1, TFT_WHITE);
        drawButton(x5, btnY, btnW, btnH, TFT_BLACK, 'D', 2, TFT_WHITE, "DELETE", 1, TFT_WHITE);

        tft_fill_rect(mx, my, mw, mh, TFT_BLACK);
        tft_draw_rect(mx, my, mw, mh, TFT_WHITE);
        resetTextStyle();
        tft_set_text_color(TFT_GREEN);
        tft_set_text_size(2);
        tft_set_text_datum(MC_DATUM);
        char title[32];
        snprintf(title, sizeof(title), "Set %s coordinate", axisName.c_str());
        drawCenteredTextAt(title, cx, my + 45, 2, TFT_GREEN);
        tft_fill_rect(mx + 20, my + 65, mw - 40, 1, TFT_DARKGREY);
        tft_set_text_color(TFT_WHITE);
        tft_set_text_datum(ML_DATUM);
        tft_draw_string("Current:", mx + 30, my + 95, TFT_WHITE, 2);
        tft_draw_string("New:", mx + 30, my + 150, TFT_WHITE, 2);
        int currentBoxX = mx + 126, currentBoxW = 120;
        tft_fill_rect(currentBoxX, my + 85, currentBoxW, 28, TFT_DARKGREY);
        tft_draw_rect(currentBoxX, my + 85, currentBoxW, 28, TFT_WHITE);
        int newBoxX = mx + 126, newBoxW = 120;
        tft_fill_rect(newBoxX, my + 140, newBoxW, 28, TFT_DARKGREY);
        tft_draw_rect(newBoxX, my + 140, newBoxW, 28, TFT_WHITE);

        // Centri per il testo
        int currentCenterX = currentBoxX + currentBoxW / 2;
        int currentCenterY = my + 85 + 14;
        int newCenterX = newBoxX + newBoxW / 2;
        int newCenterY = my + 140 + 14;

        char currBuf[16];
        snprintf(currBuf, sizeof(currBuf), "%.3f", currentValueFloat);
        drawCenteredTextAt(currBuf, currentCenterX, currentCenterY, 2, TFT_YELLOW);
        lastCurrentValue = currBuf;

        while (editing)
        {
            // Aggiorna valore corrente se cambiato
            float newCurrent = (axis == 'X') ? cnc.getWorkPositionX() : cnc.getWorkPositionZ();
            char newCurrBuf[16];
            snprintf(newCurrBuf, sizeof(newCurrBuf), "%.3f", newCurrent);
            if (strcmp(newCurrBuf, lastCurrentValue.c_str()) != 0)
            {
                tft_fill_rect(currentBoxX + 5, my + 90, currentBoxW - 10, 22, TFT_BLACK);
                drawCenteredTextAt(newCurrBuf, currentCenterX, currentCenterY, 2, TFT_YELLOW);
                lastCurrentValue = newCurrBuf;
            }

            std::string displayValue = (isNegative ? "-" : "") + inputValue;
            if (displayValue.empty())
                displayValue = "0";
            if (displayValue != lastDisplayValue || firstDraw)
            {
                tft_fill_rect(newBoxX + 5, my + 145, newBoxW - 10, 22, TFT_BLACK);
                drawCenteredTextAt(displayValue.c_str(), newCenterX, newCenterY, 2, TFT_CYAN);
                lastDisplayValue = displayValue;
                firstDraw = false;
            }

            char key = readKeypad();
            if (key >= '0' && key <= '9')
            {
                if (inputValue.length() < 10)
                    inputValue += key;
            }
            else if (key == '.' || key == '*')
            {
                if (inputValue.find('.') == std::string::npos)
                {
                    if (inputValue.empty())
                        inputValue = "0.";
                    else
                        inputValue += ".";
                }
            }
            else if (key == '-' || key == '#')
            {
                isNegative = !isNegative;
            }
            else if (key == 'C')
            {
                float value = 0.0f;
                if (!inputValue.empty())
                {
                    value = safe_stof(inputValue);
                    if (isNegative)
                        value = -value;
                }
                char cmd[30];
                snprintf(cmd, sizeof(cmd), "G10 P0 L20 %c%.3f", axis, value);
                cnc.sendCommand(cmd);
                vTaskDelay(pdMS_TO_TICKS(50));
                editing = false;
            }
            else if (key == 'D')
            {
                editing = false;
            }
            else if (key == 'B')
            {
                if (!inputValue.empty())
                    inputValue.pop_back();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        // Ripristina i pulsanti standard della scena Jog
        drawStandardButtons();
        tft_fill_rect(mx, my, mw, mh, TFT_BLACK);
        tft_draw_rect(mx, my, mw, mh, TFT_WHITE);
        updateDROBoxesIfChanged();
    }

    // ========== SCENA PRINCIPALE JOG ==========
    void jog_scene_enter(void)
    {
        unsigned long lastActivity = millis_idf();
        pulseMode = 0;
        jogFeed = 240.0f;
        unsigned long lastBlink = 0;
        bool contBlink = false;

        // ------------------------------------------------------------
        // Disegno iniziale della schermata Jog
        // ------------------------------------------------------------
        tft_fill_screen(TFT_NAVY);
        resetTextStyle();
        drawTitolo("JOG MODE ACTIVE", 2);
        tft_fill_rect(20, 30, 440, 1, TFT_WHITE);

        int mx = 25, my = 40, mw = 280, mh = 208;
        tft_fill_rect(mx, my, mw, mh, TFT_BLACK);
        tft_draw_rect(mx, my, mw, mh, TFT_WHITE);
        int cx = mx + mw / 2, cy = my + mh / 2;

        drawStandardButtons();
        drawDROBoxes();

        // Lambda per disegnare il cerchio con la modalità di passo
        auto drawPulseCircle = [&](int mode)
        {
            tft_fill_rect(cx - 35, cy - 35, 70, 70, TFT_BLACK);
            tft_fill_circle(cx, cy, 25, TFT_BLACK);
            const char *modeText[] = {"C", "0.01", "0.1", "1", "10"};
            tft_set_text_color(TFT_WHITE);
            tft_set_text_size(2);
            tft_set_font(nullptr);
            int16_t textWidth = tft_text_width(modeText[mode]);
            tft_set_text_datum(4);
            int16_t x = cx - textWidth / 2;
            int16_t y = cy;
            tft_draw_string(modeText[mode], x, y, TFT_WHITE, 2);
        };

        drawPulseCircle(pulseMode);
        int lastPulseMode = pulseMode;

        loadCalibration();

        // --- LOG CALIBRAZIONE (una sola volta) ---
        ESP_LOGI(TAG, "=== CALIBRAZIONE JOYSTICK ===");
        ESP_LOGI(TAG, "Tipo: %s", getJoystickType() == 0 ? "ANALOGICO" : "DIGITALE");
        if (getJoystickType() == 0)
        {
            ESP_LOGI(TAG, "xCenter=%d, yCenter=%d", cal.xCenter, cal.yCenter);
            ESP_LOGI(TAG, "deadzone=%d", cal.deadzone);
            ESP_LOGI(TAG, "xMin=%d, xMax=%d", cal.xMin, cal.xMax);
            ESP_LOGI(TAG, "yMin=%d, yMax=%d", cal.yMin, cal.yMax);
        }

        // --- LOG REALTIME (una sola volta) ---
        int16_t x_raw = ads1x15_read_adc(ads, 2);
        int16_t y_raw = ads1x15_read_adc(ads, 3);
        int x_val = map_ads(x_raw);
        int y_val = map_ads(y_raw);
        uint8_t state = readDigitalState();
        ESP_LOGI(TAG, "Realtime: X raw=%6d (%4d) Y raw=%6d (%4d) | state=0x%02X",
                x_raw, x_val, y_raw, y_val, state);

        // Verifica calibrazione – se non valida, avvia la procedura (senza riavvio)
        if (getJoystickType() == 0)
        {
            int16_t x_raw = ads1x15_read_adc(ads, 2);
            int16_t y_raw = ads1x15_read_adc(ads, 3);
            int x_val = map_ads(x_raw);
            int y_val = map_ads(y_raw);
            if (abs(x_val - cal.xCenter) > cal.deadzone + 100 ||
                abs(y_val - cal.yCenter) > cal.deadzone + 100)
            {
                ESP_LOGW(TAG, "Calibrazione non valida! Avvio calibrazione...");
                jog_calibration_enter();
                // 🔁 Resetta il timer di attività per evitare l'uscita immediata
                lastActivity = millis_idf();
            }
        }

        // ------------------------------------------------------------
        // Loop principale
        // ------------------------------------------------------------
        while (true)
        {
            unsigned long now = millis_idf();
            uint8_t state = readDigitalState();
            uint8_t drawState = state;
            if (pulseMode > 0)
            {
                static uint8_t hold = 0;
                if (state != 0)
                    hold = state;
                drawState = hold;
                if (state == 0)
                    hold = 0;
            }
            else
            {
                if (now - lastBlink > 250)
                {
                    contBlink = !contBlink;
                    lastBlink = now;
                }
                if (state != 0)
                    drawState = contBlink ? state : 0;
            }

            if (state != 0)
                lastActivity = now;
            if (now - lastActivity > 15000)
            {
                cnc.sendCommand(CMD_JOG_CANCEL);
                resetTextStyle();
                break; // esci, torna al menu
            }

            char key = readKeypad();
            if (key != 0)
                lastActivity = now;
            if (key == '#')
            {
                pulseMode = (pulseMode + 1) % 5;
                drawPulseCircle(pulseMode);
            }
            if (key == '*')
            {
                pulseMode = (pulseMode + 4) % 5;
                drawPulseCircle(pulseMode);
            }
            if (key == 'A')
            {
                jogFeed += JOG_FEED_STEP;
                if (jogFeed > JOG_FEED_MAX)
                    jogFeed = JOG_FEED_MAX;
            }
            if (key == 'B')
            {
                jogFeed -= JOG_FEED_STEP;
                if (jogFeed < JOG_FEED_MIN)
                    jogFeed = JOG_FEED_MIN;
            }
            if (key == 'C')
            {
                cnc.sendCommand(CMD_JOG_CANCEL);
                setCoordinate('X');
                updateDROBoxesIfChanged();
            }
            if (key == 'D')
            {
                cnc.sendCommand(CMD_JOG_CANCEL);
                setCoordinate('Z');
                updateDROBoxesIfChanged();
            }

            if (pulseMode != lastPulseMode)
            {
                drawPulseCircle(pulseMode);
                lastPulseMode = pulseMode;
            }

            updateDROBoxesIfChanged();
            drawArrow(cx, cy - 52, 0, drawState & 2);
            drawArrow(cx, cy + 52, 1, drawState & 1);
            drawArrow(cx - 52, cy, 2, drawState & 4);
            drawArrow(cx + 52, cy, 3, drawState & 8);

            if (pulseMode == 0)
                drawFeedrateBar(mx + 20, my + mh - 18, 240, 10);
            else
            {
                tft_fill_rect(mx + 20, my + mh - 38, 100, 20, TFT_BLACK);
                tft_fill_rect(mx + 20, my + mh - 18, 240, 10, TFT_BLACK);
            }

            updateJog(state);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    // ========== CALIBRAZIONE JOYSTICK ==========
    /*void jog_calibration_enter(void)
    {
        if (getJoystickType() == 1)
        {
            tft_fill_screen(TFT_BLACK);
            resetTextStyle();
            tft_set_text_color(TFT_YELLOW);
            tft_set_text_datum(MC_DATUM);
            tft_draw_string("JOYSTICK DIGITALE", 240, 120, TFT_YELLOW, 2);
            tft_draw_string("Calibrazione non necessaria", 240, 160, TFT_YELLOW, 2);
            tft_draw_string("Premi un tasto...", 240, 200, TFT_YELLOW, 2);
            while (readKeypad() == 0)
                vTaskDelay(pdMS_TO_TICKS(10));
            return;
        }

        tft_fill_screen(TFT_BLACK);
        resetTextStyle();
        tft_set_text_color(TFT_YELLOW);
        tft_set_text_size(2);
        tft_set_text_datum(MC_DATUM);
        tft_draw_string("CALIBRAZIONE JOYSTICK", 240, 20, TFT_YELLOW, 2);
        tft_set_text_color(TFT_WHITE);
        tft_draw_string("Muovi il joystick in tutte le direzioni", 240, 80, TFT_WHITE, 2);
        tft_draw_string("Premi C per confermare", 240, 120, TFT_WHITE, 2);

        int16_t xMinRaw = 32767, xMaxRaw = -32768, yMinRaw = 32767, yMaxRaw = -32768;
        unsigned long lastLog = 0;
        while (true)
        {
            int16_t x = ads1x15_read_adc(ads, 2);
            int16_t y = ads1x15_read_adc(ads, 3);
            if (x < xMinRaw)
                xMinRaw = x;
            if (x > xMaxRaw)
                xMaxRaw = x;
            if (y < yMinRaw)
                yMinRaw = y;
            if (y > yMaxRaw)
                yMaxRaw = y;

            if (millis_idf() - lastLog > 150)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "X:%5d  Y:%5d", x, y);
                tft_fill_rect(0, 200, 480, 40, TFT_BLACK);
                tft_draw_string(buf, 240, 220, TFT_WHITE, 2);
                lastLog = millis_idf();
            }

            char key = readKeypad();
            if (key == 'C')
                break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        cal.xCenter = map_ads((xMinRaw + xMaxRaw) / 2);
        cal.yCenter = map_ads((yMinRaw + yMaxRaw) / 2);
        cal.xMin = map_ads(xMinRaw);
        cal.xMax = map_ads(xMaxRaw);
        cal.yMin = map_ads(yMinRaw);
        cal.yMax = map_ads(yMaxRaw);
        cal.deadzone = 100;
        cal.valid = true;
        saveCalibration();

        tft_fill_screen(TFT_BLACK);
        tft_set_text_color(TFT_YELLOW);
        tft_draw_string("CALIBRAZIONE COMPLETATA", 240, 140, TFT_YELLOW, 2);
        tft_draw_string("Salvataggio completato", 240, 180, TFT_YELLOW, 2);
        tft_draw_string("Riavvio del sistema...", 240, 220, TFT_YELLOW, 2);
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    }*/

    void jog_calibration_enter(void)
    {
        // Dimensioni e coordinate dell'area centrale (come in jog_scene_enter)
        const int mx = 25, my = 40, mw = 280, mh = 208;
        const int cx = mx + mw / 2;

        if (getJoystickType() == 1)
        {
            // Joystick digitale: messaggio nell'area centrale
            tft_fill_rect(mx, my, mw, mh, TFT_BLACK);
            tft_draw_rect(mx, my, mw, mh, TFT_WHITE);
            resetTextStyle();
            drawCenteredTextAt("JOYSTICK DIGITALE", cx, my + 60, 2, TFT_YELLOW);
            drawCenteredTextAt("Calibrazione non necessaria", cx, my + 100, 2, TFT_YELLOW);
            drawCenteredTextAt("Premi un tasto...", cx, my + 140, 2, TFT_YELLOW);
            while (readKeypad() == 0)
                vTaskDelay(pdMS_TO_TICKS(10));
            // Ripulisci l'area centrale e ridisegna il contenuto originale del jog
            tft_fill_rect(mx, my, mw, mh, TFT_BLACK);
            tft_draw_rect(mx, my, mw, mh, TFT_WHITE);
            return;
        }

        // Joystick analogico: calibrazione
        tft_fill_rect(mx, my, mw, mh, TFT_BLACK);
        tft_draw_rect(mx, my, mw, mh, TFT_WHITE);
        resetTextStyle();
        drawCenteredTextAt("CALIBRAZIONE JOYSTICK", cx, my + 30, 2, TFT_YELLOW);
        drawCenteredTextAt("Muovi il joystick in", cx, my + 70, 2, TFT_WHITE);
        drawCenteredTextAt("tutte le direzioni", cx, my + 100, 2, TFT_WHITE);
        drawCenteredTextAt("Premi C per confermare", cx, my + 130, 2, TFT_WHITE);

        int16_t xMinRaw = 32767, xMaxRaw = -32768, yMinRaw = 32767, yMaxRaw = -32768;
        unsigned long lastLog = 0;
        while (true)
        {
            int16_t x = ads1x15_read_adc(ads, 2);
            int16_t y = ads1x15_read_adc(ads, 3);
            if (x < xMinRaw)
                xMinRaw = x;
            if (x > xMaxRaw)
                xMaxRaw = x;
            if (y < yMinRaw)
                yMinRaw = y;
            if (y > yMaxRaw)
                yMaxRaw = y;

            if (millis_idf() - lastLog > 150)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "X:%5d  Y:%5d", x, y);
                tft_fill_rect(mx + 20, my + 160, mw - 40, 30, TFT_BLACK);
                drawCenteredTextAt(buf, cx, my + 175, 2, TFT_WHITE);
                lastLog = millis_idf();
            }

            char key = readKeypad();
            if (key == 'C')
                break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Calcola e salva la calibrazione
        cal.xCenter = map_ads((xMinRaw + xMaxRaw) / 2);
        cal.yCenter = map_ads((yMinRaw + yMaxRaw) / 2);
        cal.xMin = map_ads(xMinRaw);
        cal.xMax = map_ads(xMaxRaw);
        cal.yMin = map_ads(yMinRaw);
        cal.yMax = map_ads(yMaxRaw);
        cal.deadzone = 100;
        cal.valid = true;
        saveCalibration();

        // Messaggio di completamento (breve)
        tft_fill_rect(mx + 20, my + 160, mw - 40, 30, TFT_BLACK);
        drawCenteredTextAt("Calibrazione salvata!", cx, my + 175, 2, TFT_YELLOW);
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Ripulisci l'area centrale (il resto della schermata è intatto)
        tft_fill_rect(mx, my, mw, mh, TFT_BLACK);
        tft_draw_rect(mx, my, mw, mh, TFT_WHITE);
    }