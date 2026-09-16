#include "probe_scene.h"
#include "tool_database.h"
#include "global_settings.h"
#include "graphics_helpers.h"
#include "graphics_primitive.h"
#include "graphics_lgfx.h"
#include "keypad.h"
#include "cnc_manager.h"
#include "global_vars.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scene_utils.h"
#include <functional>
#include <cstdio>
#include <cstring>
#include <string>

static const char *TAG = "PROBE_REAL";

int pending_tool_for_probe = 0;

// Flag per tracciare lo stato della calibrazione
static bool manual_x_set = false;
static bool manual_z_set = false;
static bool x_probe_done = false;
static bool z_probe_done = false;

// Parametri di calibrazione globali
static float g_outer_diam = 0;
static float g_inner_diam = 0;
static float g_ring_thick = 0;
static float g_probe_feed = 0;
static float g_safety_dist = 0;
static bool g_is_external = true;
static ToolHand g_hand = TOOL_HAND_RIGHT;

static void wait_for_motion_stop()
{
    while (cnc.getMachineState() == STATE_RUN || cnc.getMachineState() == STATE_JOG)
        vTaskDelay(pdMS_TO_TICKS(50));
}

static bool perform_probe(char axis, float direction, float max_travel, float feed)
{
    char cmd[64];
    float travel = direction * max_travel;
    snprintf(cmd, sizeof(cmd), "G91 G38.2 %c%.3f F%.1f", axis, travel, feed);
    ESP_LOGI(TAG, "Probe command: %s", cmd);
    cnc.sendCommand(cmd);

    int timeout_ms = 5000;
    for (int t = 0; t < timeout_ms; t += 50)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
        std::string pinState = cnc.getParsedStatus().pinState;
        if (pinState.find('P') != std::string::npos)
        {
            ESP_LOGI(TAG, "Probe triggered!");
            return true;
        }
        if (cnc.getMachineState() == STATE_IDLE)
            break;
    }
    wait_for_motion_stop();
    return false;
}

static void rapid_move(float x, float z)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "G90 G0 X%.3f Z%.3f", x, z);
    cnc.sendCommand(cmd);
    wait_for_motion_stop();
}

static void get_current_position(float *x, float *z)
{
    if (x) *x = cnc.getWorkPositionX();
    if (z) *z = cnc.getWorkPositionZ();
}

// ----- Probe esterne -----
static bool do_probe_x_external(int tool)
{
    rapid_move(-(g_outer_diam / 2.0f + 5.0f), 10.0f);
    float start_z = cnc.getWorkPositionZ();
    rapid_move(-(g_outer_diam / 2.0f + 5.0f), start_z);
    if (!perform_probe('X', 1.0f, 20.0f, g_probe_feed))
        return false;
    float hit_x;
    get_current_position(&hit_x, NULL);
    float offset_x = (g_outer_diam / 2.0f) + hit_x;
    tool_set_offset_x(tool, offset_x);
    return true;
}

static bool do_probe_x_internal(int tool)
{
    rapid_move(g_inner_diam / 4.0f, 10.0f);
    float start_z = cnc.getWorkPositionZ();
    rapid_move(g_inner_diam / 4.0f, start_z);
    float probe_dir = (g_hand == TOOL_HAND_RIGHT) ? -1.0f : 1.0f;
    if (!perform_probe('X', probe_dir, 15.0f, g_probe_feed))
        return false;
    float hit_x;
    get_current_position(&hit_x, NULL);
    float offset_x = (g_hand == TOOL_HAND_RIGHT) ? hit_x - (g_inner_diam / 2.0f) : (g_inner_diam / 2.0f) - hit_x;
    tool_set_offset_x(tool, offset_x);
    return true;
}

static bool do_probe_z_down(int tool)
{
    rapid_move(0.0f, 10.0f);
    if (!perform_probe('Z', -1.0f, 15.0f, g_probe_feed))
        return false;
    float hit_z;
    get_current_position(NULL, &hit_z);
    float offset_z = hit_z - g_ring_thick;
    tool_set_offset_z(tool, offset_z);
    rapid_move(0.0f, hit_z + g_safety_dist);
    return true;
}

static bool do_probe_z_internal(int tool) { return do_probe_z_down(tool); }

// =========================================================================
// Helper: pulsanti in basso (SKIP, EDIT, CAL, EXIT)
// =========================================================================
static void draw_bottom_buttons()
{
    resetTextStyle();
    const int btnW = 70, btnH = 30, btnY = 270, spacing = 15;
    const int totalWidth = btnW * 4 + spacing * 3;
    const int startX = (480 - totalWidth) / 2;
    drawButton(startX, btnY, btnW, btnH, TFT_ORANGE,    'A', 2, TFT_WHITE, "SKIP", 1, TFT_WHITE);
    drawButton(startX + btnW + spacing, btnY, btnW, btnH, TFT_DARKCYAN, 'B', 2, TFT_WHITE, "EDIT", 1, TFT_WHITE);
    drawButton(startX + 2*(btnW+spacing), btnY, btnW, btnH, TFT_DARKGREEN, 'C', 2, TFT_WHITE, "CAL", 1, TFT_WHITE);
    drawButton(startX + 3*(btnW+spacing), btnY, btnW, btnH, TFT_RED,      'D', 2, TFT_WHITE, "EXIT", 1, TFT_WHITE);
}

