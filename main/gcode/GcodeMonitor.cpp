// GcodeMonitor.cpp – Versione ESP-IDF con log dettagliato delle righe inviate al controller
#include "GcodeMonitor.h"
#include "main_scene.h"
#include "global_vars.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "keypad.h"
#include "cnc_manager.h"
#include "current_params.h"
#include "GenerateGcode.h"
#include "Tracking.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

static const char *TAG = "GCODE_MONITOR";

GcodeMonitorConfig defaultConfig = {
    .x = 25, .y = 40, .w = 280, .h = 208, .lineHeight = 15, .maxLinesVisible = 14, .charXOffset = 10, .bgColor = TFT_BLACK, .textColor = TFT_CYAN, .commentColor = TFT_GREEN, .selectedColor = TFT_YELLOW, .font = &fonts::Font2};

std::string gcodeMonitorLines[MAX_GCODE_MONITOR_LINES];
uint8_t gcodeMonitorLineStates[MAX_GCODE_MONITOR_LINES];
bool gcodeMonitorIsComment[MAX_GCODE_MONITOR_LINES];
int gcodeMonitorLineCount = 0;
int gcodeMonitorScrollIndex = 0;
int gcodeMonitorSelectedIndex = -1;

static int maxChars = 33;
static int lineNumber = 1;

static int GCODE_SPLIT_THRESHOLD = 60;
static int GCODE_SPLIT_SEARCH_START = GCODE_SPLIT_THRESHOLD - 10;
static int GCODE_SPLIT_SEARCH_END = GCODE_SPLIT_THRESHOLD;

// Forward declarations
static void resetGcodeMonitor();
static int countGcodeLines(const std::string &gcode);
static std::vector<std::string> splitGcodeAtSafePoint(const std::string &gcode);
static int findSafeSplitPoint(const std::vector<std::string> &lines, int startLine, int endLine);
static void parseGcodeBlock(const std::string &block);

