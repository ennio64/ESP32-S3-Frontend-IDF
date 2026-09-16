// scenes.h
#ifndef SCENES_H
#define SCENES_H

// Scene già implementate
#include "jog_scene.h"
#include "turning_scene.h"
#include "facing_scene.h"
#include "parting_scene.h"
#include "chamfer_billet_scene.h"
#include "hemispherical_scene.h"
#include "threading_scene.h"
#include "keyway_scene.h"
#include "spindle_position_scene.h"
#include "realtime_command_scene.h"
#include "global_settings_scene.h"
#include "taper_morse_scene.h"
#include "thread_iso_scene.h"

// Nuove scene (Tool Manager, Probe, Tool Database opzionale)
#include "tool_manager_scene.h"
#include "tool_catalog_scene.h"
#include "probe_scene.h"
// Se hai anche tool_database_scene (per edit avanzato) aggiungilo
//#include "tool_database_scene.h"

#endif // SCENES_H