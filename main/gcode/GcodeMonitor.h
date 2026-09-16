#ifndef GCODE_MONITOR_H
#define GCODE_MONITOR_H

#include "GcodeTypes.h"
#include "graphics_lgfx.h" 
#include <string>
#include <vector>

#define MAX_GCODE_MONITOR_LINES 500

struct GcodeMonitorConfig {
    int x, y, w, h;
    int lineHeight;
    int maxLinesVisible;
    int charXOffset;
    uint16_t bgColor;
    uint16_t textColor;
    uint16_t commentColor;
    uint16_t selectedColor;
    const lgfx::IFont* font;
};

struct GcodeBlockRanges {
    int setupStart;
    int setupEnd;
    int motionStart;
    int motionEnd;
    int endStart;
    int endEnd;
};

// Variabili globali (dichiarate come extern)
extern GcodeMonitorConfig defaultConfig;
extern std::string gcodeMonitorLines[MAX_GCODE_MONITOR_LINES];
extern uint8_t gcodeMonitorLineStates[MAX_GCODE_MONITOR_LINES];
extern bool gcodeMonitorIsComment[MAX_GCODE_MONITOR_LINES];
extern int gcodeMonitorLineCount;
extern int gcodeMonitorScrollIndex;
extern int gcodeMonitorSelectedIndex;

// Costanti
#define LINE_PENDING 0
#define LINE_EXECUTED 1
#define LINE_ACTIVE 2
#define LINE_ERROR 3

// Funzioni principali
void GcodeMonitorPreview(const std::string& setup, const std::string& motion, const std::string& end, const std::string& type);
void drawPreviewImage(const char* filename);
void drawPreviewForType(const std::string& type);
void drawGcodeMonitor(const GcodeMonitorConfig& cfg);
bool isScrollingEnabled();
void prepareGcodeToTrack(const std::string& gcodeType);
void clearMonitorArea(const GcodeMonitorConfig& cfg);
void printFullGcodeToSerial();
void filterGcodeMonitorLines();
GcodeBlockRanges logGcodeBlockRanges(const std::string& setup, const std::string& motion, const std::string& end);
std::string compressGcode(const std::string& gcode);
void sendGcodeExecution(const GcodeBlockRanges& ranges, const std::string& allGcode, const std::string& gcodeType);
bool unlockIfError();
float extractCoordinate(const std::string& line, char axis);
void markLineExecuted(int index);
void forceCompletion(const GcodeBlockRanges& ranges);
void centerMonitorOn(int lineIndex);
void markEndBlocks(const GcodeBlockRanges& ranges);
void estimateGcodeSize(const std::string& setup, const std::string& motion, const std::string& end);
void GcodeMonitorScene(const GcodeBlock& block);
void drawMonitorAlertBox(const std::string& msg);
void drawMonitorStopBox(const std::string& msg);
void drawMonitorRequestDataBox(const std::string& msg);
void drawMonitorSidebarPreview();
void drawMonitorSidebarGcode();
void drawMonitorSidebarM8Gcode();
void drawGcodeTypeBox(const std::string& type);

#endif