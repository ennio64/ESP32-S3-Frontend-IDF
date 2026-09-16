#ifndef TOOL_DATABASE_H
#define TOOL_DATABASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define MAX_TOOLS 6

// Tipi utensile
#define TOOL_TYPE_EXTERNAL 0
#define TOOL_TYPE_INTERNAL 1
#define TOOL_TYPE_DRILL    2
#define TOOL_TYPE_THREAD   3
#define TOOL_TYPE_GROOVE   4

// Materiali
#define TOOL_MATERIAL_STEEL    0
#define TOOL_MATERIAL_ALUMINUM 1
#define TOOL_MATERIAL_BRASS    2

// Tipo di probe richiesto
typedef enum {
    PROBE_STANDARD = 0,   // Z + X esterno (tornitura standard)
    PROBE_INTERNAL,       // Z + X interno (alesaggio)
    PROBE_ONLY_Z,         // solo Z (frese, punte, utensili assiali)
    PROBE_NO_MEASURE      // nessuna calibrazione
} ProbeType;

// Mano dell'utensile (destro/sinistro)
typedef enum {
    TOOL_HAND_RIGHT = 0,
    TOOL_HAND_LEFT = 1
} ToolHand;

typedef struct {
    float offset_x;             // offset X (raggio, mm)
    float offset_z;             // offset Z (mm)
    int   type;                 // TOOL_TYPE_*
    bool  calibrated;           // true se calibrato con probe (entrambi gli assi)
    bool  configured;           // true se associato dal catalogo
    float width;                // larghezza utensile (mm)
    float feed;                 // feed di default (mm/min)
    int   rpm;                  // RPM di default
    int   material;             // TOOL_MATERIAL_*
    char  image_name[32];       // nome file immagine
    char  tool_name[32];        // nome visuale dell'utensile (es. "SCLCR")
    ProbeType probe_type;       // tipo di probe richiesto
    ToolHand hand;              // destra o sinistra
} ToolData;

void tool_database_init(void);
bool tool_exists(int tool);
bool tool_is_associated(int tool);
bool tool_is_calibrated(int tool);
ToolData tool_get_data(int tool);
void tool_set_data(int tool, const ToolData *data);
void tool_reset(int tool);
void tool_associate(int tool, const ToolData *catalog_data);

float tool_get_offset_x(int tool);
void tool_set_offset_x(int tool, float x);
float tool_get_offset_z(int tool);
void tool_set_offset_z(int tool, float z);

void tool_save_all(void);

#ifdef __cplusplus
}
#endif

#endif