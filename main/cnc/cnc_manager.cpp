#include "cnc_manager.h"
#include <cmath>
#include "esp_log.h"
#include "freertos/task.h"
#include <cstring>
#include <algorithm>
#include "global_vars.h"   // per le variabili globali condivise

static const char* TAG = "CNC";

// ===== DEFINIZIONI GLOBALI =====
ESPNowManager cnc;           // definizione dell'oggetto cnc (specifico)

// ============================================================
//  INIZIALIZZAZIONE E CONNESSIONE (ESP‑NOW)
// ============================================================

void setupCNC()
{
    ESP_LOGI(TAG, "Inizializzazione GRBL + ESP-NOW");

    cnc.onPosition(onPositionUpdate);
    cnc.onError(onError);
    cnc.onStateChange(onStateChange);

    if (cnc.begin())
    {
        ESP_LOGI(TAG, "Pairing con il bridge riuscito");
        if (waitForGRBLConnection(15000))
        {
            initializeGRBLSettings();
        }
        else
        {
            handleConnectionError();
        }
    }
    else
    {
        ESP_LOGE(TAG, "Pairing fallito!");
        handleConnectionError();
    }
}

bool waitForGRBLConnection(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Attesa primo status report da GRBL...");
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms))
    {
        cnc.update();
        if (cnc.getMachineState() != STATE_UNKNOWN)
        {
            ESP_LOGI(TAG, "GRBL connesso e attivo");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGE(TAG, "Timeout: nessun report ricevuto");
    return false;
}

void handleConnectionError()
{
    while (true)
    {
        ESP_LOGE(TAG, "Riavvio in 3 secondi...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
}

// ============================================================
//  LETTURA PARAMETRI GRBL
// ============================================================

float readParamWithRetry(const char* param, int wait_ms)
{
    float value = -1;
    while (value <= 0)
    {
        std::string resp = cnc.queryReply(param, 2000);
        if (!resp.empty())
        {
            size_t eq = resp.find('=');
            if (eq != std::string::npos)
            {
                value = std::stof(resp.substr(eq + 1));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
    return value;
}

void readMaxSpindleRPM()
{
    float rpm = readParamWithRetry("$30", 150);
    if (rpm > 0)
    {
        maxSpindleRPM = rpm;
        ESP_LOGI(TAG, "Max RPM rilevato: %.1f", maxSpindleRPM);
    }
    else
    {
        maxSpindleRPM = 0;
        ESP_LOGW(TAG, "Max RPM non rilevato");
    }
}

void readMaxFeedRates()
{
    maxFeedX = readParamWithRetry("$110", 150);
    maxFeedZ = readParamWithRetry("$112", 150);
    ESP_LOGI(TAG, "Max Feed X ($110): %.1f mm/min", maxFeedX);
    ESP_LOGI(TAG, "Max Feed Z ($112): %.1f mm/min", maxFeedZ);
}

int readPlannerBufferSize()
{
    int value = (int)readParamWithRetry("$398", 150);
    if (value > 1000)
    {
        ESP_LOGW(TAG, "Valore anomalo (%d) → ripeto lettura...", value);
        value = (int)readParamWithRetry("$398", 150);
    }
    if (value > 0 && value <= 1000)
    {
        ESP_LOGI(TAG, "Planner buffer size ($398): %d", value);
        return value;
    }
    ESP_LOGW(TAG, "Impossibile leggere $398, uso fallback 35");
    return 35;
}

void checkAutoReportStatus()
{
    float interval = readParamWithRetry("$481", 200);
    if (interval >= 0)
    {
        AutoReportStatus = (interval > 0);
        ESP_LOGI(TAG, "Auto-Report: %s (%.0f ms)", AutoReportStatus ? "ENABLED" : "DISABLED", interval);
    }
    else
    {
        AutoReportStatus = false;
        ESP_LOGW(TAG, "Auto-Report: UNABLE TO DETECT");
    }
}

void initializeGRBLSettings()
{
    vTaskDelay(pdMS_TO_TICKS(500));
    cnc.update();

    MachineState state = cnc.getMachineState();
    ESP_LOGI(TAG, "Stato iniziale: %s", getStateString(state).c_str());

    if (state != STATE_IDLE && state != STATE_UNKNOWN)
    {
        ESP_LOGW(TAG, "Stato iniziale non Idle, invio reset...");
        cnc.sendCommand("$X");
        cnc.sendRealtime(0x18);   // Ctrl-X
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    else
    {
        ESP_LOGI(TAG, "Stato iniziale Idle, nessun reset necessario");
    }

    readMaxSpindleRPM();
    checkAutoReportStatus();
    readMaxFeedRates();
    PLANNER_BUFFER_SIZE = readPlannerBufferSize();

    ESP_LOGI(TAG, "GrblHAL Initialized");
    grblReady = true;
}

// ============================================================
//  CALLBACK
// ============================================================

void onPositionUpdate(const Position& pos)
{
    float rpm = cnc.getSpindleSpeed();

    DRO_X_Changed = fabs(cnc.getWorkPositionX() - last_DRO_X) > 0.01;
    DRO_Z_Changed = fabs(cnc.getWorkPositionZ() - last_DRO_Z) > 0.01;
    DRO_A_Changed = fabs(cnc.getWorkPositionA() - last_DRO_A) > 0.01;
    DRO_RPM_Changed = fabs(rpm - last_DRO_RPM) > 0.1;

    portENTER_CRITICAL(&tftMutex);
    DRO_X = cnc.getWorkPositionX();
    DRO_Z = cnc.getWorkPositionZ();
    DRO_A = cnc.getWorkPositionA();
    DRO_RPM = rpm;
    portEXIT_CRITICAL(&tftMutex);
}

void onError(const std::string& error)
{
    ESP_LOGE(TAG, "Errore GRBL: %s", error.c_str());
    controllerError = true;
}

void onStateChange(MachineState oldState, MachineState newState)
{
    if (gcodeTrackingActive)
    {
        if ((oldState == STATE_RUN && newState == STATE_IDLE) ||
            (oldState == STATE_IDLE && newState == STATE_RUN))
        {
            // gcodeTracker.advance();   // se implementato
        }
    }
}

std::string getStateString(MachineState state)
{
    switch (state)
    {
        case STATE_IDLE:   return "IDLE";
        case STATE_RUN:    return "RUN";
        case STATE_HOLD:   return "HOLD";
        case STATE_JOG:    return "JOG";
        case STATE_ALARM:  return "ALARM";
        case STATE_CHECK:  return "CHECK";
        case STATE_HOME:   return "HOME";
        case STATE_SLEEP:  return "SLEEP";
        case STATE_TOOL:   return "TOOL";
        default:           return "UNKNOWN";
    }
}

// ============================================================
//  VALIDAZIONE E INVIO G‑CODE (streaming)
// ============================================================

bool validateRawGcode(const std::string& gcode)
{
    if (gcode.empty()) return false;
    if (gcode.back() != '\n') return false;

    for (char c : gcode)
    {
        if (c < 9 || c > 126)
        {
            ESP_LOGE(TAG, "Carattere non valido: 0x%02X", (uint8_t)c);
            return false;
        }
    }
    return true;
}

bool sendGCodeStream(const std::string& gcode)
{
    return cnc.sendGCodeStream(gcode);
}

bool waitForCompletion(uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    while (cnc.getMachineState() == STATE_RUN)
    {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeout_ms))
            return false;
        vTaskDelay(pdMS_TO_TICKS(50));
        cnc.update();
    }
    return true;
}

std::string queryImmediateState(uint32_t timeout_ms)
{
    return cnc.queryReply("?", timeout_ms);
}

bool verifyGRBLResponse()
{
    return cnc.isReady();
}