// -------------------------------------------------------------------
// Funzioni principali
// -------------------------------------------------------------------
void GcodeMonitorScene(const GcodeBlock &block)
{
    resetTextStyle();
    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    tft_set_text_color(TFT_YELLOW);
    tft_set_text_size(2);
    tft_set_text_datum(MC_DATUM);
    drawCenteredTextAt("Gcode PREVIEW", 240, 15, 2, TFT_YELLOW);
    tft_fill_rect(20, 30, 440, 1, TFT_WHITE);
    tft_fill_rect(20, 258, 440, 1, TFT_WHITE);

    drawMonitorSidebarPreview();
    GcodeMonitorPreview(block.setup, block.motion, block.end, block.type);
    drawPreviewForType(block.type);
    drawGcodeTypeBox(block.type);

    while (true)
    {
        static bool wasTracking = false;
        if (gcodeTrackingActive)
        {
            wasTracking = true;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (wasTracking)
        {
            ESP_LOGI(TAG, "Pulizia tastiera dopo tracking...");
            while (readKeypad() != 0)
                vTaskDelay(pdMS_TO_TICKS(10));
            wasTracking = false;
        }

        char key = readKeypad();
        if (key == 'A' && gcodeMonitorScrollIndex > 0)
        {
            gcodeMonitorScrollIndex--;
            drawGcodeMonitor(defaultConfig);
        }
        if (key == 'B' && gcodeMonitorScrollIndex + defaultConfig.maxLinesVisible < gcodeMonitorLineCount)
        {
            gcodeMonitorScrollIndex++;
            drawGcodeMonitor(defaultConfig);
        }
        if (key == 'C')
        {
            resetTextStyle();
            tft_fill_rect(0, 0, 480, 29, TFT_NAVY);
            tft_set_text_color(TFT_YELLOW);
            tft_set_text_size(2);
            tft_set_text_datum(MC_DATUM);
            drawCenteredTextAt("Gcode MONITOR", 240, 15, 2, TFT_YELLOW);
            tft_fill_rect(300, 124, 160, 40, TFT_NAVY);
            tft_fill_rect(300, 31, 180, 226, TFT_NAVY);
            drawDROBoxes();
            tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
            drawMonitorSidebarGcode();
            clearMonitorArea(defaultConfig);
            prepareGcodeToTrack(block.type);
            continue;
        }
        if (key == 'D')
        {
            resetTextStyle();
            // DisplayManager();
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void clearMonitorArea(const GcodeMonitorConfig &cfg)
{
    tft_fill_rect(cfg.x, cfg.y, cfg.w, cfg.h, cfg.bgColor);
}

void drawGcodeLine(const std::string &line, int index, const GcodeMonitorConfig &cfg, int gcodeLineNumber)
{
    int y = cfg.y + (index - gcodeMonitorScrollIndex) * cfg.lineHeight;
    if (y < cfg.y || y >= cfg.y + cfg.h)
        return;

    tft_set_font(cfg.font);
    tft_set_text_size(1);
    tft_set_text_datum(TL_DATUM);

    uint16_t color;
    uint8_t state = gcodeMonitorLineStates[index];
    switch (state)
    {
    case LINE_EXECUTED:
        color = TFT_GREEN;
        break;
    case LINE_ACTIVE:
        color = TFT_YELLOW;
        break;
    case LINE_ERROR:
        color = TFT_RED;
        break;
    default:
        color = cfg.textColor;
        break;
    }
    if (gcodeMonitorIsComment[index])
        color = cfg.commentColor;
    if (index == gcodeMonitorSelectedIndex)
        color = cfg.selectedColor;

    tft_set_text_color_bg(color, cfg.bgColor);
    tft_set_cursor(cfg.x + cfg.charXOffset, y);

    if (gcodeMonitorIsComment[index])
    {
        tft_print(line.c_str());
    }
    else
    {
        std::string displayLine = line;
        if (displayLine.size() > (size_t)maxChars)
            displayLine = displayLine.substr(0, maxChars - 3) + " ...";
        std::string numbered = "N" + std::to_string(gcodeLineNumber) + " " + displayLine;
        tft_print(numbered.c_str());
    }
}

void drawGcodeMonitor(const GcodeMonitorConfig &config)
{
    tft_fill_rect(config.x, config.y, config.w, config.h, config.bgColor);
    int gcodeLineNumber = 1;
    for (int i = 0; i < gcodeMonitorLineCount; i++)
    {
        drawGcodeLine(gcodeMonitorLines[i], i, config, gcodeLineNumber);
        if (!gcodeMonitorIsComment[i])
            gcodeLineNumber++;
    }
}

bool isScrollingEnabled()
{
    return gcodeMonitorLineCount > defaultConfig.maxLinesVisible;
}

void parseGcodeBlock(const std::string &block)
{
    size_t start = 0;
    while (start < block.size())
    {
        size_t end = block.find('\n', start);
        if (end == std::string::npos)
            end = block.size();
        std::string line = block.substr(start, end - start);
        if (!line.empty() && gcodeMonitorLineCount < MAX_GCODE_MONITOR_LINES)
        {
            gcodeMonitorLines[gcodeMonitorLineCount] = line;
            gcodeMonitorIsComment[gcodeMonitorLineCount] = (line.size() > 0 && line[0] == '(');
            gcodeMonitorLineCount++;
        }
        start = end + 1;
    }
}

void GcodeMonitorPreview(const std::string &setup, const std::string &motion, const std::string &end, const std::string &type)
{
    resetGcodeMonitor();
    gcodeMonitorLineCount = 0;
    gcodeMonitorScrollIndex = 0;
    gcodeMonitorSelectedIndex = -1;
    parseGcodeBlock(setup);
    parseGcodeBlock(motion);
    parseGcodeBlock(end);
    drawGcodeMonitor(defaultConfig);
    printFullGcodeToSerial();
}

void printFullGcodeToSerial()
{
    ESP_LOGI(TAG, "G-code completo caricato nel monitor (con commenti):");
    int gcodeLineNumber = 1;
    for (int i = 0; i < gcodeMonitorLineCount; i++)
    {
        if (gcodeMonitorIsComment[i])
            ESP_LOGI(TAG, "%s", gcodeMonitorLines[i].c_str());
        else
            ESP_LOGI(TAG, "🧾 RIGA %d: %s", gcodeLineNumber++, gcodeMonitorLines[i].c_str());
    }
}

void prepareGcodeToTrack(const std::string &gcodeType)
{
    lineNumber = 1;
    resetGcodeMonitor();
    ESP_LOGI(TAG, "Invio G-code per tipo: %s", gcodeType.c_str());

    // Ottieni i parametri correnti
    TurningParams tp = getCurrentTurningParams();
    FacingParams fp = getCurrentFacingParams();
    HemisphericalParams hp = getCurrentHemisphericalParams();
    ChamferBilletParams cp = getCurrentChamferBilletParams();
    PartingParams pp = getCurrentPartingParams();
    ThreadingParams thp = getCurrentThreadingParams();
    KeywayParams kp = getCurrentKeywayParams();

    std::string setup, motion;
    std::string end = generateEndBlock(false);

    if (gcodeType == "InternalCylindrical" || gcodeType == "ExternalCylindrical")
    {
        SpindleParams sp;
        sp.useCSS = tp.useCSS;
        sp.spindleCW = tp.spindleCW;
        sp.spindleRPM = tp.spindleRPM;
        sp.spindleMmin = tp.spindleMmin;
        sp.radius = tp.actualRadius;
        setup = generateSetupBlock(sp, false);
        motion = generateCylindricalTurningGcode(tp, false);
    }
    else if (gcodeType == "ExternalTaper")
    {
        SpindleParams sp;
        sp.useCSS = tp.useCSS;
        sp.spindleCW = tp.spindleCW;
        sp.spindleRPM = tp.spindleRPM;
        sp.spindleMmin = tp.spindleMmin;
        sp.radius = tp.actualRadius;
        setup = generateSetupBlock(sp, false);
        motion = generateExternalTaperGcode(tp, false);
    }
    else if (gcodeType == "InternalTaper")
    {
        SpindleParams sp;
        sp.useCSS = tp.useCSS;
        sp.spindleCW = tp.spindleCW;
        sp.spindleRPM = tp.spindleRPM;
        sp.spindleMmin = tp.spindleMmin;
        sp.radius = tp.actualRadius;
        setup = generateSetupBlock(sp, false);
        motion = generateInternalTaperGcode(tp, false);
    }
    else if (gcodeType == "Facing")
    {
        SpindleParams sp;
        sp.useCSS = fp.useCSS;
        sp.spindleCW = fp.spindleCW;
        sp.spindleRPM = fp.spindleRPM;
        sp.spindleMmin = fp.spindleMmin;
        sp.radius = fp.startRadius;
        setup = generateSetupBlock(sp, false);
        motion = generateFacingGcode(fp, false);
    }
    else if (gcodeType == "Parting")
    {
        SpindleParams sp;
        sp.useCSS = pp.useCSS;
        sp.spindleCW = pp.spindleCW;
        sp.spindleRPM = pp.spindleRPM;
        sp.spindleMmin = pp.spindleMmin;
        sp.radius = pp.startX;
        setup = generateSetupBlock(sp, false);
        motion = generatePartingGcode(pp, false);
    }
    else if (gcodeType == "ConcaveHemisphere" || gcodeType == "ConvexHemisphere")
    {
        SpindleParams sp;
        sp.useCSS = hp.useCSS;
        sp.spindleCW = hp.spindleCW;
        sp.spindleRPM = hp.spindleRPM;
        sp.spindleMmin = hp.spindleMmin;
        sp.radius = hp.startX;
        setup = generateSetupBlock(sp, false);
        if (gcodeType == "ConcaveHemisphere")
            motion = generateConcaveHemisphericalGcode(hp, false);
        else
            motion = generateConvexHemisphericalGcode(hp, false);
    }
    else if (gcodeType == "Chamfer" || gcodeType == "Billet")
    {
        SpindleParams sp;
        sp.useCSS = cp.useCSS;
        sp.spindleCW = cp.spindleCW;
        sp.spindleRPM = cp.spindleRPM;
        sp.spindleMmin = cp.spindleMmin;
        sp.radius = cp.startX;
        setup = generateSetupBlock(sp, false);
        motion = generateChamferBilletGcode(cp, false);
    }
    else if (gcodeType == "ExternalRightThread" || gcodeType == "ExternalLeftThread" ||
             gcodeType == "InternalRightThread" || gcodeType == "InternalLeftThread")
    {
        SpindleParams sp;
        sp.useCSS = false;
        sp.spindleCW = thp.spindleCW;
        sp.spindleRPM = thp.spindleRPM;
        sp.spindleMmin = 0.0f;
        sp.radius = thp.actualRadius;
        setup = generateSetupBlock(sp, false);
        motion = generateThreadingGcode(thp, false);
    }
    else if (gcodeType == "Keyway")
    {
        setup = "";
        motion = generateKeywayGcode(kp, false);
        end = "";
    }
    else
    {
        ESP_LOGE(TAG, "Tipo G-code non riconosciuto: %s", gcodeType.c_str());
        return;
    }

    parseGcodeBlock(setup);
    parseGcodeBlock(motion);
    parseGcodeBlock(end);
    filterGcodeMonitorLines();
    drawGcodeMonitor(defaultConfig);

    std::string allGcode = setup + motion + end;
    GcodeBlockRanges ranges = logGcodeBlockRanges(setup, motion, end);

    if (!validateRawGcode(allGcode))
    {
        ESP_LOGE(TAG, "RAW FAIL (prima della compressione) → G-code corrotto");
        return;
    }
    allGcode = compressGcode(allGcode);

    // 🔹 LOG: G-code compresso pronto per essere inviato (senza commenti, con Nx)
    ESP_LOGI(TAG, "===== G-CODE COMPRESSO (inviato al controller) =====");
    ESP_LOGI(TAG, "%s", allGcode.c_str());
    ESP_LOGI(TAG, "====================================================");

    if (!validateRawGcode(allGcode))
    {
        ESP_LOGE(TAG, "RAW FAIL (dopo compressione) → G-code corrotto");
        return;
    }
    sendGcodeExecution(ranges, allGcode, gcodeType);
}

std::string compressGcode(const std::string &gcode)
{
    std::string result;
    int lineNumberLocal = 1;
    size_t start = 0;
    while (start < gcode.size())
    {
        size_t end = gcode.find('\n', start);
        if (end == std::string::npos)
            end = gcode.size();
        std::string line = gcode.substr(start, end - start);
        std::string compressed;
        for (char c : line)
            if (c != ' ')
                compressed += c;
        if (!compressed.empty())
            result += "N" + std::to_string(lineNumberLocal++) + compressed + "\n";
        start = end + 1;
    }
    return result;
}

int countGcodeLines(const std::string &gcode)
{
    int count = 0;
    size_t start = 0;
    while (start < gcode.size())
    {
        size_t end = gcode.find('\n', start);
        if (end == std::string::npos)
            break;
        std::string line = gcode.substr(start, end - start);
        bool empty = true;
        for (char c : line)
            if (!std::isspace(c))
            {
                empty = false;
                break;
            }
        if (!empty)
            count++;
        start = end + 1;
    }
    return count;
}

int findSafeSplitPoint(const std::vector<std::string> &lines, int startLine, int endLine)
{
    bool foundFirstG0 = false;
    int firstG0Index = -1;
    for (int i = startLine; i <= endLine && i < (int)lines.size(); i++)
    {
        const std::string &line = lines[i];
        if (line.find("G0") != std::string::npos)
        {
            if (!foundFirstG0)
            {
                foundFirstG0 = true;
                firstG0Index = i;
            }
            else
            {
                bool firstHasX = lines[firstG0Index].find('X') != std::string::npos;
                bool firstHasZ = lines[firstG0Index].find('Z') != std::string::npos;
                bool currentHasX = line.find('X') != std::string::npos;
                bool currentHasZ = line.find('Z') != std::string::npos;
                if ((firstHasX && currentHasZ) || (firstHasZ && currentHasX))
                    return i;
            }
        }
    }
    for (int i = startLine; i <= endLine && i < (int)lines.size(); i++)
    {
        const std::string &line = lines[i];
        if (line.find("G0") != std::string::npos && (line.find('X') != std::string::npos || line.find('Z') != std::string::npos))
            return i;
    }
    return -1;
}

std::vector<std::string> splitGcodeAtSafePoint(const std::string &gcode)
{
    std::vector<std::string> blocks;
    std::vector<std::string> allLines;
    size_t start = 0;
    while (start < gcode.size())
    {
        size_t end = gcode.find('\n', start);
        if (end == std::string::npos)
            end = gcode.size();
        std::string line = gcode.substr(start, end - start);
        bool empty = true;
        for (char c : line)
            if (!std::isspace(c))
            {
                empty = false;
                break;
            }
        if (!empty && line[0] != '(' && line[0] != ';')
            allLines.push_back(line);
        start = end + 1;
    }
    int totalLines = allLines.size();
    if (totalLines <= GCODE_SPLIT_THRESHOLD)
    {
        blocks.push_back(gcode);
        return blocks;
    }
    int currentStart = 0;
    while (currentStart < totalLines)
    {
        int remainingLines = totalLines - currentStart;
        if (remainingLines <= GCODE_SPLIT_THRESHOLD)
        {
            std::string finalBlock;
            for (int i = currentStart; i < totalLines; i++)
                finalBlock += allLines[i] + "\n";
            blocks.push_back(finalBlock);
            break;
        }
        int searchStart = currentStart + GCODE_SPLIT_SEARCH_START;
        int searchEnd = currentStart + GCODE_SPLIT_SEARCH_END;
        if (searchEnd >= totalLines)
            searchEnd = totalLines - 1;
        int splitPoint = findSafeSplitPoint(allLines, searchStart, searchEnd);
        if (splitPoint == -1)
            splitPoint = currentStart + 55;
        std::string currentBlock;
        for (int i = currentStart; i <= splitPoint; i++)
            currentBlock += allLines[i] + "\n";
        blocks.push_back(currentBlock);
        currentStart = splitPoint + 1;
    }
    return blocks;
}

void sendGcodeExecution(const GcodeBlockRanges &ranges, const std::string &allGcode, const std::string &gcodeType)
{
    if (!unlockIfError())
    {
        ESP_LOGE(TAG, "Invio G-code annullato: macchina non sbloccata");
        return;
    }
    if (!validateRawGcode(allGcode))
    {
        ESP_LOGE(TAG, "RAW FAIL in sendGcodeExecution");
        return;
    }

    // Suddividi il G‑code compresso in blocchi (soglia 60 righe)
    std::vector<std::string> gcodeBlocks;
    int totalLines = countGcodeLines(allGcode);
    if (totalLines > GCODE_SPLIT_THRESHOLD)
    {
        gcodeBlocks = splitGcodeAtSafePoint(allGcode);
        ESP_LOGI(TAG, "G-code suddiviso in %d blocchi", (int)gcodeBlocks.size());
    }
    else
    {
        gcodeBlocks.push_back(allGcode);
        ESP_LOGI(TAG, "G-code in unico blocco (%d righe)", totalLines);
    }

    int totalBlocks = gcodeBlocks.size();
    int setupLines = ranges.setupEnd - ranges.setupStart + 1;
    int endLines = ranges.endEnd - ranges.endStart + 1;

    int *currentBlockIndex = new int(0);

    // Callback che verrà chiamata da handleBlockPacing quando serve il blocco successivo
    std::function<void()> requestNextBlock = [=]() mutable
    {
        if (*currentBlockIndex >= totalBlocks)
        {
            ESP_LOGI(TAG, "Tutti i blocchi già inviati");
            return;
        }
        std::string *nextBlock = new std::string(gcodeBlocks[*currentBlockIndex]);
        int blockLines = countGcodeLines(*nextBlock);

        // Calcola quante righe di motion contiene questo blocco
        int motionCount = 0;
        if (totalBlocks == 1)
            motionCount = blockLines - setupLines - endLines;
        else if (*currentBlockIndex == 0)
            motionCount = blockLines - setupLines;
        else if (*currentBlockIndex == totalBlocks - 1)
            motionCount = blockLines - endLines;
        else
            motionCount = blockLines;

        ESP_LOGI(TAG, "📤 Invio blocco %d/%d (%d righe totali, %d motion)",
                 *currentBlockIndex + 1, totalBlocks, blockLines, motionCount);

        // ---- LOG DETTAGLIATO DEL BLOCCO INVIATO ----
        ESP_LOGI(TAG, "===== BLOCCO %d INVIATO AL CONTROLLER =====", *currentBlockIndex + 1);
        size_t pos = 0;
        std::string blockStr = *nextBlock;
        while (pos < blockStr.size())
        {
            size_t end = blockStr.find('\n', pos);
            if (end == std::string::npos)
                end = blockStr.size();
            std::string line = blockStr.substr(pos, end - pos);
            if (!line.empty())
            {
                ESP_LOGI(TAG, "  %s", line.c_str());
            }
            pos = end + 1;
        }
        ESP_LOGI(TAG, "===========================================");
        // ---------------------------------------------

        // Invia il blocco riga per riga (con ritardo interno in sendGCodeStream)
        if (!cnc.sendGCodeStream(*nextBlock))
        {
            ESP_LOGE(TAG, "Invio blocco %d fallito", *currentBlockIndex + 1);
        }
        delete nextBlock;
        (*currentBlockIndex)++;
    };

    // Crea un unico tracker che gestirà tutti i blocchi
    trackerEngineInstance = new GcodeTrackerEngine(ranges, allGcode, gcodeType, requestNextBlock, totalBlocks);
    requestNextBlock(); // invia il primo blocco
    controllerError = false;
    trackerEngineInstance->run(); // tracking (chiamerà requestNextBlock quando serve)
    delete trackerEngineInstance;
    delete currentBlockIndex;
    ESP_LOGI(TAG, "sendGcodeExecution completato");
}

bool unlockIfError()
{
    if (!controllerError)
        return true;
    ESP_LOGW(TAG, "Errore GRBL rilevato → invio $X");
    controllerError = false;
    cnc.sendCommand("$X");
    unsigned long t0 = esp_timer_get_time() / 1000;
    while ((esp_timer_get_time() / 1000 - t0) < 3000)
    {
        cnc.update();
        MachineState st = cnc.getMachineState();
        if (st == STATE_IDLE || st == STATE_RUN)
            return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

void forceCompletion(const GcodeBlockRanges &ranges)
{
    for (int i = ranges.motionStart; i <= ranges.motionEnd; i++)
        if (i < gcodeMonitorLineCount)
            gcodeMonitorLineStates[i] = LINE_EXECUTED;
    markEndBlocks(ranges);
}

void markEndBlocks(const GcodeBlockRanges &ranges)
{
    for (int i = ranges.endStart; i <= ranges.endEnd; i++)
        if (i < gcodeMonitorLineCount)
            gcodeMonitorLineStates[i] = LINE_EXECUTED;
}

void filterGcodeMonitorLines()
{
    int filteredCount = 0;
    for (int i = 0; i < gcodeMonitorLineCount; i++)
    {
        if (!gcodeMonitorIsComment[i])
        {
            gcodeMonitorLines[filteredCount] = gcodeMonitorLines[i];
            gcodeMonitorIsComment[filteredCount] = false;
            filteredCount++;
        }
    }
    gcodeMonitorLineCount = filteredCount;
}

GcodeBlockRanges logGcodeBlockRanges(const std::string &setup, const std::string &motion, const std::string &end)
{
    int setupLines = countGcodeLines(setup);
    int motionLines = countGcodeLines(motion);
    int endLines = countGcodeLines(end);
    GcodeBlockRanges ranges;
    ranges.setupStart = 0;
    ranges.setupEnd = setupLines - 1;
    ranges.motionStart = setupLines;
    ranges.motionEnd = setupLines + motionLines - 1;
    ranges.endStart = ranges.motionEnd + 1;
    ranges.endEnd = ranges.endStart + endLines - 1;
    return ranges;
}

void markLineExecuted(int index)
{
    if (index < gcodeMonitorLineCount)
        gcodeMonitorLineStates[index] = LINE_EXECUTED;
}

void resetGcodeMonitor()
{
    for (int i = 0; i < MAX_GCODE_MONITOR_LINES; i++)
    {
        gcodeMonitorLines[i].clear();
        gcodeMonitorLineStates[i] = LINE_PENDING;
        gcodeMonitorIsComment[i] = false;
    }
    gcodeMonitorLineCount = 0;
    gcodeMonitorScrollIndex = 0;
    gcodeMonitorSelectedIndex = -1;
}

float extractCoordinate(const std::string &line, char axis)
{
    size_t idx = line.find(axis);
    if (idx == std::string::npos)
        return -999;
    size_t end = line.find(' ', idx);
    if (end == std::string::npos)
        end = line.size();
    std::string value = line.substr(idx + 1, end - idx - 1);
    return std::stof(value);
}

void centerMonitorOn(int index)
{
    int halfWindow = defaultConfig.maxLinesVisible / 2;
    int scrollTarget = index - halfWindow;
    gcodeMonitorScrollIndex = std::max(0, std::min(scrollTarget, gcodeMonitorLineCount - defaultConfig.maxLinesVisible));
}

void drawPreviewImage(const char *filename)
{
    const int areaX = 310, areaY = 55, areaW = 150, areaH = 180;
    const int imgW = 148, imgH = 166;
    int imgX = areaX + (areaW - imgW) / 2;
    int imgY = areaY + areaH - imgH - 1;
    tft_fill_rect(imgX, imgY, imgW, imgH, TFT_DARKGREY);
    tft_set_text_color(TFT_WHITE);
    tft_set_text_datum(MC_DATUM);
    drawCenteredTextAt("NO IMG", imgX + imgW / 2, imgY + imgH / 2, 1, TFT_WHITE);
}

void drawPreviewForType(const std::string &type)
{
    const int x = 310, y = 55, w = 150, h = 180;
    tft_fill_rect(x, y, w, h, TFT_DARKGREY);
    tft_draw_rect(x, y, w, h, TFT_WHITE);
    const char *img = nullptr;
    if (type == "Parting")
        img = "/Parting.png";
    else if (type == "Facing")
        img = "/Facing.png";
    else if (type == "ExternalCylindrical")
        img = "/Turning.png";
    else if (type == "InternalCylindrical")
        img = "/Boring.png";
    else if (type == "ExternalTaper")
        img = "/TaperExt.png";
    else if (type == "InternalTaper")
        img = "/TaperInt.png";
    else if (type == "ExternalThread")
        img = "/ThreadExt.png";
    else if (type == "InternalThread")
        img = "/ThreadInt.png";
    else if (type == "ConcaveHemisphere")
        img = "/HemiConcave.png";
    else if (type == "ConvexHemisphere")
        img = "/HemiConvex.png";
    else if (type == "Chamfer")
        img = "/Chamfer.png";
    else if (type == "Billet")
        img = "/Billet.png";
    else if (type == "Keyway")
        img = "/Keyway.png";
    if (img)
        drawPreviewImage(img);
    else
    {
        tft_set_text_color_bg(TFT_LIGHTGREY, TFT_DARKGREY);
        drawCenteredTextAt("No image", x + w / 2, y + h / 2, 1, TFT_LIGHTGREY);
    }
}

void drawMonitorSidebarPreview()
{
    resetTextStyle();
    drawButton(30, 270, 100, 30, TFT_DARKGREY, 'A', 2, TFT_WHITE, "UP", 1, TFT_WHITE);
    drawButton(140, 270, 100, 30, TFT_DARKGREY, 'B', 2, TFT_WHITE, "DOWN", 1, TFT_WHITE);
    drawButton(250, 270, 100, 30, TFT_DARKGREEN, 'C', 2, TFT_WHITE, "SEND", 1, TFT_WHITE);
    drawButton(360, 270, 100, 30, TFT_RED, 'D', 2, TFT_WHITE, "EXIT", 1, TFT_WHITE);
}

void drawMonitorSidebarGcode()
{
    resetTextStyle();
    bool active = gcodeTrackingActive;
    const int btnW = 100, btnH = 30, spacing = 40;
    const int totalWidth = btnW * 2 + spacing;
    const int startX = 240 - (totalWidth / 2);
    uint16_t colorC = active ? TFT_LIGHTGREY : TFT_DARKGREEN;
    uint16_t colorD = active ? TFT_LIGHTGREY : TFT_RED;
    drawButton(startX, 270, btnW, btnH, colorC, 'C', 2, TFT_WHITE, "REPEAT CYCLE", 1, TFT_WHITE);
    drawButton(startX + btnW + spacing, 270, btnW, btnH, colorD, 'D', 2, TFT_WHITE, "EXIT", 1, TFT_WHITE);
}

void drawMonitorSidebarM8Gcode()
{
    resetTextStyle();
    const int btnW = 100, btnH = 30, spacing = 40;
    const int totalWidth = btnW * 2 + spacing;
    const int startX = 240 - (totalWidth / 2);
    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
    drawButton(startX, 270, btnW, btnH, TFT_DARKGREEN, 'A', 2, TFT_WHITE, "ENABLE", 1, TFT_WHITE);
    drawButton(startX + btnW + spacing, 270, btnW, btnH, TFT_RED, 'B', 2, TFT_WHITE, "DISABLE", 1, TFT_WHITE);
}

void drawMonitorAlertBox(const std::string &msg)
{
    const GcodeMonitorConfig &cfg = defaultConfig;
    tft_fill_rect(cfg.x, cfg.y, cfg.w, cfg.h, TFT_BLACK);
    int cx = cfg.x + cfg.w / 2;
    int cy = cfg.y + cfg.h / 2;
    tft_fill_triangle(cx - 30, cy - 15, cx + 30, cy - 15, cx, cy - 55, TFT_YELLOW);
    tft_set_text_color_bg(TFT_BLACK, TFT_YELLOW);
    tft_set_text_size(2);
    tft_set_text_datum(MC_DATUM);
    drawCenteredTextAt("!", cx, cy - 35, 2, TFT_BLACK);
    std::vector<std::string> lines;
    std::string temp = msg;
    int maxCharsLocal = 40;
    while (temp.length() > (size_t)maxCharsLocal)
    {
        size_t cut = temp.rfind(' ', maxCharsLocal);
        if (cut == std::string::npos)
            cut = maxCharsLocal;
        lines.push_back(temp.substr(0, cut));
        temp = temp.substr(cut + 1);
    }
    lines.push_back(temp);
    tft_set_text_color_bg(TFT_WHITE, TFT_BLACK);
    tft_set_text_size(1);
    int startY = cy + 10;
    int lineSpacing = 16;
    for (size_t i = 0; i < lines.size(); i++)
        drawCenteredTextAt(lines[i].c_str(), cx, startY + i * lineSpacing, 1, TFT_WHITE);
}

void drawMonitorStopBox(const std::string &msg)
{
    const GcodeMonitorConfig &cfg = defaultConfig;
    tft_fill_rect(cfg.x, cfg.y, cfg.w, cfg.h, TFT_BLACK);
    int cx = cfg.x + cfg.w / 2;
    int cy = cfg.y + cfg.h / 2;
    int radius = 35;
    for (int i = 0; i < 4; i++)
        tft_draw_circle(cx, cy - 40, radius - i, TFT_RED);
    tft_fill_circle(cx, cy - 40, radius - 5, TFT_WHITE);
    tft_set_text_color_bg(TFT_BLACK, TFT_WHITE);
    tft_set_text_size(1);
    tft_set_text_datum(MC_DATUM);
    drawCenteredTextAt("STOP", cx, cy - 40, 1, TFT_BLACK);
    std::vector<std::string> lines;
    std::string temp = msg;
    int maxCharsLocal = 40;
    while (temp.length() > (size_t)maxCharsLocal)
    {
        size_t cut = temp.rfind(' ', maxCharsLocal);
        if (cut == std::string::npos)
            cut = maxCharsLocal;
        lines.push_back(temp.substr(0, cut));
        temp = temp.substr(cut + 1);
    }
    lines.push_back(temp);
    tft_set_text_color_bg(TFT_WHITE, TFT_BLACK);
    int startY = cy + 10;
    int lineSpacing = 16;
    for (size_t i = 0; i < lines.size(); i++)
        drawCenteredTextAt(lines[i].c_str(), cx, startY + i * lineSpacing, 1, TFT_WHITE);
}

void drawMonitorRequestDataBox(const std::string &msg)
{
    const GcodeMonitorConfig &cfg = defaultConfig;
    tft_fill_rect(cfg.x, cfg.y, cfg.w, cfg.h, TFT_BLACK);
    int cx = cfg.x + cfg.w / 2;
    int cy = cfg.y + cfg.h / 2;
    int radius = 35;
    tft_fill_circle(cx, cy - 40, radius, TFT_YELLOW);
    tft_set_text_color_bg(TFT_BLACK, TFT_YELLOW);
    tft_set_text_size(3);
    tft_set_text_datum(MC_DATUM);
    drawCenteredTextAt("?", cx, cy - 40, 3, TFT_BLACK);
    std::vector<std::string> lines;
    std::string temp = msg;
    int maxCharsLocal = 40;
    while (temp.length() > (size_t)maxCharsLocal)
    {
        size_t cut = temp.rfind(' ', maxCharsLocal);
        if (cut == std::string::npos)
            cut = maxCharsLocal;
        lines.push_back(temp.substr(0, cut));
        temp = temp.substr(cut + 1);
    }
    lines.push_back(temp);
    tft_set_text_color_bg(TFT_WHITE, TFT_BLACK);
    tft_set_text_size(1);
    int startY = cy + 10;
    int lineSpacing = 16;
    for (size_t i = 0; i < lines.size(); i++)
        drawCenteredTextAt(lines[i].c_str(), cx, startY + i * lineSpacing, 1, TFT_WHITE);
}

void drawGcodeTypeBox(const std::string &type)
{
    tft_fill_rect(310, 55, 150, 30, TFT_NAVY);
    tft_set_text_color(TFT_WHITE);
    tft_set_text_size(1);
    tft_set_text_datum(MC_DATUM);
    drawCenteredTextAt(type.c_str(), 310 + 75, 55 + 15, 1, TFT_WHITE);
}

void estimateGcodeSize(const std::string &setup, const std::string &motion, const std::string &end)
{
    int totalSize = setup.size() + motion.size() + end.size();
    auto countLines = [](const std::string &block) -> int
    {
        int lines = 0;
        size_t pos = 0;
        while (pos < block.size())
        {
            size_t next = block.find('\n', pos);
            if (next == std::string::npos)
                break;
            lines++;
            pos = next + 1;
        }
        return lines;
    };
    int setupLines = countLines(setup);
    int motionLines = countLines(motion);
    int endLines = countLines(end);
    ESP_LOGI(TAG, "G-code size: setup %d/%d, motion %d/%d, end %d/%d, total %d",
             setup.size(), setupLines, motion.size(), motionLines, end.size(), endLines, totalSize);
}

