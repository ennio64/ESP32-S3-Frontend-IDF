#ifndef CNC_MANAGER_H
#define CNC_MANAGER_H

#include "espnow_manager.h"
#include "global_vars.h"
#include <string>
#include <vector>

extern ESPNowManager cnc;

// Dichiarazioni variabili globali (definite in cnc_manager.cpp)
extern float maxSpindleRPM;
extern float maxFeedX, maxFeedZ;
extern bool AutoReportStatus;
extern int PLANNER_BUFFER_SIZE;
extern bool controllerError;
extern bool gcodeTrackingActive;
extern int grblCurrentLineNumber;

// Mutex per il TFT (se usato)
extern portMUX_TYPE tftMutex;

// Variabili DRO
extern float DRO_X, DRO_Z, DRO_A, DRO_RPM;
extern float last_DRO_X, last_DRO_Z, last_DRO_A, last_DRO_RPM;
extern bool DRO_X_Changed, DRO_Z_Changed, DRO_A_Changed, DRO_RPM_Changed;

// Funzioni pubbliche
void setupCNC();
void handleConnectionError();
bool waitForGRBLConnection(uint32_t timeout_ms);
bool verifyGRBLResponse();
void initializeGRBLSettings();
std::string queryImmediateState(uint32_t timeout_ms);
float readParamWithRetry(const char* param, int wait_ms);
void onPositionUpdate(const Position& pos);
void onError(const std::string& error);
void onStateChange(MachineState oldState, MachineState newState);
void onStatusReceived(const std::string& status);
void readMaxSpindleRPM();
void readMaxFeedRates();
void checkAutoReportStatus();
int readPlannerBufferSize();
std::string getStateString(MachineState state);

// Funzioni di invio G‑code
bool validateRawGcode(const std::string& gcode);
bool sendGCodeStream(const std::string& gcode);
bool waitForCompletion(uint32_t timeout_ms);

// Forward declaration (se necessario)
class GCodeTracker; // definito altrove

#endif