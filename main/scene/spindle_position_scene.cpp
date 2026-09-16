// spindle_position_scene.cpp
#include "spindle_position_scene.h"
#include "scene_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "global_vars.h"
#include "global_settings.h"
#include "graphics_lgfx.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "msgbox.h"
#include "keypad.h"
#include "cnc_manager.h"
#include "grbl_commands.h"
#include "colors.h"
#include "main_scene.h"
#include <cmath>
#include <cstring>
#include <string>
#include <cstdio>

static const char *TAG = "SPINDLE_POS";

// ========== Configurazione pin encoder (da configPin.h) ==========
// Per ora definiti qui, in futuro si possono spostare in configPin.h
#ifndef ENCA_GPIO
#define ENCA_GPIO 6
#endif
#ifndef ENCB_GPIO
#define ENCB_GPIO 7
#endif

// Direzione encoder (0 = orario, 1 = antiorario) – gestita via settings
static bool encoderDir = false; // viene caricata da NVS

// Senso di visualizzazione del goniometro (0 = CW, 1 = CCW) – gestito via settings
static int spindleDir = 0; // verrà caricato da NVS

// Variabili per encoder
static volatile long encoderSteps = 0;
static volatile int lastEncoded = 0;
static const int oneTurnEncoderSteps = 1440; // 360° * 4

static float currentAngle = 0.0f;
static float lastAngle = 0.0f;
static bool spindleLocked = false;
static bool settingAngle = false;
static std::string angleInput = "";
static bool useEncoder = false; // false = servo (GRBL), true = encoder fisico

static bool showAlert = false;
static unsigned long alertStartTime = 0;

// Lampeggio stato mandrino
static unsigned long lastBlinkTime = 0;
static bool blinkState = true;
static const unsigned long BLINK_ON_TIME = 750;
static const unsigned long BLINK_OFF_TIME = 250;

// Millis per IDF
static inline unsigned long millis_idf()
{
    return (unsigned long)(esp_timer_get_time() / 1000);
}

// Forward declarations
static void updateSpindleAngle();
static void drawIndicator(int x, int y, int r, float angle, float &lastAngle);
static void updateButtonA();
static void updateButtonB();
static void updateButtonC();
static void updateStatusBox();
static void redrawButtonsArea();
static void drawAnglePopup();
static void drawGoniometer(int x, int y, int r);

// ========== Gestione encoder con interrupt ESP‑IDF ==========
static void IRAM_ATTR encoder_isr_handler(void *arg)
{
    int msb = gpio_get_level((gpio_num_t)ENCA_GPIO);
    int lsb = gpio_get_level((gpio_num_t)ENCB_GPIO);
    int encoded = (msb << 1) | lsb;
    int sum = (lastEncoded << 2) | encoded;
    if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
        encoderSteps = encoderSteps + 1;
    if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
        encoderSteps = encoderSteps - 1;
    lastEncoded = encoded;
}

static void initEncoder()
{
    // configura i pin encoder
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.pin_bit_mask = (1ULL << ENCA_GPIO) | (1ULL << ENCB_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)ENCA_GPIO, encoder_isr_handler, nullptr);
    gpio_isr_handler_add((gpio_num_t)ENCB_GPIO, encoder_isr_handler, nullptr);
    // stato iniziale
    int msb = gpio_get_level((gpio_num_t)ENCA_GPIO);
    int lsb = gpio_get_level((gpio_num_t)ENCB_GPIO);
    lastEncoded = (msb << 1) | lsb;
}

static void disableEncoder()
{
    gpio_isr_handler_remove((gpio_num_t)ENCA_GPIO);
    gpio_isr_handler_remove((gpio_num_t)ENCB_GPIO);
    // Non disinstallare il servizio ISR globale se usato altrove
    // gpio_uninstall_isr_service();
}

// ========== Funzioni di controllo mandrino ==========
static void setSpindleLockState()
{
    int unlockParam = settings_get_spindle_unlock_param();
    char cmd[20];
    snprintf(cmd, sizeof(cmd), "$37=%d", unlockParam);
    cnc.sendCommand(cmd);
    spindleLocked = false;
    ESP_LOGI(TAG, "Stato iniziale: MANDINO SBLOCCATO (comando %s)", cmd);
}

