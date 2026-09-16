#include "global_vars.h"

// Code e sincronizzazione
QueueHandle_t gcodeQueue = NULL;
bool displayReady = false;
bool grblReady = false;
bool gcodeTrackingActive = false;
int grblCurrentLineNumber = -1;

// Parametri macchina
float maxSpindleRPM = 0;
bool coolantEnabled = false;   // valore di default
float maxFeedX = 0, maxFeedZ = 0;
bool AutoReportStatus = false;
int PLANNER_BUFFER_SIZE = 35;
bool controllerError = false;

// Freno
bool brakeActive = false;

// Mutex TFT
portMUX_TYPE tftMutex = portMUX_INITIALIZER_UNLOCKED;

// DRO
float DRO_X = 0, DRO_Z = 0, DRO_A = 0, DRO_RPM = 0;
float last_DRO_X = 0, last_DRO_Z = 0, last_DRO_A = 0, last_DRO_RPM = 0;
bool DRO_X_Changed = false, DRO_Z_Changed = false, DRO_A_Changed = false, DRO_RPM_Changed = false;

// In global_vars.cpp
#include "ads1x15.h"   // per il tipo completo

ads1x15_t* ads = nullptr;
float k_XDRO = 1.0f;   // valore di default
bool R_D = false;