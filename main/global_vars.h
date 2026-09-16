#ifndef GLOBAL_VARS_H
#define GLOBAL_VARS_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string>
#include "esp_timer.h"

struct Position {
    float x = 0, y = 0, z = 0, a = 0;
    Position operator-(const Position& other) const {
        return {x - other.x, y - other.y, z - other.z, a - other.a};
    }
};

enum MachineState {
    STATE_IDLE,
    STATE_RUN,
    STATE_HOLD,
    STATE_JOG,
    STATE_ALARM,
    STATE_CHECK,
    STATE_HOME,
    STATE_SLEEP,
    STATE_TOOL,
    STATE_UNKNOWN
};

// Code e sincronizzazione
extern QueueHandle_t gcodeQueue;
extern bool displayReady;
extern bool grblReady;
extern bool gcodeTrackingActive;
extern int grblCurrentLineNumber;

// Parametri macchina
extern float maxSpindleRPM;
extern bool coolantEnabled;
extern float maxFeedX, maxFeedZ;
extern bool AutoReportStatus;
extern int PLANNER_BUFFER_SIZE;
extern bool controllerError;

// Freno
extern bool brakeActive;

// Mutex TFT
extern portMUX_TYPE tftMutex;

// DRO
extern float DRO_X, DRO_Z, DRO_A, DRO_RPM;
extern float last_DRO_X, last_DRO_Z, last_DRO_A, last_DRO_RPM;
extern bool DRO_X_Changed, DRO_Z_Changed, DRO_A_Changed, DRO_RPM_Changed;

// Forward declaration per evitare di includere ads1x15.h qui
typedef struct ads1x15_t ads1x15_t;

extern ads1x15_t* ads;
extern float k_XDRO;
extern bool R_D;

#endif