// Tracking.cpp – Versione ESP-IDF completa (senza watchdog)
#include "Tracking.h"
#include "global_vars.h"
#include "GcodeMonitor.h"
#include "cnc_manager.h"
#include "GRBLParser.h"
#include "graphics_helpers.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstring>
#include <cmath>

static const char* TAG = "TRACKING";

// Istanza globale (definita qui, dichiarata come extern in Tracking.h)
GcodeTrackerEngine* trackerEngineInstance = nullptr;

// ------------------------------------------------------------
// COSTRUTTORE
// ------------------------------------------------------------
GcodeTrackerEngine::GcodeTrackerEngine(const GcodeBlockRanges &ranges,
                                       const std::string &allGcode,
                                       const std::string &gcodeType,
                                       std::function<void()> requestNextBlock,
                                       int totalBlocks)
    : ranges(ranges),
      allGcode(allGcode),
      gcodeType(gcodeType),
      requestNextBlockCallback(requestNextBlock),
      totalBlocks(totalBlocks)
{
    currentBlockIndex = 0;
    allBlocksSent = false;
    lastBlockSendTime = 0;
    lastLnTime = 0;                     // Inizializza il timer di stallo
    if (gcodeType == "Keyway") {
        skipSetup = true;
        skipEnd = true;
    }
}

// ------------------------------------------------------------
// FUNZIONE PRINCIPALE
// ------------------------------------------------------------
void GcodeTrackerEngine::run() {
    gcodeTrackingActive = true;
    drawMonitorSidebarGcode();

    ESP_LOGI(TAG, "Tracking modulare avviato");

    if (!skipSetup) {
        if (!trackSetupBlock())
            return;
    }

    trackMotionBlock();

    if (!skipEnd)
        trackEndBlock();

    gcodeTrackingActive = false;
    drawMonitorSidebarGcode();

    ESP_LOGI(TAG, "Tracking completato");
}