// ========== Aggiornamento angolo ==========
static void updateAngleFromEncoder()
{
    float angularRatio = 360.0f / oneTurnEncoderSteps;
    long steps = encoderSteps % oneTurnEncoderSteps;
    if (steps < 0)
        steps += oneTurnEncoderSteps;
    currentAngle = steps * angularRatio;
    if (currentAngle >= 360.0f)
        currentAngle = 0.0f;
    if (currentAngle < 0)
        currentAngle += 360.0f;
}

static void updateAngleFromGrbl()
{
    float aPosition = DRO_A; // deve essere definita in global_vars.h
    currentAngle = fmod(aPosition * 360.0f, 360.0f);
    if (currentAngle < 0)
        currentAngle += 360.0f;
    if (currentAngle >= 360.0f)
        currentAngle = 0.0f;
}

static void updateSpindleAngle()
{
    if (useEncoder)
        updateAngleFromEncoder();
    else
        updateAngleFromGrbl();
}

// ========== Funzioni di disegno pulsanti ==========
static void updateButtonA()
{
    resetTextStyle();
    int x = 320, y = 50, w = 130, h = 35;
    tft_fill_rect(x, y, w, h + 15, TFT_NAVY);
    uint16_t buttonColor = spindleLocked ? TFT_RED : TFT_DARKGREEN;
    std::string buttonText = spindleLocked ? "UNLOCK SPINDLE" : "LOCK SPINDLE";
    drawButton(x, y, w, h, buttonColor, 'A', 2, TFT_WHITE, buttonText, 1, TFT_WHITE);
}

static void updateButtonB()
{
    resetTextStyle();
    int x = 320, y = 120, w = 130, h = 35;
    tft_fill_rect(x, y, w, h + 15, TFT_NAVY);
    uint16_t buttonColor = useEncoder ? TFT_MAGENTA : TFT_DARKGREY;
    std::string buttonText = useEncoder ? "AUTO POSITIONING" : "MANUAL POSITIONING";
    drawButton(x, y, w, h, buttonColor, 'B', 2, TFT_WHITE, buttonText, 1, TFT_WHITE);
}

static void updateButtonC()
{
    resetTextStyle();
    int x = 320, y = 190, w = 130, h = 35;
    tft_fill_rect(x, y, w, h + 15, TFT_NAVY);
    std::string buttonText = useEncoder ? "SET 0" : "SET ANGLE";
    drawButton(x, y, w, h, TFT_DARKCYAN, 'C', 2, TFT_WHITE, buttonText, 1, TFT_WHITE);
}

static void redrawButtonsArea()
{
    resetTextStyle();
    tft_fill_rect(291, 41, 189, 274, TFT_NAVY);
    tft_draw_line(290, 40, 290, 315, TFT_WHITE);
    drawButton(320, 260, 130, 35, TFT_BLACK, 'D', 2, TFT_WHITE, "EXIT", 1, TFT_WHITE);
}

static void updateStatusBox()
{
    int statusBoxX = 150 - 70;
    int statusBoxY = 288;
    int statusBoxW = 140;
    int statusBoxH = 28;
    tft_fill_round_rect(statusBoxX, statusBoxY, statusBoxW, statusBoxH, 8, TFT_BLACK);
    tft_draw_round_rect(statusBoxX, statusBoxY, statusBoxW, statusBoxH, 8, TFT_WHITE);
    tft_set_text_size(1);
    tft_set_text_datum(MC_DATUM);
    std::string statusText = spindleLocked ? "SPINDLE LOCKED" : "SPINDLE UNLOCKED";
    if (tft_text_width(statusText.c_str()) > statusBoxW - 10)
        statusText = spindleLocked ? "LOCKED" : "UNLOCKED";
    if (spindleLocked)
    {
        if (blinkState)
        {
            tft_set_text_color(TFT_WHITE);
            drawCenteredTextAt(statusText.c_str(), 150, statusBoxY + statusBoxH / 2, 1, TFT_WHITE);
        }
        else
        {
            // invisibile su sfondo nero
            tft_set_text_color(TFT_BLACK);
            drawCenteredTextAt(statusText.c_str(), 150, statusBoxY + statusBoxH / 2, 1, TFT_BLACK);
        }
    }
    else
    {
        tft_set_text_color(TFT_WHITE);
        drawCenteredTextAt(statusText.c_str(), 150, statusBoxY + statusBoxH / 2, 1, TFT_WHITE);
    }
}

