#ifndef JOG_SCENE_H
#define JOG_SCENE_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

void jog_scene_enter(void);          // punto di ingresso (chiamata da main_scene)
void jog_calibration_enter(void);    // calibrazione joystick (opzionale)

#ifdef __cplusplus
}
#endif

#endif // JOG_SCENE_H