// =========================================================================
// Helper: editor manuale offset
// =========================================================================
static void edit_offset(char axis, int tool, float &offset_value,
                        int view_x, int view_y, int view_w, int view_h,
                        std::function<void()> redraw_central_area)
{
    std::string inputValue = "";
    bool isNegative = false;
    bool editing = true;

    int mx = view_x, my = view_y, mw = view_w, mh = view_h;
    int cx = mx + mw/2;

    tft_fill_rect(mx, my, mw, mh, TFT_BLACK);
    tft_draw_rect(mx, my, mw, mh, TFT_WHITE);
    resetTextStyle();
    tft_set_text_color(TFT_GREEN);
    tft_set_text_size(2);
    char title[32];
    snprintf(title, sizeof(title), "Set OFFSET %c", axis);
    drawCenteredTextAt(title, cx, my+45, 2, TFT_GREEN);
    tft_fill_rect(mx+20, my+65, mw-40, 1, TFT_DARKGREY);
    tft_set_text_color(TFT_WHITE);
    tft_set_text_datum(ML_DATUM);
    tft_draw_string("Current:", mx+30, my+95, TFT_WHITE, 2);
    tft_draw_string("New:",     mx+30, my+150, TFT_WHITE, 2);

    int currentBoxX = mx+126, currentBoxW = 120;
    tft_fill_rect(currentBoxX, my+85, currentBoxW, 28, TFT_DARKGREY);
    tft_draw_rect(currentBoxX, my+85, currentBoxW, 28, TFT_WHITE);
    int newBoxX = mx+126, newBoxW = 120;
    tft_fill_rect(newBoxX, my+140, newBoxW, 28, TFT_DARKGREY);
    tft_draw_rect(newBoxX, my+140, newBoxW, 28, TFT_WHITE);

    int currentCenterX = currentBoxX + currentBoxW/2;
    int currentCenterY = my+85+14;
    int newCenterX    = newBoxX + newBoxW/2;
    int newCenterY    = my+140+14;

    char currBuf[16];
    snprintf(currBuf, sizeof(currBuf), "%.3f", offset_value);
    drawCenteredTextAt(currBuf, currentCenterX, currentCenterY, 2, TFT_YELLOW);

    // Pulsanti numerici in basso
    resetTextStyle();
    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
    tft_fill_rect(0, 260, 480, 1, TFT_WHITE);
    int btnW = 70, btnH = 30, btnY = 270, spacing = 10, btnW0_9 = 80;
    int totalWidth = btnW + spacing + btnW + spacing + btnW0_9 + spacing + btnW + spacing + btnW;
    int startX = (480 - totalWidth)/2;
    int x1=startX, x2=x1+btnW+spacing, x3=x2+btnW+spacing,
        x4=x3+btnW0_9+spacing, x5=x4+btnW+spacing;
    drawButton(x1, btnY, btnW, btnH, TFT_BLACK, '*', 2, TFT_WHITE, "(.)", 1, TFT_WHITE);
    drawButton(x2, btnY, btnW, btnH, TFT_BLACK, '#', 2, TFT_WHITE, "(-)", 1, TFT_WHITE);
    drawCenteredText2InRoundRect("0-9", x3+btnW0_9/2, btnY+btnH/2, btnW0_9, btnH, TFT_BLACK, TFT_WHITE);
    drawButton(x4, btnY, btnW, btnH, TFT_BLACK, 'C', 2, TFT_WHITE, "CONFIRM", 1, TFT_WHITE);
    drawButton(x5, btnY, btnW, btnH, TFT_BLACK, 'D', 2, TFT_WHITE, "DELETE", 1, TFT_WHITE);

    std::string lastDisplayValue = "";
    bool firstDraw = true;

    while (editing)
    {
        std::string displayValue = (isNegative ? "-" : "") + inputValue;
        if (displayValue.empty()) displayValue = "0";
        if (displayValue != lastDisplayValue || firstDraw)
        {
            tft_fill_rect(newBoxX+5, my+145, newBoxW-10, 22, TFT_BLACK);
            drawCenteredTextAt(displayValue.c_str(), newCenterX, newCenterY, 2, TFT_CYAN);
            lastDisplayValue = displayValue;
            firstDraw = false;
        }

        char key = readKeypad();
        if (key >= '0' && key <= '9')
        {
            if (inputValue.length() < 10) inputValue += key;
        }
        else if (key == '.' || key == '*')
        {
            if (inputValue.find('.') == std::string::npos)
            {
                if (inputValue.empty()) inputValue = "0.";
                else inputValue += ".";
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
                if (isNegative) value = -value;
            }
            offset_value = value;
            if (axis == 'X')
            {
                tool_set_offset_x(tool, offset_value);
                manual_x_set = true;
            }
            else
            {
                tool_set_offset_z(tool, offset_value);
                manual_z_set = true;
            }
            // Messaggio di conferma
            tft_fill_rect(mx, my, mw, mh, TFT_BLACK);
            tft_draw_rect(mx, my, mw, mh, TFT_WHITE);
            tft_set_font(&fonts::Font2);
            tft_set_text_size(1);
            drawCenteredTextAt("Manual offset saved", cx, my+80, 2, TFT_YELLOW);
            drawCenteredTextAt("A = keep this value and skip", cx, my+115, 1, TFT_CYAN);
            drawCenteredTextAt("C = calibrate with probe",   cx, my+135, 1, TFT_CYAN);
            vTaskDelay(pdMS_TO_TICKS(2500));
            redraw_central_area();
            editing = false;
        }
        else if (key == 'D')
        {
            editing = false;
        }
        else if (key == 'B')
        {
            if (!inputValue.empty()) inputValue.pop_back();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    tft_fill_rect(0, 260, 480, 60, TFT_NAVY);
    tft_fill_rect(0, 260, 480, 1, TFT_WHITE);
    draw_bottom_buttons();
    redraw_central_area();
}

// =========================================================================
// SCHERMATA STEP X (ESTERNO) - con disegno completo
// =========================================================================
static void show_x_instructions()
{
    ToolData data = tool_get_data(pending_tool_for_probe);
    float current_off_x = data.offset_x;
    float current_off_z = data.offset_z;

    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    resetTextStyle();
    drawCenteredTextAt("CALIBRATION STEP 1/2: X PROBE", 240, 20, 2, TFT_YELLOW);
    tft_draw_line(20, 35, 460, 35, TFT_WHITE);

    const int VIEW_X = 10, VIEW_Y = 45, VIEW_W = 280, VIEW_H = 205;
    tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
    tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);

    draw_dro_cal(current_off_x, current_off_z);

    auto draw_central = [&]()
    {
        tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
        tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);

        // SIDE VIEW
        tft_set_text_color(TFT_CYAN);
        drawCenteredTextAt("SIDE VIEW (Z, X)", VIEW_X + 70, VIEW_Y + 10, 1, TFT_CYAN);

        int probeX = VIEW_X + 70, probeZ = VIEW_Y + 55;
        tft_fill_rect(probeX - 25, probeZ - 20, 50, 40, TFT_DARKGREY);
        tft_draw_rect(probeX - 25, probeZ - 20, 50, 40, TFT_WHITE);
        drawCenteredTextAt("PROBE", probeX, probeZ, 1, TFT_WHITE);

        const int TOOL_W = 20, TOOL_H = 12, TOOL_TIP = 15, TOOL_CLEARANCE = 5;
        int toolVW = TOOL_H, toolVH = TOOL_W;
        int toolTipY = (probeZ + 20) + TOOL_CLEARANCE;
        int toolVZ = toolTipY + TOOL_TIP;
        int toolVX = probeX - toolVW / 2;
        tft_fill_rect(toolVX, toolVZ, toolVW, toolVH, TFT_LIGHTGREY);
        tft_fill_triangle(toolVX + toolVW/2, toolVZ - TOOL_TIP, toolVX, toolVZ, toolVX + toolVW, toolVZ, TFT_RED);
        drawCenteredTextAt("TOOL", toolVX - 25, toolVZ + toolVH/2, 1, TFT_WHITE);
        int arrowVX = toolVX + toolVW + 18;
        int arrowVEndY = toolVZ - TOOL_TIP;
        int arrowVStartY = toolVZ + toolVH;
        tft_draw_line(arrowVX, arrowVStartY, arrowVX, arrowVEndY, TFT_YELLOW);
        tft_fill_triangle(arrowVX, arrowVEndY, arrowVX-5, arrowVEndY+8, arrowVX+5, arrowVEndY+8, TFT_YELLOW);
        drawCenteredTextAt("X+", arrowVX + 12, (arrowVStartY+arrowVEndY)/2, 1, TFT_YELLOW);

        // FRONT VIEW
        int frontViewX = VIEW_X + VIEW_W - 110;
        tft_set_text_color(TFT_CYAN);
        drawCenteredTextAt("FRONT VIEW (X, Y)", frontViewX + 30, VIEW_Y + 10, 1, TFT_CYAN);
        int probeFX = frontViewX + 50, probeFY = VIEW_Y + 55, probeR = 25;
        tft_draw_circle(probeFX, probeFY, probeR, TFT_WHITE);
        tft_draw_circle(probeFX, probeFY, probeR - 10, TFT_WHITE);
        drawCenteredTextAt("PROBE", probeFX, probeFY + 35, 1, TFT_WHITE);
        int toolFX = probeFX - probeR - TOOL_CLEARANCE - TOOL_TIP - (TOOL_W/2);
        int toolFY = probeFY;
        tft_fill_rect(toolFX - TOOL_W/2, toolFY - TOOL_H/2, TOOL_W, TOOL_H, TFT_LIGHTGREY);
        tft_fill_triangle(toolFX + TOOL_W/2, toolFY - TOOL_H/2, toolFX + TOOL_W/2, toolFY + TOOL_H/2, toolFX + TOOL_W/2 + TOOL_TIP, toolFY, TFT_RED);
        drawCenteredTextAt("TOOL", toolFX, toolFY + 15, 1, TFT_WHITE);
        int arrowFXStart = toolFX - TOOL_W/2;
        int arrowFXEnd = toolFX + TOOL_W/2 + TOOL_TIP;
        int arrowFY = toolFY - 20;
        tft_draw_line(arrowFXStart, arrowFY, arrowFXEnd, arrowFY, TFT_YELLOW);
        tft_fill_triangle(arrowFXEnd, arrowFY, arrowFXEnd-8, arrowFY-5, arrowFXEnd-8, arrowFY+5, TFT_YELLOW);
        drawCenteredTextAt("X+", (arrowFXStart+arrowFXEnd)/2, arrowFY-10, 1, TFT_YELLOW);

        // Istruzioni
        int y = VIEW_Y + 130;
        resetTextStyle();
        tft_set_text_size(1);
        tft_set_text_color(TFT_WHITE);
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("1. Ensure probe is fixed and connected.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("2. Position tool tip near the probe.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("3. Press [C] to calibrate.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("4. The tool will move up until contact.");
    };

    draw_central(); // primo disegno
    tft_draw_line(20, 258, 460, 258, TFT_WHITE);
    draw_bottom_buttons();

    while (true)
    {
        char key = readKeypad();
        if (key == 'A')
        {
            return; // skip: niente probe X
        }
        else if (key == 'B')
        {
            edit_offset('X', pending_tool_for_probe, current_off_x,
                        VIEW_X, VIEW_Y, VIEW_W, VIEW_H, draw_central);
            draw_dro_cal(current_off_x, current_off_z);
            current_off_x = tool_get_offset_x(pending_tool_for_probe);
            current_off_z = tool_get_offset_z(pending_tool_for_probe);
            draw_dro_cal(current_off_x, current_off_z);
        }
        else if (key == 'C')
        {
            bool success = g_is_external ? do_probe_x_external(pending_tool_for_probe)
                                         : do_probe_x_internal(pending_tool_for_probe);
            if (success)
            {
                x_probe_done = true;
                current_off_x = tool_get_offset_x(pending_tool_for_probe);
                current_off_z = tool_get_offset_z(pending_tool_for_probe);
                draw_dro_cal(current_off_x, current_off_z);
                return;
            }
            else
            {
                tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
                tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);
                drawCenteredTextAt("Probe failed! Check connection.", 240, 200, 1, TFT_RED);
                vTaskDelay(pdMS_TO_TICKS(1500));
                draw_central();
                draw_dro_cal(current_off_x, current_off_z);
            }
        }
        else if (key == 'D')
        {
            pending_tool_for_probe = 0;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =========================================================================
// SCHERMATA STEP Z (ESTERNO)
// =========================================================================
static void show_z_instructions()
{
    ToolData data = tool_get_data(pending_tool_for_probe);
    float current_off_x = data.offset_x;
    float current_off_z = data.offset_z;

    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    resetTextStyle();
    drawCenteredTextAt("CALIBRATION STEP 2/2: Z PROBE", 240, 20, 2, TFT_YELLOW);
    tft_draw_line(20, 35, 460, 35, TFT_WHITE);

    const int VIEW_X = 10, VIEW_Y = 45, VIEW_W = 280, VIEW_H = 205;
    tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
    tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);

    draw_dro_cal(current_off_x, current_off_z);

    auto draw_central = [&]()
    {
        tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
        tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);

        tft_set_text_color(TFT_CYAN);
        drawCenteredTextAt("SIDE VIEW (Z, X)", VIEW_X + VIEW_W/2, VIEW_Y + 10, 1, TFT_CYAN);

        int ringX = VIEW_X + VIEW_W/2, ringZ = VIEW_Y + 75;
        tft_fill_rect(ringX - 30, ringZ - 20, 60, 40, TFT_DARKGREY);
        tft_draw_rect(ringX - 30, ringZ - 20, 60, 40, TFT_WHITE);
        drawCenteredTextAt("PROBE", ringX, ringZ, 1, TFT_WHITE);

        const int TOOL_W = 20, TOOL_H = 12, TOOL_TIP = 15, TOOL_CLEARANCE = 5;
        int toolTipX = (ringX + 30) + TOOL_CLEARANCE;
        int toolCenterX = toolTipX + TOOL_TIP + TOOL_W/2;
        int toolCenterY = ringZ;
        tft_fill_rect(toolCenterX - TOOL_W/2, toolCenterY - TOOL_H/2, TOOL_W, TOOL_H, TFT_LIGHTGREY);
        tft_fill_triangle(toolCenterX - TOOL_W/2, toolCenterY - TOOL_H/2, toolCenterX - TOOL_W/2, toolCenterY + TOOL_H/2, toolTipX, toolCenterY, TFT_RED);
        drawCenteredTextAt("TOOL", toolCenterX, toolCenterY + 18, 1, TFT_WHITE);
        int arrowStart = toolCenterX + TOOL_W/2;
        int arrowEnd = toolTipX;
        int arrowY = toolCenterY - 20;
        tft_draw_line(arrowStart, arrowY, arrowEnd, arrowY, TFT_YELLOW);
        tft_fill_triangle(arrowEnd, arrowY, arrowEnd+8, arrowY-5, arrowEnd+8, arrowY+5, TFT_YELLOW);
        drawCenteredTextAt("Z-", (arrowStart+arrowEnd)/2, arrowY-10, 1, TFT_YELLOW);

        int y = VIEW_Y + 130;
        resetTextStyle();
        tft_set_text_size(1);
        tft_set_text_color(TFT_WHITE);
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("1. Ensure probe is fixed and connected.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("2. Position tool tip near the probe.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("3. Press [C] to calibrate.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("4. The tool will move left until contact.");
    };

    draw_central();
    tft_draw_line(20, 258, 460, 258, TFT_WHITE);
    draw_bottom_buttons();

    while (true)
    {
        char key = readKeypad();
        if (key == 'A')
        {
            return; // skip Z
        }
        else if (key == 'B')
        {
            edit_offset('Z', pending_tool_for_probe, current_off_z,
                        VIEW_X, VIEW_Y, VIEW_W, VIEW_H, draw_central);
            draw_dro_cal(current_off_x, current_off_z);
            current_off_x = tool_get_offset_x(pending_tool_for_probe);
            current_off_z = tool_get_offset_z(pending_tool_for_probe);
            draw_dro_cal(current_off_x, current_off_z);
        }
        else if (key == 'C')
        {
            bool success = do_probe_z_down(pending_tool_for_probe); // uguale per esterno/interno
            if (success)
            {
                z_probe_done = true;
                current_off_z = tool_get_offset_z(pending_tool_for_probe);
                draw_dro_cal(current_off_x, current_off_z);
                return;
            }
            else
            {
                tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
                tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);
                drawCenteredTextAt("Probe failed! Check connection.", 240, 200, 1, TFT_RED);
                vTaskDelay(pdMS_TO_TICKS(1500));
                draw_central();
                draw_dro_cal(current_off_x, current_off_z);
            }
        }
        else if (key == 'D')
        {
            pending_tool_for_probe = 0;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =========================================================================
// SCHERMATE PER UTENSILI INTERNI (stessa logica, disegno specifico)
// =========================================================================
static void show_x_internal_instructions()
{
    ToolData data = tool_get_data(pending_tool_for_probe);
    float current_off_x = data.offset_x;
    float current_off_z = data.offset_z;

    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    resetTextStyle();
    drawCenteredTextAt("CALIBRATION STEP 1/2: X PROBE", 240, 20, 2, TFT_YELLOW);
    tft_draw_line(20, 35, 460, 35, TFT_WHITE);

    const int VIEW_X = 10, VIEW_Y = 45, VIEW_W = 280, VIEW_H = 205;
    tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
    tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);

    draw_dro_cal(current_off_x, current_off_z);

    auto draw_central = [&]()
    {
        tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
        tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);

        const int h1 = 12, h2 = 40, h3 = 12, w = 60;
        const int gap = 80;

        // BEFORE (Z-)
        tft_set_text_color(TFT_CYAN);
        drawCenteredTextAt("BEFORE (Z-)", VIEW_X + 80, VIEW_Y + 10, 1, TFT_CYAN);
        int bx = VIEW_X + 30, by = VIEW_Y + 30;
        // Anello superiore
        tft_draw_rect(bx, by, w, h1, TFT_WHITE);
        for (int t = -h1; t <= w + 10; t += 4) {
            int x1 = bx + (t < 0 ? 0 : t);
            int y1 = by + (t < 0 ? -t : 0);
            int x2 = bx + (t + h1 > w + 10 ? w + 10 : t + h1);
            int y2 = by + (t + h1 > w + 10 ? (t + h1 - w - 10) : h1);
            if (x1 < x2 && y1 < y2) tft_draw_line(x1, y1, x2, y2, TFT_WHITE);
        }
        tft_fill_rect(bx + w, by, 11, h1 + 1, TFT_BLACK);
        tft_fill_rect(bx, by + h1, w, h2, TFT_DARKGREY);
        // Anello inferiore
        tft_draw_rect(bx, by + h1 + h2, w, h3, TFT_WHITE);
        for (int t = -h3; t <= w + 10; t += 4) {
            int x1 = bx + (t < 0 ? 0 : t);
            int y1 = (by + h1 + h2) + (t < 0 ? -t : 0);
            int x2 = bx + (t + h3 > w + 10 ? w + 10 : t + h3);
            int y2 = (by + h1 + h2) + (t + h3 > w + 10 ? (t + h3 - w - 10) : h3);
            if (x1 < x2 && y1 < y2) tft_draw_line(x1, y1, x2, y2, TFT_WHITE);
        }
        tft_fill_rect(bx + w, by + h1 + h2, 11, h3 + 1, TFT_BLACK);
        tft_fill_rect(bx, by + h1 + h2 + h3, w, 1, TFT_BLACK);
        int h_tool = 30, w_tool = 8;
        int ux = bx + w + 10, uy = by + h1 + h2/2 + 4;
        tft_fill_rect(ux, uy - w_tool, h_tool, w_tool, TFT_LIGHTGREY);
        tft_fill_triangle(ux, uy, ux + 10, uy, ux + 5, uy + 8, TFT_RED);
        int fx1 = ux + h_tool + 10, fy1 = uy - w_tool/2 - 14;
        tft_draw_line(fx1, fy1, fx1 - 40, fy1, TFT_YELLOW);
        tft_fill_triangle(fx1 - 40, fy1, fx1 - 32, fy1 - 4, fx1 - 32, fy1 + 4, TFT_YELLOW);
        drawCenteredTextAt("Z-", fx1 - 20, fy1 - 15, 1, TFT_YELLOW);

        // AFTER (X+)
        int afterStartX = bx + w + gap;
        tft_set_text_color(TFT_CYAN);
        drawCenteredTextAt("AFTER (X+)", afterStartX + 30, VIEW_Y + 10, 1, TFT_CYAN);
        int bx2 = afterStartX, by2 = by;
        // Anello superiore
        tft_draw_rect(bx2, by2, w, h1, TFT_WHITE);
        for (int t = -h1; t <= w + 10; t += 4) {
            int x1 = bx2 + (t < 0 ? 0 : t);
            int y1 = by2 + (t < 0 ? -t : 0);
            int x2 = bx2 + (t + h1 > w + 10 ? w + 10 : t + h1);
            int y2 = by2 + (t + h1 > w + 10 ? (t + h1 - w - 10) : h1);
            if (x1 < x2 && y1 < y2) tft_draw_line(x1, y1, x2, y2, TFT_WHITE);
        }
        tft_fill_rect(bx2 + w, by2, 11, h1 + 1, TFT_BLACK);
        tft_fill_rect(bx2, by2 + h1, w, h2, TFT_DARKGREY);
        // Anello inferiore
        tft_draw_rect(bx2, by2 + h1 + h2, w, h3, TFT_WHITE);
        for (int t = -h3; t <= w + 10; t += 4) {
            int x1 = bx2 + (t < 0 ? 0 : t);
            int y1 = (by2 + h1 + h2) + (t < 0 ? -t : 0);
            int x2 = bx2 + (t + h3 > w + 10 ? w + 10 : t + h3);
            int y2 = (by2 + h1 + h2) + (t + h3 > w + 10 ? (t + h3 - w - 10) : h3);
            if (x1 < x2 && y1 < y2) tft_draw_line(x1, y1, x2, y2, TFT_WHITE);
        }
        tft_fill_rect(bx2 + w, by2 + h1 + h2, 11, h3 + 1, TFT_BLACK);
        tft_fill_rect(bx2, by2 + h1 + h2 + h3, w, 1, TFT_BLACK);
        int ux2 = bx2 + 40, uy2 = by2 + h1 + h2 - 15;
        tft_fill_rect(ux2, uy2 - w_tool, h_tool, w_tool, TFT_LIGHTGREY);
        tft_fill_triangle(ux2, uy2, ux2 + 10, uy2, ux2 + 5, uy2 + 8, TFT_RED);
        int fx2 = ux2 + h_tool + 5, fy2 = uy2 - w_tool/2 - 10;
        tft_draw_line(fx2, fy2, fx2, fy2 + 40, TFT_YELLOW);
        tft_fill_triangle(fx2, fy2 + 40, fx2 - 4, fy2 + 32, fx2 + 4, fy2 + 32, TFT_YELLOW);
        drawCenteredTextAt("X+", fx2 + 20, fy2 + 20, 1, TFT_YELLOW);

        // Istruzioni
        int y = VIEW_Y + 130;
        resetTextStyle();
        tft_set_text_size(1);
        tft_set_text_color(TFT_WHITE);
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("1. Ensure probe is fixed and connected.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("2. Insert tool tip inside the probe hole.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("3. Press [C] to calibrate.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("4. Tool moves down to touch inner race.");
    };

    draw_central();
    tft_draw_line(20, 258, 460, 258, TFT_WHITE);
    draw_bottom_buttons();

    while (true)
    {
        char key = readKeypad();
        if (key == 'A') return;
        else if (key == 'B')
        {
            edit_offset('X', pending_tool_for_probe, current_off_x,
                        VIEW_X, VIEW_Y, VIEW_W, VIEW_H, draw_central);
            draw_dro_cal(current_off_x, current_off_z);
            current_off_x = tool_get_offset_x(pending_tool_for_probe);
            current_off_z = tool_get_offset_z(pending_tool_for_probe);
            draw_dro_cal(current_off_x, current_off_z);
        }
        else if (key == 'C')
        {
            bool success = do_probe_x_internal(pending_tool_for_probe);
            if (success)
            {
                x_probe_done = true;
                current_off_x = tool_get_offset_x(pending_tool_for_probe);
                current_off_z = tool_get_offset_z(pending_tool_for_probe);
                draw_dro_cal(current_off_x, current_off_z);
                return;
            }
            else
            {
                tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
                tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);
                drawCenteredTextAt("Probe failed! Check connection.", 240, 200, 1, TFT_RED);
                vTaskDelay(pdMS_TO_TICKS(1500));
                draw_central();
                draw_dro_cal(current_off_x, current_off_z);
            }
        }
        else if (key == 'D')
        {
            pending_tool_for_probe = 0;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void show_z_internal_instructions()
{
    ToolData data = tool_get_data(pending_tool_for_probe);
    float current_off_x = data.offset_x;
    float current_off_z = data.offset_z;

    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    resetTextStyle();
    drawCenteredTextAt("CALIBRATION STEP 2/2: Z PROBE", 240, 20, 2, TFT_YELLOW);
    tft_draw_line(20, 35, 460, 35, TFT_WHITE);

    const int VIEW_X = 10, VIEW_Y = 45, VIEW_W = 280, VIEW_H = 205;
    tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
    tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);

    draw_dro_cal(current_off_x, current_off_z);

    auto draw_central = [&]()
    {
        tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
        tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);

        tft_set_text_color(TFT_CYAN);
        drawCenteredTextAt("SIDE VIEW (Z, X)", VIEW_X + VIEW_W/2, VIEW_Y + 10, 1, TFT_CYAN);

        int ringX = VIEW_X + VIEW_W/2, ringZ = VIEW_Y + 75;
        tft_fill_rect(ringX - 30, ringZ - 20, 60, 40, TFT_DARKGREY);
        tft_draw_rect(ringX - 30, ringZ - 20, 60, 40, TFT_WHITE);
        drawCenteredTextAt("PROBE", ringX, ringZ, 1, TFT_WHITE);

        const int h_tool = 30, w_tool = 8, CLEARANCE = 5;
        int toolLeftX = (ringX + 30) + CLEARANCE;
        int toolBottomY = ringZ + w_tool / 2;
        tft_fill_rect(toolLeftX, toolBottomY - w_tool, h_tool, w_tool, TFT_LIGHTGREY);
        tft_fill_triangle(toolLeftX, toolBottomY, toolLeftX + 10, toolBottomY, toolLeftX + 5, toolBottomY + 8, TFT_RED);
        drawCenteredTextAt("TOOL", toolLeftX + h_tool / 2, ringZ + 18, 1, TFT_WHITE);

        int arrowStart = toolLeftX + h_tool;
        int arrowEnd = toolLeftX;
        int arrowY = ringZ - 20;
        tft_draw_line(arrowStart, arrowY, arrowEnd, arrowY, TFT_YELLOW);
        tft_fill_triangle(arrowEnd, arrowY, arrowEnd + 8, arrowY - 5, arrowEnd + 8, arrowY + 5, TFT_YELLOW);
        drawCenteredTextAt("Z-", (arrowStart + arrowEnd) / 2, arrowY - 10, 1, TFT_YELLOW);

        int y = VIEW_Y + 130;
        resetTextStyle();
        tft_set_text_size(1);
        tft_set_text_color(TFT_WHITE);
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("1. Ensure probe is fixed and connected.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("2. Position tool tip near the probe.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("3. Press [C] to calibrate.");
        y += 18;
        tft_set_cursor(VIEW_X + 12, y);
        tft_print("4. The tool will move left until contact.");
    };

    draw_central();
    tft_draw_line(20, 258, 460, 258, TFT_WHITE);
    draw_bottom_buttons();

    while (true)
    {
        char key = readKeypad();
        if (key == 'A') return;
        else if (key == 'B')
        {
            edit_offset('Z', pending_tool_for_probe, current_off_z,
                        VIEW_X, VIEW_Y, VIEW_W, VIEW_H, draw_central);
            draw_dro_cal(current_off_x, current_off_z);
            current_off_x = tool_get_offset_x(pending_tool_for_probe);
            current_off_z = tool_get_offset_z(pending_tool_for_probe);
            draw_dro_cal(current_off_x, current_off_z);
        }
        else if (key == 'C')
        {
            bool success = do_probe_z_internal(pending_tool_for_probe);
            if (success)
            {
                z_probe_done = true;
                current_off_z = tool_get_offset_z(pending_tool_for_probe);
                draw_dro_cal(current_off_x, current_off_z);
                return;
            }
            else
            {
                tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
                tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);
                drawCenteredTextAt("Probe failed! Check connection.", 240, 200, 1, TFT_RED);
                vTaskDelay(pdMS_TO_TICKS(1500));
                draw_central();
                draw_dro_cal(current_off_x, current_off_z);
            }
        }
        else if (key == 'D')
        {
            pending_tool_for_probe = 0;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =========================================================================
// SCHERMATA GENERICA (per utensili sinistri)
// =========================================================================
static void show_generic_instructions()
{
    ToolData data = tool_get_data(pending_tool_for_probe);
    float current_off_x = data.offset_x;
    float current_off_z = data.offset_z;

    tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
    drawCenteredTextAt("CALIBRATION", 240, 20, 2, TFT_YELLOW);
    tft_draw_line(20, 35, 460, 35, TFT_WHITE);

    const int VIEW_X = 10, VIEW_Y = 45, VIEW_W = 280, VIEW_H = 160;
    tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
    tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);

    draw_dro_cal(current_off_x, current_off_z);

    auto draw_central = [&]()
    {
        tft_fill_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_BLACK);
        tft_draw_rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, TFT_WHITE);
        int y = VIEW_Y + 30;
        resetTextStyle();
        tft_set_text_size(1);
        tft_set_text_color(TFT_WHITE);
        tft_set_cursor(VIEW_X + 20, y);
        tft_print("1. Position tool tip above the ring.");
        y += 25;
        tft_set_cursor(VIEW_X + 20, y);
        tft_print("2. The probe will perform Z and X measurements.");
        y += 25;
        tft_set_cursor(VIEW_X + 20, y);
        tft_print("3. Ensure ring is fixed and probe connected.");
    };

    draw_central();
    tft_draw_line(20, 258, 460, 258, TFT_WHITE);
    draw_bottom_buttons();

    while (true)
    {
        char key = readKeypad();
        if (key == 'A') return;
        else if (key == 'B')
        {
            drawCenteredTextAt("Not available", 240, 200, 1, TFT_RED);
            vTaskDelay(pdMS_TO_TICKS(1000));
            draw_central();
            draw_dro_cal(current_off_x, current_off_z);
        }
        else if (key == 'C') return;
        else if (key == 'D')
        {
            pending_tool_for_probe = 0;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =========================================================================
// FUNZIONE PRINCIPALE
// =========================================================================
void probe_scene_enter(void)
{
    int tool = pending_tool_for_probe;
    if (tool < 1 || tool > MAX_TOOLS)
    {
        ESP_LOGE(TAG, "Tool non valido: %d", tool);
        return;
    }

    manual_x_set = false;
    manual_z_set = false;
    x_probe_done = false;
    z_probe_done = false;

    ToolData data = tool_get_data(tool);
    if (!data.configured)
    {
        ESP_LOGW(TAG, "Tool %d non associato", tool);
        return;
    }

    // Carica parametri globali
    g_outer_diam   = settings_get_probe_ring_outer_diameter();
    g_inner_diam   = settings_get_probe_ring_inner_diameter();
    g_ring_thick   = settings_get_probe_ring_thickness();
    g_probe_feed   = settings_get_probe_feed();
    g_safety_dist  = settings_get_probe_safety_dist();
    g_is_external  = (data.probe_type == PROBE_STANDARD);
    g_hand         = data.hand;

    bool is_external = g_is_external;

    if (data.hand == TOOL_HAND_RIGHT)
    {
        if (is_external)
        {
            show_x_instructions();
            if (pending_tool_for_probe == 0) return;
            show_z_instructions();
            if (pending_tool_for_probe == 0) return;
        }
        else
        {
            show_x_internal_instructions();
            if (pending_tool_for_probe == 0) return;
            show_z_internal_instructions();
            if (pending_tool_for_probe == 0) return;
        }
    }
    else // LEFT
    {
        show_generic_instructions();
        if (pending_tool_for_probe == 0) return;
    }

    // Verifica se la calibrazione è riuscita (almeno una probe o entrambi manuali)
    bool success = (x_probe_done || z_probe_done) || (manual_x_set && manual_z_set);

    if (success)
    {
        resetTextStyle();
        data.calibrated = true;
        tool_set_data(tool, &data);
        ESP_LOGI(TAG, "Tool %d calibrato con successo", tool);
        tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
        drawCenteredTextAt("Calibration SUCCESS", 240, 140, 2, TFT_GREEN);
        drawCenteredTextAt("Returning to tool manager...", 240, 180, 1, TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    else
    {
        resetTextStyle();
        ESP_LOGE(TAG, "Calibrazione fallita");
        tft_fill_rect(0, 0, 480, 320, TFT_NAVY);
        drawCenteredTextAt("Calibration FAILED", 240, 140, 2, TFT_RED);
        drawCenteredTextAt("Check probe connection and ring", 240, 180, 1, TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}