// ========== Popup per inserimento angolo ==========
static void drawAnglePopup()
{
    int popupX = 300, popupY = 100, popupW = 170, popupH = 130;
    tft_fill_round_rect(popupX, popupY, popupW, popupH, 10, TFT_DARKGREY);
    tft_draw_round_rect(popupX, popupY, popupW, popupH, 10, TFT_YELLOW);
    tft_draw_round_rect(popupX + 2, popupY + 2, popupW - 4, popupH - 4, 8, TFT_YELLOW);
    tft_set_text_color(TFT_YELLOW);
    tft_set_text_size(1);
    tft_set_text_datum(TC_DATUM);
    drawCenteredTextAt("ENTER SPINDLE ANGLE", popupX + popupW / 2, popupY + 20, 1, TFT_YELLOW);
    int valueBoxX = popupX + 20, valueBoxY = popupY + 45, valueBoxW = popupW - 40, valueBoxH = 35;
    tft_fill_rect(valueBoxX, valueBoxY, valueBoxW, valueBoxH, TFT_BLACK);
    tft_draw_rect(valueBoxX, valueBoxY, valueBoxW, valueBoxH, TFT_CYAN);
    tft_set_text_color(TFT_CYAN);
    tft_set_text_size(2);
    tft_set_text_datum(MC_DATUM);
    std::string displayValue = angleInput.empty() ? "0" : angleInput;
    drawCenteredTextAt(displayValue.c_str(), valueBoxX + valueBoxW / 2, valueBoxY + valueBoxH / 2, 2, TFT_CYAN);
    tft_set_text_color(TFT_WHITE);
    tft_set_text_size(1);
    tft_set_text_datum(TC_DATUM);
    drawCenteredTextAt("C: CONFIRM   D: EXIT", popupX + popupW / 2, popupY + popupH - 15, 1, TFT_WHITE);
}

// ========== Goniometro e indicatore (usano spindleDir) ==========
static void drawGoniometer(int x, int y, int r)
{
    tft_fill_circle(x, y, r, TFT_BLACK);
    tft_draw_circle(x, y, r, TFT_WHITE);
    for (int i = 0; i < 360; i += 15)
    {
        float angle = (spindleDir == 1) ? (360 - i) : i;
        float rad = (angle - 90) * M_PI / 180.0f;
        int dx = (int)(r * cos(rad));
        int dy = (int)(r * sin(rad));
        if (i % 90 == 0)
        {
            tft_draw_line(x + dx, y + dy, x + (int)(dx * 0.85), y + (int)(dy * 0.85), TFT_YELLOW);
            char buffer[4];
            snprintf(buffer, sizeof(buffer), "%d", i);
            int textX = x + (int)(dx * 0.70);
            int textY = y + (int)(dy * 0.70);
            tft_set_text_size(2);
            tft_set_text_color(TFT_YELLOW);
            tft_set_text_datum(MC_DATUM);
            drawCenteredTextAt(buffer, textX, textY, 2, TFT_YELLOW);
        }
        else
        {
            tft_draw_line(x + dx, y + dy, x + (int)(dx * 0.92), y + (int)(dy * 0.92), TFT_WHITE);
        }
    }
}

static void drawIndicator(int x, int y, int r, float angle, float &lastAngle)
{
    float displayAngle = angle;
    float lastDisplayAngle = lastAngle;
    if (spindleDir == 1)
    {
        displayAngle = 360.0f - angle;
        lastDisplayAngle = 360.0f - lastAngle;
        if (displayAngle >= 360.0f)
            displayAngle = 0.0f;
        if (lastDisplayAngle >= 360.0f)
            lastDisplayAngle = 0.0f;
    }
    float lastRad = (lastDisplayAngle - 90) * M_PI / 180.0f;
    int lastAx = (int)((r + 12) * cos(lastRad));
    int lastAy = (int)((r + 12) * sin(lastRad));
    tft_fill_circle(x + lastAx, y + lastAy, r / 20, TFT_NAVY);
    float rad = (displayAngle - 90) * M_PI / 180.0f;
    int ax = (int)((r + 12) * cos(rad));
    int ay = (int)((r + 12) * sin(rad));
    tft_fill_circle(x + ax, y + ay, r / 20, TFT_RED);
    int textBoxW = 110, textBoxH = 30;
    tft_fill_rect(x - textBoxW / 2, y - textBoxH / 2, textBoxW, textBoxH, TFT_BLACK);
    float showAngle = angle;
    if (showAngle >= 359.99f)
        showAngle = 0.0f;
    char buffer[10];
    snprintf(buffer, sizeof(buffer), "%4.2f", showAngle);
    tft_set_text_size(3);
    tft_set_text_color(TFT_GREEN);
    tft_set_text_datum(MC_DATUM);
    drawCenteredTextAt(buffer, x, y, 3, TFT_GREEN);
    tft_set_text_size(1);
    uint16_t textColor = useEncoder ? TFT_WHITE : TFT_BLACK;
    tft_set_text_color(textColor);
    tft_set_text_datum(TC_DATUM);
    drawCenteredTextAt("ENCODER ACTIVE", x, y + 25, 1, textColor);
}

