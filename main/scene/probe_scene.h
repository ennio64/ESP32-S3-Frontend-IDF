#ifndef PROBE_SCENE_H
#define PROBE_SCENE_H

#include "tool_database.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int pending_tool_for_probe;

void probe_scene_enter(void);

#ifdef __cplusplus
}
#endif

#endif