// ------------------------------------------------------------
// TRACK SETUP BLOCK
// ------------------------------------------------------------
bool GcodeTrackerEngine::trackSetupBlock() {
    ESP_LOGI(TAG, "Tracking setup...");

    unsigned long start = esp_timer_get_time() / 1000;
    while ((esp_timer_get_time() / 1000 - start) < 30000) {
        updateDROBoxesIfChanged();
        if (cnc.getSpindleSpeed() > 0)
            break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (cnc.getSpindleSpeed() == 0) {
        cnc.resetController();
        ESP_LOGE(TAG, "Invio reset controller a causa di mandrino non attivo");
        ESP_LOGE(TAG, "Tracking interrotto");
        drawMonitorStopBox(
            "Operation aborted: spindle not detected. This can occasionally happen. "
            "To repeat the execution, press 'C'.");
        gcodeTrackingActive = false;
        drawMonitorSidebarGcode();
        return false;
    }

    // Marca righe setup come eseguite
    for (int i = ranges.setupStart; i <= ranges.setupEnd; i++) {
        if (i < gcodeMonitorLineCount) {
            const std::string& line = gcodeMonitorLines[i];
            if (!line.empty() && (line[0] == 'G' || line[0] == 'M')) {
                gcodeMonitorLineStates[i] = LINE_EXECUTED;
            }
        }
    }
    drawGcodeMonitor(defaultConfig);
    return true;
}

// ------------------------------------------------------------
// DISPATCHER MOTION
// ------------------------------------------------------------
void GcodeTrackerEngine::trackMotionBlock() {
    ESP_LOGI(TAG, "Tracking motion type: %s", gcodeType.c_str());
    if (gcodeType == "Keyway") {
        trackMotionKeyway();
    } else {
        trackMotionUniversal();
    }
}

// ------------------------------------------------------------
// UNIVERSAL TRACKING (basato su Ln + stato macchina)
// ------------------------------------------------------------
void GcodeTrackerEngine::trackMotionUniversal() {
    ESP_LOGI(TAG, "TRACKING UNIVERSALE BASATO SU Ln=Nxx (START)");

    const int TOTAL_MOTION_LINES = (ranges.motionEnd - ranges.motionStart + 1);

    // 0️⃣ Assicurati che GRBL non sia in RUN
    unsigned long t0 = esp_timer_get_time() / 1000;
    while (cnc.getMachineState() == STATE_RUN && (esp_timer_get_time() / 1000 - t0) < 2000) {
        updateDROBoxesIfChanged();
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // 1️⃣ Marca le righe di setup non significative come eseguite
    for (int idx : nonSignificantLines) {
        if (idx >= 0 && idx < gcodeMonitorLineCount) {
            gcodeMonitorLineStates[idx] = LINE_EXECUTED;
            markLineExecuted(idx);
        }
    }

    // 2️⃣ Aspetta che GRBL vada in RUN
    t0 = esp_timer_get_time() / 1000;
    while (cnc.getMachineState() != STATE_RUN && (esp_timer_get_time() / 1000 - t0) < 5000) {
        updateDROBoxesIfChanged();
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // 3️⃣ Calcola la riga motion attiva in base al line number GRBL
    int firstMotionN = ranges.motionStart + 1; // la prima riga motion ha N = motionStart+1
    int ln = grblCurrentLineNumber;
    int activeMotionIndex = (ln > 0) ? (ln - firstMotionN) : 0;
    activeMotionIndex = std::max(0, std::min(activeMotionIndex, TOTAL_MOTION_LINES - 1));
    int activeAbsIdx = ranges.motionStart + activeMotionIndex;

    // Marca tutte le righe precedenti come eseguite
    for (int i = ranges.motionStart; i < activeAbsIdx; i++) {
        if (i < gcodeMonitorLineCount)
            gcodeMonitorLineStates[i] = LINE_EXECUTED;
    }
    // Attiva la riga corrente
    if (activeAbsIdx >= ranges.motionStart && activeAbsIdx <= ranges.motionEnd) {
        if (activeAbsIdx > ranges.motionStart)
            advanceLine(activeAbsIdx - 1, activeAbsIdx);
        else
            advanceLine(activeAbsIdx, activeAbsIdx);
    }

    int absIdx = activeAbsIdx;
    int lastLn = ln;
    lastLnTime = esp_timer_get_time() / 1000;   // Usa la variabile membro
    unsigned long safetyStart = 0;

    // 4️⃣ Loop di tracking
    while (true) {
        updateDROBoxesIfChanged();

        // Leggi il comando corrente (parsing dal report di stato)
        std::string currentCmd = GRBLParser::parseMotionCommand(cnc.getFullStatus());

        // Se è G76, resetta il timer di stallo Ln (per filettatura lunga)
        if (currentCmd == "G76") {
            lastLnTime = esp_timer_get_time() / 1000;
        }

        // Legge nuovo line number da GRBL
        ln = grblCurrentLineNumber;
        if (ln > 0 && ln != lastLn) {
            lastLnTime = esp_timer_get_time() / 1000;
            int newMotionIndex = ln - firstMotionN;
            newMotionIndex = std::max(0, std::min(newMotionIndex, TOTAL_MOTION_LINES - 1));
            int newAbsIdx = ranges.motionStart + newMotionIndex;

            if (newAbsIdx > absIdx) {
                int delta = newAbsIdx - absIdx;
                ESP_LOGI(TAG, ">[%d] -> [%d] (Ln=%d, delta=%d)", absIdx+1, newAbsIdx+1, ln, delta);

                if (delta == 1) {
                    advanceLine(absIdx, newAbsIdx);
                } else {
                    // Salto di più righe – marca tutte le intermedie
                    for (int i = absIdx; i < newAbsIdx; i++) {
                        if (i < gcodeMonitorLineCount) {
                            gcodeMonitorLineStates[i] = LINE_EXECUTED;
                            markLineExecuted(i);
                        }
                    }
                    advanceLine(newAbsIdx - 1, newAbsIdx);
                }
                absIdx = newAbsIdx;
            }
            lastLn = ln;
        }

        // Controlla lo stato del planner e della macchina
        int plannerFree = 0, serialFree = 0;
        cnc.getBufferState(plannerFree, serialFree);
        MachineState state = cnc.getMachineState();

        // Pacing per blocchi multipli (se necessario)
        if (plannerFree == PLANNER_BUFFER_SIZE && serialFree == 1023) {
            ESP_LOGI(TAG, "Chiamo handleBlockPacing");
            handleBlockPacing(absIdx, gcodeMonitorLines[absIdx]);
        }

        // Condizione di uscita: IDLE E (ultima riga OPPURE (stallo Ln E nessun altro blocco))
        bool machineIdle = (state == STATE_IDLE);
        bool lastMotionReached = (absIdx >= ranges.motionEnd);
        bool lnStalled = ((esp_timer_get_time() / 1000 - lastLnTime) > 3000) && (absIdx > ranges.motionStart);
        bool noMoreBlocks = allBlocksSent;   // true solo dopo l'ultimo blocco

        if (machineIdle && (lastMotionReached || (lnStalled && noMoreBlocks))) {
            ESP_LOGI(TAG, "Fine motion: macchina IDLE, uscita");
            for (int i = ranges.motionStart; i <= ranges.motionEnd; i++) {
                if (i < gcodeMonitorLineCount)
                    gcodeMonitorLineStates[i] = LINE_EXECUTED;
            }
            drawGcodeMonitor(defaultConfig);
            break;
        }

        // Uscita di sicurezza (macchina non in RUN per >10s)
        if (state != STATE_RUN) {
            if (safetyStart == 0) safetyStart = esp_timer_get_time() / 1000;
            if ((esp_timer_get_time() / 1000 - safetyStart) > 10000) {
                ESP_LOGE(TAG, "USCITA DI SICUREZZA: macchina non in RUN per 10s");
                for (int i = ranges.motionStart; i <= ranges.motionEnd; i++) {
                    if (i < gcodeMonitorLineCount)
                        gcodeMonitorLineStates[i] = LINE_EXECUTED;
                }
                drawGcodeMonitor(defaultConfig);
                break;
            }
        } else {
            safetyStart = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // 5️⃣ Attesa finale che la macchina sia stabilmente IDLE
    unsigned long idleStart = 0;
    t0 = esp_timer_get_time() / 1000;
    while ((esp_timer_get_time() / 1000 - t0) < 5000) {
        if (cnc.getMachineState() == STATE_IDLE) {
            if (idleStart == 0) idleStart = esp_timer_get_time() / 1000;
            if ((esp_timer_get_time() / 1000 - idleStart) > 50) break;
        } else {
            idleStart = 0;
        }
        updateDROBoxesIfChanged();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "TRACKING UNIVERSALE MOTION COMPLETATO");
}

// ------------------------------------------------------------
// TRACK END BLOCK
// ------------------------------------------------------------
bool GcodeTrackerEngine::trackEndBlock() {
    ESP_LOGI(TAG, "Tracking end block...");

    int rStart = ranges.endStart;
    int rM9 = -1, rM5 = -1, rM30 = -1;

    if (rStart < gcodeMonitorLineCount && gcodeMonitorLines[rStart].find("M9") == 0) {
        rM9 = rStart;
        rM5 = rStart + 1;
        rM30 = rStart + 2;
    } else {
        rM5 = rStart;
        rM30 = rStart + 1;
    }

    if (rM9 != -1 && rM9 < gcodeMonitorLineCount) {
        gcodeMonitorLineStates[rM9] = LINE_ACTIVE;
        gcodeMonitorSelectedIndex = rM9;
        drawGcodeMonitor(defaultConfig);
        vTaskDelay(pdMS_TO_TICKS(150));
        gcodeMonitorLineStates[rM9] = LINE_EXECUTED;
        markLineExecuted(rM9);
    }

    // M5
    if (rM5 < gcodeMonitorLineCount) {
        gcodeMonitorLineStates[rM5] = LINE_ACTIVE;
        gcodeMonitorSelectedIndex = rM5;
        drawGcodeMonitor(defaultConfig);
        ESP_LOGI(TAG, "Attesa mandrino a 0 RPM...");
        while (cnc.getSpindleSpeed() > 0) {
            updateDROBoxesIfChanged();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        gcodeMonitorLineStates[rM5] = LINE_EXECUTED;
        markLineExecuted(rM5);
    }

    // M30
    if (rM30 < gcodeMonitorLineCount) {
        gcodeMonitorLineStates[rM30] = LINE_ACTIVE;
        gcodeMonitorSelectedIndex = rM30;
        drawGcodeMonitor(defaultConfig);
        vTaskDelay(pdMS_TO_TICKS(250));
        gcodeMonitorLineStates[rM30] = LINE_EXECUTED;
        markLineExecuted(rM30);
    }

    gcodeMonitorSelectedIndex = -1;
    drawGcodeMonitor(defaultConfig);
    ESP_LOGI(TAG, "END BLOCK COMPLETATO");
    return true;
}

// ------------------------------------------------------------
// KEYWAY (M800 CYCLE)
// ------------------------------------------------------------
void GcodeTrackerEngine::trackMotionKeyway() {
    const unsigned long GLOBAL_TIMEOUT = 300000; // 5 minuti

    gcodeTrackingActive = true;

    // Marca prime 4 righe come eseguite
    for (int i = 0; i < 4 && i < gcodeMonitorLineCount; i++) {
        gcodeMonitorLineStates[i] = LINE_EXECUTED;
        markLineExecuted(i);
    }

    // Attiva riga 4 (indice 4)
    int currentActiveIdx = 4;
    if (currentActiveIdx < gcodeMonitorLineCount) {
        gcodeMonitorLineStates[currentActiveIdx] = LINE_ACTIVE;
        gcodeMonitorSelectedIndex = currentActiveIdx;
        if (isScrollingEnabled())
            centerMonitorOn(currentActiveIdx);
    }
    drawGcodeMonitor(defaultConfig);

    // Trova M30
    int lineM30 = -1;
    for (int i = 0; i < gcodeMonitorLineCount; i++) {
        if (gcodeMonitorLines[i].find("M30") == 0) {
            lineM30 = i;
            break;
        }
    }

    unsigned long startTime = esp_timer_get_time() / 1000;
    int lastLn = -1;

    while (true) {
        if ((esp_timer_get_time() / 1000 - startTime) > GLOBAL_TIMEOUT) {
            ESP_LOGE(TAG, "TIMEOUT");
            break;
        }

        updateDROBoxesIfChanged();

        int currentLn = grblCurrentLineNumber;
        if (currentLn > 0 && currentLn != lastLn) {
            int completedIdx = currentLn - 1;
            ESP_LOGI(TAG, "Ln: %d → riga %d completata", currentLn, completedIdx+1);

            for (int i = currentActiveIdx; i <= completedIdx && i < gcodeMonitorLineCount; i++) {
                gcodeMonitorLineStates[i] = LINE_EXECUTED;
                markLineExecuted(i);
            }

            currentActiveIdx = completedIdx + 1;

            if (currentActiveIdx <= lineM30 && currentActiveIdx < gcodeMonitorLineCount) {
                gcodeMonitorLineStates[currentActiveIdx] = LINE_ACTIVE;
                gcodeMonitorSelectedIndex = currentActiveIdx;
                if (isScrollingEnabled())
                    centerMonitorOn(currentActiveIdx);
                drawGcodeMonitor(defaultConfig);
            }

            lastLn = currentLn;

            if (completedIdx >= lineM30) {
                ESP_LOGI(TAG, "M30 completato");
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    gcodeMonitorSelectedIndex = -1;
    drawGcodeMonitor(defaultConfig);
    ESP_LOGI(TAG, "Tracking Keyway completato");
}

// ------------------------------------------------------------
// HELPER: handleBlockPacing
// ------------------------------------------------------------
bool GcodeTrackerEngine::handleBlockPacing(int absIdx, const std::string &lineCurr) {
    ESP_LOGI(TAG, "handleBlockPacing() START → absIdx=%d", absIdx);

    if (currentBlockIndex + 1 >= totalBlocks) {
        ESP_LOGI(TAG, "Nessun blocco da inviare (current=%d total=%d)", currentBlockIndex+1, totalBlocks);
        allBlocksSent = true;
        return false;
    }

    if (allBlocksSent) {
        ESP_LOGI(TAG, "allBlocksSent=TRUE → esco");
        return false;
    }

    unsigned long now = esp_timer_get_time() / 1000;
    if (now - lastBlockSendTime < 2000) {
        ESP_LOGI(TAG, "Troppo presto per pacing (%lu ms < 2000)", now - lastBlockSendTime);
        return false;
    }

    int plannerFree = -1, serialFree = -1;
    cnc.getBufferState(plannerFree, serialFree);
    if (plannerFree < PLANNER_BUFFER_SIZE) {
        ESP_LOGI(TAG, "Planner NON vuoto → niente pacing");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    MachineState s = cnc.getMachineState();
    if (s != STATE_IDLE) {
        ESP_LOGI(TAG, "Macchina NON in IDLE → pacing annullato");
        return false;
    }

    if (absIdx > ranges.motionStart) {
        gcodeMonitorLineStates[absIdx - 1] = LINE_EXECUTED;
        markLineExecuted(absIdx);
    }

    ESP_LOGI(TAG, "INVIO BLOCCO #%d...", currentBlockIndex);
    if (requestNextBlockCallback)
        requestNextBlockCallback();

    vTaskDelay(pdMS_TO_TICKS(1000));

    lastBlockSendTime = esp_timer_get_time() / 1000;
    currentBlockIndex++;
    // allBlocksSent diventa true solo dopo l'invio dell'ultimo blocco
    allBlocksSent = (currentBlockIndex >= totalBlocks);

    // 🔁 Reset del timer di stallo per evitare falso stall durante l'esecuzione del nuovo blocco
    lastLnTime = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "handleBlockPacing() COMPLETATO");
    return true;
}

// ------------------------------------------------------------
// HELPER: preAnalyzeFirstMotionLines
// ------------------------------------------------------------
int GcodeTrackerEngine::preAnalyzeFirstMotionLines() {
    nonSignificantLines.clear();

    int first = ranges.motionStart;
    int second = first + 1;

    float mposX = DRO_X;
    float mposZ = DRO_Z;

    float t1x = extractCoordinate(gcodeMonitorLines[first], 'X');
    float t1z = extractCoordinate(gcodeMonitorLines[first], 'Z');
    float t2x = extractCoordinate(gcodeMonitorLines[second], 'X');
    float t2z = extractCoordinate(gcodeMonitorLines[second], 'Z');

    bool r1moves = false;
    if (t1x != -999) {
        float diff = std::abs(t1x - mposX);
        if (diff > 0.01f) r1moves = true;
    }
    if (!r1moves && t1z != -999) {
        float diff = std::abs(t1z - mposZ);
        if (diff > 0.01f) r1moves = true;
    }

    if (!r1moves) {
        nonSignificantLines.push_back(first);
        bool r2moves = false;
        if (t2x != -999) {
            float diff = std::abs(t2x - mposX);
            if (diff > 0.01f) r2moves = true;
        }
        if (!r2moves && t2z != -999) {
            float diff = std::abs(t2z - mposZ);
            if (diff > 0.01f) r2moves = true;
        }
        if (!r2moves) {
            nonSignificantLines.push_back(second);
            return second + 1;
        }
        return second;
    }
    return first;
}

// ------------------------------------------------------------
// HELPER: advanceLine
// ------------------------------------------------------------
void GcodeTrackerEngine::advanceLine(int absIdx, int nextAbsIdx) {
    if (nextAbsIdx > ranges.motionStart) {
        for (int i = ranges.motionStart; i < nextAbsIdx; i++) {
            if (i < gcodeMonitorLineCount)
                gcodeMonitorLineStates[i] = LINE_EXECUTED;
        }
    }
    if (absIdx < gcodeMonitorLineCount) {
        gcodeMonitorLineStates[absIdx] = LINE_EXECUTED;
        markLineExecuted(absIdx);
    }
    if (nextAbsIdx <= ranges.motionEnd && nextAbsIdx < gcodeMonitorLineCount) {
        gcodeMonitorLineStates[nextAbsIdx] = LINE_ACTIVE;
        gcodeMonitorSelectedIndex = nextAbsIdx;
        if (isScrollingEnabled())
            centerMonitorOn(nextAbsIdx);
        drawGcodeMonitor(defaultConfig);
    }
}