// ========== Carica impostazioni da NVS ==========
static void loadSpindleSettings()
{
    spindleDir = settings_get_spindle_dir(); // 0 o 1
    encoderDir = settings_get_encoder_dir(); // 0 o 1
    // Nota: encoderDir potrebbe essere usato per scambiare i pin, ma nel nostro codice ISR
    // possiamo gestirlo invertendo il conteggio. Per semplicità, ora non lo usiamo.
    ESP_LOGI(TAG, "Spindle dir = %d, Encoder dir = %d", spindleDir, encoderDir);
}

// ========== Scena principale ==========
void drawSpindlePositionScene()
{
    // Arresta mandrino
    char *stopSpindle = strdup("M5\n");
    if (stopSpindle && xQueueSend(gcodeQueue, &stopSpindle, 0) != pdTRUE)
    {
        free(stopSpindle);
        ESP_LOGE(TAG, "Coda piena per M5");
    }

    // Carica impostazioni
    loadSpindleSettings();

    // Imposta stato iniziale (sbloccato)
    setSpindleLockState();

    useEncoder = false;
    disableEncoder();

    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("SPINDLE POSITION", 240, 15, 2, TFT_YELLOW);
    tft_draw_line(20, 30, 460, 30, TFT_WHITE);
    tft_draw_line(290, 40, 290, 315, TFT_WHITE);

    drawGoniometer(150, 160, 110);
    updateSpindleAngle();
    drawIndicator(150, 160, 110, currentAngle, lastAngle);
    lastAngle = currentAngle;
    updateStatusBox();
    redrawButtonsArea();
    updateButtonA();
    updateButtonB();
    updateButtonC();

    // Loop principale
    while (true)
    {
        // Gestione lampeggio stato
        if (spindleLocked)
        {
            unsigned long now = millis_idf();
            unsigned long elapsed = now - lastBlinkTime;
            if (blinkState && elapsed >= BLINK_ON_TIME)
            {
                blinkState = false;
                lastBlinkTime = now;
                updateStatusBox();
            }
            else if (!blinkState && elapsed >= BLINK_OFF_TIME)
            {
                blinkState = true;
                lastBlinkTime = now;
                updateStatusBox();
            }
        }

        if (!spindleLocked && !settingAngle)
        {
            updateSpindleAngle();
            if (fabs(currentAngle - lastAngle) > 0.01f)
            {
                drawIndicator(150, 160, 110, currentAngle, lastAngle);
                lastAngle = currentAngle;
            }
        }

        char key = readKeypad();

        // Modalità impostazione angolo
        if (settingAngle)
        {
            if (key == 'C')
            {
                float targetAngle = safe_stof(angleInput);
                if (targetAngle >= 0 && targetAngle <= 360)
                {
                    if (!useEncoder)
                    {
                        float targetRevolutions = targetAngle / 360.0f;
                        char cmd[50];
                        snprintf(cmd, sizeof(cmd), "G0 A%.3f\n", targetRevolutions);
                        cnc.sendCommand(cmd);
                        ESP_LOGI(TAG, "Spostamento A a %.1f° (%.3f giri)", targetAngle, targetRevolutions);
                        vTaskDelay(pdMS_TO_TICKS(500));
                    }
                    else
                    {
                        ESP_LOGW(TAG, "In modalità Encoder, SET ANGLE non disponibile");
                    }
                    updateSpindleAngle();
                    drawIndicator(150, 160, 110, currentAngle, lastAngle);
                    lastAngle = currentAngle;
                }
                settingAngle = false;
                angleInput.clear();
                redrawButtonsArea();
                updateButtonA();
                updateButtonB();
                updateButtonC();
                updateStatusBox();
            }
            else if (key == 'D')
            {
                settingAngle = false;
                angleInput.clear();
                redrawButtonsArea();
                updateButtonA();
                updateButtonB();
                updateButtonC();
                updateStatusBox();
            }
            else if (key >= '0' && key <= '9')
            {
                if (angleInput.length() < 7)
                {
                    angleInput += key;
                    drawAnglePopup();
                }
            }
            else if (key == '*' && angleInput.find('.') == std::string::npos)
            {
                if (angleInput.empty())
                    angleInput = "0";
                if (angleInput.find('.') == std::string::npos)
                {
                    angleInput += '.';
                    drawAnglePopup();
                }
            }
            else if (key == '#')
            {
                if (!angleInput.empty())
                {
                    angleInput.pop_back();
                    drawAnglePopup();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Pulsante A
        if (key == 'A')
        {
            if (spindleLocked)
            {
                int unlockParam = settings_get_spindle_unlock_param();
                char cmd[20];
                snprintf(cmd, sizeof(cmd), "$37=%d", unlockParam);
                cnc.sendCommand(cmd);
                spindleLocked = false;
                ESP_LOGI(TAG, "Mandrino sbloccato (comando %s)", cmd);
                updateSpindleAngle();
                drawIndicator(150, 160, 110, currentAngle, lastAngle);
                lastAngle = currentAngle;
            }
            else
            {
                int lockParam = settings_get_spindle_lock_param();
                char cmd[20];
                snprintf(cmd, sizeof(cmd), "$37=%d", lockParam);
                cnc.sendCommand(cmd);
                spindleLocked = true;
                ESP_LOGI(TAG, "Mandrino bloccato (comando %s)", cmd);
            }
            updateButtonA();
            updateStatusBox();
        }

        // Pulsante B
        if (key == 'B')
        {
            useEncoder = !useEncoder;
            if (useEncoder)
            {
                float angularRatio = 360.0f / oneTurnEncoderSteps;
                encoderSteps = (long)(currentAngle / angularRatio);
                encoderSteps %= oneTurnEncoderSteps;
                if (encoderSteps < 0)
                    encoderSteps += oneTurnEncoderSteps;
                initEncoder();
                ESP_LOGI(TAG, "Modalità ENCODER attivata - angolo %.1f° (%ld passi)", currentAngle, encoderSteps);
            }
            else
            {
                disableEncoder();
                ESP_LOGI(TAG, "Modalità SERVO attivata");
            }
            updateButtonB();
            updateButtonC();
            updateSpindleAngle();
            drawIndicator(150, 160, 110, currentAngle, lastAngle);
            lastAngle = currentAngle;
            if (settingAngle)
            {
                settingAngle = false;
                angleInput.clear();
                updateStatusBox();
            }
        }

        // Pulsante C
        if (key == 'C')
        {
            if (useEncoder)
            {
                encoderSteps = 0;
                currentAngle = 0.0f;
                drawIndicator(150, 160, 110, currentAngle, lastAngle);
                lastAngle = currentAngle;
                ESP_LOGI(TAG, "Encoder resettato a 0°");
            }
            else
            {
                if (spindleLocked)
                {
                    // Alert
                    int alertX = 300, alertY = 132, alertW = 170, alertH = 80;
                    tft_fill_round_rect(alertX, alertY, alertW, alertH, 10, TFT_RED);
                    tft_draw_round_rect(alertX, alertY, alertW, alertH, 10, TFT_YELLOW);
                    tft_draw_round_rect(alertX + 2, alertY + 2, alertW - 4, alertH - 4, 8, TFT_YELLOW);
                    drawCenteredTextAt("UNLOCK SPINDLE", alertX + alertW / 2, alertY + 27, 1, TFT_WHITE);
                    drawCenteredTextAt("BEFORE SET ANGLE", alertX + alertW / 2, alertY + 50, 1, TFT_WHITE);
                    ESP_LOGW(TAG, "Sbloccare il mandrino prima di impostare l'angolo");
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    // Ripristina
                    redrawButtonsArea();
                    updateButtonA();
                    updateButtonB();
                    updateButtonC();
                    updateStatusBox();
                    drawGoniometer(150, 160, 110);
                    drawIndicator(150, 160, 110, currentAngle, lastAngle);
                }
                else
                {
                    settingAngle = true;
                    angleInput.clear();
                    drawAnglePopup();
                }
            }
        }

        // Pulsante D
        if (key == 'D')
        {
            disableEncoder();
            resetTextStyle();
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void spindle_position_scene_enter(void)
{
    drawSpindlePositionScene();
}